/* 
 * This file is a part of the ESPShell Arduino library (Espressif's ESP32-family CPUs)
 *
 * Latest source code can be found at Github: https://github.com/vvb333007/espshell/
 * Stable releases: https://github.com/vvb333007/espshell/tags
 *
 * Feel free to use this code as you wish: it is absolutely free for commercial and 
 * non-commercial, education purposes.  Credits, however, would be greatly appreciated.
 *
 * Author: Viacheslav Logunov <vvb333007@gmail.com>
 */

// -- "If" and "Every": GPIO / Timer / Network events --
//
// An "ifcond" (short for "if condition") is a data structure that holds:
// - a GPIO or timer event
// - the action to perform in response
// - extra statistics (hit count and timestamp of last trigger)
//
// ifconds are created by the "if" and "every" shell commands and added to a global
// array (see ifconds[] below). They can be deleted using "if delete" or
// "every delete".
//
// When listed (see ifc_show_all()), each ifcond is assigned an ID number,
// which can be used to manipulate it: delete it, clear its timestamp,
// or reset the hit counter.
//
// When a GPIO interrupt occurs, all ifconds bound to that GPIO are checked,
// and their associated aliases are executed. Timed events are managed
// by esp_timer.
//
// Thread safety:
// The ifcond list is protected by a global rwlock (ifc_rw) *and* by disabling
// GPIO interrupts. Userspace tasks acquire the reader lock; the ISR does not
// use locks at all.
// Any modification of the ifcond list (performed by the "if" and "every"
// commands) guarantees that the writer lock is held and GPIO interrupts
// are disabled.
// (See ifc_delete0(...) for details on proper locking.)
//
// An extra layer of safety comes from the fact that both struct ifcond and
// struct alias are *persistent pointers*, meaning they always point to
// valid memory.
//
// TODO: Variables: if ($var_name eq|lt|gt|le|ge|ne imm)*
// TODO: One-shots: absence of rising/falling/poll keywords indicates a one-shot
//       condition, which is discarded after use
// TODO: Refactor to use userinput_read_timespec
// TODO: WiFi and IP event catcher (if got|lost ip, if sta|ap connected)
// TODO: "break" keyword to interrupt alias execution

#ifdef COMPILING_ESPSHELL
#if WITH_ALIAS

// ifcond: this is what the "if rising 5 low 6 high 10 ..." or
//         "every 1 day ..." commands are parsed into.
//
// There is a list of ifconds for each pin: ifconds[0..NUM_PINS-1], used by
// "if rising|falling X ..." commands.
// ifconds in a list always belong to the same pin. Rising/falling ifconds
// for GPIO1 are stored in ifconds[1].
//
// There is a list of "polled" ifconds: ifconds[NO_TRIGGER], used by
// "if low|high X ... poll ...".
//
// These ifconds are not bound to any pin: they are activated by timers,
// not by GPIO interrupts.
//
// There is a list of "every" ifconds: ifconds[EVERY_IDX], used by
// "every ..." commands.
// This is a variant of polled ifconds.
//
struct ifcond {

  struct ifcond *next;      // ifconds with the same pin
  struct alias  *exec;      // pointer to the alias. Alias pointers are persistent
                            // and always valid.
                            // Only one alias per ifcond; use multiple ifconds
                            // to execute multiple aliases.

  uint8_t trigger_pin;      // GPIO where a RISING or FALLING edge is expected, or
                            // NO_TRIGGER for a pure conditional ifcond, or
                            // EVERY_IDX for ifconds created by the "every" command

  uint8_t trigger_rising:1; // 1 == rising, 0 == falling
  uint8_t has_high:1;       // has "high" statements?
  uint8_t has_low:1;        // has "low" statements?
  uint8_t has_limit:1;      // has "max-exec" keyword?
  uint8_t has_rlimit:1;     // has "rate-limit" keyword?
  uint8_t has_delay:1;      // has "delay" keyword?
  uint8_t alive:1;          // is this entry active, or is it on the ifc_unused list?
                            // set/reset by ifc_get() and ifc_put()
  uint8_t disabled:1;       // disabled entries skip alias execution

  uint16_t id;              // unique ID for delete/clear commands
  uint16_t rlimit;          // once per X milliseconds (max ~1 time per minute,
                            // 65535 ms)
  uint32_t poll_interval;   // poll interval, in seconds, for non-trigger ifconds
  timer_t  timer;           // timer handle for periodic events
  uint32_t limit;           // max number of hits
                            // if (ifc->hits > ifc->limit) { ignore } else { process }
  uint32_t delay_ms;        // initial delay; used for "every" ifconds

  uint32_t high;            // GPIO 0..31: bit X set => GPIO X must be HIGH
  uint32_t high1;           // GPIO 32..63: ...
  uint32_t low;             // GPIO 0..31: bit X set => GPIO X must be LOW
  uint32_t low1;            // GPIO 32..63: ...
  // TODO: members accessed from an ISR must be volatile
  uint32_t hits;            // number of times this condition matched
  uint32_t drops;           // number of times alias execution was skipped
                            // (rate-limited or max-exec-limited)
  uint64_t tsta;            // timestamp, microseconds: time when the condition matched
  uint64_t tsta0;           // previous timestamp: time when the alias was last executed
                            // updated from tsta on each alias execution
};


// index of "no trigger" entry in the ifconds array
#define NO_TRIGGER NUM_PINS

// index where "every" command stores its rules
#define EVERY_IDX (NO_TRIGGER + 1)

// Ifconds array. Each element of the array is a list of ifconds.
// For example, ifconds[5] contains all "if rising|falling 5" statements.
// "No trigger" statements (i.e. those without rising or falling keywords)
// are stored in ifconds[NO_TRIGGER].
static struct ifcond *ifconds[NUM_PINS + 2] = { 0 };   // +1 for the NO_TRIGGER entry and +1 for "every" entries

// RWLock protecting the ifcond lists. The lists are modified only by the
// "if/every" and "if/every delete" commands (the "writers").
// All others are "readers", including the GPIO ISR, which traverses these lists.
// Therefore, acquiring the writer lock alone is not sufficient: GPIO interrupts
// must also be disabled, since the ISR does not use any locking mechanism at all.
//
static rwlock_t ifc_rw = RWLOCK_INITIALIZER_UNLOCKED;

// Defines the size of the message pipe. If the ISR below matches more than
// MPIPE_CAPACITY ifconds, any additional ones are dropped.
// For example, if 17 "if" statements are triggered at once, the 17th message
// sent from the ISR to ifc_task() will be discarded.
// Do not set this too small.
//
// If it is too large, no events will be missed, but it will consume more RAM.
// Do not set this too large either.

#define MPIPE_CAPACITY 16

static void ifc_task(void *arg);

// message pipe (from the ifc_anyedge_interrupt() and esp_timer to ifc_task())
static mpipe_t ifc_mp = MPIPE_INIT;
static unsigned int ifc_mp_drops = 0;


#define IFCOND_PRIORITY 22  // Run at esp_timer priority so that both esp_timer-driven
                           // events and interrupt-driven events run at the same
                           // priority level


// Create message pipe and start a daemon task
//
static __attribute__((constructor)) void __ifc_init() {

  task_t ifc_handle = NULL; // daemon task id

  if ((ifc_mp = mpipe_create(MPIPE_CAPACITY)) != MPIPE_INIT) {
    if ((ifc_handle = task_new(ifc_task, NULL, "ifcond", shell_core)) != NULL) {
      task_set_priority(ifc_handle, IFCOND_PRIORITY); 
    } else {

      mpipe_destroy(ifc_mp);
      ifc_mp = NULL;
    }
  }
}

// Check if we request given ifcond too fast. Flood protection
//
// Having 64 bit math on counters on a 32bit arch in a time-critical code is not a good idea,
// so unlike ifc_not_expired(), this one (ifc_too_fast()) is called from a daemon task, not from the ISR
//
// Note on the timer counter wrap/overflow: it happens in ~500000 years 
//
static inline bool ifc_too_fast(struct ifcond *ifc) {
  return  ifc->has_rlimit && 
         (ifc->tsta - ifc->tsta0 < 1000ULL*ifc->rlimit);
}

// Check whether an ifcond must not be used because it has reached the "max-exec"
// limit or was manually disabled.
// This is a macro rather than a function; otherwise it would have to be declared
// with IRAM_ATTR.
//
#define ifc_not_expired(_Ifc) \
  (!_Ifc->disabled && (_Ifc->has_limit ? _Ifc->hits < _Ifc->limit : true))

// Mark ifcond entry as "disabled"
#define ifc_set_disabled(_Ifc) \
  _Ifc->disabled = 1;

// Clear "disabled" flag
#define ifc_clear_disabled(_Ifc) \
  _Ifc->disabled = 0;

// GPIO mask indicating where an ISR is registered. A set bit (e.g. bit 17)
// means that GPIO17 has an ISR attached.
// This mask is used when enabling/disabling GPIO interrupts as part of
// access protection in ifc_delete().
//
static uint64_t isr_enabled = 0;

// Check if GPIO has ISR handler installed
#define ifc_isr_is_registered(_Gpio) \
  (isr_enabled & (1ULL << (_Gpio)))

// Set "ISR Installed" flag for pin _Gpio
#define ifc_set_isr_registered(_Gpio) \
  (isr_enabled |= (1ULL << (_Gpio)))

// Clear "ISR Installed" flag for pin _Gpio
#define ifc_clear_isr_registered(_Gpio) \
  (isr_enabled &= ~(1ULL << (_Gpio)))


// GPIO interrupt routine, implemented using the GPIO ISR Service API.
// ESP-IDF provides a global GPIO handler that calls user-defined routines.
//
// Using the GPIO ISR Service (as opposed to a custom global GPIO handler)
// reduces friction when coexisting with sketches that also use GPIO
// interrupts. Arduino sketches use GPIO interrupts via the GPIO ISR
// Service, so we do the same.
//
// We always install the ISR service, even if it is already installed:
// a user sketch may unregister it.
//
// Handles "trigger" ifconds, i.e. ifconds with "rising" or "falling" keywords.
//
static void IRAM_ATTR ifc_anyedge_interrupt(void *arg) {

  bool rising;
  unsigned int pin = (unsigned int )arg;

  // ifc points to the head of ifcond list associated with given pin
  struct ifcond *ifc = ifconds[pin];

// Read pin values (all at once, via a direct register read).
// We need them:
//   1. for edge detection (rising or falling; ESP32 provides no edge type
//      indication when an interrupt occurs)
//   2. for condition matching (the "cond" part of an ifcond)
//
  uint32_t in = REG_READ(GPIO_IN_REG);   // GPIO 0..31
  uint32_t in1 = REG_READ(GPIO_IN1_REG); // GPIO 32..63
  
  // Edge detect: if pin is HIGH, then it was "rising" event
  rising = pin < 32 ? (in & (1UL << pin))
                    : (in1 & (1UL << (pin - 32)));


// Traverse the "if" clauses (all associated with this pin).
// No locking is used here (although ideally an RWLock would be).
// The ifcond list is modified only by the "if" shell command,
// either when adding a new ifcond or deleting one (via "if delete").
//
// Instead of locking, GPIO interrupts are temporarily disabled
// while the "if" command runs, preventing ifc_anyedge_interrupt()
// from traversing a list that is being modified.
//
// The list is traversed from an ISR, so keep it short.
//
// Gotos were added in an attempt to reduce execution time.

  bool force_yield = false;

  while (ifc) {
    // Do quick reject in the ISR, do not offload it to the ifc_task()
    // Number of "if"s with the same trigger pin is what can slow things down
    // 1. edge match?
    if (ifc->trigger_rising == rising) {
    // 2. entry is not expired?
      if (ifc_not_expired(ifc)) {
    // 3. "high" condition match?
        if (ifc->has_high)
          if ((ifc->high & in) != ifc->high  ||   // MASK & READ_VALUES == MASK?
              (ifc->high1 &  in1) != ifc->high1)
            goto next_ifc;
            
    // 4. "low" condition match?
        if (ifc->has_low)
          if ((ifc->low & ~in) != ifc->low  ||
              (ifc->low1 &  ~in1) != ifc->low1)
            goto next_ifc;

    // 5. Full match: send the ifc pointer to ifc_task() and continue processing
    //    (there may be more matched ifconds). ifc_task() will drain the queue,
    //    fetching pointers and executing the associated aliases.
    
        force_yield |= mpipe_send_from_isr(ifc_mp, ifc);
      } else // if expired
        ifc->drops++;
    } // edge match?
next_ifc:
    ifc = ifc->next;
  }

  // mpipe_send() has unblocked a higher priority task: request rescheduling.
  if (force_yield)
    q_yield_from_isr();
}



// I don't think we need them, but it is left here for future extensions
//
static void ifc_disable_periodic_timers() {}
static void ifc_enable_periodic_timers() {}

// Allocate timer for polled events (if ... poll and every ...poll)
//
static void ifc_claim_timer(struct ifcond *ifc, bool delayed_already);

// Timer callback for polled entries (e.g. "if low 5 poll ..." or "every ...").
// Called periodically by the esp_timer system task.
// This is analogous to the ifc_anyedge_interrupt() handler, but used for polling.
//
// Reminder: this code relies on pointer persistence. This means that deleting
// an ifcond ("if delete") does not immediately make it inaccessible — access
// to it remains valid. Access to its ->exec (a pointer to an alias) is also safe,
// since alias pointers are persistent as well.
//
// However, by the time this callback runs, the user may have already deleted
// the ifcond. Deleting an ifcond also removes its timer, but if the timer still
// manages to fire, it will execute that "deleted" condition. This is undesirable,
// but still preferable to a memory access violation.
//
static void ifc_callback(void *arg) {

  uint32_t in, in1;
  struct ifcond *ifc;
  
  MUST_NOT_HAPPEN(arg == NULL);

  ifc = (struct ifcond *)arg;

  // Must be "alive" (code integrity check)
  MUST_NOT_HAPPEN (ifc->alive == 0);

  // Read all GPIO values
  in = REG_READ(GPIO_IN_REG);   // GPIO 0..31
  in1 = REG_READ(GPIO_IN1_REG); // GPIO 32..63

  // 1. entry is not expired/disabled? 
  if (ifc_not_expired(ifc)) {
  // 2. "high" condition match?
    if (ifc->has_high)
      if ((ifc->high & in) != ifc->high  ||   // MASK & READ_VALUES == MASK?
          (ifc->high1 &  in1) != ifc->high1)
        return ;
            
  // 3. "low" condition match?
    if (ifc->has_low)
      if ((ifc->low & ~in) != ifc->low  ||
          (ifc->low1 &  ~in1) != ifc->low1)
        return ;

  // 4. Send to the ifc_task() for execution
    if (mpipe_send(ifc_mp, ifc))
      return ;
  }

  ifc->drops++;
}

// When an entry has the "delay" keyword, its first execution is postponed - hence the name.
//
static void ifc_callback_delayed(void *arg) {
  struct ifcond *ifc = (struct ifcond *)arg;
  if (ifc) {

    if (!ifc->alive) // TODO: not gonna work on multicore CPU
      return ;

    // delay time has passed: execute alias and schedule periodic timer. remove old timer
    if (!ifc->disabled)
      ifc_callback(arg);
    else
      ifc->drops++;

    // Stop one-shot timer and schedule new, periodic timer
    esp_timer_stop(ifc->timer);
    esp_timer_delete(ifc->timer);
    ifc->timer = TIMER_INIT;

    // Claim the timer again, with the 'true' flag indicating that the delay
    // has already been applied.
    ifc_claim_timer(ifc, true);
  }
}

// Allocate a timer for polled events, either periodic or one-shot.
// The mode depends on ifc.has_delay:
// if the ifcond specifies the "delay" option, the timer is set up in two steps:
//
//   1. Start a one-shot timer with a duration of ifc.delay_ms.
//   2. When it expires, schedule a periodic timer.
//
// This function may also be called from the one-shot timer itself to set up
// the periodic events. That is the purpose of the second argument:
//   - Normally, it should be set to 'false'.
//   - It is set to 'true' only when invoked from the one-shot timer callback.
//
static void ifc_claim_timer(struct ifcond *ifc, bool delayed_already) {

  timer_t handle = TIMER_INIT;

  MUST_NOT_HAPPEN(ifc == NULL);
  MUST_NOT_HAPPEN(ifc->exec == NULL);

  // Default timer callback is ifc_callback
  esp_timer_create_args_t timer_args = {
      .callback = &ifc_callback,
      .dispatch_method = ESP_TIMER_TASK,
      .arg = ifc,
      .name = ifc->exec->name,
  };

  // 2 stage-setup for delayed events:
  // First callback to be called is ifc_callback_delayed(), which reclaims timer again, and sets up periodic timer
  if (ifc->has_delay && !delayed_already)
    timer_args.callback = &ifc_callback_delayed;
#ifdef ESP_TIMER_ISR  
    // Experimental: no delay / or delayed already: we can use "interrupt" dispatch method here because all we do in our
    // callback is sending an ifcond to the execution task.
  else
    timer_args.dispatch_method = ESP_TIMER_ISR;
#endif

  if (ESP_OK == esp_timer_create(&timer_args, &handle)) {
    ifc->timer = handle;
    if (ifc->has_delay && !delayed_already)
      esp_timer_start_once(handle, 1000ULL * ifc->delay_ms);
    else {
      // First executions is right now, subsequent - after a delay
      if (!delayed_already)
        ifc_callback(ifc);
      esp_timer_start_periodic(handle, 1000ULL * ifc->poll_interval);
    }
  } else {
    VERBOSE(q_print("% Failed to create timer\r\n"));
  }
}

// Release timer, delete callbacks
//
static void ifc_release_timer(struct ifcond *ifc) {
  if (likely(ifc != NULL && ifc->timer != NULL)) {
    esp_timer_stop(ifc->timer);
    esp_timer_delete(ifc->timer);
    ifc->timer = TIMER_INIT;
  }
}

// Request an interrupt for the pin. If it is already registered, do nothing.
// Otherwise, install a GPIO ANYEDGE interrupt handler and enable interrupts on the pin.
//
static void ifc_claim_interrupt(uint8_t pin) {

  // pin existence must be checked before calling ifc_claim_interrupt!
  MUST_NOT_HAPPEN( pin_exist_silent(pin) == false );

  // If we have requested interrupt before - do nothing
  if (!ifc_isr_is_registered(pin)) {

    ifc_set_isr_registered(pin);

    // gpio_install_isr_service() can be called multiple times — if it is already
    // installed, it simply returns with a warning.
    // Since the shell must coexist with a user sketch, which may call
    // gpio_isr_service_uninstall(), we should restore our interrupt handling
    // whenever possible.

    gpio_install_isr_service((int)ARDUINO_ISR_FLAG);
    gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(pin, ifc_anyedge_interrupt, (void *)(unsigned int)pin);
    gpio_intr_enable(pin);
  }
}

// Release a GPIO interrupt. The last user disables the GPIO interrupt.
//
// Interrupts are disabled only when no conditions exist for the given trigger pin.
// For example, if ifc_claim_interrupt(5, ...) is called six times, the interrupt is
// registered on the first call; the remaining five calls have no effect.
// When removing an ifcond, the entry must first be removed from the list, and only
// then should ifc_release_interrupt() be called.
// The latter checks whether the corresponding ifcond[] list is empty.
//
static void ifc_release_interrupt(uint8_t pin) {
  if (pin_exist_silent(pin)) {
    if (ifc_isr_is_registered(pin)) {
      // When releasing an interrupt for a pin, we check if any ifconds are still associated with it.
      // If there are no remaining "if"s (i.e., the ifcond list is empty), we can remove the ISR handler
      // and disable interrupts for that pin.
      //
      if (!ifconds[pin]) {
        gpio_intr_disable(pin);
        gpio_isr_handler_remove(pin);
        ifc_clear_isr_registered(pin);
        
      }
    } else {
      VERBOSE(q_printf("%% ifc_release_interrupt() : GPIO#%u, ISR is not registered!\r\n",pin));
    }
  }
}


// Display the content of a single ifcond by its pointer.
// Shows information in a compact (clamped) form: this is a /brief/ version of ifc_show_single().
//
// NOTE:
// This function is intended to be used within ifc_show_all() and is assumed to be called in a loop,
// repeatedly. Therefore, it takes a *pointer* as a parameter.
// In contrast, ifc_show_single() takes an *ifcond ID* and performs a search internally.
// Shortened keywords are used (e.g., rate instead of rate-limit) due to limited space (80 columns).
//
static void ifc_show(struct ifcond *ifc) {

  if (ifc) {
    uint8_t i;

    if (ifc->trigger_pin == EVERY_IDX)
      q_print("every ");
    else if (ifc->trigger_pin != NO_TRIGGER)
      q_printf("if %s %u ", ifc->trigger_rising ? "rising" : "fall", ifc->trigger_pin);
    else
      q_print("if ");

    if (ifc->has_high)
      for (i = 0; i <32; i++) {
        if (ifc->high & (1UL << i))  q_printf("hi %u ",i);
        if (ifc->high1 & (1UL << i)) q_printf("hi %u ",i + 32);
      }

    if (ifc->has_low)
      for (i = 0; i <32; i++) {
        if (ifc->low & (1UL << i))  q_printf("lo %u ",i);
        if (ifc->low1 & (1UL << i)) q_printf("lo %u ",i + 32);
      }

    // poll_interval is either poll interval for the "if" statement
    // or a frequency of an "every" statement. Measured in milliseconds (49 days max interval)
    if (ifc->poll_interval) {
      if (ifc->trigger_pin == EVERY_IDX) {
        // Some heuristics for "every" variant, to save screen space in table view:
        //   For times below 10 sec  display "XXXX millis"
        //   Anything above 120 sec is displayed in minutes
        //   Anything in between is displayed as seconds
        if (ifc->poll_interval < 10000)
          q_printf("%lu milli ",ifc->poll_interval);
        else if (ifc->poll_interval > 120*1000)
          q_printf("%lu min ",ifc->poll_interval / (1000 * 60));
        else
          q_printf("%lu sec ",ifc->poll_interval / 1000);
      } else
        q_printf("poll %lu ",ifc->poll_interval);
    }

    if (ifc->has_delay)
      q_printf("delay %lu ",ifc->delay_ms);

    // shortened to save screen space
    if (ifc->has_limit)
      q_printf("max %lu ",ifc->limit);

    // shortened to save screen space
    if (ifc->has_rlimit)
      q_printf("rate %u ",ifc->rlimit);

    // Use quotes: aliases could have spaces in their names and we want to generate
    // "executable" output, which can be simply copy/pasted to the espshell prompt again
    if (ifc->exec)
      q_printf("exec \"%s\"",ifc->exec->name);

    q_print(CRLF);
  }
}

// Display an ifcond by its ID.
// Shows more detailed information for a given ifcond than "show ifs"
// (the table view).
// Used by "show if NUM" and mainly intended for viewing counters
// greater than 99999, which are not fully displayed in the table view
// ("show ifs").
//
static void ifc_show_single(unsigned int num) {
  int i,j;

  rw_lockr(&ifc_rw);
  for (i = j = 0; i < NUM_PINS + 2; i++) { // NUM_PINS+2 is the size of ifconds array: GPIOs+Polling+Every events
    struct ifcond *ifc = ifconds[i];
    while (ifc) {
      if (ifc->id == num) {

        const char *cname = ifc->trigger_pin == EVERY_IDX ? "every" : "if";

        q_printf("%% \"%s\" condition #%u", cname, num);
        if (!ifc->hits)
          q_print(", never executed (triggered)");
        if (ifc->disabled)
          q_printf(", <w>disabled</>, (\"%s enable %u\" to enable)",cname, num);
        else if (!ifc_not_expired(ifc))
          q_printf(", <w>expired</>, (\"%s clear %u\" to reset)",cname, num);
        q_print(CRLF);

        if (ifc->hits)
          q_printf("%% Last executed: <i>%llu</> seconds ago, <i>%lu</> times total\r\n",(q_micros() - ifc->tsta0) / 1000000ULL, ifc->hits);
        

        if (ifc->drops)
          q_printf("%% Execution skipped (event dropped): <i>%lu</> times\r\n",ifc->drops);

        if (ifc->has_limit)
          q_printf("%% Expires after <i>%lu</> executions (%s)\r\n",ifc->limit, ifc_not_expired(ifc) ? "Not expired yet" : "Expired already");
        else
          q_print("% Never expires\r\n");

        if (ifc->has_rlimit)
          q_printf("%% Minimum interval between two executions: <i>%u</> ms\r\n",ifc->rlimit);
        else
          q_print("% Not rate-limited\r\n");
        
        if (ifc->poll_interval)
          q_printf("%% Poll interval: every %lu milliseconds\r\n", ifc->poll_interval);
        
        if (ifc->has_delay)
          q_printf("%% Initial (first exec) delay: %lu milliseconds\r\n", ifc->delay_ms);

        // ifconds are created with non-null alias pointer even alias was not existing: ifc_create() creates
        // alias if it does not exist. Alias pointers are persistent (always valid, even for a deleted alias)
        MUST_NOT_HAPPEN(ifc->exec == NULL);

        // See if alias is empty or not. TODO: make alias_is_empty() macro
        if (ifc->exec->lines == NULL)
          q_printf("%% Note that alias <i>\"%s\" is empty!</> (\"alias %s\" to edit)\r\n", ifc->exec->name, ifc->exec->name);
        else
          q_printf("%% Action: <i>Execute alias \"%s\"</>\r\n", ifc->exec->name);
        rw_unlockr(&ifc_rw);
        return ;
      }

      ifc = ifc->next;
    }
  }
  rw_unlockr(&ifc_rw);
  q_print("% Wrong ID. Use \"<i>show ifs</>\" to list all IDs)\r\n");
}

// TODO: remove, make generic version and move it in qlib!!!
// These functions are not reentrant, use with care: subsequent calls destroy previous value
// as it is static buffers used. Because of this these functions are not general use API and thus is not in qlib
//
// Convert seconds to "XXX day", "XXX sec", "XXX hrs" and so on, 7 symbols
static char *q_strtime(uint32_t seconds) {

  static char buf[8] = { 0 };
  uint32_t divider = 60*60*24;

  if (seconds >= divider) {
    sprintf(buf,"%3lu", seconds / divider);
    strcat(buf, "day");
    return buf;
  }
  divider /= 24;
  if (seconds >= divider) {
    sprintf(buf,"%3lu", seconds / divider);
    strcat(buf, "hrs");
    return buf;
  }
  divider /= 60;
  if (seconds >= divider) {
    sprintf(buf,"%3lu", seconds / divider);
    strcat(buf, "min");
    return buf;
  }
  sprintf(buf,"%3lu", seconds);
  strcat(buf, "sec");
  return buf;
}

// Display 5 digit number as is, but after 99999 display ">99999"
// WARNING: returns static buffer, not reentrant!
static char *q_strnum_sat(uint32_t num) {
  static char buf[8] = { 0 };
  if (num > 99999)
    strcpy(buf,">99999");
  else
    sprintf(buf,"%lu",num);
  return buf;
}

// Display all ifconds and assign a number to each entry.
// These numbers can be used to delete or clear an entry
// ("if delete|clear NUMBER|all").
//
static void ifc_show_all() {
  int i,j;

  q_printf("%%<r>ID#|  Hits | Last | Drops| Condition and action                               </>\r\n"
           "%%---+-------+------+------+----------------------------------------------------\r\n");

  rw_lockr(&ifc_rw);
  for (i = j = 0; i < NUM_PINS + 2; i++) {
    struct ifcond *ifc = ifconds[i];
    while (ifc) {
      j++;
      const char *pre = " ", *pos = "";
      if (!ifc_not_expired(ifc)) {
        pre = "<w>!";
        pos = "</>";
      }
      if (ifc->hits)
        q_printf("%%%3u|%s%6lu%s|%6s|%6s|",
            ifc->id, pre, ifc->hits, pos,
            q_strtime((uint32_t)((q_micros() - ifc->tsta) / 1000000ULL)),
            q_strnum_sat(ifc->drops));
      else
        q_printf("%%%3u|%s%6lu%s|never |%6s|",ifc->id, pre, ifc->hits, pos,q_strnum_sat(ifc->drops));
      ifc_show(ifc);
      ifc = ifc->next;
    }
  }
  rw_unlockr(&ifc_rw);

  if (!j)
    q_print("%\r\n% <i>No conditions were defined</>; Use command \"if\" to add some\r\n");
  else
    q_print("%---+-------+------+------+----------------------------------------------------\r\n");
}

// Delete/Clear ifcond entry/entries
//
#define ifc_delete(X)       ifc_delete0(X, false)           // delete one entry by its ID
#define ifc_delete_pin(X)   ifc_delete0(-X, false)          // delete all entries that are triggered by pin X
#define ifc_delete_poll()  ifc_delete0(-NO_TRIGGER, false) // delete all "if low|high poll" entries
#define ifc_delete_every() ifc_delete0(-EVERY_IDX, false)  // delete all "every" entries
#define ifc_delete_all()    ifc_delete0(0, true)            // delete all entries

#define ifc_clear(X)     ifc_clear0(X, false)               // clear counters for one entry by its ID
#define ifc_clear_pin(X) ifc_clear0(-X, false)              // clear counters for all entries that are triggered by pin X
#define ifc_clear_poll()  ifc_clear0(-NO_TRIGGER, false) // clear all "if low|high poll" entries
#define ifc_clear_every() ifc_clear0(-EVERY_IDX, false)  // clear all "every" entries
#define ifc_clear_all()  ifc_clear0(0, true)                // clear counters for all entries


#define MULTIPLE_IFCONDS ((num <= 0) || all) // have to process multiple ifconds or just one?


static struct mb_pool ifc_pool = MB_POOL(sizeof(struct ifcond),0);




// Allocate an ifcond. (this function is a twin brother of ha_get() in task.h)
// It is either allocated via malloc() or, if available, reused from the pool of deleted ifconds.
//
static struct ifcond *ifc_get() {

  static _Atomic uint16_t id = 1;
  struct ifcond *ret = NULL;

  if (NULL != (ret = mb_get(&ifc_pool))) {
    ret->exec = NULL;
    ret->timer = TIMER_INIT;
    ret->alive = 1;
    ret->disabled = 0;
    ret->id = atomic_fetch_add_explicit(&id, 1, memory_order_relaxed); // wrap is allowed
  }
  return ret;
}

// Return unused ifcond back to "ifc_unused" list
//
static void ifc_put(struct ifcond *ifc) {
  if (likely(ifc)) {

    if (unlikely(ifc->timer != NULL))
      VERBOSE(q_printf("ifc_put() : ifcond.id=%u has an active timer still counting\r\n",ifc->id));

    if (unlikely(ifc->alive == 0))
      VERBOSE(q_printf("ifc_put() : ifcond.id=%u is dead\r\n",ifc->id));

    // alive flag is checked by timer callbacks. code logic prevents callbacks to fire if corresponding ifcond
    // entry is removed, because corresponding timers are removed also. It is an extra layer of safety
    ifc->alive = 0;
    ifc->disabled = 1;

    mb_put(&ifc_pool, ifc);
  }
}

// Delete an ifcond entry (or all entries)
//
// num <=0 ? -1*num is a pin number. Remove all entries belonging to that pin 
//           (i.e. whole ifconds[-num] list). Note that specifying -NO_TRIGGER will remove 
//           all "polling" ifconds.
// num > 0 ? num is the ifcond ID. Remove one single ifcond and exit
// all == true ? ignore /num/ and remove all entries
//
static void ifc_delete0(int num, bool all) {

  int i, max_entry = EVERY_IDX; // by default we run through /if/ and /every/ entries

  MUST_NOT_HAPPEN(!all && num <= -(NUM_PINS + 2));

  // If we are about to modify one of the ifconds[] lists, acquire writers lock
  rw_lockw(&ifc_rw);

  // If num <= 0, i.e. removal of all ifconds that belongs to GPIO abs(num) is requested
  // then we start our "for" loop from that requested GPIO number and only perform 1 cycle, and finish with "break"
  //
  // If num > 0, i.e. removal of specified ifcond (by its ID, num == ID) was requested, then 
  // we process all pins starting from 0 to NUM_PINS
  //
  // If /all/ is /true/, then value of /num/ is ignored, all entries are 
  // removed, except /every/ entries
  //
  if (all) {
    num = 0;
    max_entry = NO_TRIGGER;
  }

  for (i = num < 0 ? -num : 0; i <= max_entry; i++) {

    // if there are ifconds associated with the pin, we disable GPIO interrupts on this particular GPIO
    // to prevent ifc_anyedge_interrupt() from traversing ifconds lists
    if (ifconds[i]) {

      // 0..NUM_PINS-1 => GPIOs ("if rising|falling" conditions) 
      // NUM_PINS == NO_TRIGGER => "if poll" conditions
      // NUM_PINS+1 == EVERY_IDX => "every" conditions
      if (i < NO_TRIGGER)
        gpio_intr_disable(i);
      else
        ifc_disable_periodic_timers();
      

      struct ifcond *ifc = ifconds[i], *prev = NULL;

      while (ifc) {
        // Found an item user wishes to delete
        // If num < 0, then we got a match, because we are traversing list which belongs to the pin -num
        // If num == ID, then we got a match for this particular ID. Once it processed - job is done
        if (MULTIPLE_IFCONDS || ifc->id == num) {
            // Unlink /ifc/ from the list and q_free() it
           
            struct ifcond *tmp = ifc;
            if (!prev)
              ifc = ifconds[i] = ifc->next;
            else
              ifc = prev->next = ifc->next;

            // Interrupt must be released AFTER ifc is unlinked from the list:
            // ifc_release_interrupt() checks if list is empty and if it is - uninstalls the ISR
            // Real GPIO: attempt to elease interrupt
            // Timed events: release the rimer
            if (tmp->trigger_pin < NO_TRIGGER)
              ifc_release_interrupt(tmp->trigger_pin);
            else
              ifc_release_timer(tmp);
            

            // return ifcond memory to the pool
            ifc_put(tmp);

          // We had processed 1 element. Should we continue or return?
          if (!MULTIPLE_IFCONDS) {

            rw_unlockw(&ifc_rw);

            // Enable interrupts (for real GPIOs only,and only if there is an ISR handler registered)
            // For periodic events and polling conditions - enable timer service
            if (i < NO_TRIGGER && ifc_isr_is_registered(i))
              gpio_intr_enable(i); 
            if (i >= NO_TRIGGER)
              ifc_enable_periodic_timers();

            return;
          }
        } else {
          // Proceed to the next ifcond

          prev = ifc;
          ifc = ifc->next;
        }
      } // while(ifc)


      // Enable interrupts (for real GPIOs only,and only if there is an ISR handler registered)
      // For periodic events and polling conditions - enable timer service
      if (i < NO_TRIGGER && ifc_isr_is_registered(i))
        gpio_intr_enable(i); 
      if (i >= NO_TRIGGER)
        ifc_enable_periodic_timers();

      // If we were processing an ifcond chain that belongs to a GPIO - we are done.
      if (!all && (num <= 0))
        break;

    }
  }
  rw_unlockw(&ifc_rw);

}

// Clear counters (hits and tsta)
// Arguments are the same as those for ifc_delete0, the difference is that we use readers lock. Yes we are actually
// writing to the counters but it is ok as long as we do not modify the ifcond list itself.
//
// NOTE: clearing /hits/ will reenable expired ifconds (those with "max-exec" or "rate-limit" keywords)
//
static void ifc_clear0(int num, bool all) {

  int i;

  MUST_NOT_HAPPEN(!all && num <= -(NUM_PINS + 2));

  // Clearing counters does not modify list itself, so treat clearing as a reader's operation
  rw_lockr(&ifc_rw);

  // "all" means all :). Triggered and non-triggered ifconds.
  // Non-triggered entries belong to non-existing pins NO_TRIGGER and EVERY_IDX
  
  if (all) {
    ifc_mp_drops = 0; // "all" also clears global mpipe drops counter
    num = 0;          // start with pin#0
  }

  for (i = num < 0 ? -num : 0; i <= EVERY_IDX; i++) {

    if (ifconds[i]) {
      struct ifcond *ifc = ifconds[i];

      while (ifc) {
        // Found an item user wishes to clear?
        if (MULTIPLE_IFCONDS || ifc->id == num) {
            ifc->hits = 0;
            ifc->tsta0 = 0;
            ifc->drops = 0;
            ifc->tsta = q_micros();

            // Clear by icond ID? return then. 
            // NOTE: ifc->id is always > 0, so "if clear 0" is about the GPIO#0, not ifcond.id == 0
            if (ifc->id == num) {
              rw_unlockr(&ifc_rw);
              return;
            }
        }
        ifc = ifc->next;
      }

      // If we were deleting/clearing ifconds for specified GPIO - break the "for" loop, we are done
      if (!all && num <= 0)
        break;
    } // if ifconds[i]
  } // for each pin
  rw_unlockr(&ifc_rw);
}


// Create an ifcond.
//
// trigger_pin : either a pin number (rising/falling events), or NO_TRIGGER
// rising      : must be true for rising events, ignored otherwise
// high        : GPIO mask for pins expected to be HIGH, or 0 if "dont care"
// low         : GPIO mask for pins expected to be LOW, or 0 if "dont care"
// limit       : if >0, then sets the limit for number of executions. Counter can be reset via "clear if counters"
// exec        : alias name to execute on successfull match
//
static struct ifcond *ifc_create( uint8_t     trigger_pin, 
                                  bool        rising, 
                                  uint64_t    high, 
                                  uint64_t    low,
                                  uint32_t    limit,
                                  const char *exec) {

  
  struct alias *al;
  struct ifcond *n;

  // enforce alias creation if it doesn't exist
  if ((al = alias_create_or_find(exec)) == NULL)
    return NULL;

  // allocate a new ifcond structure, fill in values and link to ifconds[pin] list
  if ((n = ifc_get()) != NULL) {

    // n->id is already initialized by ifc_get()
    n->trigger_pin = trigger_pin;
    n->trigger_rising = rising;
    n->disabled = 0;
    n->exec = al;
    n->has_delay = 0;
    n->delay_ms = 0;
    n->has_rlimit = 0;
    n->rlimit = 0;
    n->poll_interval = 0;
    n->timer = TIMER_INIT;
    n->has_limit = limit > 0;
    n->limit = limit;

    // Passed as 64 bit types, pin masks are split to 32 bit chunks
    // for faster processing
    n->has_low = low > 0;
    n->has_high = high > 0;

    n->low = (uint32_t)(low & 0xffffffffULL);
    n->low1 = (uint32_t)((low >> 32) & 0xffffffffULL);
    n->high = (uint32_t)(high & 0xffffffffULL);
    n->high1 = (uint32_t)((high >> 32) & 0xffffffffULL);

    n->hits = 0;
    n->drops = 0;
    n->tsta = q_micros();
    n->tsta0 = 0;
    
    // disable interrupts on real GPIOs, do nothing for NO_TRIGGER 
    if (trigger_pin < NO_TRIGGER)
      gpio_intr_disable(trigger_pin);

    // Still need to rw_lockw(), even with interrupts disabled : 
    // command "if" MAY be executed not from the main espshell context (e.g. execution of an alias, containing "if" statements)
    // Insert new item into pin's ifcond list
    rw_lockw(&ifc_rw);
    n->next = ifconds[trigger_pin];
    ifconds[trigger_pin] = n;
    rw_unlockw(&ifc_rw);

    // if trigger_pin is a real GPIO and ISR (must be) enabled - reenable it. 
    if (trigger_pin < NUM_PINS && ifc_isr_is_registered(trigger_pin))
      gpio_intr_enable(trigger_pin);
  }
  return n;
}

// ifcond daemon.
// Reads data arriving on message pipe, and executes them. Messages are pointers to the ifcond structure which 
// needs to be executed.
// Update timestamp & hits counter, execute corresponding alias in a background
//
static void ifc_task(void *arg) {

  while( true ) {
    struct ifcond *ifc = (struct ifcond *)mpipe_recv(ifc_mp);
    if (ifc) {
      
      // Store the timestamp.
      // It is required for ifc_too_fast()
      ifc->tsta = q_micros();
      if (!ifc_too_fast(ifc)) {
        ifc->tsta0 = ifc->tsta;
        // Exec in a background as separate task because we can not block here: multiple
        // events can fire shortly one after another
        alias_exec_in_background(ifc->exec);
        ifc->hits++;
      } else
        ifc->drops++;
    }
  }
  /* UNREACHED */
  task_finished();
  
}

// Variant of ifc_show() which is fullform and outputs to a file stream.
// This one is used to save espshell configuration
//
static void ifc_show_fp(FILE *fp, struct ifcond *ifc) {

  if (ifc) {
    uint8_t i;

    if (ifc->trigger_pin == EVERY_IDX)
      fprintf(fp, "every ");
    else if (ifc->trigger_pin != NO_TRIGGER)
      fprintf(fp, "if %s %u ", ifc->trigger_rising ? "rising" : "falling", ifc->trigger_pin);
    else
      fprintf(fp, "if ");

    if (ifc->has_high)
      for (i = 0; i <32; i++) {
        if (ifc->high & (1UL << i))  fprintf(fp, "high %u ",i);
        if (ifc->high1 & (1UL << i)) fprintf(fp, "high %u ",i + 32);
      }

    if (ifc->has_low)
      for (i = 0; i <32; i++) {
        if (ifc->low & (1UL << i))  fprintf(fp, "low %u ",i);
        if (ifc->low1 & (1UL << i)) fprintf(fp, "low %u ",i + 32);
      }

    // poll_interval is either poll interval for the "if" statement
    // or a frequency of an "every" statement. Measured in milliseconds (49 days max interval)
    if (ifc->poll_interval) {
      if (ifc->trigger_pin == EVERY_IDX) {
        // Some heuristics for "every" variant, to save screen space in table view:
        //   For times below 10 sec  display "XXXX millis"
        //   Anything above 120 sec is displayed in minutes
        //   Anything in between is displayed as seconds
        if (ifc->poll_interval < 10000)
          fprintf(fp, "%lu millis ",ifc->poll_interval);
        else if (ifc->poll_interval > 120*1000)
          fprintf(fp, "%lu min ",ifc->poll_interval / (1000 * 60));
        else
          fprintf(fp, "%lu sec ",ifc->poll_interval / 1000);
      } else
        fprintf(fp, "poll %lu ",ifc->poll_interval);
    }

    // Normally, only EVERY entries can have delays, but we let it be for the "future extensions (c)"
    if (ifc->has_delay)
      fprintf(fp, "delay %lu ",ifc->delay_ms);

    // shortened to save screen space
    if (ifc->has_limit)
      fprintf(fp, "max-exec %lu ",ifc->limit);

    // shortened to save screen space
    if (ifc->has_rlimit)
      fprintf(fp, "rate-limit %u ",ifc->rlimit);

    // Use quotes: aliases could have spaces in their names and we want to generate
    // "executable" output, which can be simply copy/pasted to the espshell prompt again
    if (ifc->exec)
      fprintf(fp, "exec \"%s\"",ifc->exec->name);

    fprintf(fp, CRLF);
  }
}




// "if disable NUM|all"
// "if enable NUM|all"
// "every enable ..."
// "every disable ..."
//
static int cmd_if_disable_enable(int argc, char **argv) {

  int num,i, start = 0, stop = NO_TRIGGER, disable = 1;

  if (argc < 3)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[0],"every"))
    start = stop = EVERY_IDX;

  if (!q_strcmp(argv[1],"enable"))
    disable = 0;

  num = q_atoi(argv[2],0); // applied to "all" returns 0. There are no ifconds with ID==0, so we use this ID as disable-all marker

  struct ifcond *ifc;

  for (i = start; i <= stop; i++)
    if ((ifc = ifconds[i]) != NULL)
      while(ifc) {
        if (ifc->id == num || num == 0) {
          ifc->disabled = disable;
          if (num)
            return 0;
        }
        ifc = ifc->next;
      }

  return 0;
}

// "if|every delete|clear NUM|all"
// "if delete|clear gpio NUM"
// "if delete|clear poll"
//
static int cmd_if_delete_clear(int argc, char **argv) {

    int num;
    // if delete|clear gpio NUM
    if (!q_strcmp(argv[2],"gpio")) {

      if (argc < 4) {
bad_gpio_number:        
        q_print("% A GPIO number is expected after the \"gpio\" keyword\r\n");
        return CMD_FAILED;
      }
      if ((num = q_atoi(argv[3], -1)) < 0)
        goto bad_gpio_number;

      if (argv[1][0] == 'd') 
        ifc_delete_pin(num); else ifc_clear_pin(num);

    // if delete|clear all
    // every delete|clear all
    } else if (!q_strcmp(argv[2],"all")) {

      if (!q_strcmp(argv[0],"every")) {
        if (argv[1][0] == 'd')
          ifc_delete_every(); else ifc_clear_every();
      } else {
        if (argv[1][0] == 'd')
          ifc_delete_all(); else ifc_clear_all();
      }

    // if delete|clear polling
    } else if (!q_strcmp(argv[2],"poll")) {
      if (argv[1][0] == 'd')
        ifc_delete_poll(); else ifc_clear_poll();

    // if|every delete|clear NUM
    } else if (isnum(argv[2])) {

      if ((num = q_atoi(argv[2], -1)) < 0)
        return 2;

      if (argv[1][0] == 'd') 
        ifc_delete(num); else ifc_clear(num);
    // unrecognized keywords :(
    } else
        return 2;

    return 0;
}

// "if|every save ID|* /FILENAME"
//
static int cmd_if_save(int argc, char **argv) {

  FILE *fp = NULL;
  int id;
  struct ifcond *ifc;

  if (argc < 4)
    return CMD_MISSING_ARG;

  if (files_touch(argv[3]) < 0) {
    q_print("% Is filesystem mounted?\r\n");
    return CMD_FAILED;
  }
  // Append to existing file or create new.
  // By default we append, so every module can write its configuratuion into single config file
  if ((fp = files_fopen(argv[3],"a")) == NULL)
    return CMD_FAILED;

  id = q_atoi(argv[1],-1);
  
  fprintf(fp,"\r\n// \"if\" and \"every\" statements:\r\n//\r\n");

  rw_lockr(&ifc_rw);
  for (int i=0; i < NUM_PINS+2; i++) {
    ifc = ifconds[i];
    while (ifc) {
      if (id < 0 || ifc->id == id)
        ifc_show_fp(fp,ifc);
      ifc = ifc->next;
    }
  }

  rw_unlockr(&ifc_rw);
  fclose(fp);

  return 0;
}


// Create an "if" or "every" condition and performs many other things
// being a gateway to other cmd_if_... handlers.
//
// if rising|falling NUM [low|high NUM]* [max-exec NUM] [rate-limit MSEC] exec ALIAS_NAME
// if low|high NUM [low|high NUM]* [poll MSEC] [max-exec NUM] [rate-limit MSEC] exec ALIAS_NAME
// every ...
//
// TODO: this functions is huge. must be split in smaller routines
//
static int cmd_if(int argc, char **argv) {

  unsigned int cond_idx = 1, max_exec = 0, rate_limit = 0, poll = 0, delay_ms = 0;
  const char *exec = NULL; // alias name
  unsigned char trigger_pin = NO_TRIGGER;
  bool rising = false;
  uint64_t low = 0, high = 0;

  // min command is "if clear 6" which is 3 keywords long
  if (argc < 3)
    return CMD_MISSING_ARG;

  //////////////////////////////////////////////
  // "if|every save"
  //////////////////////////////////////////////
  if (!q_strcmp(argv[1],"save"))
    return cmd_if_save(argc, argv);

  //////////////////////////////////////////////
  // if|every "disable" and "enable"
  //////////////////////////////////////////////
  if (!q_strcmp(argv[1],"disable") || !q_strcmp(argv[1],"enable"))
    return cmd_if_disable_enable(argc, argv);

  //////////////////////////////////////////////
  // if|every "delete" and "clear"
  //////////////////////////////////////////////
  if (!q_strcmp(argv[1],"delete") || !q_strcmp(argv[1],"clear"))
    return cmd_if_delete_clear(argc, argv);

  if (argc < 5)
    return CMD_MISSING_ARG;

  ///////////////////////////////////////////////////////
  // The "every" command start with a TIMESPEC statement
  ///////////////////////////////////////////////////////
  if (!q_strcmp(argv[0],"every")) {

    int stop = -1;
    trigger_pin = EVERY_IDX;  // database index where to store this "every" entry

    // first argument of the "every" command is a number
    if (!q_isnumeric(argv[1])) {
      q_print("% Numeric value expected (interval)\r\n");
      return 1;
    }
    
    // Internally, "every" statement uses the same mechanism "if .. poll" uses: "every" event is just
    // and empty (conditionless) if:  "if poll 1000 exec alias". Read polling interval, make sure it is not zero
    // TODO: make poll to be 64 bit
    if (0 == (poll = (unsigned int)(userinput_read_timespec(argc, argv, 1, &stop) / 1000ULL)))
      return stop > 1 ? stop : CMD_FAILED;

    // cond_idx is the index in argv[] from which we scontinue our processing
    cond_idx = stop;
  } else {
    /////////////////////////////////////////////////
    // Normal "if" statement
    /////////////////////////////////////////////////

    // Read trigger condition, if any
    rising = (argv[1][0] == 'r');

    if (rising || argv[1][0] == 'f') {
      if ((trigger_pin = q_atoi(argv[2], NO_TRIGGER)) == NO_TRIGGER)
        return 2;
      if (!pin_exist(trigger_pin) || pin_isvirtual(trigger_pin))
        return 2;
      cond_idx += 2;
    }

    // Read conditions
    
    while( ((cond_idx + 1) < argc) &&                                   // while there are at least 2 keywords available
            (argv[cond_idx][0] == 'l' || argv[cond_idx][0] == 'h') ) {  // and the first keyword is either "low" or "high"

      unsigned char pin;

      // Read pin number, check if it is ok. Enable input on them otherwise we can't access
      // them in the ISR
      if ((pin = q_atoi(argv[cond_idx + 1], NO_TRIGGER)) == NO_TRIGGER)
        return cond_idx + 1;

      if (!pin_exist(pin) || pin_isvirtual(pin))
        return cond_idx + 1;

      // Every GPIO which is used in a condition test must be readable, i.e. INPUT-enabled
      // Even if GPIO is LOW, for "low" condition to work this GPIO must be readable.
      //gpio_input_enable(pin);
      gpio_ll_input_enable(&GPIO, pin); // TODO: 

      // Set corresponding bit and a gpio bitmask
      if (argv[cond_idx][0] == 'l')
        low |= 1ULL << pin;
      else
        high |= 1ULL << pin;

      // Next 2 keywords
      cond_idx += 2;
    }

    if (!low && !high && (trigger_pin == NO_TRIGGER))
      trigger_pin = EVERY_IDX;
  }

  // Common part
  // Read "max-exec NUM", "rate-limit NUM", "poll NUM", "delay NUM" and "exec ALIAS_NAME"
  // all of them are 2-keywords statements

  while (cond_idx + 1 < argc) {

    // TODO: refactor to use userinput_read_timespec()
    if ( !q_strcmp(argv[cond_idx],"delay")) { 

      if (0 == (delay_ms = q_atoi(argv[++cond_idx], 0))) {
        HELP(q_print("% <e>Delay value (milliseconds) is expected</>\r\n"));
        return cond_idx;
      }

    } else if ( !q_strcmp(argv[cond_idx],"poll")) {

      if (0 == (poll = q_atoi(argv[++cond_idx], 0))) {
        HELP(q_print("% <e>Polling value (milliseconds) is expected</>\r\n"));
        return cond_idx;
      }

    } else if ( !q_strcmp(argv[cond_idx],"max-exec")) {

      if (0 == (max_exec = q_atoi(argv[++cond_idx], 0))) {
        HELP(q_print("% <e>Numeric value is expected</>\r\n"));
        return cond_idx;
      }

    } else if ( !q_strcmp(argv[cond_idx],"rate-limit")) {

      if (0 == (rate_limit = q_atoi(argv[++cond_idx], 0))) {
        HELP(q_print("% <e>Time interval (milliseconds) is expected</>\r\n"));
        return cond_idx;
      }

    } else if ( !q_strcmp(argv[cond_idx],"exec")) {

      exec = argv[++cond_idx];

    } else {

      q_print("% <e>Expected \"max-exec\", \"poll\", \"rate-limit\", \"delay\" or \"exec\" keyword</>\r\n");
      return cond_idx;

    }

    cond_idx++;
  }

  if (exec == NULL) {
    q_print("% <e>What should we execute? (\"exec\" keyword expected)</>\r\n");
    return CMD_FAILED;
  }

  // Check if alias exist and show a warning if it doesn't: it helps 
  // catching typos in alias names when writing "if" shell clauses
  struct alias *al;
  if ((al = alias_by_name(exec)) == NULL)
    q_printf("%% <i>Warning</>: alias \"%s\" does not exist, will be created (empty)\r\n", exec);
  else if (alias_is_empty(al))
    q_printf("%% <i>Warning</>: alias \"%s\" exists but it is empty\r\n", exec);

  struct ifcond *ifc = ifc_create(trigger_pin,
                                  rising, 
                                  high, 
                                  low,
                                  max_exec,
                                  exec);
  if (!ifc) {
    q_print("% Failed. Out of memory?\r\n");
    return 0;
  }

  // No-Trigger entries:
  // If not set, "poll interval" defaults to 1 second.
  if (trigger_pin == NO_TRIGGER || trigger_pin == EVERY_IDX) {
    if (!poll)
      poll = 1000;
    if (rate_limit) {
      q_print("% \"<i>rate-limit</>\" keyword is ignored for polling conditions:\r\n"
              "% rate is a constant which is defined by \"<i>poll</>\" keyword\r\n");
      rate_limit = 0;
    }
  } else { // Rising/Falling conditions:
    if (poll || delay_ms) {
      q_print("% \"poll\" and \"delay\" keywords are ignored for rising/falling conditions\r\n");
      poll = 0;
      delay_ms = 0;
    }
  }

 // The rate limit can range from 0 to 65 535 milliseconds.
// A 16-bit field is chosen to save memory.
// This limiter's only purpose is to prevent interrupt flooding, so values above 1 second are questionable.

  if (rate_limit) {
    if (rate_limit > 0xffff) {
      q_print("% \"rate-limit\" is set to maximum of 65.5 seconds\r\n");
      rate_limit = 0xffff;
    }
    ifc->has_rlimit = 1;
    ifc->rlimit = rate_limit;
  }

  ifc->poll_interval = poll;

  if (delay_ms) {
    ifc->has_delay = 1;
    ifc->delay_ms = delay_ms;
  }
  
  // allocate an interrupt (allocated or reused - decides ifc_claim_interrupt())
  // /ifc/ pointer is in the list but not yet attached to an interrupt or to a timer so it is 
  // guaranteed to still be on the list
  if (trigger_pin < NO_TRIGGER)
    ifc_claim_interrupt(trigger_pin);
  else
    ifc_claim_timer(ifc, false);
  // WARNING:
  // Here /ifc/ pointer already may be invalid (returned to the ifc_unused) so we must not write to its
  // fields here

  return 0;
}

// "show ifs"
// Displays list of active ifconds and mpipe drops stats
//
static int cmd_show_ifs(int argc, char **argv) {

  if (argc < 3)
  // show all rules
    ifc_show_all();
  else
  // show specified rule
    ifc_show_single(q_atol(argv[2], 0)); // no rule with ID0 exist

// Display the number of queue drops.
// A drop occurs when more than MPIPE_CAPACITY "if" events trigger at the same time.
// This pushes excess items into the pipe while the ifc_task daemon is suspended
// (because we are inside the ISR), so no one is unloading messages and the pipe eventually overflows.
  if (ifc_mp_drops) {
    q_printf("%% <e>Dropped events (more than %u conds at once): %u</>\r\n", MPIPE_CAPACITY, ifc_mp_drops);
    q_print("% <e>Use \"rate-limit\" or increase MPIPE_CAPACITY</>\r\n");
  }
  return 0;
}




#endif // WITH_ALIAS
#endif // COMPILING_ESPSHELL
