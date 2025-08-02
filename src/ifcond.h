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
//
// "ifcond" or "if conditional" is a data structure that holds information about: 
// a GPIO event, an action that should be done in response to the event and some extra
// statistics information - number of "hits" and a timestamp of "last use"
//
// ifconds are created with "if" and "every" shell commands and are added to a global array of ifconds
// (see ifconds[] below)
//
// When displayed (see ifc_show_all() ) every ifcond is assigned a number which can be used to manipulate 
// that ifcond: delete it, clear timestamp ,clear hit count
//
// When a GPIO interrupt happens, ifconds belonging to that GPIO are checked, 
// and associated aliases are executed
//
// Thread safety:
// ifcond lists are protected by a global rwlock (ifc_rw) AND by disabling GPIO interrupts: userspace tasks
// acquire "reader" lock, ISR does not use locking at all, and actual editing of ifconds which is done by
// "if" and "every" shell commands ensures that "writer" lock is obtained and GPIO interrupts are disabled
//
//
// every <TIME> [delay <TIME>] exec ARG [<LIMIT>]    : execute alias periodically, starting from (now + delay time)
//
// if clear [gpio] NUM|all   : clear counters for all ifconds, all ifconds belonging to GPIO, all "no trigger" ifconds or specific (NUM) ifcond
// every clear NUM|all       : clear counters of periodic events
//
// if delete [gpio] NUM|all  : delete ifcond by its number, all of them or all belonging to specific GPIO
// every delete NUM|all      : delete periodic events

#ifdef COMPILING_ESPSHELL

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
  uint8_t reserved3:3;
  uint16_t id;              // unique ID for delete command
  uint16_t rlimit;          // once per X milliseconds (max ~ 1 time per minute [65535 msec])
  uint16_t poll_interval;   // poll interval, in seconds, for non-trigger ifconds
  uint32_t limit;           // max number of hits (if (ifc->hits > ifc->limit) { ignore } else { process } )
  

  uint32_t high;            // GPIO 0..31: bit X set == GPIO X must be HIGH for condition to match
  uint32_t high1;           // GPIO 32..63: ...
  uint32_t low;             // GPIO 0..31: bit X set == GPIO X must be LOW for condition to match
  uint32_t low1;            // GPIO 32..63: ...

  uint32_t hits;            // number of times this condition was true
  uint64_t tsta;            // timestamp, microseconds. 
                            // If /hits/ is nonzero, then this field has a timestamp of last successful match'
                            // If /hits/ is 0, then this field contains a timestamp of the last "counters clear"
  
};

// index of "no trigger" entry in the ifconds array
#define NO_TRIGGER NUM_PINS

// Ifconds array. Each element of the array is a list of 
// ifconds: ifconds[5] contains all "if rising|falling 5" statements for example.
// "no trigger" statements (i.e. those without rising or falling keywords) are located
// in the last entry of this array: ifconds[NUM_PINS]
static struct ifcond *ifconds[NUM_PINS + 1] = { 0 };   // plus 1 for the last NO_TRIGGER entry

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

// daemon task
static task_t ifc_handle = NULL;

// Create message pipe and start a daemon task
// Daemon priority set is just below system tasks priority
//
static __attribute__((constructor)) void ifc_init_once() {
  
  if ((ifc_mp = mpipe_create(MPIPE_CAPACITY)) != MPIPE_INIT) {
    if ((ifc_handle = task_new(ifc_task, NULL, "ifcond")) != NULL)
      task_set_priority(ifc_handle, 21); // prio 22..24 are used by system tasks of ESP-IDF, don't disturb them
    else {
      mpipe_destroy(ifc_mp);
      ifc_mp = NULL;
    }
  }
}

// Check if an ifcond with "limit NUM" keyword has been executed NUM times already
// These: "if rising 5 exec alias1 limit 8"
// It is not inline but a macro otherwise it must be declared with IRAM_ATTR
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
// Periodical ifconds ("if low 8 poll 5 sec exec foo") and "every" timers ("every 1 day delay 5 hours exec Foo")
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
    // TODO: play with ifc_task() priority
        force_yield |= mpipe_send_from_isr(ifc_mp, ifc);
      } // if not expired?
    } // edge match?
next_ifc:
    ifc = ifc->next;
  }

  // mpipe_send() has unblocked a higher priority task: request rescheduling.
  if (force_yield)
    q_yield_from_isr();
}


// Displays content of a single ifcond
//
static void ifc_show(struct ifcond *ifc) {

  if (ifc) {
    uint8_t i;

    if (ifc->trigger_pin != NO_TRIGGER)
      q_printf("if %s %u ", ifc->trigger_rising ? "rising" : "falling", ifc->trigger_pin);
    else
      q_print("if ");

    if (ifc->has_high)
      for (i = 0; i <32; i++) {
        if (ifc->high & (1UL << i))  q_printf("high %u ",i);
        if (ifc->high1 & (1UL << i)) q_printf("high %u ",i + 32);
      }

    if (ifc->has_low)
      for (i = 0; i <32; i++) {
        if (ifc->low & (1UL << i))  q_printf("low %u ",i);
        if (ifc->low1 & (1UL << i)) q_printf("low %u ",i + 32);
      }

    if (ifc->poll_interval)
      q_printf("poll %u ",ifc->poll_interval);

    if (ifc->has_limit)
      q_printf("max-exec %lu ",ifc->limit);

    if (ifc->has_rlimit)
      q_printf("rate-limit %u ",ifc->rlimit);


    if (ifc->exec)
      q_printf("exec %s",ifc->exec->name);
    q_print(CRLF);
  }
}

// Display all ifconds, assign a number to each line
// These line numbers can be used to delete the entity ("if delete NUMBER|all")
//


static void ifc_show_all() {
  int i,j;

  q_printf("%% ID# | Hits | Last hit | Condition and action                                 \r\n"
           "%%-----+------+----------+------------------------------------------------------\r\n");


  rw_lockr(&ifc_rw);
  for (i = j = 0; i < NUM_PINS + 1; i++) {
    struct ifcond *ifc = ifconds[i];
    while (ifc) {
      j++;
      q_printf("%%%5u|%6lu|%6u sec|",ifc->id, ifc->hits, (unsigned int)((q_micros() - ifc->tsta) / 1000000ULL));
      ifc_show(ifc);
      ifc = ifc->next;
    }
  }
  rw_unlockr(&ifc_rw);

  if (!j)
    q_print("%\r\n% <i>No conditions were defined</>; Use command \"if\" to add some\r\n");
  else
    q_print("%-----+------+----------+------------------------------------------------------\r\n");
}

// Delete/Clear ifcond entry/entries
//
#define ifc_delete(X)     ifc_delete0(X, false)    // delete one entry by its ID
#define ifc_delete_pin(X) ifc_delete0(-X, false)   // delete all entries that are triggered by pin X
#define ifc_delete_all()  ifc_delete0(0, true)     // delete all entries

#define ifc_clear(X)     ifc_clear0(X, false)      // clear counters for one entry by its ID
#define ifc_clear_pin(X) ifc_clear0(-X, false)     // clear counters for all entries that are triggered by pin X
#define ifc_clear_all()  ifc_clear0(0, true)       // clear counters for all entries


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
// num <=0 ? -1*num is a pin number
// num > 0 ? num is the ifcond ID
//
static void ifc_delete0(int num, bool all) {

  int i, maxp = NUM_PINS;

  if (!all && num < -(NUM_PINS - 1)) {
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
  if (all) {
    num = 0;
    maxp++;   // including NO_TRIGGER ifconds
  }

  for (i = num < 0 ? -num : 0; i < maxp; i++) {

    // if there are ifconds associated with the pin, we disable GPIO interrupts on this particular GPIO
    // to prevent ifc_anyedge_interrupt() from traversing ifconds lists
    if (ifconds[i]) {

      gpio_intr_disable(i);
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

            // Interrupt must be release AFTER ifc is unlinked from the list
            if (ifc->trigger_pin != NO_TRIGGER)
              ifc_release_interrupt(ifc->trigger_pin);

            ifc_put(tmp);
            

          // We had processed 1 element. Should we continue or return?
          if (!MULTIPLE_IFCONDS) {
            rw_unlockw(&ifc_rw);
            gpio_intr_enable(i); //TODO: do not enable if ifconds[i] == NULL
            return;
          }
        } else {
          // Proceed to the next ifcond
          prev = ifc;
          ifc = ifc->next;
        }
      } // while(ifc)

      gpio_intr_enable(i);
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

  int i, maxp = NUM_PINS;

  if (!all && num < -(NUM_PINS - 1)) {
    q_printf("%% Pin number %u is out of range\r\n", -num);
    return;
  }

  // Clearing counters does not modify list itself, so treat clearing as a reader's operation
  rw_lockr(&ifc_rw);

  // "all" means all :). Triggered and non-triggered ifconds.
  // Non-triggered entries belong to non-existing pin, next to the maximum GPIO number
  if (all) {
    num = 0;
    maxp++;
  }

  for ( i = num < 0 ? -num : 0; 
        i < maxp;
        i++) {
    if (ifconds[i]) {
      struct ifcond *ifc = ifconds[i];

      while (ifc) {
        // Found an item user wishes to clear?
        if (MULTIPLE_IFCONDS || ifc->id == num) {

            ifc->hits = 0;
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
    n->has_rlimit = 0;
    n->rlimit = 0;
    n->poll_interval = 0;
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
    n->tsta = q_micros();
    
    // disable interrupts on real GPIOs, do nothing for NO_TRIGGER 
    if (trigger_pin < NUM_PINS)
      gpio_intr_disable(trigger_pin);

    // Still need to rw_lockw(), even with interrupts disabled : 
    // command "if" MAY be executed not from the main espshell context (e.g. execution of an alias, containing "if" statements)
    // Insert new item into pin's ifcond list
    rw_lockw(&ifc_rw);
    n->next = ifconds[trigger_pin];
    ifconds[trigger_pin] = n;
    rw_unlockw(&ifc_rw);

    if (trigger_pin < NUM_PINS)
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
      //q_printf("ifc_task() : ifcond#%u received\r\n",ifc->id);
     
      ifc->tsta = q_micros();
      // "expired" means one of two things:
      //   1. TODO: clamped by rate-limit
      //   2. clamped by max-exec
      if (ifc_not_expired(ifc))
        alias_exec_in_background(ifc->exec);

      ifc->hits++;
    }
  }
  task_finished();
  /* UNREACHED */
}

// GPIO mask where ISR is enabled
static uint64_t isr_enabled = 0;

static void ifc_claim_interrupt(uint8_t pin) {
  if (pin_exist(pin)) {
    if (!(isr_enabled & (1ULL << pin))) {
      isr_enabled |= 1ULL << pin;

      VERBOSE(q_printf("%% ifc_claim_interrupt() : Registering an ISR for GPIO#%u\r\n",pin));
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
    if (isr_enabled & (1ULL << pin)) {
      if (!ifconds[pin]) {
        gpio_intr_disable(pin);
        gpio_isr_handler_remove(pin);
        isr_enabled &= ~(1ULL << pin);
        VERBOSE(q_printf("%% ifc_release_interrupt() : removing ISR for GPIO#%u\r\n",pin));
      }
    }
  }
}

// Create an "if" condition
//
// if rising|falling NUM [low|high NUM]* [max-exec NUM] [rate-limit MSEC] exec ALIAS_NAME
// if low|high NUM [low|high NUM]* [poll MSEC] [max-exec NUM] [rate-limit MSEC] exec ALIAS_NAME
//
static int cmd_if(int argc, char **argv) {

  int cond_idx = 1, max_exec = 0, rate_limit = 0, poll = 0;
  const char *exec = NULL; // alias name
  unsigned char trigger_pin = NO_TRIGGER;
  bool rising = false;

  // min command is "if clear 6" which is 3 keywords long
  if (argc < 3)
    return CMD_MISSING_ARG;

  // "delete" and "clear"
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

  if (argc < 5)
    return CMD_MISSING_ARG;

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
  uint64_t low = 0, high = 0;
  while( ((cond_idx + 1) < argc) &&                                   // while there are at least 2 keywords available
          (argv[cond_idx][0] == 'l' || argv[cond_idx][0] == 'h') ) {  // and the first keyword is either "low" or "high"

    unsigned char pin;

    // Read pin number, check if it is ok
    if ((pin = q_atoi(argv[cond_idx + 1], NO_TRIGGER)) == NO_TRIGGER)
      return cond_idx + 1;

    if (!pin_exist(pin))
      return cond_idx + 1;

    // Set corresponding bit and a gpio bitmask
    if (argv[cond_idx][0] == 'l')
      low |= 1ULL << pin;
    else
      high |= 1ULL << pin;

    // Next 2 keywords
    cond_idx += 2;
  }

  if (!low && !high && (trigger_pin == NO_TRIGGER)) {
    q_print("% Condition is expected after \"if\"\r\n");
    return 1;
  }

  // Read "max-exec NUM", "rate-limit NUM", "poll NUM" and "exec ALIAS_NAME"
  // all of them are 2-keywords statements
  while (cond_idx + 1 < argc) {

    if ( !q_strcmp(argv[cond_idx],"poll")) {
      if (0 > (poll = q_atoi(argv[cond_idx + 1], -1)))
        return cond_idx + 1;
    } else if ( !q_strcmp(argv[cond_idx],"max-exec")) {
      if (0 > (max_exec = q_atoi(argv[cond_idx + 1], -1)))
        return cond_idx + 1;
    } else if ( !q_strcmp(argv[cond_idx],"rate-limit")) {
      if (0 > (rate_limit = q_atoi(argv[cond_idx + 1], -1)))
        return cond_idx + 1;
    } else if ( !q_strcmp(argv[cond_idx],"exec")) {
      exec = argv[cond_idx + 1];
    } else {
      q_print("% <e>\"max-exec\", \"poll\", \"rate-limit\" or \"exec\" keywords are expected</>\r\n");
      return cond_idx;
    }
    cond_idx += 2;
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
  if (trigger_pin == NO_TRIGGER) {
    if (!poll)
      poll = 1;
    if (rate_limit) {
      q_print("% \"<i>rate-limit</>\" keyword is ignored for polling conditions:\r\n"
              "% rate is a constant which is defined by \"<i>poll</>\" keyword\r\n");
      rate_limit = 0;
    }
  } else if (poll) {
    q_print("% \"poll\" keyword is ignored for rising/falling conditions\r\n");
    poll = 0;
  }

  if (rate_limit) {
    ifc->has_rlimit = 1;
    ifc->rlimit = rate_limit;
  }

  ifc->poll_interval = poll;
  
  if (trigger_pin != NO_TRIGGER)
    ifc_claim_interrupt(trigger_pin);

  return 0;
}

//
//
//
static int cmd_show_ifs(int argc, char **argv) {

  ifc_show_all();
  return 0;
}

#endif // COMPILING_ESPSHELL


