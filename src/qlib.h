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

// -- Q-Lib: helpful routines: ascii to number conversions,platform abstraction, etc --
//
// 1. OS/Kernel lightweight abstraction layer (mutexes, message pipes, time intervals, delays, etc.
//    Part of it is in task.h file as well. QLib is used in number of different projects, thats why
//    I came up with the idea of a lightweight wrappers. In most cases these wrappers are simply synonims for
//    the OS, sometimes it is a small amout of code added.
//
// 2. Memory logger (for leaks detection, normally disabled)
// 3. Bunch of number->string and string->number conversion functions
// 4. Core functions like q_printf(), core variables etc
//
// TODO: find every place where pointers are assumed to be uint32_t in size and refactor using intptr_t.
//       Most of them are in var.h and task.h tho
// TODO: implement "static uintptr_t hex2uintptr(const char *hex_string, uintptr_t def)". Now we use 
//       hex2uint32 to convert pointers (user input, e.g. TASK_ID) to uint32_t which is not portable
//       across 32/64 bit architectures.
//
// TODO: consider implementing q_delay via NotifyWait: then every q_delay becomes interruptible and we can get rid
//       of this ugly delay_interruptible() logic? How to implement keypress interruption then?
#if COMPILING_ESPSHELL

#include <stdatomic.h>

// GCC-specific branch prediction optimization macros 
// Arduino Core is shipped with precompiled ESP-IDF where they have redefined likely() and unlikely() to be 
// an empty statement. We just turn it back as it should be: it is used alot in /if/ expression which are rarely
// executed (if at all)
//
#undef likely
#undef unlikely
#define unlikely(_X)   __builtin_expect(!!(_X), 0)
#define likely(_X)     __builtin_expect(!!(_X), 1)

// Memory type: a number from 0 to 15 to identify newly allocated memory block usage; 
// Used as a second argument of q_malloc() 
// Newly allocated memory is assigned one of the types below. Command "show memory" invokes
// q_memleaks() function to dump memory allocation information. Leak detection requires #define MEMTEST 1
// in espshell.h

enum {
  MEM_TMP = 0,   // tmp buffer. must not appear on q_memleaks() report
  MEM_STATIC,    // memory allocated once, never freed: sketch variables is an example
  MEM_EDITLINE,  // allocated by editline library (general)
  MEM_ARGIFY,    // argify() output
  MEM_ARGCARGV,  // refcounted user input
  MEM_LINE,      // input string from editline lib
  MEM_HISTORY,   // command history entry
  MEM_TEXT2BUF,  // TEXT argument (fs commands write,append,insert or uart's write are examples) converted to byte array
  MEM_PATH,      // path (c-string)
  MEM_GETLINE,   // memory allocated by files_getline()
  MEM_SEQUENCE,  // sequence-related allocations
  MEM_TASKID,    // Task remap entry
  MEM_ALIAS,     // Aliases and related allocation
  MEM_IFCOND,    // "if" and "every" conditions
  MEM_SERVER,    // server(s) name or address
  MEM_UNUSED15
  // NOTE: only values 0..15 are allowed, do not add more!
};

static NORETURN void must_not_happen(const char *message, const char *file, int line);

// Check if condition ... is true and if it is - halt ESPShell
// A wrapper for the must_not_happen() function
//
#define MUST_NOT_HAPPEN( ... ) \
  { \
    if ( unlikely(__VA_ARGS__) ) \
      must_not_happen(#__VA_ARGS__, __FILE__, __LINE__ ); \
  }

#define NOT_YET() \
  do { \
    q_print("% This feature is under development and is not implemented yet\r\n"); \
  } while( 0 )

// OS Abstraction Layer. Thin wrapper to hide all OS-specific API and keep it in one place.
// It includes: delay(), millis(), micros(), semaphores, mutexes, rwlocks, tasks etc. As a result, rest of the
// ESPShell code is free from FreeRTOS-specific calls. This layer allows me to develop & debug big chunks of 
// the code on my Cygwin installation.
//
// I call it "thin wrappers" because in most cases it is just a #define MyName() TheirName() however it is not
// always the case. Finally, FreeRTOS does not have concept of an infinite delay, so this part is implemented in
// this wrapper
//


// inlined version of millis() & delay() for better accuracy on small intervals
// (because of the decreased overhead). q_millis vs millis shows 196 vs 286 CPU cycles on ESP32
//
#define q_delay(_Delay) vTaskDelay(_Delay / portTICK_PERIOD_MS);
#define q_micros()      esp_timer_get_time()
#define q_millis()      ((unsigned long )(q_micros() / 1000ULL))


// Scheduler "task-switch" requests
//
#define q_yield() vPortYield()                    // run next task
#define q_yield_from_isr() portYIELD_FROM_ISR()   // reschedule tasks


//  -- Mutexes and Semaphores--

// Mutexes and semaphores are initialized on first use (i.e. on first mutex_lock() / sem_lock() call)
// These are fail-safe: mutex_lock() will continue to create a mutex if it was not created; it ensures
// stability in OOM scenarios

// Mutex type. Mutex is a semaphore on FreeRTOS
#define mutex_t SemaphoreHandle_t

// Initializer: "mutex_t mux = MUTEX_INIT;"
#define MUTEX_INIT NULL

// Acquire mutex
// Blocks forever, initializes mutex object on a first use. portMAX_DELAY is 0xffffffff, 
// which is roughly 1200 hours given a 1000Hz tickrate
//
#define mutex_lock(_Name) \
  do { \
    if (unlikely(_Name == NULL)) \
      _Name = xSemaphoreCreateMutex(); \
    if (likely(_Name != NULL))  \
      while (xSemaphoreTake(_Name, portMAX_DELAY) == pdFALSE) { \
      /* 1200 hours have passed. Repeat. */ \
      } \
  } while( 0 )

// Release mutex
#define mutex_unlock(_Name) \
  do { \
    if (likely(_Name != NULL)) \
      xSemaphoreGive(_Name); \
  } while( 0 )


// Semaphore
#define sem_t SemaphoreHandle_t

// Semaphore initializer
#define SEM_INIT NULL

// Binary semaphore.
// Similar to a mutex, but **any** task can sem_unlock(), not just the task
// which called sem_lock(): no ownership tracking. These are used in RW-locking
// code
//
#define sem_lock(_Name) \
  do { \
    if (unlikely(_Name == NULL)) { \
      _Name = xSemaphoreCreateBinary(); \
      break; /* created locked */\
    } \
    if (likely(_Name != NULL)) \
      while (xSemaphoreTake(_Name, portMAX_DELAY) == pdFALSE) { \
      /* 1200 hours have passed. Repeat. */ \
      } \
  } while( 0 )

// Release
#define sem_unlock(_Name) \
  mutex_unlock(_Name)


// Destroy mutex
#define mutex_destroy(_Name) \
  do { \
    if (likely(_Name != NULL)) { \
      vSemaphoreDelete(_Name); \
      _Name = NULL; \
    } \
  } while ( 0 )

// Destroy semaphore
#define sem_destroy(_Name) \
  mutex_destroy(_Name)

// -- Memory access barrier --
//
// Code behind the barrier (i.e. the code between barrier_lock() / barrier_unlock()) must be kept small and linear.
// Defenitely not call q_printf() or q_delay() while in the barrier, or watchdog will bark (interruptas are disabled!)
//
// Barrier lock is the only way to guarantee exclusive access both from tasks and interrupts on a multicore system
//
// TODO: refactor to use spinlock_t 
//
#define barrier_t portMUX_TYPE

// Initializer: barrier_t mux = BARRIER_INIT;
#define BARRIER_INIT portMUX_INITIALIZER_UNLOCKED
 
// Enter/Exit critical sections. On ESP32 these can be called from both ISR and TASK context.
#define barrier_lock(_Name) portENTER_CRITICAL(&_Name)
#define barrier_unlock(_Name) portEXIT_CRITICAL(&_Name)


// -- Readers/Writer lock --
//
// Implements classic "Many readers, One writer" scheme, write-preferring, simple RW locks
// Queued write requests prevent new readers from acquiring the lock
// Queued writers are blocking on a semaphore if there are active readers;
// Queued readers will spin if there are active writers
//
// RWLocks are used mostly with lists: one example is the alias.h where RWLocks are used 
// to protect aliases list, another is ifcond.c, where we have array of lists, which is traversed from within an ISR
// TODO: stress test with multiple reader/writer tasks

// RWLock type
typedef struct {
  _Atomic uint32_t wreq; // Number of pending write requests
  _Atomic int      cnt;  // <0 : write_lock, 0: unlocked, >0: reader_lock
  sem_t            sem;  // binary semaphore, acts like blocking object
} rwlock_t;

// initializer: rwlock_t a = RWLOCK_INIT;
#define RWLOCK_INIT { 0, 0, SEM_INIT} 

// Obtain exclusive ("Writer") access.
//
// If there were readers or writers, this function will block on /rw->sem/ and signal that further read requests
// must be postponed.
// If there are no readers/writers, then we grab a binary semaphore /rw->sem/ and change /cnt/ to negative
// value meaning "Write" lock has been acquired
//
void rw_lockw(rwlock_t *rw) {

  // Set "Write Lock Request" flag before acquiring /rw->sem/: this will stop new readers to obtain the reader lock
  atomic_fetch_add(&rw->wreq,1);
try_again:  
  // Grab main sync object. 
  // If it is held by readers or other writer then we simply block here
  sem_lock(rw->sem); 

  // Ok, we just acquired the semaphore; lets check if a
  // reader somehow sneaked in during the process: if it happened, then we retry whole procedure
  if (atomic_load(&rw->cnt) != 0) {
    sem_unlock(rw->sem);
    q_yield();
    goto try_again;
  }
  atomic_fetch_sub(&rw->cnt,1); //  i.e. rw->cnt = -1, "active writer" TODO: replace with atomic_store(.., -1)
  atomic_fetch_sub(&rw->wreq,1);
}

// WRITE unlock
// /cnt/ is expected to be -1 (Write Lock). If it isn't then we have bugs in out RW code
//
void rw_unlockw(rwlock_t *rw) {
  atomic_fetch_add(&rw->cnt,1); //same as rw->cnt = 0;
  sem_unlock(rw->sem);
}

// Obtain reader lock.
// Yield if theres writer lock obtained already
// 
// Dominant type of lock
//
// probably a race condition,results in erroneous sem_lock(), but it is not critical,
// just one reader will be blocked as if he is writer

void rw_lockr(rwlock_t *rw) {

  int cnt;

  // Wait until there are no readers and no writers and no writelock request is queued
  // Let "writer" task to do its job
  //
  while (atomic_load_explicit(&rw->cnt, memory_order_acquire) < 0 ||
         atomic_load_explicit(&rw->wreq, memory_order_acquire) > 0)
    q_yield(); 

  // Atomically increment READERS count
  cnt = atomic_fetch_add_explicit(&rw->cnt, 1, memory_order_acq_rel);

  // First of readers acquires rw->sem, so subsequent rw_lockw() will block immediately
  // If concurrent wr_lockw() obtains /sem/ just after rw->cnt++ then we simply block here,
  // for a tiny amount of time while rw_lockw() goes through its "try_again:"
  //
  if (!cnt)
      sem_lock(rw->sem);
}

// Reader unlock
//
void rw_unlockr(rwlock_t *rw) {
  if (atomic_fetch_sub(&rw->cnt, 1) == 1)
    sem_unlock(rw->sem);
}

//void rw_dump(const rwlock_t *rw) {
//  q_printf("rwlock(0x%p) : cnt=%d, wreq=%lu\r\n", rw, rw->cnt, rw->wreq);
//}


// -- Message Pipes --
//
// A machinery to send messages from an ISR to a task, or from a task to another task
// Messages are fixed in size (4 bytes, sizeof(void *)).
//
// Main idea around mpipes is to be able "to send" a pointer from an ISR to a task: e.g. GPIO ISR sends 
// messages to some processing task, but it is not limited to it.
//
// Depending on MPIPE_USES_MSGBUF macro, message pipes (mpipes) are implemented 
// either via FreeRTOS Message Buffers or FreeRTOS Queues
// Example of use:
//    void *test = (void *)0x12345678;          // <--- sample data to send, 4 bytes
//    unsigned int pipe_drops = 0;              // <--- "message dropped" counter. must be declared as PipeName + "_drops"
//    mpipe_t pipe = mpipe_create( 123 );       // <--- create pipe, 123 entries long
//    
// if (mpipe_send_from_isr( pipe, test))        // <--- send a message from an ISR
//      q_yield_from_isr();                     // <-+
//
// if (mpipe_send( pipe, test))                 // <--- send message from a task
//      printf("pipe is full\r\n");             // <-+
//
// mpipe_destroy(pipe);
//
// NOTE: NOTE: NOTE: NOTE: NOTE: NOTE: NOTE: NOTE: NOTE: 
//
//  when creating a pipe, a static variable must be defined (of any integer type) like below:
//
//   static mpipe_t comm = MPIPE_INIT;    --> message pipe itself
//   static unsigned int comm_drops = 0;  --> message pipe "dropped message" counter.
//

#ifdef MPIPE_USES_MSGBUF

// Message Pipe type, Message Pipe initializer
#  define mpipe_t MessageBufferHandle_t
#  define MPIPE_INIT NULL

// Create a new message pipe.
// Parameters:
//  _NumElements - maximum number of pending messages in the mpipe
// Returns:
//  A pointer to a created mpipe or NULL
//
#  define mpipe_create(_NumElements) \
    xMessageBufferCreate(_NumElements * sizeof(void *))

// Delete mpipe
// Parameters:
//  _Pipe - pointer to a mpipe or NULL
//
#  define mpipe_destroy(_Pipe) \
    { \
     if (likely(_Pipe != MPIPE_INIT)) \
       vMessageBufferDelete(_Pipe); \
    }

// Send an item (a pointer. we send pointers, not data), from isr to the task: isr->task
// 
// _Data : a variable (not an expression!), containing data to be sent (4 bytes, void *, unsigned int, float ...
// _Pipe : pointer to a mpipe or NULL
// 
// "void *value = 0x12345678; mpipe_send(ThePipe, value);"
// 
// Returns /true/ if CPU yield should be done to rechedule higher priority task
//
#  define mpipe_send_from_isr(_Pipe, _Data) \
    ({ \
      BaseType_t ret = pdFALSE; \
      if (likely(_Pipe != MPIPE_INIT)) \
        if (unlikely(xMessageBufferSendFromISR(_Pipe, &_Data, sizeof(void *), &ret) != sizeof(void *))) \
          _Pipe ## _drops++; \
      ret; /* returned value, true or false */\
    })

// Same as above, but task->task, and does not require manual rescheduling
//
#  define mpipe_send(_Pipe, _Data) \
    ({ \
      BaseType_t timo = 1; /* 1 tick timeout. */ \
      if (likely(_Pipe != MPIPE_INIT)) \
        if (unlikely(xMessageBufferSend(_Pipe, &_Data, sizeof(void *), timo) != sizeof(void *))) { \
          _Pipe ## _drops++; \
          timo = 0; \
        } \
      timo; /* returned value, true or false */\
    })


// Get an item out of a pipe
// If pipe is empty then mpipe_recv() will block, spinning in while() with portMAX_DELAY.
// Not suitable for use in ISR.
// _Pipe : pointer to a mpipe or NULL
// Returns a message, 4 bytes, as a void*: "void *value = mpipe_recv(ThePipe);"
//
#  define mpipe_recv(_Pipe) \
    ({ \
      void *ptr = NULL; \
      if (likely(_Pipe != MPIPE_INIT)) \
        while (xMessageBufferReceive(_Pipe, &ptr, sizeof(void *), portMAX_DELAY) == 0) \
          ; \
      ptr; \
    })

#else // use queues as underlying API

// Message Pipe type and its initializer
//
#  define mpipe_t QueueHandle_t
#  define MPIPE_INIT NULL

// Create a new message pipe
// _NumElements : maximum number of elements (i.e. pointers) this mpipe can hold  before it 
//                starts to drop new incoming messages
//
// Returns pointer to the newly created pipe or NULL
//
#  define mpipe_create(_NumElements) \
    xQueueCreate(_NumElements, sizeof(void *))

// Delete mpipe
// _Pipe : pointer to a mpipe or NULL
//
#  define mpipe_destroy(_Pipe) \
    { \
      if (likely(_Pipe != MPIPE_INIT)) \
        vQueueDelete(_Pipe); \
    }


// Send a message (_Data must be a variable: we are taking address of it)
// This is ISR->task routine. For task->task use mpipe_send()
//
// _Pipe : pointer to a mpipe or NULL
// _Data : a variable, usually - a pointer, but can be any 4 bytes simple type
//
// Returns /true/ if taskYIELD is required
//
#  define mpipe_send_from_isr(_Pipe, _Data) \
    ({ \
      BaseType_t ret = pdFALSE; \
      if (likely(_Pipe != MPIPE_INIT)) \
        if (unlikely(xQueueSendFromISR(_Pipe, &_Data, &ret) != pdPASS)) \
          _Pipe ## _drops++; \
      ret; \
    })

// ... same-same, but task->task messages
// NOTE: Despite having similar name to the mpipe_send_from_isr(), the major difference is the return value
// Returns /true/ if message was successfully sent. nothing about manual yielding CPU
#  define mpipe_send(_Pipe, _Data) \
    ({ \
      BaseType_t timo = 1; \
      if (likely(_Pipe != MPIPE_INIT)) \
        if (unlikely(xQueueSend(_Pipe, &_Data, timo) != pdPASS)) { \
          timo = 0; \
          _Pipe ## _drops++; \
        } \
      timo; \
    })

// Get an item out of the message pipe. Spin in while() forever, using portMAX_DELAY, to simulate infinite timeout
// Can not be used in ISR
// _Pipe : pointer to a mpipe or NULL
// Returns an item (4 byte-long variable, usually a pointer)
//
#  define mpipe_recv(_Pipe) \
    ({ \
      void *ptr = 0; \
      if (likely(_Pipe != MPIPE_INIT)) \
        while (xQueueReceive(_Pipe, &ptr, portMAX_DELAY) == pdFALSE) \
          ; \
      ptr; \
    })
#endif // MessageBuffers or Queues?

// Timers
#include <esp_timer.h>
#define timer_t esp_timer_handle_t
#define TIMER_INIT NULL
#define timerfunc_t esp_timer_cb_t

/// OS Abstraction Layer End;


// Some globals as well.
static bool Exit = false;            // True == close the shell and kill its FreeRTOS task. Can be restarted again with espshell_start()

static bool ColorAuto = AUTO_COLOR;  // Autoenable coloring if terminal permits
static bool Color = false;           // Coloring is enabled?


static signed char  Echo = STARTUP_ECHO;     // Runtime echo flag: -1=silent,0=off,1=on
static signed char  Echop = 0;               // "Previous" state of the /Echo/. Used to temporary off echo by "@" symbol



// -- Coloring / ANSI sequences --
// ESPShell messages can have **color tags** embedded into them like in example below:
//       "This <b>text is bold</><u><g>And this one green and underlined</>"
//
// The HTML-looking tags we use are single-letter tags: <b> <e> <i> ... 
// Closing tag </> simply sets standart colors and text attributes (cancels action of ALL previous tags)
// Tag actions are additive: <u><g> will set text color to green and turns underline font option; 
// the </> tag afterwards will cancel both <g> and <u>
//
// Color tags are processed in q_print() (there are 1 direct use of color sequence in editline.h) and
// either replaced with ANSI coloring sequences or they are simply gets stripped if coloring is turned off
//
// Sequences below have their **length** encoded as the very first byte to save on strlen() later.
//
static const char *ansi_tags['z' - 'a' + 1] = {
  // Foreground color
  ['b' - 'a'] = "\07\033[1;97m",   // [b]old bright white
  ['d' - 'a'] = "\07\033[37m",     // [d]ark white  (<b><d> Bold Dark White</>)
  ['e' - 'a'] = "\05\033[95m",     // [e]rror message (bright magenta)
  ['i' - 'a'] = "\010\033[33;93m", // [i]important information (eye-catching bright yellow)
  ['n' - 'a'] = "\04\033[0m",      // [n]ormal colors. cancels all tags
  ['r' - 'a'] = "\04\033[7m",      // [r]everse video
  ['w' - 'a'] = "\05\033[91m",     // [w]arning message ( bright red )
  ['o' - 'a'] = "\05\033[33m",     // [o]ptional dark yellow
  ['u' - 'a'] = "\07\033[4;37m",   // [u]nderlined, normal white. (<u><b>Underlined Bright White</>)
  ['g' - 'a'] = "\05\033[92m",     // [g]reen. Bright green
  ['c' - 'a'] = "\05\033[36m",     // [c]yan. dark
  ['z' - 'a'] = "\05\033[96m",     // [z]yan. bright

  // Background color
  ['x' - 'a'] = "\05\033[41m",     // red background ( X - no no no)
  ['y' - 'a'] = "\05\033[42m",     // green background ( Y - yes yes yes)
  //other definitions can be added here as well as long as they are in [a-z] range
};

// This strange looking comment line below is here to keep Arduino IDE's colorer happy
/*"*/

// Return an ANSI terminal sequence which corresponds to given tag.
// NOTE: tag </> is a synonym for <n>, i.e. a "normal" text attributes
static __attribute__((const)) const char *tag2ansi(char tag) {

  return tag == '/' ? ansi_tags['n' - 'a'] + 1
                    : (tag >= 'a' && tag <= 'z' ? ansi_tags[tag - 'a'] + 1
                                                : NULL);
}

// PPA(Number) generates 2 arguments for a printf ("%u%s",PPA(Number)), adding an "s" where its needed:
// printf("%u second%s", PPA(1))  --> "1 second"
// printf("%u second%s", PPA(2))  --> "2 seconds"
//
// NEE(Number) are similar to the PPA above except it generates "st", "nd",
// "rd" and "th" depending on /Number/:
// printf("You are %u%s on the queue", NEE(1))  --> "You are 1st on the queue"
// printf("You are %u%s on the queue", NEE(2))  --> "You are 2nd on the queue"
//
static inline __attribute__((const)) const char *number_english_ending(unsigned int const n) {
  const char *endings[] = { "th", "st", "nd", "rd" };
  return n > 3 ? endings[0] : endings[n];

}

#define PPA(_X) _X, (_X) == 1 ? "" : "s"
#define NEE(_X) _X, number_english_ending(_X)




// Check if memory address is in valid range. This function does not check memory access
// rights, only boundaries are checked.
// sizeof(unsigned int) == sizeof(void *) is ensured in espshell.c static_asserts section
//
static bool __attribute__((const)) is_valid_address(const void *addr, unsigned int count) {
  
  return  ((unsigned int)addr >= 0x20000000) && ((unsigned int)addr + count <= 0x80000000);
}

#if MEMTEST
// If MEMTEST is non-zero then ESPShell provides its own versions of q_malloc, q_strdup, q_realloc and q_free
// functions which do memory statistics/tracking and perform some checks on pointers
// being freed. When MEMTEST is 0 then q_malloc(), q_free(), ... are aliases for malloc(), free() ...
//
// Memory allocation API (malloc(), realloc(), free() and strdup()) are wrapped to keep track of
// allocations and report memory usage statistics. This code here is for debugging ESPShell itself only!
//
// ESPShell keeps track of all its memory allocations in a SL list and also creates a 2 bytes "buffer 
// overwrite detection zone" at the end of the buffer allocated. These are checked upon q_free()
//
// Statistics is displayed by "show memory" command
//
typedef struct list_s {
  struct list_s *next;
} list_t;

// memory record struct
typedef struct {
  list_t         li;      // list item. must be first member of a struct
  unsigned char *ptr;     // pointer to memory (as per malloc())
  unsigned int   len:20;  // length (as per malloc())
  unsigned int   type:4;  // user-defined type
} memlog_t;

// human-readable memory types
static const char *memtags[] = {

  "TMP",
  "STATIC",
  "EDITLINE",
  "ARGIFY",
  "ARGCARGV",
  "LINE",
  "HISTORY",
  "TEXT2BUF",
  "PATH",
  "GETLINE",
  "SEQUENCE",
  "TASKID",
  "ALIAS",
  "IFCOND",
  "SERVER",
  "UNUSED15"
};

// allocated blocks
static memlog_t *head = NULL;

// allocated memory total, and overhead added by memory logger
static unsigned int allocated = 0, internal = 0;

// memory logger mutex to access memory records list
static mutex_t Mem_mux;

// memory allocated with extra 2 bytes: those are memory buffer overrun
// markers. we check these at every q_free()
static void *q_malloc(size_t size, int type) {

  unsigned char *p = NULL;
  memlog_t *ml;

  // yes, keeping a memlog_t structure as an extension to the memory being allocated
  // is better, than keeping a separate list of memory entries as it is done below.
  // The memlog code is supposed to be used only during development phase where speed is
  // not an issue
  if ((type >= 0) && (type < 16) && (size < 0x80000) && (size > 0))
    if ((p = (unsigned char *)malloc(size + 2)) != NULL)
      if ((ml = (memlog_t *)malloc(sizeof(memlog_t))) != NULL) {
        ml->ptr = p;
        ml->len = size;
        ml->type = type;
        
        mutex_lock(Mem_mux);
        ml->li.next = (list_t *)head;
        head = ml;
        allocated += size;
        internal += sizeof(memlog_t) + 2;
        mutex_unlock(Mem_mux);

        // naive barrier. detects linear buffer overruns
        p[size + 0] = 0x55;
        p[size + 1] = 0xaa;
      }
  return (void *)p;
}

// free() wrapper. check if memory was allocated by q_malloc/realloc or q_strdup. 
// does not free() memory if address is not on the list.
// ignores NULL pointers, checks buffer integrity (2 bytes at the end of the buffer)
//
static void q_free(void *ptr) {
  if (!ptr)
    q_printf("<w>WARNING: q_free() : attempt to free(NULL) ignored</>\r\n");
  else {
    memlog_t *ml, *prev = NULL;
    const unsigned char *p = (const unsigned char *)ptr;
    //memlog_lock();
    mutex_lock(Mem_mux);
    for (ml = head; ml != NULL; ml = (memlog_t *)(ml->li.next)) {
      if (ml->ptr == p) {
        if (prev)
          prev->li.next = ml->li.next;
        else
          head = (memlog_t *)ml->li.next;
        allocated -= ml->len;
        internal -= (sizeof(memlog_t) + 2);
        break;
      }
      prev = ml;
    }
    //memlog_unlock();
    mutex_unlock(Mem_mux);
    if (ml) {
      // check for memory buffer linear write overruns
      if (ml->ptr[ml->len + 0] != 0x55 || ml->ptr[ml->len + 1] != 0xaa)
        q_printf("<w>CRITICAL: q_free() : buffer %p (length: %d, type %d), overrun detected</>\r\n",ptr,ml->len,ml->type);
      free(ptr);
      free(ml);
    }
    else
      q_printf("<w>WARNING: q_free() : address %p is not  on the list, do nothing</>\r\n",ptr);
  }
}

// generic realloc(). it is much worse than newlib's one because this one
// doesn't know anything about heap structure and can't simple "extend" block.
// instead straightforward "allocate then copy" strategy is used
//
static void *q_realloc(void *ptr, size_t new_size,UNUSED int type) {

  char *nptr;
  memlog_t *ml;

  // Trivial case #1
  if (ptr == NULL)
    return q_malloc(new_size,type);

  // Be a good realloc(), accept size of 0
  if (new_size == 0 && ptr != NULL) {
    q_free(ptr);
    return NULL;
  }

  // A memory pointer being reallocated must be on a list. If its no - then it simply means that memory 
  // user is trying to realloc() was not allocated through q_malloc() or may be it is a bad/corrupted pointer
  mutex_lock(Mem_mux);
  for (ml = head; ml != NULL; ml = (memlog_t *)(ml->li.next))
    if (ml->ptr == (unsigned char *)ptr)
      break;
  
  if (!ml) {
    mutex_unlock(Mem_mux);
    q_printf("<w>ERROR: q_realloc() : trying to realloc pointer %p which is not on the list</>\r\n",ptr);
    return NULL;
  }

  // trivial case #2: requested size is the same as current size, so do nothing
  // TODO: should it be new_size <= ml->len ?
  if (new_size == ml->len) {
    mutex_unlock(Mem_mux);
    return ptr;
  }

  // Allocate a memory buffer of a new size plus 2 bytes for a naive "buffer overrun" detector
  if ((nptr = (char *)malloc(new_size + 2)) != NULL) {

    nptr[new_size + 0] = 0x55;
    nptr[new_size + 1] = 0xaa;

    // copy content to the new resized buffer and free() the old one. we can't use q_free() here because we want to keep
    // a memlog entry (which otherwise gets deleted)
    memcpy(nptr, ptr, (new_size < ml->len) ? new_size : ml->len);
    free(ptr);

    // Update the memory entry (memlog_t) with new size and new pointer values
    ml->ptr = (unsigned char *)nptr;
    allocated -= ml->len;
    ml->len = new_size;
    allocated += new_size;
  }

  //memlog_unlock();
  mutex_unlock(Mem_mux);
  return nptr;
}

// last of the family: strdup()
// correctly duplicates NULL pointer
//
// /ptr/  - asciiz to be duplicated
// /type/ - memory type
//
//  returns NULL if ptr NULL or is empty string, or it is out of memory
//  return pointer to new buffer with string copied
//
static char *q_strdup(const char *ptr, int type) {
  char *p = NULL;
  if (likely(ptr != NULL))
    if ((p = (char *)q_malloc(strlen(ptr) + 1,type)) != NULL)
      strcpy(p,ptr);
  return p;
}

// Display memory usage statistics by ESPShell
// Warning signs (possible leaks): 
//
//  - MEM_HISTORY or MEM_LINE entries count increase (beyond 20 and 1 resp.)
//  - MEM_TMP buffers
//  - Multiple MEM_ARGIIFY or/and multiple MEM_ARGCARGV
//  - Multiple (>2) MEM_PATH
//
static void q_memleaks(const char *text) {
  int count = 0;

  unsigned int counters[16] = { 0 };

  q_printf("%%%s\r\n%% Allocated by ESPShell: <i>%u bytes</> (+ <i>%u bytes</> used by memory tracker)\r\n%%\r\n",text,allocated,internal); 

  q_print("<r>"
          "%  Entry | Memory  type |   Size  |  Address  \r\n"
          "%--------+--------------+---------+-----------</>\r\n");

  for (memlog_t *ml = head; ml; ml = (memlog_t *)(ml->li.next)) {
    
    q_printf("%%  %5u | %12s | %7u | %p \r\n",++count,memtags[ml->type],ml->len,ml->ptr);
    
    counters[ml->type]++;
  }

  if ((counters[MEM_HISTORY] > HIST_SIZE) ||
      (counters[MEM_LINE] > 1) ||
      (counters[MEM_TMP] > 0) ||
      (counters[MEM_ARGIFY] > 1) ||
      (counters[MEM_ARGCARGV] > 1))
    q_printf("%% <i>WARNING: possible memory leak(s) detected</>\r\n");

#if WITH_HELP
  q_printf("<r>%% Tracking %07u memory block%s              </>\r\n"
              "%% Use command \"show mem ADDRESS [COUNT]\" to display data at memory address\r\n",PPA(count));
#endif
}
#else // MEMTEST==0
#  define q_malloc(_size, _type)            malloc((_size))
#  define q_realloc(_ptr, _new_size, _type) realloc((_ptr), (_new_size))
#  define q_strdup(_ptr, _type)             strdup((_ptr))
#  define q_free(_ptr)                      free((_ptr))
#  define q_memleaks(_X)                    do {} while(0)
#endif // MEMTEST



// strdup() + extra 256 bytes
// TODO: This is bad. Make generic q_strdup_tailroom(const char *string, int tailroom)
static char *q_strdup256(const char *ptr, int type) {
  char *p = NULL;
  if (likely(ptr != NULL)) {
    int len = strlen(ptr);
    if ((p = (char *)q_malloc(len + 256 + 1, type)) != NULL)
      strcpy(p, ptr);
  }
  return p;
}


// Convert an asciiz (7bit per char) string to lowercase.
// Conversion is done for characters 'A'..'Z' by setting 5th bit
//
// /p/   - pointer to the string being converted (must be writeable memory)
//
static void q_tolower(char *p) {
  if (likely(p != NULL)) {
    char pp;
    do {
      pp = *p; // pp undergo CSE removal, while *p is not :(
      if (pp >= 'A' && pp <= 'Z')
        *p = pp | (1 << 5);
      p++;
    } while( pp );
  }
}

// Check if given ascii string represents a decimal number. Ex.: "12345", "-12", "+2"
// "minus" sign is only accepted at the beginning (must be 1st symbol)
// Can be called with p = NULL, it is normal
static bool isnum(const char *p) {
  if (likely(p && *p)) {
    if (*p == '-' || *p == '+')
      p++;
    //just single sign is not a valid number
    if (*p) {
      //only digits 0..9 are valid symbols
      while (*p >= '0' && *p <= '9')
        p++;
      //if *p is 0 then all the chars were digits (end of line reached).
      return *p == '\0';  
    }
  }
  return false;
}

// Inlined optimized versions of isnum() and atoi() for special case: these are called from cmd_pin() handler,
// and this optimization is to decrease delays when processing multiple "pin" arguments. Also the "-" sign is
// not allowed. Functions below are intended to process small numbers (2 digits), e.g. pin numbers 
static inline bool isnum2(const char *p) {
  return p[0] >= '0' && p[0] <= '9' && (p[1] == 0 || (p[1] >= '0' && p[1] <= '9'));
}
// ...
static inline int atoi2(const char *p) {
  return p[1] ? (p[0] - '0') * 10 + p[1] - '0'
              : (p[0] - '0');
}

// Check if ascii string represents a floating point number
// "0.5" and "-.5" are both valid inputs
// TODO: "-." and "." are both valid input also, however subsequent call to q_atof() will return 0
// The same behavior for isnum() was fixed (i.e. isnum() does not accept single "-" character as a valid number).
//
static bool isfloat(const char *p) {
  if (p && *p) {
    bool dot = false;
    if (*p == '-' || *p == '+')
      p++;
    while ((*p >= '0' && *p <= '9') || (*p == '.' && !dot))
      if (*p++ == '.')
        dot = true;
    return *p == '\0';  //if *p is 0 then all the chars were ok. (end of line reached).
  }
  return false;
}

// Check if given ascii string is a hex number
// String may or may not start with "0x"
// Strings "a" , "5a", "0x5" and "0x5Ac52345645645234564756" are all valid input
//
static bool ishex(const char *p) {
  if (likely(p && *p)) {
    char c;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
      p += 2;
    while ((c = *p) != '\0') {
      
      // Convert to lowercase
      if (unlikely(c >= 'A' && c <= 'Z'))
        c |= 1 << 5;

      if (c < '0' || (c > '9' && c < 'a') || c > 'f')
        break;
      p++;
    }
    return *p == '\0';
  }
  return false;
}

// check only first 1-2 bytes (not counting "0x" if present)
// TODO: refactor to decrease number of branches
static bool ishex2(const char *p) {

  if (likely(p && *p)) {
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
      p += 2;

    if ((*p >= '0' && *p <= '9') ||
        (*p >= 'a' && *p <= 'f') ||
        (*p >= 'A' && *p <= 'F')) {
      p++;
      if ((*p == 0) ||
          (*p >= '0' && *p <= '9') ||
          (*p >= 'a' && *p <= 'f') ||
          (*p >= 'A' && *p <= 'F')) return true;
    }
  }
  return false;
}

// Check, if asciiz string represents an octal number
// "0777" is a valid input, '0888' is not
//
static bool isoct(const char *p) {

  if (p && *p == '0') {
    p++;

    while (*p >= '0' && *p < '8') 
      p++;

    return *p == '\0';  //if *p is 0 then all the chars were digits (end of line reached).
  }
  return false;
}

// Same as above but for binary data
// "0b00101110101" 
static bool isbin(const char *p) {

  if (likely(p && *p)) {

    if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B'))
      p += 2;

    while (*p == '0' || *p == '1')
      p++;

    return *p == '\0';  //if *p is 0 then all the chars were digits (end of line reached).
  }

  return false;
}

// Check if string can be converted to a number, trying all possible formats: 
// floats, octal, binary or hexadecimal with leading 0x or without it, both signed and unsigned
//
static bool q_isnumeric(const char *p) {
  if (likely(p && *p)) {
    if (p[0] == '0') {
      if (p[1] == 'x' || p[1] == 'X') return ishex(p + 2);
      if (p[1] == 'b' || p[1] == 'B') return isbin(p + 2);
      if (p[1] == '.') return isfloat(p);
      return isoct(p);
    }
    return isnum(p) || isfloat(p);
  }
  return false;
}

// Convert hex ascii byte, unrolled
// Strings "A", "5a" "0x5a" are all valid input
//
static unsigned char hex2uint8(const char *p) {

  unsigned char f = 0, l;  //first and last

  // Skip leading "0x" if any
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    p += 2;

  // Single character hex? (i.e "a", "A" or "0xC")
  // Calculate the first nibble
  if (p[1] != '\0') {
    f = *p++;
    // Code below will treat any unexpected symbol (e.g. letter "k") as a zero;
    // Thus strings like "0xKK" will convert to 0, "0xAZ" -> 0xa0, "0xZA" -> 0x0a
    f = f - (f >= 'A' && f <= 'F' ? ('A' - 10) 
                                  : (f >= 'a' && f <= 'f' ? ('a' - 10)
                                                          : (f >= '0' && f <= '9' ? '0' 
                                                                                  : f )));
    f <<= 4;
  }
  // ..and the last
  l = *p++;
  // Code below expands either to a number or, if input was incorrect, zero is returned
  // This leads to equaliuty between "0", "0x" and "0x0" - all of these strings get converted to zero
  l = l - (l >= 'A' && l <= 'F' ? ('A' - 10)
                                : (l >= 'a' && l <= 'f' ? ('a' - 10)
                                                        : (l >= '0' && l <= '9' ? '0' 
                                                                                : l )));
  return f | l;
}

// Convert a hex string to uint32_t
// If string is too long then number converted will be equal to the last 4 bytes of the string
// Returns zero if string contains unexpected characters
//
static unsigned int hex2uint32(const char *p) {

  unsigned int value = 0;
  unsigned int four = 0;
  char c;

  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    p += 2;

  while ((c = *p) != '\0') {
      
    // turn to lowercase.
    if (unlikely(c >= 'A' && c <= 'Z'))
      c |= 1 << 5;

    if (c >= '0' && c <= '9') four = c - '0'; else 
    if (c >= 'a' && c <= 'f') four = c - 'a' + 10; else break;

    value = (value << 4) | four;
    p++;
  }
  return value;
}

// Used to read pointer/address values
//
static uintptr_t hex2uintptr(const char *p) {

  uintptr_t value = 0;
  unsigned int four = 0;
  char c;

  // skip leading "0x" if present
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
    p += 2;

  // scan /p/ till the end, convert 4 bits at a time
  while ((c = *p) != '\0') {
      
    // turn to lowercase.
    if (unlikely(c >= 'A' && c <= 'Z'))
      c |= 1 << 5;

    if (c >= '0' && c <= '9') four = c - '0'; else 
    if (c >= 'a' && c <= 'f') four = c - 'a' + 10; else
      break; // stop scanning on first "bad" symbol, return what was read 

    value = (value << 4) | four;
    p++;
  }
  return value;

}

// Same as above but for octal numbers (e.g. "012346773" with leading 0)
// If string has symbols outside of the allowed range (`0`..`7`) then this
// function return what was read. Empty strings ("") gets converted to zero
//
static unsigned int octal2uint32(const char *p) {
  unsigned int value = 0;
  while (*p >= '0' && *p <= '7') {
    value = (value << 3) | (*p - '0');
    p++;
  }
  return value;
}

// Convert strings 0b10010101 and 10100101 (with or without leading "0b") to unsigned int values
// If there are more than 32 bits in the string then only last 32 bits will be read, while some leading bits 
// will be ignored
// Empty strings or strings with characters other than '0' or '1' gets converted to zero
//
static unsigned int binary2uint32(const char *p) {
  unsigned int value = 0;

  if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B'))
    p += 2;

  while (*p == '0' || *p == '1') {
    value = (value << 1) | (*p - '0');
    p++;
  }
  return value;
}

#if WITH_WIFI
// Network utility functions: IP parsing, MAC parsing etc
// We assume LittleEndian arch here (all Espressif chips are LE as of Nov-2025)
//
#if 1 // set to 0 for hypotethical big-endian MPU
#  define q_ntohl(_X) __builtin_bswap32(_X)
#  define q_htonl(_X) __builtin_bswap32(_X)
#  define q_ntohs(_X) __builtin_bswap16(_X)
#  define q_htons(_X) __builtin_bswap16(_X)
#else
#  define q_ntohl(_X) (_X)
#  define q_htonl(_X) (_X)
#  define q_ntohs(_X) (_X)
#  define q_htons(_X) (_X)
#endif



// Ascii to MAC address
// Convert asciiz "  0001:0203:0405" or "0102 03:0405 06" to /unsigned char[6]/
//
static bool q_atomac(const char *text, unsigned char *out) {

  int k = 0;
  unsigned char out0[6];

  if (!out)
    out = out0;

  if (likely(text)) {
    while(*text && k < 6) {
      if (ishex2(text)) {
        out[k++] = hex2uint8(text);
        text++;
      }
      text++;
    }
  }
  return k == 6;
}

// IP or IP/PREFIX ascii string to an IP address and a mask
// "1.2.3.4" , "1.2.3.4/24", "1.2/16"
// Returns 0 if input is either invalid or "0.0.0.0"
// Storage for /mask/ is optional, can be NULL
//
// TODO: add third "def" parameter
//
static uint32_t q_atoip(const char *p, uint32_t *mask) {
  
  int      piece  = 0;  // numbers between dots and slashes
  uint32_t result = 0;  // read IP
  int      dots   = 0;  // number of dots seen

  // Set default mask. Update it later, if we have prefix length ("a.b.c.d/prefix_length")
  if (mask)
    *mask = 0xffffffffUL; //255.255.255.255 - host mask

  // Substract here, because of ++p in /while(*++p)/
  if (p--) 
    while(*++p)
      switch (*p) {
        // Accumulate digits. If resulting number is >255 then retrun with error
        case '0' ... '9':
          if ((piece = piece * 10 + (*p - '0')) > 255)
            return 0;
          break;

        // A dot: commit accumulated number. Expected not more than 3 dots 
        case '.':
          result = result * 256 + piece;
          piece = 0;
          if (++dots > 3)
            return 0;
          break;

        // Prefix length. Commit last accumulated number
        case '/':
        case '\\':
          result = (result * 256 + piece) << ((3 - dots) * 8);
          piece = 0;
          while(*++p)
            if (*p >= '0' && *p <= '9')
              if ((piece = piece * 10 + (*p - '0')) > 32)
                return 0;
          if (mask)
            *mask = *mask << (32 - piece);
          return result;
        default:
          return 0;
      };

  // "no prefix length" values end up here, must be 3 dots
  // and a non-zero last piece
  return dots == 3 && piece > 0 ? result * 256 + piece : 0;
}


#endif // WITH_WIFI


#define DEF_BAD ((unsigned int)(-1)) // to be used as "default" value for q_atol

// q_atol() : extended version of atol()
// 1. Accepts decimal, hex,octal or binary numbers (0x for hex, 0 for octal, 0b for binary)
// 2. If conversion fails (bad symbols in string, empty string etc) the
//    "def" value is returned
//
static unsigned int q_atol(const char *p, unsigned int def) {
   // 1. If condition is true -> continue to the right, else
   // 2. If condition is false, continue from "?" down to the first ":"
   // 3. Go to 1
   return p && *p ? (p[0] == '0' ? (p[1] == 'x' || p[1] == 'X'  ? (ishex(p) ? hex2uint32(p)
                                                                            : def) 
                                                                : (p[1] == 'b' || p[1] == 'B' ? (isbin(p) ? binary2uint32(p)
                                                                                                          : def)
                                                                                              : (isoct(p) ? octal2uint32(p)
                                                                                                          : def)))
                                 : (isnum(p) ? atol(p) 
                                             : def))
                  : def;
}

// q_atoi only accepts decimal numbers
//
static inline int q_atoi(const char *p, int def) {
  return isnum(p) ? atoi(p) : def;
}


// Safe conversion to /float/ type. Returns /def/ if conversion can not be done
//
static inline float q_atof(const char *p, float def) {
  return isfloat(p) ? atof(p) : def;
}


// Loose strcmp() which performs a **partial** match. It is used to match commands and parameters which are shortened:
// e.g. user typed "seq" instead of "sequence" or "m w" instead of "mount wwwroot".
//
// In:
// /partial/ - string which may to be incomplete/shortened, usually user input (i.e. argv[X]).
// /full/    - "full" string to compare against.
//
// Out:
//  0 - match (full or partial)
//  1 - mismatch
// Examples:
//   q_strcmp("seq","sequence") == 0, match
//   q_strcmp("sequence","seq") == 1, no match
//   q_strcmp("seq","aseq") == 1, no match
//
// Optimized for short (0..10 symbols) strings.
//
static int IRAM_ATTR q_strcmp(const char *partial, const char *full) {

  // Quick reject
  if (unlikely(partial == NULL &&
               full    == NULL &&
              *partial == '\0'))
    return 1;

  // Quick reject 2
  if (*partial++ != *full++)
    return 1;

  // Run through every character of the /partial/ and compare them to characters of /full/
  // If /partial/ is longer than /full/, then matching will fail on /full's/ zero byte
  while(*partial)
    if (*partial++ != *full++)
      return 1;

  return 0;
}

// 
static inline const char *q_findchar(const char *str, char sym) {
  if (str) {
    while (*str && sym != *str)
      str++;
    if (sym == *str)
      return str;
  }
  return NULL;
}


// Adopted from esp32-hal-uart.c Arduino Core.
// Internal buffer is changed from static to stack because q_printf() can be called from different tasks
// This function can be used in Out-of-memory situation however caller must not use strings larger than 128 bytes (after % expansion)
// TODO: if we have PSRMA: allocate permanent 5kb buffer to minimize calls to vsnprintf 
static int __printfv(const char *format, va_list arg) {

  /*static */char buf[128 + 1]; //TODO: profile the shell to find out an optimal size. optimal == no calls to q_malloc
  char *temp = buf;
  uint32_t len;
  int ret;
  va_list copy;

  // make fake vsnprintf to find out required buffer length
  va_copy(copy, arg);
  len = vsnprintf(NULL, 0, format, copy);
  va_end(copy);

  // if required buffer is larger than built-in one then allocate
  // a new buffer
  if (len >= sizeof(buf))
    if ((temp = (char *)q_malloc(len + 1, MEM_TMP)) == NULL)
      return 0;

  // actual printf()
  vsnprintf(temp, len + 1, format, arg);
  ret = q_print(temp);
  if (temp != buf) {
    // TODO: place a counter here to see how often it gets allocated
    q_free(temp);
  }
  return ret;
}

// same as printf() but uses global var 'uart' to direct
// its output to different uarts
// NOTE: add -Wall or at least -Wformat to Arduino's c_flags for __attribute__ to have
//       effect.
static int PRINTF_LIKE q_printf(const char *format, ...) {
  int len;
  va_list arg;

  if (Echo < 0)  //"echo silent"
    return 0;
  
  va_start(arg, format);
  len = __printfv(format, arg);
  va_end(arg);
  return len;
}


// Version of q_printf() which does NOT process format tags (%u, %s. etc)
// It is faster, than q_printf
//
static int q_print(const char *str) {

  size_t len = 0;

  if (Echo < 0)  //"echo silent"
    return 0;

  if (str && *str) {

    const char *ins, *p, *pp = str;
    
    // /pp/ is the "current pointer" - a pointer to a currently analyzed chunk of an input string (i.e. /str/)
    while (*pp) {
      
      // Shortcut #1: No color tags? Send it straight to console, fast operation
      if ((p = q_findchar(pp, '<')) == NULL)
        return console_write_bytes(pp, strlen(pp));

      // Found something looking like color tag. Process them, inserting corresponding ANSI sequence into
      // output, replacing color tags.
      // Check if it is a color
      //
      if (p[1] && p[2] == '>') {

        ins = Color ? tag2ansi(p[1]) : NULL; // NOTE: ins can only have values returned by tag2ansi() as they are of special format (pascal-like)

        // Send everything _before _the tag to the console
        len += console_write_bytes(pp, p - pp);

        // if there is something to insert - send it to the console
        if (ins)
          len += console_write_bytes(ins, *(ins - 1)); // NOTE: ins has its length prepended
        // advance source pointer by 3: the length of a color tag sequence <b>
        pp = p + 3;

      } else {
        // Tag does not appear to be "our" color tag: there was opening "<" but closing tag and/or tag value was missing.
        // Send a string, including opening tag bracket (i.e. "<") to the console. Advance /pp/ so it points to the character
        // next to "<"
        len += console_write_bytes(pp, p - pp + 1);
        pp = p + 1;
      }
    }
  }
  return len;
}

// print /Address : Value/ pairs, decoding the data according to data type
// 1,2 and 4 bytes long data types are supported
// If it is more than 1 element in the table, then print a header also
//
static void q_printtable(const unsigned char *p, unsigned int count, unsigned char length, bool isu, bool isf, bool isp) {
    if (p && count && length) {
      if (count > 1)
        q_printf("%% Array of %u elements, %u bytes each\r\n%%  Address   :  Value    \r\n",count,length);
      while (count) {
        q_printf("%% %p : ", p);
        if (isp) {
          q_printf("%p\r\n", (void *)(*((intptr_t *)p)));
        } else if (isf) {
          q_printf("%ff\r\n", *((float *)p));
        } else {
          if (length == sizeof(unsigned int)) {
            if (isu)
              q_printf("%u (0x%x)\r\n",*((unsigned int *)p),*((unsigned int *)p));
            else
              q_printf("%i\r\n",*((signed int *)p));
          } else if (length == sizeof(unsigned short)) {
            if (isu)
              q_printf("%u (0x%x)\r\n",*((unsigned short *)p),*((unsigned short *)p));
            else
              q_printf("%i\r\n",*((signed short *)p));
          } else if (length == sizeof(unsigned char)) {
            if (isu)
              q_printf("%u (0x%x)\r\n",*((unsigned char *)p),*((unsigned char *)p));
            else
              q_printf("%i\r\n",*((signed char *)p));
          } else {
            MUST_NOT_HAPPEN( true ); // fatal error, likely memory corruption. abort the shell, don't make things worse
          }
        }
        p += length;
        count--;
      }
    }
}
// make fancy hex data output: mixed hex values
// and ASCII. Useful to examine I2C EEPROM contents.
//
// data printed 16 bytes per line, a space between hex values, 2 spaces
// after each 4th byte. then separator and ascii representation are printed
//
static short tbl_min_len = 16;

static void q_printhex(const unsigned char *p, unsigned int len) {

  if (!p || !len)
    return;

  if (len < tbl_min_len) {
    // data array is too small. just do simple output
    do {
      q_printf("%02x ", *p++);
    } while (--len);
    q_print("\r\n");
    return;
  }


  char ascii[16 + 2 + 1];
  unsigned int space = 1;

  q_print("<r>       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F  |0123456789ABCDEF</>\r\n");
  q_print("<r>-----</>-----------------------------------------------------+----------------\r\n");

  for (unsigned int i = 0, j = 0; i < len; i++) {
    // add offset at the beginning of every line. it doesnt hurt to have it.
    // and it is useful when exploring eeprom contens
    if (!j)
      q_printf("<r>%04x:</> ", i);

    //print hex byte value
    q_printf("%02x ", p[i]);

    // add an extra space after each 4 bytes printed
    if ((space++ & 3) == 0)
      q_print(" ");

    //add printed byte to ascii representation
    //dont print anything with codes less than 32 and higher than 127
    ascii[j++] = (p[i] < ' ' || p[i] > '~') ? '.' : p[i];

    // one complete line could be printed:
    // we had 16 bytes or we reached end of the buffer
    if ((j > 15) || (i + 1) >= len) {

      // end of buffer but less than 16 bytes:
      // pad line with spaces
      if (j < 16) {
        unsigned char spaces = (16 - j) * 3 + (j <= 4 ? 3 : (j <= 8 ? 2 : (j <= 12 ? 1 : 0))) + 1;  // empirical :)
        char tmp[spaces + 1];
        memset(tmp, ' ', spaces);
        tmp[spaces] = '\0';
        q_print(tmp);
      }

      // print a separator and the same line but in ascii form
      q_print("|");
      ascii[j++] = '\r';
      ascii[j++] = '\n';
      ascii[j] = '\0';
      q_print(ascii);
      j = 0;
    }
  }
}

// convert argument TEXT for uart/write and files/write commands (and others)
// to a buffer. This function is used to join up argv's which are the same logical text.
// Alternatively one can use quote, but they were introduced much later, so text2buf still is in use.
//
// "arg1" "arg2" "arg3" --> "arg1 arg2 arg3"
//
// Since all whitespace between arguments is lost due to argify(), this function can not recover multiple spaces:
// esp32#>write This        is     a TEXT
// will be read as "This is a TEXT". The only way to preserve repeating spaces is to use quotes, or encode spaces as \20
//
// /argc/
// /argv/
// /i/    - first argv to start collecting text from
// /out/  - allocated buffer
// Returns number of bytes in buffer /*out/
// TODO: move to userinput.h, rename to userinput_as_string
static int text2buf(int argc, char **argv, int i /* START */, char **out) {

  int size = 0;
  char *b;

  if (i >= argc)
    return -1;

  //instead of estimating buffer size just allocate 512 bytes buffer: espshell
  // input strings are limited to 500 bytes.
  if ((*out = b = (char *)q_malloc(ESPSHELL_MAX_INPUT_LENGTH + 12, MEM_TEXT2BUF)) != NULL) {
    // go thru all the arguments and send them. the space is inserted between arguments
    do {
      char *p = argv[i];
      while (*p) {
        char c = *p;
        p++;
        if (c == '\\') {
          switch (*p) {
            case '\\':             p++;              c = '\\';              break;
            case 'n':              p++;              c = '\n';              break;
            case 'r':              p++;              c = '\r';              break;
            case 't':              p++;              c = '\t';              break;
            case '"':              p++;              c = '"';              break;
            //case 'e':              p++;              c = '\e';              break;  //interferes with \HEX numbers
            case 'v':              p++;              c = '\v';              break;
            //case 'b':              p++;              c = '\b';              break;  //interferes with \HEX numbers
            default:
              if (ishex2(p)) {
                c = hex2uint8(p);
                if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
                  p += 2;
                p++;
                if (*p) 
                  p++;
              } else {
                // unknown escape sequence: fallthrough to get "\" printed
              }
          }
        }
        *b++ = c;
        size++;
      }
      i++;
      //if there are more arguments - insert a space
      if (i < argc) {
        *b++ = ' ';
        size++;
      }
      // input line length limiting. just in case. normally editline() must not accept lines which are too long
      // TODO: editline() does not care
      if (size > ESPSHELL_MAX_INPUT_LENGTH)
        break;
    } while (i < argc);
  }
  return size;
}



// version of delay() which can be interrupted by user input (terminal
// keypress)or by a "kill" command. If called from a different context (i.e. from a "background" command or
// any task which is not an espshell_task) then it can not be interrupted by a keypress.
//
// `duration` - delay time in milliseconds, or DELAY_INFINITE for the infinite delay
//  returns `duration` if everything was ok, 
//  returns value less than `duration` if was interrupted (returns real time spent in delay_interruptible())
//
#define TOO_LONG       2999
#define DELAY_POLL     250
#define DELAY_INFINITE 0xffffffffUL

static unsigned int delay_interruptible(unsigned int duration) {
  
  unsigned int now, duration0 = duration;

  now = q_millis();

  // Called from a background task? Wait for the signal from "kill" command, ignore keypresses
  if (!is_foreground_task()) {
    uint32_t note;
    if (task_wait_for_signal(&note, duration) == true)
      return q_millis() - now; // Interrupted
    return duration;           // Success! (Important: return exactly what was passed as an argument, not real time!)
                               // This returned value is used to verify if delay was interrupted or timeouted on its own
  }

  // Called from the foreground task (i.e. called from main espshell task context)
  // Only foreground task can be interrupted by a keypress and only if delay is over 3 sec long.
  // Foreground "pin" command can be interrupted regardless delay duration.
  if (duration > TOO_LONG)
    while (duration >= DELAY_POLL) {

      if (duration != DELAY_INFINITE)
        duration -= DELAY_POLL;

      q_delay(DELAY_POLL);

      if (anykey_pressed())
        return q_millis() - now;  // interrupted by a keypress
    }
  
  q_delay(duration);

  // Success! Return exactly requested time, not the real one. 
  // Don't change this behaviour or if you do, examine all calls to delay_interruptible()
  return duration0;
}


// Variable Sized Array.
// Consist of blocks 256 bytes in size, linked into list.
// Currently it is used only by task.h to keep track of running tasks and is the subject for removal
//
#define VSASIZE 256

typedef void *  vsaval_t; 

// Dynamic array type
typedef struct vsa_s {
  struct vsa_s *next;
  vsaval_t      values[0];
} vsa_t;


// Find/create VSA and Index by /value/
//
vsa_t *vsa_find_slot(vsa_t **vsa0, int *slot0, vsaval_t value, bool create) {

  vsa_t *vsa, *tmp = 0, *nil_vsa = 0;
  int nil_idx, dummy_slot = 0;

  if (!vsa0)
    return NULL;

  if (!slot0)
    slot0 = &dummy_slot;

  vsa = *vsa0;
  while (vsa) {
    // Search chunks starting from *slot0
    for (int i = *slot0; i < VSASIZE/sizeof(vsaval_t); i++) {
      if (vsa->values[i] == value) {
        *slot0 = i;
        return vsa;
      }
      if (vsa->values[i] == 0 && nil_vsa == NULL) {
        nil_vsa = vsa;
        nil_idx = i;
      }
    }
    tmp = vsa;
    vsa = vsa->next;
    // only first vsa is searched starting from provided *slot0. 
    *slot0 = 0;
  }

  if (create) {

    // Did we see nil slot before? If yes - use it
    // If now - then allocate a new chunk. It is possible to miss nil element when search is done with 
    // non-zero starting index
    if (nil_vsa) {
      nil_vsa->values[nil_idx] = value;
      *slot0 = nil_idx;
      return nil_vsa;
    }

    // Allocate new chunk
    vsa_t *n = (vsa_t *)q_malloc(sizeof(vsa_t ) + VSASIZE, MEM_TMP);
    if (!n)
      return NULL;

    // Insert new chunk to the list
    n->next = 0;
    memset(n->values, 0, VSASIZE);
    n->values[0] = value;
    if (tmp)
      tmp->next = n;
    else
      *vsa0 = n;
    *slot0 = 0;
    return n;
  }

  return NULL;
}

#endif //#if COMPILING_ESPSHELL


