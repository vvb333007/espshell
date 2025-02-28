/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


 // -- Pulse Counter (PCNT) --
 //
 // ESP32 has 8 pulse counter units (ESP32S3 has 4) each of which is equipped 
 // with 2 channels: channel#0 (is what espshell uses) and channel#1 (unused).
 //
  // ESPShell selects first available unit for its operations: it checks for units in range [pcnt_unit .. PCNT_UNITS_MAX]
 // By default /pcnt_unit/ is set to PCNT_UNIT_0 which allows ESPShell to use any of PCNT units. In case user sketch uses 
 // some units , the /pcnt_unit/ value can be adjusted to prevent ESPShell from using these reserved PCNT's.
 //
 // There are different types of counting:
 //
 // 1. Immediate counting: command "count PIN_NUMBER", a blocking call
 // 2. Background counting: command "count ... &", async call
 // 3. Triggered counting (either immediate or background: "count ... trigger" or  "count ... trigger &"
 //
 // "Trigger" feature uses simple and thus inaccurate logic which is not suitable to count **exact** number of high frequency pulses
 // When "trigger" keyword is specified, the counter blocks until an interrupt is received from the pin. After that count proceed
 // normally

#if COMPILING_ESPSHELL

#define TRIGGER_POLL   1000  // A keypress check interval, msec (better be >= PULSE_WAIT)
#define PULSE_WAIT     1000  // Default measurement time, msec
#define PCNT_OVERFLOW 20000  // PCNT interrupt every 20000 pulses (range is [1..2^16-1])
#define UNUSED_PIN       -1  

static int               pcnt_unit = PCNT_UNIT_0;        // First PCNT unit which is allowed to use by ESPShell, convar (accessible thru "var" command)
static int               pcnt_counters = 0;              // Number of counters currently running.

static MUTEX(pcnt_mux); // protects access to units[] array

// Counter structure. Each element of the array corresponds to separate PCNT unit
// (i.e. entry 5 corresponds to PCNT unit5). Counters currently running have their /.in_use/ set to 1. 
// When counter stops, its information is filled in this structure: number of pulses received, pin number, measurement interval and so on
//
// NOTE: /.overflow/ member is incremented in ISR 
//
static volatile struct {
  unsigned int  overflow;    // incremented after every PCNT_OVERFLOW pulses
  unsigned int  count;       // Pulses counted                         (only valid for stopped counters) 
  unsigned int  interval;    // Measurement interval in milliseconds   (only valid for stopped counters) 
  unsigned int  pin:8;       // Pin where pulses were counted
  unsigned int  in_use:1;    // non-zero means pcnt unit is used by ESPShell
  unsigned int  trigger:1;   // non-zero means pcnt unit is waiting for the first pulse to start counting. this flag is set to 0 by incoming pulse!
  unsigned int  been_used:1; // Set to 1 on a first use, never set to 0 afterwards
  unsigned int  been_triggered:1;
  unsigned int  tsta;        // millis() just before counting starts
  unsigned int  taskid;      // ID of the task responsible for counting

} units[PCNT_UNIT_MAX] = { 0 };


// PCNT interrupt handler. Fired when counting limit is reached (20000 pulses, PCNT_OVERFLOW)
//
static void IRAM_ATTR pcnt_unit_interrupt(void *arg) {
  pcnt_unit_t unit = (pcnt_unit_t )arg;
  units[unit].overflow++;
  PCNT.int_clr.val = BIT(unit);
 }


// Find first unused entry in units[] array.
// Entries are searched from beginning to the end (i.e. from PCNT0 to PCNT7), however entries which
// number is equal or less than /pcnt_unit/ convar are ignored. This 'offset' is required for cases
// when ESPShell interferes with sketch's PCNT code
//
// The function also increments global active counters count (/pcnt_counters/)

static int count_claim_unit() {
  pcnt_unit_t i;

  mutex_lock(pcnt_mux);
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
      units[i].taskid = (unsigned int)taskid_self(); 
      pcnt_counters++;
      mutex_unlock(pcnt_mux);
      return (int)i;
    }
  }
  // Nothing found. 
  mutex_unlock(pcnt_mux);
  return -1;
}

// Mark PCNT unit as "Stopped"
static void count_release_unit(int unit) {
  mutex_lock(pcnt_mux);
  if (unit < PCNT_UNIT_MAX && unit >= 0 && units[unit].in_use) {
    units[unit].in_use = 0;
    units[unit].taskid = 0; // don't display irrelevant TaskID's: suspend/resume/kill on this ID will likely crash whole system
    --pcnt_counters;
  }
  mutex_unlock(pcnt_mux);
}

// Disables events and interrupts on given PCNT unit. If it was the last actiove unit, then global ISR handler
// is unregistered too.
// NOTE: Must be called **before** calling count_release_unit()
//
static void count_release_interrupt(pcnt_unit_t unit) {
  mutex_lock(pcnt_mux);

  pcnt_event_disable(unit, PCNT_EVT_H_LIM);

  // remove per-unit interrupt handler
  pcnt_isr_handler_remove(unit);

  // if there are no active counting units left - uninstall ISR service also
  if (pcnt_counters < 2)
    pcnt_isr_service_uninstall();
  mutex_unlock(pcnt_mux);
}

// Enable H_LIM event & enable interrupts on the unit. 
// 
static void count_claim_interrupt(pcnt_unit_t unit) {
  mutex_lock(pcnt_mux);
  pcnt_event_enable(unit, PCNT_EVT_H_LIM);
  pcnt_event_disable(unit, PCNT_EVT_ZERO); // or you will get extra interrupts (x2)

  // Install ISR service, and register an interrupt handler. Don't use global PCNT interrupt here - it is buggy
  if (pcnt_counters == 1)
    pcnt_isr_service_install(0);

  pcnt_isr_handler_add(unit, pcnt_unit_interrupt, (void *)unit);
  mutex_unlock(pcnt_mux);
}

// Read pulses count, calculates frequency and returns time interval during which measurement were made. Can be called on stopped
// or running counters. Stopped counters retain their values for futher reference via "show counters" shell command.
//
// /unit/     -  PCNT unit number
// /freq/     - If not NULL: Pointer where calculated frequency will be stored
// /interval/ - If not NULL: Exact measurement time
//
// Returns number of pulses counted
//
static unsigned int count_read_counter(int unit, unsigned int *freq, unsigned int *interval) {

  int16_t      count = 0;    // Content of a PCNT counter register (16 bit)
  unsigned int cnt = 0,      // Calculated total number of pulses counted (Number_Of_Interrupts * 20000 + count)
               tsta = 0;     // Total measurement time

  MUST_NOT_HAPPEN(unit < 0  || unit >= PCNT_UNIT_MAX);

  // 1) Counter is running; reading a live counter will result in approximate values
  if (units[unit].in_use) {

    // read values only if it is counting counter. counters in "trigger" state read as 0
    if (!units[unit].trigger) {
      pcnt_get_counter_value(unit, &count);                               // current counter value ([0 .. 20000])
      cnt = units[unit].overflow * PCNT_OVERFLOW + (unsigned int)count;   // counter total. TODO: fix "if this counter was "trigger" then we miss 1 pulse here"
      tsta = q_millis() - units[unit].tsta;                               // milliseconds elapsed so far (for the frequency calculation)
    }
  } else {
    // 2) Counter is stopped; values are already in units[]
    cnt = units[unit].count;                                  // Pulse count is calculated already for stopped counters
    tsta = units[unit].interval;                              // Total time spent by command is stored in units[unit].interval
  }

  if (freq)
    *freq = tsta ? (uint32_t)((uint64_t)cnt * 1000ULL / (uint64_t)tsta) : 0;

  if (interval)
    *interval = tsta;

  return cnt;
}

// Human-readable PCNT unit state 
// /i/ is the PCNT unit index (array units[] index)
//
static const char *count_state_name(int i) {
  return units[i].in_use  ? (units[i].trigger ? "<i>Trigger</>"
                                              : "<g>Running</>")
                          : (units[i].been_used ? "<o>Stopped</>"
                                                : "Unused ");
}

// Clear counter(s) which was/are associated with given /pin/
// There may be more than 1 PCNT unit associated with given pin.
//
static int count_clear_counter(int pin) {
  int unit;
  mutex_lock(pcnt_mux);
  for (unit = pcnt_unit; unit < PCNT_UNIT_MAX; unit++) {
    if (units[unit].pin == pin && units[unit].been_used) {

      if (units[unit].in_use)
        pcnt_counter_pause(unit);

      pcnt_counter_clear(unit);
      units[unit].count = units[unit].overflow = units[unit].interval = 0;

      if (units[unit].in_use) {
        if (!units[unit].trigger)
          pcnt_counter_resume(unit);
        units[unit].tsta = q_millis();
      } else {
        // its ok to clear pin,taskid & tsta on stopped counter
        units[unit].pin = units[unit].taskid = units[unit].tsta = 0;
      }
      q_printf("%% Counter #%u (while is in %s state) has been cleared\r\n", unit, count_state_name(unit));
    }
  }
  mutex_unlock(pcnt_mux);
  return 0;
}

// Display counters (stopped or running. information is retained on stopped counters)
// as a fancy table.
//
// This one is called from cmd_show(...)
//
static int count_show_counters() {

  int i;
  unsigned int cnt, interval,freq;

  // Fancy header
  q_print("<r>"
          "%PCNT|GPIO#|  Status |   TaskID   | Pulse count | Time, msec | Frequency  </>\r\n"
          "%----+-----+---------+------------+-------------+------------+------------\r\n");

  mutex_lock(pcnt_mux);
  for (i = 0; i < PCNT_UNIT_MAX; i++) {
    cnt = count_read_counter(i,&freq,&interval);
    
// wish we can have #pragma in #define ..
  #pragma GCC diagnostic ignored "-Wformat"
    q_printf("%%  %d | % 3u | %s | 0x%08x | % 11u | % 10u | % 8u Hz\r\n", i, units[i].pin, count_state_name(i), units[i].taskid, cnt, interval, freq);
  #pragma GCC diagnostic warning "-Wformat"
  }

  if (pcnt_counters) {
    q_printf("%% %u counter%s %s currently in use\r\n",PPA(pcnt_counters), pcnt_counters == 1 ? "is" : "are");
    HELP(q_print("% Use command \"<i>kill TASK_ID</>\" to stop a running counter\r\n"));
  }
  else
    q_print("% All counters are stopped\r\n");
  mutex_unlock(pcnt_mux);
  return 0;
}

// Argument of count_pin_anyedge_interrupt(). When interrupt fires, the abovementioned function uses this argument
// to send an event to a calling task.
struct trigger_arg {
  TaskHandle_t taskid;  // counter's task id
  unsigned int pin;     // pin that has generated the interrupt
};


// PIN interrupt helpers. 
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
  gpio_set_intr_type(t->pin, GPIO_INTR_DISABLE);
}


// This function blocks until at least 1 of 3 conditions is true:
//
// 1. A task notification SIGNAL_PIN is received (which is sent by a GPIO interrupt handler when (see function above)) 
// 2.                     SIGNAL_TERM is received (which means user issued "kill -9" command)
// 3. A keypress is detected (not applicable for "background" commands)
//
// Returns /false/ when the further processing is better to be stopped
//         /true/  when it is ok to continue with counting.
// /False/ is returned when this function was interrupted by one of conditions above.
//
bool count_wait_for_the_first_pulse(unsigned int pin) {

  struct trigger_arg t = {          // An argument for the ISR
    taskid_self(),
    pin
  };

  bool  ret = false,                // Return code. true= everything is ok, a pulse has been received. false=stop measurement, discard the result
        fg = is_foreground_task();  // Foreground task? if yes then we add possibility to interrupt it by a keypress

  uint32_t value = SIGNAL_TERM;     // Notification, sent by an ISR (GPIO interrupt handler) or sent by the "kill" command

  gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);

  // It is ok to call it multiple times, however IDF issues a warning; 
  // the reason for calling it each time is to be sure that code will work as intended even if user sketch has uninstalled GPIO ISR service
  gpio_install_isr_service((int)ARDUINO_ISR_FLAG);            
  gpio_isr_handler_add(pin, count_pin_anyedge_interrupt, &t);

  static_assert(TRIGGER_POLL >= PULSE_WAIT,"Set trigger poll interval >= default measurement time");

  if (!fg)
    ret = task_wait_for_signal(&value, 0); // for a background tasks we dont have to poll. Delay of 0 means "wait forever"
  else
    // Detect keypress only if we are running in foreground.
    // We do poll here instead of interrupt/task notification scheme because of its simplicity.
    // Default value of a TRIGGER_POLL must be equal or higher than default measurement time to exclude calls to anykey_pressed()
    while( (ret = task_wait_for_signal(&value, TRIGGER_POLL)) == false)
      if (anykey_pressed()) 
        break;

  // Either "kill" command (sends SIGNAL_TERM) or interrupted by a keypress (/value/ didn't change, so SIGNAL_TERM again)
  // In both cases we don't want to continue execution of calling function (i.e. cmd_count()) so return /false/ here
  if (value == SIGNAL_TERM) 
    ret = false;
    
  gpio_isr_handler_remove(pin);

  return ret;
}

// Shell command handler
//"count PIN [DELAY_MS] [trigger]"
//"count PIN clear"
//
// TODO: investigate /filter/ feature and add appropriate arguments to the "count" command
static int cmd_count(int argc, char **argv) {

  pcnt_config_t cfg = { 0 };         // PCNT unit configuration
  unsigned int  pin,                 // Which pin is used to count pulses on?
                wait = PULSE_WAIT;   // Measurement time. Default is 1000ms
  int16_t       count;               // Contents of a PCNT counter
  int           unit,                // PCNT unit number
                i;                   // Index to argv
  
  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!pin_exist((pin = q_atol(argv[1], 999))))
    return 1;

  // "count X clear" command?
  if (argc > 2)
    if (!q_strcmp(argv[2],"clear"))
        return count_clear_counter(pin);

  // Allocate new counter unit: find an index to units[] array which is free to use
  if ((unit = count_claim_unit()) < 0) {
    q_print("% <e>All " xstr(PCNT_UNIT_MAX) "counters are in use</>\r\n% Use \"kill\" to free up counter resources\r\n");
    if (pcnt_unit != PCNT_UNIT_0)
      HELP(q_printf("%% Or decrease \"pcnt_unit\" variable: (\"var pcnt_unit %u\")\r\n",pcnt_unit - 1));
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

  // Read rest of the parameters: DURATION and/or keyword "trigger"
  i = 1;
  while (++i < argc) {

    if (!q_strcmp(argv[i],"trigger"))
      units[unit].trigger = 1;
    else
    if ((wait = q_atol(argv[2], DEF_BAD)) == DEF_BAD) {
      HELP(q_print("% A keyword \"trigger\" or a NUMBER (duration, msec) is expected\r\n"));
      count_release_unit(unit);
      return i;
    }
  }

  // Store counter parameters
  units[unit].pin = pin;
  units[unit].interval = wait; // store planned time, update it with real one later
  
  q_printf("%% %s pulses on GPIO%d...", units[unit].trigger ? "Waiting for" : "Counting", pin);
  if (is_foreground_task())
    HELP(q_print("(press <Enter> to abort)"));
  q_print(CRLF);

  // Configure selected PCNT unit, stop and clear it
  pcnt_unit_config(&cfg);
  pcnt_counter_pause(unit);
  pcnt_counter_clear(unit);

  // Allocate & attach interrupt handler for the unit. Unit is configured to generate an interrupt every 20000 pulses.
  count_claim_interrupt(unit);

  // A "trigger" keyword. Wait until first pulse, then proceed normally
  if (units[unit].trigger == 1) {
    
    units[unit].been_triggered = count_wait_for_the_first_pulse(pin) ? 1 : 0; //TODO: ref
    units[unit].trigger = 0;

    // interrupted by the "kill" or a keypress while was in waiting state? 
    if (units[unit].been_triggered == 0) {
      q_print("% Interrupted\r\n");
      goto release_hardware_and_exit;
    }
  }

  // Record a timestamp and start counting pulses for /wait/ milliseconds.
  units[unit].tsta = q_millis();
  pcnt_counter_resume(unit);

  // delay_interruptible() returns its argument if wasn't interrupted.
  if (wait != delay_interruptible(wait))  
    wait = q_millis() - units[unit].tsta; // actual measurement time

  // Stop
  pcnt_counter_pause(unit);
  
 // Free up resources associated with the counter. Free up interrupt, stop and clear counter, calculate
 // frequency, pulses count (yes it is calculated). Store calculated values & a timestamp in /units[]/ for later reference
release_hardware_and_exit:

  // read 16-bit counter value, add Number_Of_Interrupts * Number_Of_Pulses_Per_Interrupt
  pcnt_get_counter_value(unit, &count);

  count_release_interrupt(unit);
  units[unit].count = units[unit].overflow * PCNT_OVERFLOW + (unsigned int)count + units[unit].been_triggered;

  // real value. it will be different from planned value if command "count" was interrupted
  units[unit].interval = wait;

  // mark this PCNT unit as unused
  count_release_unit(unit);

  // print measurement results
  q_printf("%% %u pulses in %.3f seconds (%.1f Hz, %u interrupts)\r\n", units[unit].count, (float)wait / 1000.0f, units[unit].count * 1000.0f / (float)wait,units[unit].overflow);
  return 0;
}
#endif // #if COMPILING_ESPSHELL
