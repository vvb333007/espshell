/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


 // -- Pulse Counter (PCNT) --
 //
 // ESP32 has 8 pulse counter units each of which is equipped with 2 channels. We use only channel #0 on every unit.
 // ESPShell selects first available unit for its operations: it checks for units in range [pcnt_unit .. PCNT_UNITS_MAX]
 // By default /pcnt_unit/ is set to PCNT_UNIT_0 which allows ESPShell to use any of PCNT units. In case user skript uses 
 // some units , the pcnt_unit value can be adjusted to prevent ESPShell from using these reserved PCNT's.
 //
 // There are different types of counting:
 // 1. Immediate counting: command "count PIN_NUMBER", blocking call
 // 2. Background counting: command "count ... &", async call
 // 3. Triggered counting (either immediate or background when & symbol is used) by using command "count ... trigger"
 //
 // "Trigger" feature uses simple and thus inaccurate logic which is not suitable to count exact number of high frequency pulses
 // When "trigger" keyword is specified, the counter blocks until an interrupt is received from the pin. After that count proceed
 // normally

#if COMPILING_ESPSHELL

#define TRIGGER_POLL    500  // How often check for keyboard press (to abort the command)
#define PULSE_WAIT     1000  // Default counting time, 1000ms
#define PCNT_OVERFLOW 20000  // PCNT interrupt every 20000 pulses
#define UNUSED_PIN       -1  // Pin number when we don't need the function

static int               pcnt_channel = PCNT_CHANNEL_0;  // Channel to use (always channel0), convar. 
static int               pcnt_unit = PCNT_UNIT_0;        // First PCNT unit which is allowed to use by ESPShell,convar (accessible thru "var" command)
static int               pcnt_counters = 0;              // Number of counters currently running.

static MUTEX(pcnt_mux); // protects access to units[] array

// Counter structure. Each element of the array corresponds to separate PCNT unit
// (i.e. entry 5 corresponds to PCNT unit5). Counters currently running have their in_use set to 1. 
// When counter stops its information is filled in this structure: number of pulses received, pin number, measurement interval and so on
//
// NOTE: /overflow/ member is incremented in ISR 
static volatile struct {

  volatile unsigned int  overflow;  // incremented on every PCNT_OVERFLOW pulses
  volatile unsigned int  count;              // Pulses counted                         (only valid for stopped counters) 
  volatile unsigned int  interval;           // Measurement interval in milliseconds   (only valid for stopped counters) 
  volatile unsigned int  pin:8;                // Pin where pulses were counted
  volatile unsigned int  in_use:1;             // non-zero means pcnt unit is used by ESPShell
  volatile unsigned int  trigger:1;             // non-zero means pcnt unit is waiting for the first pulse to start counting
  volatile unsigned int  tsta;               // millis() just before counting starts
  volatile unsigned int  taskid;             // ID of the task responsible for counting

} units[PCNT_UNIT_MAX] = { 0 };



// Per unit service ISR. I were not able to get global ISR version working: two counters
// running in parallel behave chaotically. Interrupts are generated only by unit#0. 
// Per unit version works as intended
//
static void IRAM_ATTR pcnt_unit_interrupt(void *arg) {
  pcnt_unit_t unit = (pcnt_unit_t )arg;
  units[unit].overflow++;
  PCNT.int_clr.val = BIT(unit);
 // pcnt_counter_clear(unit); //TODO: check if we need to clear the counter
}


// Find first unused entry in units[] array. This is used to initialize & start new counter.
// Entries are searched from beginning to the end (from PCNT0 to PCNT7), however entries which
// number is equal or less than /pcnt_unit/ convar are ignored. This 'offset' is required for cases
// when ESPShell interferes with sketch's PCNT code
//
// It alo increments global active counters count. When it hits "1" then the global ISR is installed.
// Once it reaches 0 the global PCNT ISR get uninstalled
static int count_claim_unit() {
  pcnt_unit_t i;

  mutex_lock(pcnt_mux);
  for (i = pcnt_unit; i < PCNT_UNIT_MAX; i++) {
    if (!units[i].in_use) {

      // found one. mark it as /used/ and clear its counters
      units[i].in_use = 1;
      units[i].count = units[i].overflow = units[i].interval = units[i].pin = units[i].tsta = 0;
      units[i].taskid = (unsigned int)taskid_self(); //TODO: change base type in struct definition instead of typecasting here
      pcnt_counters++;
      //count_unlock();
      mutex_unlock(pcnt_mux);
      return (int)i;
    }
  }
  // Nothing found. 
  //count_unlock();
  mutex_unlock(pcnt_mux);
  q_printf("%% All PCNT units are in use (units %u .. %u)\r\n", pcnt_unit, PCNT_UNIT_MAX - 1);
  if (pcnt_unit != PCNT_UNIT_0)
    HELP(q_print("% Try to decrease \"pcnt_unit\" variable: (\"var pcnt_unit 0\")\r\n"));
  return -1;
}

// Mark PCNT unit as /unused/
static void count_release_unit(int unit) {
  mutex_lock(pcnt_mux);
  if (unit < PCNT_UNIT_MAX && unit >= 0 && units[unit].in_use) {
    units[unit].in_use = 0;
    units[unit].taskid = 0;
    --pcnt_counters;
  }
  mutex_unlock(pcnt_mux);
}

// must be called before pcnt_unit_release();
// disables events and interrupts on unit. If it was the last unit used then global ISR handler
// is unregistered too
//
static void count_release_interrupt(pcnt_unit_t unit) {
  mutex_lock(pcnt_mux);
  pcnt_event_disable(unit, PCNT_EVT_H_LIM);

  //q_printf("%% Removing ISR for PCNT unit%u\r\n",unit);
  pcnt_isr_handler_remove(unit);

  if (pcnt_counters < 2) {
   // q_print("% Last counter is stopped: unregistering PCNT service ISR\r\n");
    pcnt_isr_service_uninstall();
  }
  mutex_unlock(pcnt_mux);
}

// Enable H_LIM event & enable interrupts on unit
// If it is the first counter used by ESPShell then global PCNT ISR handler is registered
// 
static void count_claim_interrupt(pcnt_unit_t unit) {
  mutex_lock(pcnt_mux);
  pcnt_event_enable(unit, PCNT_EVT_H_LIM);
  pcnt_event_disable(unit, PCNT_EVT_ZERO);
  if (pcnt_counters == 1) {
   // q_print("% Registering PCNT service ISR\r\n");
    pcnt_isr_service_install(0);
  }
  //q_printf("%% Adding ISR handler for unit %u\r\n",unit);
  pcnt_isr_handler_add(unit, pcnt_unit_interrupt, (void *)unit);
  mutex_unlock(pcnt_mux);
}

// For stopped counters exact value is returned
// For running counters an approximate value is returned
// Read pulses count, calculates frequency and returns time interval during which measurement were made
//
static unsigned int count_read_counter(int unit, unsigned int *freq, unsigned int *interval) {

  int16_t count = 0;
  unsigned int cnt = 0, tsta = 0;

  if (unit < 0  && unit >= PCNT_UNIT_MAX)
    abort(); //must not happen

  // Find out number of pulses counted so far & frequency
  // 1) Counter is running; reading a live counter will result of approximate values
  if (units[unit].in_use) {
    // read values only if it is counting counter. counters in trigger state read as 0
    if (!units[unit].trigger) {
      pcnt_get_counter_value(unit, &count);                               // current counter value ([0 .. 20000])
      cnt = units[unit].overflow * PCNT_OVERFLOW + (unsigned int)count;   // counter total
      tsta = millis() - units[unit].tsta;                                 // milliseconds elapsed so far (for the frequency calculation)
    }
  } else {
    // 2) Counter is stopped; values are already in units[]
    cnt = units[unit].count;                                  // Pulse count is calculated already for stopped counters
    tsta = units[unit].interval;                              // Total time spent by command is stored in units[unit].interval
  }

  if (freq)
    *freq = tsta ? (uint32_t)((uint64_t)cnt * 1000ULL / (uint64_t)tsta) : 0; // TODO: should probably use uint64_t here

  if (interval)
    *interval = tsta;

  return cnt;
}

// Used by "count PIN clear" to clear counters for given pin
// Clears all counters (running or stopped) which were associated with given pin
// or are associated now
//
static int count_clear_counter(int pin) {
  int unit;
  mutex_lock(pcnt_mux);
  for (unit = pcnt_unit; unit < PCNT_UNIT_MAX; unit++) {
    if (units[unit].pin == pin) {

      if (units[unit].in_use)
        pcnt_counter_pause(unit);

      pcnt_counter_clear(unit);
      units[unit].count = units[unit].overflow = units[unit].interval = 0;

      if (units[unit].in_use) {
        if (!units[unit].trigger)
          pcnt_counter_resume(unit);
        units[unit].tsta = millis();
      } else {
        // its ok to clear pin,taskid & tsta on stopped counter
        units[unit].pin = units[unit].taskid = units[unit].tsta = 0;
      }
      q_printf("%% %s counter#%u has been cleared\r\n",units[unit].in_use ? "Running" : "Stopped / Trigger",unit);
    }
  }
  mutex_unlock(pcnt_mux);
  return 0;
}

// Human-readable PCNT unit state 
// /i/ is the PCNT unit index (array units[] index)
//
static const char *count_state_name(int i) {

  const char *st = "Unused!";

  if (units[i].in_use)
    st = units[i].trigger ? "<i>Trigger</>" : "<3>Running</>";
  else if (units[i].count || units[i].overflow || units[i].pin)
    st = "<1>Stopped</>";

  return st;
}

// Display counters (stopped or running. information is retained on stopped counters)
// This one is called from cmd_show(...)
//
static int count_show_counters() {

  int i;
  unsigned int cnt, interval;
  unsigned int freq;
  const char *st;

  // Fancy header
  q_print("<r>"
          "%PCNT|GPIO#|  Status |   TaskID   | Pulse count | Time, msec | Frequency  </>\r\n"
          "%----+-----+---------+------------+-------------+------------+------------\r\n");

  mutex_lock(pcnt_mux);
  for (i = 0; i < PCNT_UNIT_MAX; i++) {
    cnt = count_read_counter(i,&freq,&interval);
    st = count_state_name(i);

  #pragma GCC diagnostic ignored "-Wformat"
    q_printf("%%  %d | % 3u | %s | 0x%08x | % 11u | % 10u | % 8u Hz\r\n", i, units[i].pin, st, units[i].taskid, cnt, interval, freq);
  #pragma GCC diagnostic warning "-Wformat"
  }

  if (pcnt_counters) {
    q_printf("%% %u counter%s is currently in use\r\n",pcnt_counters,pcnt_counters == 1 ? "" : "s");
    HELP(q_print("% Use command \"<i>kill TASK_ID</>\" to stop a running counter\r\n"));
  }
  else
    q_print("% All counters are stopped\r\n");
  mutex_unlock(pcnt_mux);
  return 0;
}

// PIN interrupt helpers. ESPShell uses GPIO interrupt to catch the first pulse when counter is in "trigger" mode
// Once pulse is detected the interrupt is fired (count_pin_anyedge_interrupt(void *)) which disables further interrupts
// and unblocks counter task so counter can start counting

// Pointer to the struct below is passed to GPIO interrupt handler
struct trigger_arg {
  TaskHandle_t taskid;  // counter's task id
  unsigned int pin;     // pin that has generated interrupt
};


// GPIO interrupt handler. We use GPIO Service ISR approach: per-GPIO interrupts
//
static void IRAM_ATTR count_pin_anyedge_interrupt(void *arg) {
  struct trigger_arg *t = (struct trigger_arg *)arg;
  // Send an event to PCNT task blocking on TaskNotifyWait so it can unblock and start counting.
  task_signal_from_isr(t->taskid, SIGNAL_GPIO);
  // Disable further interrupts immediately
  gpio_set_intr_type(t->pin, GPIO_INTR_DISABLE);
}


// block until:
// 1. a pulse comes to the pin OR
// 2. "kill" commmand detected
// 3. command interrupted by a keypress (foreground commands only)
//
bool count_wait_for_the_first_pulse(unsigned int pin) {

  struct trigger_arg t;
  bool ret = false;
  bool fg = is_foreground_task();

  t.pin = pin;
  t.taskid = taskid_self();

  gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
  gpio_install_isr_service((int)ARDUINO_ISR_FLAG);   // can be called multiple times
  gpio_isr_handler_add(pin, count_pin_anyedge_interrupt, &t);

  uint32_t value = 0;

  while( (ret = task_wait_for_signal(&value, TRIGGER_POLL)) == false)
    if (fg && anykey_pressed())
      break;

  // 1. "kill" command sends "0", as notification value, (pcnt_pin_interrupt() sends 1). Upon reception of a "kill" (i.e. SIGNAL_KILL)
  //    this function returns false which prevents cmd_count() from execution
  //                                 OR
  // 2. Value was unchanged from default 0 and that means task_wait_for_signal(...) has been interrupted by a keypress and thus
  //    we don't want to continue with the rest of cmd_count()
  if (value == 0) 
    ret = false;
    
  // Disable further interrupts
  
  gpio_isr_handler_remove(pin);

  return ret;
}




//TAG:count
//"count PIN [DELAY_MS]"
// Count pulses on given pin
//
static int cmd_count(int argc, char **argv) {

  pcnt_config_t cfg = { 0 };
  unsigned int pin, wait = PULSE_WAIT;
  int16_t count;
  int unit,i;
  
  if (argc < 2)
    return -1;

  if (!pin_exist((pin = q_atol(argv[1], 999))))
    return 1;

  // "count X clear" command?
  if (argc > 2)
    if (!q_strcmp(argv[2],"clear"))
        return count_clear_counter(pin);

  // Allocate new counter unit
  if ((unit = count_claim_unit()) < 0) {
    q_print("% <e>Can not allocate new PCNT hardware</>\r\n");
    return 0;
  }

  // Configure counting unit
  cfg.pulse_gpio_num = pin;          // pin where we count pulses
  cfg.ctrl_gpio_num = UNUSED_PIN;    // don't use "control pin" feature
  cfg.channel = pcnt_channel;        // Normally CHANNEL_0 but can be changed via convar
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
      count_release_unit(unit);
      return i;
    }
  }

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

  // Allocate & attach interrupt handler for the unit
  count_claim_interrupt(unit);

  if (units[unit].trigger) {
    bool triggered = count_wait_for_the_first_pulse(pin); // blocking call
    units[unit].trigger = 0;
    if (!triggered) { // interrupted by "kill" or a keypress while was in waiting state
      q_print("% Interrupted\r\n");
      goto release_hardware_and_exit;
    }
  }
  // Start counting for /wait/ milliseconds
  units[unit].tsta = millis(); // TODO: make our own q_millis()
  pcnt_counter_resume(unit);
  /*wait = */delay_interruptible(wait);
  pcnt_counter_pause(unit);
  wait = millis() - units[unit].tsta; // actual measurement time

release_hardware_and_exit:
  pcnt_get_counter_value(unit, &count);

  count_release_interrupt(unit);
  units[unit].count = units[unit].overflow * PCNT_OVERFLOW + (unsigned int)count + units[unit].trigger;

  // real value. it will be different from planned value if command "count" was interrupted
  units[unit].interval = wait;

  count_release_unit(unit);
  q_printf("%% %u pulses in %.3f seconds (%.1f Hz, %u interrupts)\r\n", units[unit].count, (float)wait / 1000.0f, units[unit].count * 1000.0f / (float)wait,units[unit].overflow);
  return 0;
}
#endif // #if COMPILING_ESPSHELL
