/* 
 * This file is a part of the ESPShell Arduino library (Espressif's ESP32-family CPUs)
 *
 * Latest source code can be found at Github: https://github.com/vvb333007/espshell/
 * Stable releases: https://github.com/vvb333007/espshell/tags
 *
 * Feel free to use this code as you wish: it is absolutely free for commercial and 
 * non-commercial, education purposes.  Credits, however, would be greatly appreciated.
 *
 * Author: Viachesav Loglunov <vvb333007@gmail.com>
 */


 // -- Pulse Counter / Frequency Meter (PCNT) --
 //
 // ESP32 has 8 pulse counter units (ESP32S3 has 4) each of which is equipped 
 // with 2 channels: channel#0 (is what espshell uses) and channel#1 (unused).
 //
 // ESPShell selects first available unit for its operations: it checks for units in range [pcnt_unit .. PCNT_UNITS_MAX]
 // By default /pcnt_unit/ is set to PCNT_UNIT_0 which allows ESPShell to use any of PCNT units. In case user sketch uses 
 // some PCNT units (say, sketch is using UNIT0), the /pcnt_unit/ value can be adjusted ("var pcnt_unit 1") to prevent ESPShell
 // from accessing PCNT UNIT0.
 //
 // There are different types of counting:
 //
 // 1. Immediate counting: command "count PIN_NUMBER", a blocking call. User waits for the command to complete
 // 2. Background counting: command "count ... &", user can issue new espshell commands immediately
 // 3. Triggered counting (either immediate or background: "count ... trigger" or  "count ... trigger &"
 //
 // Terminology:
 // "measurement time", "wait" or "delay" - is the time interval when counter is counting (not paused). Accuracy of this one defines overall measurement accuracy
 // "trigger", "trigger state" - a counter, which is paused and gets resumed by the first incoming pulse; a counter waiting to be resumed
 // "pcnt overflow interrupt" - an ISR which gets called each time counter reaches 20000 pulses
 // "GPIO anyedge interrupt" - an ISR which gets called on incoming pulse
 //
#if COMPILING_ESPSHELL

#define TRIGGER_POLL    1000           // A keypress check interval, msec (better keep it >= PULSE_WAIT)
#define PULSE_WAIT      1000           // Default measurement time, msec
#define PCNT_OVERFLOW   20000          // PCNT interrupt every 20000 pulses (range is [1..2^16-1])
#define COUNT_INFINITE  (uint64_t)(-1)  

static int               pcnt_unit = PCNT_UNIT_0;        // First PCNT unit which is allowed to use by ESPShell, convar (accessible thru "var" command)
static int               pcnt_counters = 0;              // Number of currently running counters.

static mutex_t PCNT_mux; // protects access to units[] array

// Hardware counters are described by /units/ array, whose elements are per-counter data;
// units[0] is used for PCNT#0, unit[5] -> PCNT#5 and so on.
// Active (running) counters have their /units[].in_use/ field set to 1 and information frequency informattion is approximate
// When counter stops, its information is filled in its units[] entry: exact number of pulses received, pin number, measurement 
// interval and so on.
//
// Through the code every counter is referenced simply by its number. 
//
static volatile struct {
  unsigned int overflow;    // incremented in an ISR (counter overflow event, fires every 20000 pulses)
  unsigned int count;       // Pulses counted                         (only valid for stopped counters) 
  uint64_t     interval;    // Measurement interval in microseconds   (precise value for stopped counters, approximate for running ones) 

  unsigned int pin:8;            // Pin where pulses were counted

  unsigned int in_use:1;         // non-zero means pcnt unit is used by ESPShell
  unsigned int trigger:1;        // non-zero means pcnt unit is waiting for the first pulse to start counting. this flag is set to 0 by incoming pulse!
  unsigned int been_used:1;      // Set to 1 on a first use, never set to 0 afterwards
  unsigned int been_triggered:1; // is "trigger" counter been triggered by a pulse or was interrupted by user?
  unsigned int filter_enabled:1; // PCNT filter enabled?

  unsigned int filter_value:16;  // PCNT filter value, nanoseconds;

  uint64_t tsta;           // q_micros() just before counting starts
  unsigned int   taskid;         // ID of the task responsible for counting

} units[PCNT_UNIT_MAX] = { 0 };


// Argument of count_pin_anyedge_interrupt(). When interrupt fires, the abovementioned function uses this argument
// to send an event to a calling task.
struct trigger_arg {
  task_t taskid;  // counter's task id
  unsigned int pin;     // pin that has generated the interrupt
};



// PCNT overflow handler. Fired when counting limit is reached (20000 pulses, PCNT_OVERFLOW)
// ISR accesses units[] array without using mutex because this increment won't disrupt any data nor generate illegal memoy access
//
static void IRAM_ATTR pcnt_unit_interrupt(void *arg) {
  const pcnt_unit_t unit = (const pcnt_unit_t )arg;
  units[unit].overflow++;
  PCNT.int_clr.val = BIT(unit);
 }


// ESPShell uses GPIO interrupt to catch the first pulse when counter is in "trigger" mode
// Once pulse is detected the interrupt is fired (count_pin_anyedge_interrupt(void *)) which disables further interrupts
// and unblocks counter task so counter can start counting. All these transient processes **may** have impact on accuracy
// at higher frequencies.
//

// "ISR Services" style interrupt handler.
// Called by the IDF whenever a pulse (edge) is detected on a pin
//
static void IRAM_ATTR count_pin_anyedge_interrupt(void *arg) {

  struct trigger_arg *t = (struct trigger_arg *)arg;
  
  // Send an event to PCNT task blocking on TaskNotifyWait so it can unblock and start counting.
  task_signal_from_isr(t->taskid, SIGNAL_GPIO);
  // Disable further interrupts immediately
  // XXX
  //gpio_set_intr_type(t->pin, GPIO_INTR_DISABLE);
  gpio_intr_disable(t->pin);
}



// Find first unused entry in units[] array.
// Entries are searched from the beginning to the end (i.e. from PCNT0 to PCNT7), however entries which
// number is equal or less than /pcnt_unit/ convar are ignored. This 'offset' is required for cases
// when ESPShell interferes with sketch's PCNT code
//
// The function also increments global active counters count (/pcnt_counters/)

static int count_claim_unit() {
  pcnt_unit_t i;

  mutex_lock(PCNT_mux);
  for (i = pcnt_unit; i < PCNT_UNIT_MAX; i++) {
    if (!units[i].in_use) {

      // found one. mark it as /used/ and clear its counters
      units[i].in_use = 1;
      units[i].been_used = 1; // Only set to 1, never cleared.
      units[i].count = 0;
      units[i].overflow = 0;
      units[i].interval = 0;
      units[i].pin = 0;
      units[i].tsta = 0;
      units[i].trigger = 0;
      units[i].been_triggered = 0;
      units[i].filter_enabled = 0;
      units[i].taskid = (unsigned int)taskid_self(); 
      pcnt_counters++;
      mutex_unlock(PCNT_mux);
      return (int)i;
    }
  }
  // Nothing found. 
  mutex_unlock(PCNT_mux);
  return -1;
}

// Mark PCNT unit as "Stopped"
//
static void count_release_unit(int unit) {
  mutex_lock(PCNT_mux);
  if (unit < PCNT_UNIT_MAX && unit >= 0 && units[unit].in_use) {
    units[unit].in_use = 0;
    units[unit].taskid = 0; // don't display irrelevant TaskID's: suspend/resume/kill on this ID will likely crash whole system
    --pcnt_counters;
  }
  mutex_unlock(PCNT_mux);
}

// Configure & enable interrupts on the unit; installs ISR service and attaches "overflow interrupt" handler
// 
static void count_claim_interrupt(pcnt_unit_t unit) {
  mutex_lock(PCNT_mux);
  pcnt_event_enable(unit, PCNT_EVT_H_LIM);
  pcnt_event_disable(unit, PCNT_EVT_ZERO); // or you will get extra interrupts (x2)

  // Install ISR service, and register an interr2upt handler. Don't use global PCNT interrupt here - it is buggy
  if (pcnt_counters == 1)
    pcnt_isr_service_install(0);

  pcnt_isr_handler_add(unit, pcnt_unit_interrupt, (void *)unit);
  mutex_unlock(PCNT_mux);
}

// Disables events and interrupts on given PCNT unit. If it was the last actiove unit, then global ISR handler
// is unregistered too.
// NOTE: Must be called **before** calling count_release_unit()
//
static void count_release_interrupt(pcnt_unit_t unit) {
  mutex_lock(PCNT_mux);

  pcnt_event_disable(unit, PCNT_EVT_H_LIM);

  // remove per-unit interrupt handler
  pcnt_isr_handler_remove(unit);

  // if there are no active counting units left - uninstall ISR service also
  if (pcnt_counters < 2)
    pcnt_isr_service_uninstall();
  mutex_unlock(PCNT_mux);
}


// Read pulses count, calculates frequency and returns time interval during which measurement were made. Can be called on stopped
// or running counters. Stopped counters retain their values for futher reference via "show counters" shell command.
//
// /unit/     - IN:  PCNT unit number
// /freq/     - OUT: If not NULL: Pointer where calculated frequency will be stored
// /interval/ - OUT: If not NULL: Exact measurement time in microseconds
//
// Returns number of pulses counted
//
static unsigned int count_read_counter(int unit, unsigned int *freq, uint64_t *interval) {

  int16_t      count = 0; // Content of a PCNT counter register (16 bit)
  unsigned int cnt = 0;   // Calculated total number of pulses counted (Number_Of_Interrupts * 20000 + count)
  uint64_t     tsta = 0;  // Total measurement time, usec

  MUST_NOT_HAPPEN(unit < 0  || unit >= PCNT_UNIT_MAX);

  // 1) Counter is running; reading a live counter will result in approximate values
  if (units[unit].in_use) {

    // read values only if it is counting counter. counters in "trigger" state read as 0
    if (!units[unit].trigger) {
      pcnt_get_counter_value(unit, &count);                               // current counter value ([0 .. 20000])
      cnt = units[unit].overflow * PCNT_OVERFLOW + (unsigned int)count;   // counter total. TODO: fix "if this counter was "trigger" then we miss 1 pulse here"
      tsta = q_micros() - units[unit].tsta;                               // microseconds elapsed so far (for the frequency calculation)
    }
  } else {
    // 2) Counter is stopped; values are already in units[]
    cnt = units[unit].count;                                  // Pulse count is calculated already for stopped counters
    tsta = units[unit].interval;                              // Total time spent by command is stored in units[unit].interval
  }

  if (freq)
    *freq = tsta ? (uint32_t)((uint64_t)cnt * 1000000ULL / tsta) : 0;

  if (interval)
    *interval = tsta;

  return cnt;
}

// Human-readable PCNT unit state 
//
static const char *count_state_name(int unit) {

  MUST_NOT_HAPPEN(unit < 0  || unit >= PCNT_UNIT_MAX);

  return units[unit].in_use  ? (units[unit].trigger ? "<i>Trigger</>"
                                                    : "<g>Running</>")
                             : (units[unit].been_used ? "<o>Stopped</>"
                                                      : "Unused ");
}

// Clear counter(s) which was/are associated with given /pin/
// There may be more than 1 PCNT unit associated with given pin.
//
static int count_clear_counter(int pin) {
  int unit;
  mutex_lock(PCNT_mux);
  for (unit = pcnt_unit; unit < PCNT_UNIT_MAX; unit++) {
    if (units[unit].pin == pin && units[unit].been_used) {

      if (units[unit].in_use)
        pcnt_counter_pause(unit);

      pcnt_counter_clear(unit);
      units[unit].count = units[unit].overflow = 0;
      units[unit].interval = 0;

      if (units[unit].in_use) {
        units[unit].tsta = q_micros();
        if (!units[unit].trigger)
          pcnt_counter_resume(unit);
      } else {
        // its ok to clear pin,taskid & tsta on stopped counter
        units[unit].pin = units[unit].taskid = 0;
        units[unit].tsta = 0;
      }
      q_printf("%% Counter #%u (%s state) has been cleared\r\n", unit, count_state_name(unit));
    }
  }
  mutex_unlock(PCNT_mux);
  return 0;
}


// This function blocks until at least 1 of 3 conditions is true:
//
// 1. A task notification SIGNAL_PIN is received (which is sent by a GPIO interrupt handler) 
// 2.                     SIGNAL_TERM is received (which means user issued "kill -9" command)
// 3. A keypress is detected (not applicable for "background" commands)
//
// Returns /0/ when the further processing is better to be stopped
//         />0/  when it is ok to continue with counting.
// /0/ is returned when this function was interrupted by one of conditions above.
//
bool count_wait_for_the_first_pulse(unsigned int pin) {

  struct trigger_arg t = {          // argument for the ISR
    taskid_self(),
    pin
  };

  bool  ret = false,                // Return code. >0 everything is ok, a pulse has been received. 0=stop measurement, discard the result
        fg = is_foreground_task();  // Foreground task? if yes then we add possibility to interrupt it by a keypress

  uint32_t value = SIGNAL_TERM;     // Notification, (sent by an ISR (GPIO interrupt handler) or sent by the "kill" command)

  // Always install the GPIO isr service. Even if it was installed before.
  // The reason for calling it each time is to be sure that code will work as intended even if user sketch has uninstalled GPIO ISR service
  gpio_install_isr_service((int)ARDUINO_ISR_FLAG);
  gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
  gpio_isr_handler_add(pin, count_pin_anyedge_interrupt, &t);
  gpio_intr_enable(pin);
  

  // Keeping TRIGGER_POLL >= PULSE_WAIT helps minimize number of calls to anykey_pressed()
#if TRIGGER_POLL < PULSE_WAIT
#   warning "Trigger poll interval is less than default measurement time. This can decrease frequency meter accuracy"
#endif

  // Wait for a notification:
  // SIGNAL_GPIO is sent by gpio ISR when pulse is received (and this is what we wait for actually)
  // SIGNAL_TERM is sent by command "kill" or (foreground tasks only) by pressing a key
  if (!fg)
    ret = task_wait_for_signal(&value, DELAY_INFINITE);
  else
    // foreground tasks can be interrupted by a keypress, so we poll console with TRIGGER_POLL interval.
    while( (ret = task_wait_for_signal(&value, TRIGGER_POLL)) == false)
      if (anykey_pressed()) 
        break;

  if (value == SIGNAL_TERM) 
    ret = false; // cancel further processing
    
  gpio_isr_handler_remove(pin);

  return ret;
}

// Frequency meter / pulse counter main command
//"count PIN [DELAY_MS | trigger | filter NANOSECONDS]*"
//"count PIN clear"
//
static int cmd_count(int argc, char **argv) {

  pcnt_config_t cfg = { 0 };         // PCNT unit configuration
  unsigned int  pin;                 // Which pin is used to count pulses on?
  uint64_t      wait = PULSE_WAIT;   // Measurement time, in _milliseconds_. Default is 1000ms
  int16_t       count;               // Contents of a PCNT counter
  int           unit,                // PCNT unit number
                i;                   // Index to argv
  bool          filter = false;      // enable filtering
  unsigned short val;                // Normalized filter value [1..1023]
  
  // must be at least 2 tokens ("count" and a pin number)
  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!pin_exist((pin = q_atol(argv[1], 255))))
    return 1; // arg1 is bad

  // "count X clear" command?
  if (argc > 2)
    if (!q_strcmp(argv[2],"clear"))
        return count_clear_counter(pin);

  // Allocate new counter unit: find an index to units[] array which is free to use
  // TODO:3 move it after options processing to get rid of count_release_unit() on errors
  if ((unit = count_claim_unit()) < 0) {
    q_print("% <e>All " xstr(PCNT_UNIT_MAX) "counters are in use</>\r\n% Use \"kill\" to free up counter resources\r\n");
    if (pcnt_unit != PCNT_UNIT_0)
      HELP(q_printf("%% Or decrease the \"pcnt_unit\" variable: (\"var pcnt_unit %u\")\r\n",pcnt_unit - 1));
    return 0;
  }

  // PCNT unit configuration: unit number, pin number, counting mode etc

  cfg.pulse_gpio_num = pin;          // pin where we count pulses
  cfg.ctrl_gpio_num = UNUSED_PIN;    // don't use "control pin" feature
  cfg.channel = PCNT_CHANNEL_0;    
  cfg.unit = unit;                   // Counter unit number. From 0 to 7 on ESP32
  cfg.pos_mode = PCNT_COUNT_INC;     // Increase counter on positive edge
  cfg.neg_mode = PCNT_COUNT_DIS;     // Do nothing on negative edge
  cfg.counter_h_lim = PCNT_OVERFLOW; // Higher limit is 20000 pulses and then an interrupt is generated

  // Read rest of the parameters: DURATION and/or keywords "trigger", "filter" and others
  i = 1; 
  // start from the 2nd argument to the command: if it is the "filter" keyword, then read and check filter value
  while (++i < argc) {
    if (!q_strcmp(argv[i],"filter")) {
      short int low, high;               // min/max filter values. calculated from APB frequency. (TODO: do all Espressif chips have APB?)

      MUST_NOT_HAPPEN(APBFreq == 0);

      // PCNT filter value register is 10-bit wide, with max value of 1023: the number is the "number of cycles of APB bus".
      // Naive reading of Espressif docs on PCNT makes you think that 1 APB cycle is 1/80MHz, i.e. 12.5ns; Experiments with ESP32, however
      // shows that APBFreq must be divided by 2 in order to get things working right.
      // /low/     - lowest possible value for a register, i.e. 1 APB cycle, (25ns if APB is at 80MHz)
      // /high/    - highest possible value, 1023 * 25 ns
      // /1000.0f/ - a scalefactor to convert MHz to ns
#define MAGIC_NUMBER 2      
      low = (short int)(1000.0f * 1.0f / (float )(APBFreq/MAGIC_NUMBER) + 0.5f/* roundup */); 
      high = (short int)(1023 * 1000.0f * 1.0f / (float )(APBFreq/MAGIC_NUMBER));
#undef MAGIC_NUMBER
      if (i + 1 >= argc) {
bad_filter:        
        HELP(q_printf("%% Pulse width in nanoseconds [%d .. %d] is expected\r\n"
                      "%% Time interval precision is %u ns; means %uns and %uns are the same\r\n", low, high, low, 5*low + 1, 6*low - 1));
        count_release_unit(unit);
        return CMD_MISSING_ARG;
      }

      // position to the next argument (a filter value "count 10 trigger filter VALUE")
      // numeric argument is expected
      i++;
      if (isnum(argv[i])) { 
  
        unsigned int val_ns;
        val_ns = val = q_atol(argv[i],0);

        // clamp filter value, convert it from nanoseconds to APB cycles (i.e. to 1..1023 range)
        // We do substract 1 from the divisor to compensate for roundup of /low/ we made before. 
        // This eventually may lead to values > 1023
        if (val < low) val = low; else 
        if (val > high) val = high;
        if ((val = val / (low - 1)) > 1023) val = 1023;

        filter = true;
        // these 2 are purely for "show counters"
        units[unit].filter_enabled = 1;
        units[unit].filter_value = val_ns;
        
      } else
        goto bad_filter;

    } else
    if (!q_strcmp(argv[i],"trigger")) units[unit].trigger = 1; else
    if (!q_strcmp(argv[i],"infinite")) wait = COUNT_INFINITE; else
    if (isnum(argv[i])) wait = q_atol(argv[i], 1000);
    else {
        // unrecognized keyword argv[i]
        count_release_unit(unit);
        return i;
    }
  }
  // Done processing command arguments.

  // Store counter parameters
  units[unit].pin = pin;
  units[unit].interval = (wait == COUNT_INFINITE ? wait : wait * 1000ULL); // store planned time, update it with real one later
  
  q_printf("%% %s pulses on GPIO%d...", units[unit].trigger ? "Waiting for" : "Counting", pin);
  if (is_foreground_task())
    HELP(q_print("(press <Enter> to abort)"));
  q_print(CRLF);

  // Configure selected PCNT unit, stop and clear it
  pcnt_unit_config(&cfg);
  pcnt_counter_pause(unit);
  pcnt_counter_clear(unit);

  if (filter) {
    pcnt_set_filter_value(unit, val );
    pcnt_filter_enable(unit);
    VERBOSE(q_printf("%% PCNT filter is enabled: %u APB cycles (%u ns)\r\n",(uint16_t)val, units[unit].filter_value));
  } else
    pcnt_filter_disable(unit);


  // Allocate & attach interrupt handler for the unit. Unit is configured to generate an interrupt every 20000 pulses.
  count_claim_interrupt(unit);

  // A "trigger" keyword. Wait until first pulse, then proceed normally
  if (units[unit].trigger == 1) {
    
    units[unit].been_triggered = count_wait_for_the_first_pulse(pin) ? 1 : 0;
    units[unit].trigger = 0;

    // interrupted by the "kill" or a keypress while was in waiting state? 
    if (units[unit].been_triggered == 0) {
      q_print("% Interrupted\r\n");
      wait = 0;
      goto release_hardware_and_exit;
    }
  }


   MUST_NOT_HAPPEN(wait == 0);

  // Actual measurement is made here:
  // START
  units[unit].tsta = q_micros();            // record a timestamp in micro seconds
  pcnt_counter_resume(unit);                // enable counter
  delay_interruptible(wait);                // delay 
  pcnt_counter_pause(unit);                 // stop counting as soon as possible to get more accurate results, especially at higher frequencies
  wait = q_micros() - units[unit].tsta;     // actual measurement time in MICROSECONDS
  // STOP
  
 // Free up resources associated with the counter. Free up interrupt, stop and clear counter, calculate
 // frequency, pulses count (yes it is calculated). Store calculated values & a timestamp in /units[]/ for later reference
 // /wait/ is expected to hold measurement interval value in microseconds or 0 at this point.
release_hardware_and_exit:

  // read 16-bit counter value, add Number_Of_Interrupts * Number_Of_Pulses_Per_Interrupt
  pcnt_get_counter_value(unit, &count);

  count_release_interrupt(unit);
  units[unit].count = units[unit].overflow * PCNT_OVERFLOW + (unsigned int)count + units[unit].been_triggered;

  units[unit].interval = wait; 

  // mark this PCNT unit as unused
  count_release_unit(unit);

  // print measurement results. TODO:2 make <1Hz display possible
  unsigned int freq;
  count_read_counter(unit,&freq,NULL);
  q_printf("%% %u pulses in approx. %llu ms (%u Hz, %u IRQs)\r\n", units[unit].count, units[unit].interval / 1000ULL, freq, units[unit].overflow);

  return 0;
}

// Display counters (stopped or running. information is retained on stopped counters)
// as a fancy table.
//
// This one is called from cmd_show(...)
//
static int cmd_show_counters(UNUSED int argc, UNUSED char **argv) {

  int i;
  unsigned int cnt, freq;
  uint64_t interval;

  // Fancy header
  q_print("<r>"
          "%PCNT|Pin|  Status |   TaskID   | Pulse count | Time, msec |Frequency |Filter,ns</>\r\n"
          "%----+---+---------+------------+-------------+------------+----------+---------\r\n");

  mutex_lock(PCNT_mux);
  for (i = 0; i < PCNT_UNIT_MAX; i++) {
    cnt = count_read_counter(i,&freq,&interval);
    
// wish we can have #pragma in #define ..

    q_printf("%%  %d |%3u| %s | 0x%08x | <g>%11u</> | %10u | %8u | ", 
                  i, 
                  units[i].pin,
                  count_state_name(i),
                  units[i].taskid,
                  cnt,
                  (unsigned int )(interval / 1000ULL), // TODO:3 bad typecast
                  freq); 
    if (units[i].filter_enabled)
      q_printf(" <i>%u</>\r\n", units[i].filter_value);
    else
      q_print("-off-\r\n");

  }

  if (pcnt_counters) {
    q_printf("%% %u counter%s %s currently in use\r\n",PPA(pcnt_counters), pcnt_counters == 1 ? "is" : "are");
    HELP(q_print("% Use the command \"<i>kill TASK_ID</>\" to stop a running counter\r\n"));
  }
  else
    q_print("% All counters are stopped\r\n");
  mutex_unlock(PCNT_mux);
  return 0;
}

#endif // #if COMPILING_ESPSHELL
