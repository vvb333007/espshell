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

// -- "If" and "Every" : GPIO and Timer events --
// BIG MESS
//
// "ifcond" or "if conditional" is a data structure that holds information about: 
// a GPIO event, an action that should be done in response to the event and some extra
// statistics information - number of "hits" and a timestamp of "last use"
//
// ifconds are created with "if" and "every" shell commands and are added to a global array of ifconds
// (see ifconds[] below)
//
// When displayed (see ifc_show_all() ) every ifcond appears assigned a number (ID) which can be used
// to manipulate that ifcond: delete it, clear timestamp ,clear hit count
//
// When a GPIO interrupt happens, ifconds belonging to that GPIO are checked, 
// and associated aliases are executed. Timed events are governed by esp_timer.
//
// Thread safety:
// ifcond lists are protected by a global rwlock (ifc_rw) AND by disabling GPIO interrupts: userspace tasks
// acquire "reader" lock, ISR does not use locking at all, and actual editing of ifconds which is done by
// "if" and "every" shell commands ensures that "writer" lock is obtained and GPIO interrupts are disabled
//  (see ifc_delete0(...) on how to properly lock to modify lists)
//
#ifdef COMPILING_ESPSHELL

// ifcond: this is what "if rising 5 low 6 high 10 ..." or "every 1 day ..." commands are parsed into.
//
// There are lists of ifconds per every pin: ifconds[0..NUM_PINS-1]) and these  are for "if rising|falling X ... "
//       ifconds on a list are always belong to the same pin
// There are list of "polled" ifconds: ifconds[NO_TRIGGER], and these are for "if low|high X ... poll ..."
// There are list of "every" ifconds: ifconds[EVERY_IDX], and these are for "every ..."
//
struct ifcond {

  struct ifcond *next;      // ifconds with the same pin
  struct alias  *exec;      // pointer to alias. alias pointers are persistent, always valid
                            // only 1 alias per ifcond. Use extra ifconds to execute multiple aliases

  uint8_t trigger_pin;      // GPIO, where RISING or FALLING edge is expected or 
                            // NO_TRIGGER if it is pure conditional ifcond

  uint8_t trigger_rising:1; // 1 == rising, 0 == falling
  uint8_t has_high:1;       // has "high" statements?
  uint8_t has_low:1;        // has "low" statements?
  uint8_t has_limit:1;      // has "max-exec" keyword?
  uint8_t has_rlimit:1;     // has "rate-limit" keyword?
  uint8_t has_delay:1;      // has "delay" keyword?
  uint8_t reserved2:2;
  uint16_t id;              // unique ID for delete/clear commands
  uint16_t rlimit;          // once per X milliseconds (max ~ 1 time per minute [65535 msec])
  uint32_t poll_interval;   // poll interval, in seconds, for non-trigger ifconds
  timer_t  timer;           // timer handle for periodic events
  uint32_t limit;           // max number of hits (if (ifc->hits > ifc->limit) { ignore } else { process } )
  uint32_t delay_ms;        // initial delay. used for "every" ifconds
  

  uint32_t high;            // GPIO 0..31: bit X set == GPIO X must be HIGH for condition to match
  uint32_t high1;           // GPIO 32..63: ...
  uint32_t low;             // GPIO 0..31: bit X set == GPIO X must be LOW for condition to match
  uint32_t low1;            // GPIO 32..63: ...

  uint32_t hits;            // number of times this condition was true
  uint32_t drops;           // number of times alias was not executed (rate-limited or max-exec-limited)
  uint64_t tsta;            // timestamp, microseconds. 
  uint64_t tsta0;           // previous timestamp
                            // If /hits/ is nonzero, then this field has a timestamp of last successful match'
                            // If /hits/ is 0, then this field contains a timestamp of the last "counters clear"
  
};


// index of "no trigger" entry in the ifconds array
#define NO_TRIGGER NUM_PINS

// index where "every" command stores its rules
#define EVERY_IDX (NO_TRIGGER + 1)

// Ifconds array. Each element of the array is a list of 
// ifconds: ifconds[5] contains all "if rising|falling 5" statements for example.
// "no trigger" statements (i.e. those without rising or falling keywords) are located
// in the last entry of this array: ifconds[NUM_PINS]
static struct ifcond *ifconds[NUM_PINS + 2] = { 0 };   // plus 1 for the NO_TRIGGER entry and plus 1 for "every" entries

// RWLock to protect lists of ifconds. Lists are modified only by "if" and "if delete" commands.
// Others are "readers", including the GPIO ISR (which traverses these lists). Obtaining a writer lock
// thus is not enough: interrupts for the GPIO must be disabled (ISR does not have any locking mechanism at all).
//
static rwlock_t ifc_rw = RWLOCK_INIT;

// Defines the size of a message pipe. If the ISR below will match more than MPIPE_CAPACITY
// ifconds, it will drop any extra. For example, you have 17 "if" statements which were triggered at once:
// 17th message from the ISR to ifc_task() will be dropped. Don't make it too small.
//
// If it is too big, then no events will be missed but at cost of higher RAM usage. Don't make it too big
// TODO: implement a counter for dropped messages, display it on "show ifs"
#define MPIPE_CAPACITY 16

static void ifc_task(void *arg);

// message pipe (from the ifc_anyedge_interrupt() to ifc_task())
static mpipe_t ifc_mp = MPIPE_INIT;
static unsigned int ifc_mp_drops = 0;


#define IFCOND_PRIORITY 22 // Run at the esp_timer priority so both esp_timer-controlled events 
                           // and interrupt-driven events will run at the same priority level


// Create message pipe and start a daemon task
// Daemon priority set is just below system tasks priority
//
static __attribute__((constructor)) void ifc_init_once() {

  task_t ifc_handle = NULL; // daemon task id

  if ((ifc_mp = mpipe_create(MPIPE_CAPACITY)) != MPIPE_INIT) {
    if ((ifc_handle = task_new(ifc_task, NULL, "ifcond")) != NULL) {
      task_set_priority(ifc_handle, IFCOND_PRIORITY); 
    } else {
      // TODO: StartupFailed = "ifcond daemon"
      mpipe_destroy(ifc_mp);
      ifc_mp = NULL;
    }
  } // else
  // TODO: StartupFailed = "ifcond message pipe";
  
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
// Check if ifcond must not be used, because it has reached "max-exec" limit.
// It is not a function but a macro otherwise it must be declared with IRAM_ATTR
//
#define ifc_not_expired(ifc) \
  (ifc->has_limit ? ifc->hits < ifc->limit \
                  : true)


// GPIO Interrupt routine, implemented via "GPIO ISR Service" API: a global GPIO handler is implemented in ESP-IDF
// calls user-defined routines. 
//
// Using an GPIO ISR Service creates less headache when co-existing together with a sketch which also uses 
// GPIO interrupts. Arduino sketches use GPIO interrupts via GPIO ISR Service so we do the same.
//
// Handles "trigger ifconds", i.e. ifconds which have "rising" or "falling" keywords
//
static void IRAM_ATTR ifc_anyedge_interrupt(void *arg) {

  bool rising;
  unsigned int pin = (unsigned int )arg;

  // ifc points to the head of ifcond list associated with given pin
  struct ifcond *ifc = ifconds[pin];

  // Read pin values (all at once, via direct register read). 
  // We need them: 
  //   1. for the edge detect (rising or falling. ESP32 has no edge type indication when interrupt happens)
  //   2. for condition match ("cond" part of "ifcond")
  // 
  uint32_t in = REG_READ(GPIO_IN_REG);   // GPIO 0..31
  uint32_t in1 = REG_READ(GPIO_IN1_REG); // GPIO 32..63
  
  // Edge detect: if pin is HIGH, then it was "rising" event
  rising = pin < 32 ? (in & (1UL << pin))
                    : (in1 & (1UL << (pin - 32)));


  // Go through "if" clauses. (all that are associated with the /pin/)
  // We do not use any locking mechanism here (RWLock is what we should use here): the only code which modifies 
  // ifconds lists is the "if" shell command: lists are modified on addition of a new ifcond or on deletion 
  // (via "if delete").
  //
  // Instead, when user enters "if" command, for the short period of time GPIO interrupts are disabled 
  // to keep ifc_anyedge_interrupt() from traversing lists being modified
  //
  // List are traversed in ISR, please keep them short
  //
  // gotos were added in attempt to optimize execution time
  //
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

    // 5. Full match: send ifc pointer to the ifc_task() and continue processing 
    // (there may be more matched ifconds). ifc_task() will go through the queue, fetching pointers 
    // and executing associated aliases
    
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


// GPIO mask where ISR is enabled. Bit 17 set means GPIO17 has its ISR registered 
static uint64_t isr_enabled = 0;

// Check if GPIO has ISR handler installed
#define ifc_isr_is_enabled(_Gpio) \
  (isr_enabled & (1ULL << (_Gpio)))

#define ifc_isr_enable(_Gpio) \
  (isr_enabled |= (1ULL << (_Gpio)))

#define ifc_isr_disable(_Gpio) \
  (isr_enabled &= ~(1ULL << (_Gpio)))


// I don't think we need them
//

static void ifc_disable_periodic_timers() {
  // TODO:
}

static void ifc_enable_periodic_timers() {
  // TODO:  
}

static void ifc_claim_timer(struct ifcond *ifc);

// Timer callback for polled entries ("if low 5 poll .." or "every ..")
// It is called periodically by esp_timer system task. It is analog of ifc_anyedge_interrupt() handler but for polling
//
static void ifc_callback(void *arg) {

  struct ifcond *ifc = (struct ifcond *)arg;

  uint32_t in = REG_READ(GPIO_IN_REG);   // GPIO 0..31
  uint32_t in1 = REG_READ(GPIO_IN1_REG); // GPIO 32..63

  // 1. entry is not expired?
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

  // 4. Execute
    if (mpipe_send(ifc_mp, ifc))
      return ;
  }

  ifc->drops++;
}

// When entry has "delay" keyword we must postpone first execution hence the
// name
static void ifc_callback_delayed(void *arg) {
  struct ifcond *ifc = (struct ifcond *)arg;
  if (ifc) {
    // delay time has passed: execute and schedule periodic timer. remove old timer
    ifc_callback(arg);

    esp_timer_stop(ifc->timer);
    esp_timer_delete(ifc->timer);

    // Temporary set has_delay to 0 so ifc_claim_timer() will choose right callback function
    // (ifc_callback instead of ifc_callback_delayed). Same ifc can not be executed from two different tasks,
    // so it is safe to temporary change ifc->has_delay value.
    ifc->has_delay = 0;
    ifc_claim_timer(ifc);
    ifc->has_delay = 1;
  }
}

// Allocate periodic timer for polled events
//
static void ifc_claim_timer(struct ifcond *ifc) {

    timer_t handle = TIMER_INIT;

    esp_timer_create_args_t timer_args = {
        .callback = &ifc_callback,
        .dispatch_method = ESP_TIMER_TASK,
        .arg = ifc,
        .name = ifc->exec ? ifc->exec->name : "unnamed",
    };

    // 2 stage-setup for delayed events
    if (ifc->has_delay)
      timer_args.callback = &ifc_callback_delayed;

    if (ESP_OK == esp_timer_create(&timer_args, &handle)) {
      ifc->timer = handle;
      if (ifc->has_delay)
        esp_timer_start_once(handle, 1000ULL * ifc->delay_ms);
      else {
        ifc_callback(ifc);
        esp_timer_start_periodic(handle, 1000ULL * ifc->poll_interval);
      }
    }
}

static void ifc_release_timer(struct ifcond *ifc) {
  if (ifc && ifc->timer) {
    VERBOSE(q_print("ifc_release_timer()\r\n"));
    esp_timer_stop(ifc->timer);
    esp_timer_delete(ifc->timer);
    ifc->timer = NULL;
  }
}

// Ask for an interrupt for the pin. If it is already registered then nothing is done. Otherwise we
// install a GPIO ANYEDGE interrupt handler and enable interrupts on that pin
//
static void ifc_claim_interrupt(uint8_t pin) {
  if (pin_exist(pin)) {
    if (!ifc_isr_is_enabled(pin)) {
      ifc_isr_enable(pin);

      //VERBOSE(q_printf("%% ifc_claim_interrupt() : Registering an ISR for GPIO#%u\r\n",pin));
      // install_isr_service can be called multiple times - if already installed it just returns with a warning message
      // Since shell has to co-exist together with user sketch which can do isr_uninstall, we should restore our interrupt logic
      // whenever possible
      gpio_install_isr_service((int)ARDUINO_ISR_FLAG);
      gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
      gpio_isr_handler_add(pin, ifc_anyedge_interrupt, (void *)(unsigned int)pin);
      gpio_intr_enable(pin);
    }
  }
}

// Interrupts are disabled only when no conditions with trigger pin /pin/ exists.
// Suppose we had called ifc_claim_interrupt(5, ...) six times: we had registered an interrupt for the first call, other
// five calls had no effect. Upon ifcond removal, first the entry must be removed from the list
// and only then ifc_release_interrupt() must be called - the latter checks if corresponding ifcond[] list is empty
//
static void ifc_release_interrupt(uint8_t pin) {
  if (pin_exist_silent(pin)) {
    if (ifc_isr_is_enabled(pin)) {
      if (!ifconds[pin]) {
        gpio_intr_disable(pin);
        gpio_isr_handler_remove(pin);
        ifc_isr_disable(pin);
        //VERBOSE(q_printf("%% ifc_release_interrupt() : removing ISR for GPIO#%u\r\n",pin));
      }
    }
  }
}


// Displays content of a single ifcond, by its pointer.
// Displays information in compact (and clamped) form: it is a /brief/ version if ifc_show_single(). 
//
// NOTE: NOTE: NOTE:
// Its purpose is to be used from within ifc_show_all(), and it is assummed to be called in a loop, repeatedely,
// so it takes a *pointer* as a parameter.
// in contrast, ifc_show_single() takes *ifcond ID* as a parameter and performs search by itself
// Uses shortened keywords (rate not rate-limit etc) as we have limited space (80 columns!)
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

    
    if (ifc->poll_interval) {
      if (ifc->trigger_pin == EVERY_IDX)
        // TODO: display TIME for "every" and milliseconds for others
        q_printf("%lu mil ",ifc->poll_interval);
      else
        q_printf("poll %lu ",ifc->poll_interval);
    }

    if (ifc->has_delay)
      q_printf("delay %lu",ifc->delay_ms);

    if (ifc->has_limit)
      q_printf("max %lu ",ifc->limit);

    if (ifc->has_rlimit)
      q_printf("rate %u ",ifc->rlimit);


    if (ifc->exec)
      q_printf("exec %s",ifc->exec->name);
    q_print(CRLF);
  }
}

// Display ifcond by its ID
// Shows a bit more detailed information on given ifcond.
//
static void ifc_show_single(unsigned int num) {
  int i,j;

  rw_lockr(&ifc_rw);
  for (i = j = 0; i < NUM_PINS + 2; i++) {
    struct ifcond *ifc = ifconds[i];
    while (ifc) {
      if (ifc->id == num) {

        q_printf("%% \"%s\" condition#%u", ifc->trigger_pin == EVERY_IDX ? "Every" : "If", num);
        if (!ifc->hits)
          q_print(", never executed (triggered)");
        if (!ifc_not_expired(ifc))
          q_printf(", <w>expired</>, (\"if clear %u\" to reset)",num);
        q_print(CRLF);

        if (ifc->hits)
          q_printf("%% Last executed: <i>%llu</> seconds ago, <i>%lu</> times total\r\n",(q_micros() - ifc->tsta0) / 1000000ULL, ifc->hits);
        

        if (ifc->drops)
          q_printf("%% Execution skipped (event dropped): <i>%lu</>\r\n",ifc->drops);

        if (ifc->has_limit)
          q_printf("%% Expires after <i>%lu</> executions\r\n",ifc->limit);
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
  q_printf("%% No \"if\" condition with ID = %u. (\"show ifs\" to list all IDs)\r\n", num);
}


// These functions are not reentrant, use with care: subsequent calls destroy previous value
// as it is static buffers used. Because of this these functions are not general use API and thus is not in qlib
//
// Convert seconds to "XXX d", "XXX s", "XXX h" and so on, 5 symbols
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
static char *q_strnum_sat(uint32_t num) {
  static char buf[8] = { 0 };
  if (num > 99999)
    strcpy(buf,">99999");
  else
    sprintf(buf,"%lu",num);
  return buf;
}

// Display all ifconds, assign a number to each line
// These line numbers can be used to delete the entity ("if delete NUMBER|all")
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
#define ifc_delete_poll(X)  ifc_delete0(-NO_TRIGGER, false) // delete all "if low|high poll" entries
#define ifc_delete_every(X) ifc_delete0(-EVERY_IDX, false)  // delete all "every" entries
#define ifc_delete_all()    ifc_delete0(0, true)            // delete all entries

#define ifc_clear(X)     ifc_clear0(X, false)               // clear counters for one entry by its ID
#define ifc_clear_pin(X) ifc_clear0(-X, false)              // clear counters for all entries that are triggered by pin X
#define ifc_delete_poll(X)  ifc_delete0(-NO_TRIGGER, false) // clear all "if low|high poll" entries
#define ifc_delete_every(X) ifc_delete0(-EVERY_IDX, false)  // clear all "every" entries
#define ifc_clear_all()  ifc_clear0(0, true)                // clear counters for all entries


#define MULTIPLE_IFCONDS ((num <= 0) || all) // have to process multiple ifconds or just one?

static barrier_t ifc_mux = BARRIER_INIT;  // a critical section to protect ifc_unused. TODO: do we really need it?
static struct ifcond *ifc_unused = NULL;  // list of free entries. entries are reused, ID is retained

// Allocate an ifcond.
// It is either allocated via malloc() or, if available, reused from the pool of deleted ifconds.
// When deleted, ifconds are not physically removed: instead, such unused ifconds go on "ifc_unused" list
// and can be reused. Mainly to reuse ID's which otherwise start to grow
//
static struct ifcond *ifc_get() {

  static _Atomic uint16_t id = 1;
  struct ifcond *ret = NULL;

  barrier_lock(ifc_mux);
  if (ifc_unused) {
    ret = ifc_unused;
    ifc_unused = ret->next;
  }
  barrier_unlock(ifc_mux);

  // New entries get new ID; Reused entries must use their previously assigned ID.
  // ifc_get() only guarantees that /->id/ field is initialized, while all other fields
  // contain some old data
  if (!ret) {
    ret = (struct ifcond *)q_malloc(sizeof(struct ifcond),MEM_IFCOND);
    if (ret)
      ret->id = id++; // is Var++ on _Atomic variable atomic?
    // TODO: handle overflow (must be 65535 active ifconds for it to happen)
  }
  
  return ret;
}

// Return unused ifcond back to "ifc_unused" list
// Yes, classic mutex is enough here, but critical section is simple. 
//
static void ifc_put(struct ifcond *ifc) {
  if (ifc) {
    barrier_lock(ifc_mux);
    ifc->next = ifc_unused;
    ifc_unused = ifc;
    barrier_unlock(ifc_mux);
  }
}

// Delete an ifcond entry (or all entries)
//
// num <=0 ? -1*num is a pin number. Remove all entries belonging to that pin 
//           (i.e. whole ifconds[-num] list). Note that specifying -NO_TRIGGER will remove 
//           all "polling" ifconds. TODO: The latter is not available through the shell commands yet
// num > 0 ? num is the ifcond ID. Remove one single ifcond and exit
// all == true ? ignore /num/ and remove all entries
//
static void ifc_delete0(int num, bool all) {

  int i;

  if (!all && num < -(NUM_PINS + 2)) {
    q_printf("%% Pin number %u is out of range\r\n", -num);
    return;
  }

  // If we are about to modify one of the ifconds[] lists, acquire writers lock
  rw_lockw(&ifc_rw);

  // If num <= 0, i.e. removal of all ifconds that belongs to GPIO abs(num) is requested
  // then we start our "for" loop from that requested GPIO number and only perform 1 cycle, and finish with "break"
  //
  // If num > 0, i.e. removal of specified ifcond (by its ID, num == ID) was requested, then 
  // we process all pins starting from 0 to NUM_PINS
  //
  // If /all/ is /true/, then value of /num/ is ignored, all entries are removed
  //
  if (all)
    num = 0;

  for (i = num < 0 ? -num : 0; i <= EVERY_IDX; i++) {

    // if there are ifconds associated with the pin, we disable GPIO interrupts on this particular GPIO
    // to prevent ifc_anyedge_interrupt() from traversing ifconds lists
    if (ifconds[i]) {

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
            if (i < NO_TRIGGER && ifc_isr_is_enabled(i))
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
      if (i < NO_TRIGGER && ifc_isr_is_enabled(i))
        gpio_intr_enable(i); 
      if (i >= NO_TRIGGER)
        ifc_enable_periodic_timers();

      if (!all && (num <= 0))
        break;

    }
  }
  rw_unlockw(&ifc_rw);

}

// Clear counters (hits and tsta)
// Arguments are the same as those for ifc_delete0
//
// NOTE: clearing /hits/ will reenable expired ifconds (those with "limit" argument)
//
static void ifc_clear0(int num, bool all) {

  int i;

  if (!all && num < -(NUM_PINS + 2))) {
    q_printf("%% Pin number %u is out of range\r\n", -num);
    return;
  }

  // Clearing counters does not modify list itself, so treat clearing as a reader's operation
  rw_lockr(&ifc_rw);

  // "all" means all :). Triggered and non-triggered ifconds.
  // Non-triggered entries belong to non-existing pin, next to the maximum GPIO number
  if (all)
    num = 0;

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

            // Clear by icond ID? return then. NOTE: ifc->id is always > 0
            if (ifc->id == num) {
              rw_unlockr(&ifc_rw);
              return;
            }
        }
        ifc = ifc->next;
      }

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

    if (trigger_pin < NUM_PINS && ifc_isr_is_enabled(trigger_pin))
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
//        if (ifc->has_delay)
//          alias_exec_in_background_delayed(ifc->exec,ifc->delay_ms);
//        else
          alias_exec_in_background(ifc->exec);
        ifc->hits++;
      } else
        ifc->drops++;
    }
  }
  task_finished();
  /* UNREACHED */
}


// Create an "if" condition
//
// if rising|falling NUM [low|high NUM]* [max-exec NUM] [rate-limit MSEC] exec ALIAS_NAME
// if low|high NUM [low|high NUM]* [poll MSEC] [max-exec NUM] [rate-limit MSEC] exec ALIAS_NAME
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
  // "delete" and "clear"
  //////////////////////////////////////////////
  if (!q_strcmp(argv[1],"delete") || !q_strcmp(argv[1],"clear")) {

    int num;
    // if delete|clear gpio NUM
    if (!q_strcmp(argv[2],"gpio")) {

      if (argc < 4) {
bad_gpio_number:        
        q_print("% A GPIO number is expected after \"gpio\" keyword\r\n");
        return CMD_FAILED;
      }
      if ((num = q_atoi(argv[3], -1)) < 0)
        goto bad_gpio_number;

      if (argv[1][0] == 'd') 
        ifc_delete_pin(num); else ifc_clear_pin(num);

    // if delete|clear all
    } else if (!q_strcmp(argv[2],"all")) {

      if (argv[1][0] == 'd')
        ifc_delete_all(); else ifc_clear_all();

    // if delete|clear polling
    // TODO: if delete|clear every
    } else if (!q_strcmp(argv[2],"polling")) {

      if (argv[1][0] == 'd')
        ifc_delete_pin(NO_TRIGGER); else ifc_clear_pin(NO_TRIGGER);

    // if delete|clear NUM
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


 // if rising 5 exec Alias -> 5
 // if low 5 exec Alias -> 5
 // every 1 day exec Alias -> 5
  if (argc < 5)
    return CMD_MISSING_ARG;

  ///////////////////////////////////////////////////////
  // The "every" command start with TIME statement
  ///////////////////////////////////////////////////////
  if (!q_strcmp(argv[0],"every")) {
    trigger_pin = EVERY_IDX;
    if (!q_isnumeric(argv[1])) {
      q_print("% Numeric value expected (interval)\r\n");
      return 1;
    }
    poll = q_atol(argv[1],1000);
    if (!q_strcmp(argv[2],"days"))
      poll *= 24*60*60*1000;
    else if (!q_strcmp(argv[2],"hours"))
      poll *= 60*60*1000;
    else if (!q_strcmp(argv[2],"minutes"))
      poll *= 60*1000;
    else if (!q_strcmp(argv[2],"seconds"))
      poll *= 1000;
    else if (!q_strcmp(argv[2],"milliseconds"))
      poll *= 1;
    else {
      q_print("% Time unit is expected (days, hours, minutes, seconds or milliseconds)\r\n");
      return 2;
    }

    cond_idx += 2;
  } else {
    /////////////////////////////////////////////////
    // Normal "if" statement
    /////////////////////////////////////////////////

    // Read trigger condition, if any
    rising = (argv[1][0] == 'r');

    if (rising || argv[1][0] == 'f') {
      if ((trigger_pin = q_atoi(argv[2], NO_TRIGGER)) == NO_TRIGGER)
        return 2;
      if (!pin_exist(trigger_pin))
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

      if (!pin_exist(pin))
        return cond_idx + 1;

      // Every GPIO which is used in a condition must be readable, i.e. INPUT-enabled
      gpio_input_enable(pin);

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

  // Read "max-exec NUM", "rate-limit NUM", "poll NUM", "delay NUM" and "exec ALIAS_NAME"
  // all of them are 2-keywords statements

  while (cond_idx + 1 < argc) {

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
      // Check if alias exist and show a warning if it doesn't: it helps to catch typos when writing "if" shell clauses
    } else {
      q_print("% <e>\"max-exec\", \"poll\", \"rate-limit\", \"delay\" or \"exec\" keywords are expected</>\r\n");
      return cond_idx;
    }
    cond_idx++;
  }

  if (exec == NULL) {
    q_print("<e>% \"exec ALIAS_NAME\" keyword expected</>\r\n");
    return 0;
  }

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

  // Rate limit can be anything from 0 to 65535 milliseconds.
  // 16 bit is choosen to save memory. The sole purpose of this limiter is not to get flooded by
  // interrupts, so values greater than 1 sec are meaningless
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
  if (trigger_pin < NO_TRIGGER)
    ifc_claim_interrupt(trigger_pin);
  else
    ifc_claim_timer(ifc);
  

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

  // Display queue drops. 
  // Drops happen when more than MPIPE_CAPACITY "if"s fire together: it results in more items flowing to the pipe,
  // but ifc_task daemon is suspended (we are in the ISR!), so nobody unloads messages from the pipe and it eventually overflows
  if (ifc_mp_drops) {
    q_printf("%% <e>Dropped events (more than %u conds at once): %u</>\r\n", MPIPE_CAPACITY, ifc_mp_drops);
    q_print("% <e>Use \"rate-limit\" or increase MPIPE_CAPACITY</>\r\n");
  }
  return 0;
}
#endif // COMPILING_ESPSHELL
