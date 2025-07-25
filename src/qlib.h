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
//    Part of it is in task.h file as well
//
// 2. Memory manager (for leaks detection, normally disabled)
// 3. Bunch of number->string and string->number conversion functions
// 4. Core functions like q_printf(), core variables etc
//
#if COMPILING_ESPSHELL

#include <stdatomic.h>

// gcc-specific branch prediction optimization macros 
#undef likely
#undef unlikely
#define unlikely(_X)     __builtin_expect(!!(_X), 0)
#define likely(_X)     __builtin_expect(!!(_X), 1)

// Check if condition ... is true and if it is - halt ESPShell
// A wrapper for the must_not_happen() function
//
static NORETURN void must_not_happen(const char *message, const char *file, int line);

#define MUST_NOT_HAPPEN( ... ) \
  { \
    if ( unlikely(__VA_ARGS__) ) \
      must_not_happen(#__VA_ARGS__, __FILE__, __LINE__ ); \
  }

// OS glue: timers & delays
// ------------------------
// inlined version of millis() & delay() for better accuracy on small intervals
// (because of decreased overhead). q_millis vs millis shows 196 vs 286 CPU cycles on ESP32
#define q_micros() esp_timer_get_time()
#define q_millis() ((unsigned long )(q_micros() / 1000ULL))
#define q_delay(_Delay) vTaskDelay(_Delay / portTICK_PERIOD_MS);

// OS glue: context switch requests
// --------------------------------
#define q_yield() vPortYield()
#define q_yield_from_isr() portYIELD_FROM_ISR()


// OS glue : sync objects (mutexes, binary semaphores, critical sections and rwlocks)
// ----------------------------------------------------------------------------------
//  -- Mutexes and Semaphores--

// Mutexes and semaphores are initialized on first use (i.e. on first mutex_lock() / sem_lock() call)
// These are simple wrappers which **do not increase code size** but allow for unified names 
// and better portability. This OS->ESPShell "glue" must be kept simple and, ideally, inlineable 
// or defined as a macro

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
// Similar to mutex, but **any** task can sem_unlock(), not just the task
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
// (AKA critical section AKA spinlock. we use "barrier" instead of "spinlock" to not 
// confuse with FreeRTOS functions and types)
//
// Code behind the barrier (i.e. the code between barrier_lock() / barrier_unlock()) must be kept small and linear.
// Defenitely not call q_printf() or q_delay() while in the barrier, or watchdog will bark (interruptas are disabled!)
//
// Barrier lock is the only way to guarantee exclusive access both from tasks and interrupts on a multicore system
#define barrier_t portMUX_TYPE

// Initializer: barrier_t mux = BARRIER_INIT;
#define BARRIER_INIT portMUX_INITIALIZER_UNLOCKED
 
// Enter/Exit critical sections
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
// 

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
  rw->wreq++;
try_again:  
  // Grab main sync object. 
  // If it is held by readers or other writer then we simply block here
  sem_lock(rw->sem); 

  // Ok, we just acquired the semaphore; lets check if a
  // reader somehow sneaked in during the process: if it happened, then we retry whole procedure
  if (rw->cnt != 0) {
    sem_unlock(rw->sem);
    q_yield();
    goto try_again;
  }
  rw->cnt--; //  i.e. rw->cnt = -1, "active writer"
  rw->wreq--;
}

// WRITE unlock
// /cnt/ is expected to be -1 (Write Lock). If it isn't then we have bugs in out RW code
//
void rw_unlockw(rwlock_t *rw) {
  rw->cnt++; //same as rw->cnt = 0;
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
#ifdef ALT_RW_VER
  while (atomic_load_explicit(&rw->cnt, memory_order_acquire) < 0 ||
         atomic_load_explicit(&rw->wreq, memory_order_acquire) > 0)
#else
  while ((cnt = rw->cnt) < 0  ||
          rw->wreq)
#endif
    q_yield(); 

  // Atomically increment READERS count
#ifdef ALT_RW_VER
  cnt = atomic_fetch_add_explicit(&rw->cnt, 1, memory_order_acq_rel);
#else
  rw->cnt++; 
#endif

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
#ifdef ALT_RW_VER
  if (atomic_fetch_sub(&rw->cnt, 1) == 1)
#else
  if (0 == --rw->cnt)
#endif
    sem_unlock(rw->sem);
}

//void rw_dump(const rwlock_t *rw) {
//  q_printf("rwlock(0x%p) : cnt=%d, wreq=%lu\r\n", rw, rw->cnt, rw->wreq);
//}


// -- Message Pipes --
// A machinery to send messages from an ISR to a task.
// Note that this code can not be used to send messages from task to task, only ISR->task is allowed
// Messages are fixed in size (4 bytes, sizeof(void *)).
//
// Main idea around mpipes is to be able "to send" a pointer from an ISR to a task: e.g. GPIO ISR sends 
// messages to some processing task.
//
// Depending on MPIPE_USES_MSGBUF macro, message pipes (mpipes) are implemented 
// either via FreeRTOS Message Buffers or FreeRTOS Queues
//

#ifdef MPIPE_USES_MSGBUF

// Message Pipe type.
// e.g. "static mpipe_t comm = MPIPE_INIT"
#  define mpipe_t MessageBufferHandle_t
#  define MPIPE_INIT NULL

// Create a new message pipe
// returns pointer to a created mpipe
// _NumElements : maximum number of pending messages in the mpipe
//
#  define mpipe_create(_NumElements) \
    xMessageBufferCreate(_NumElements * sizeof(void *))

// Delete mpipe
// _Pipe : pointer to a mpipe or NULL
//
#  define mpipe_destroy(_Pipe) \
    do { \
      if (likely(_Pipe != MPIPE_INIT)) \
        vMessageBufferDelete(_Pipe) \
    } while(0)

// Send an item (a pointer. we send pointers, not data)
// _Data : a variable (not an expression!), containing data to be sent (4 bytes, void *, unsigned int, float ...
// _Pipe : pointer to a mpipe or NULL
// 
// "void *value = 0x12345678; mpipe_send(ThePipe, value);"
// 
// Returns /false/ on failure
//
#  define mpipe_send(_Pipe, _Data) \
    ({ \
      BaseType_t ret; \
      if (likely(_Pipe != MPIPE_INIT)) \
        xMessageBufferSendFromISR(_Pipe, &_Data, sizeof(void *), &ret); \
      else \
        ret = pdFALSE; \
      (ret == pdTRUE); /* returned value, true or false */\
    })

// Get an item out of a pipe
// If pipe is empty then mpipe_recv() will block
// _Pipe : pointer to a mpipe or NULL
// Returns a message, 4 bytes, as a void*: "void *value = mpipe_recv(ThePipe);"
//
#  define mpipe_recv(_Pipe) \
    ({ \
      void *ptr; \
      if (likely(_Pipe != MPIPE_INIT)) \
        while (xMessageBufferReceive(_Pipe, &ptr, sizeof(void *), portMAX_DELAY) == 0) /* Nothing here */; \
      else \
        ptr = NULL; \
      ptr; \
    })

#else // use queues as underlying API

// Message Pipe type
#  define mpipe_t QueueHandle_t
#  define MPIPE_INIT NULL

// Create a new message pipe
// _NumElements : maximum number of elements (i.e. pointers) this mpipe can hold  before it 
//                starts to drop new incoming messages
//
// Returns pointer to the newly created pipe
//
#  define mpipe_create(_NumElements) \
    xQueueCreate(_NumElements, sizeof(void *))

// Delete mpipe
// _Pipe : pointer to a mpipe or NULL
//
#  define mpipe_destroy(_Pipe) \
    do { \
      if (likely(_Pipe != MPIPE_INIT)) \
        vQueueDelete(_Pipe) \
    } while(0)


// Send a message (_Data must be variable: we are taking address of it)
// _Pipe : pointer to a mpipe or NULL
// _Data : a variable, usually - a pointer, but can be any 4 bytes simple type
//
// Returns /true/ if taskYIELD was requested
//
#  define mpipe_send(_Pipe, _Data) \
    ({ \
      BaseType_t ret; \
      if (likely(_Pipe != MPIPE_INIT)) \
        xQueueSendFromISR(_Pipe, &_Data, &ret); \
      else \
        ret = pdFALSE; \
      (ret == pdTRUE); \
    })

// Get an item out of the message pipe
// _Pipe : pointer to a mpipe or NULL
// Returns an item (4 byte-long variable, usually a pointer)
//
#  define mpipe_recv(_Pipe) \
    ({ \
      void *ptr; \
      if (likely(_Pipe != MPIPE_INIT)) \
        while (xQueueReceive(_Pipe, &ptr, portMAX_DELAY) == pdFALSE) /* Nothing here */; \
      else \
        ptr = NULL; \
      ptr; \
    })
#endif // MessageBuffers or Queues?

/// OS Glue End;

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
// Tag actions are additive: <g><u> will set text color to green and then turns underline font option; 
// the </> tag afterwards will cancel both <g> and <u>
//
// Color tags are processed in q_print() (there are 1 direct use of color sequence in editline.h) and
// either replaced with ANSI coloring sequences or they are simply gets stripped if coloring is turned off
//
// Sequences below have their **length** encoded as the very first byte to save on strlen() later.
//
static const char *ansi_tags['z' - 'a' + 1] = {
  ['b' - 'a'] = "\07\033[1;97m",   // [b]old bright white
  ['e' - 'a'] = "\05\033[95m",     // [e]rror message (bright magenta)
  ['i' - 'a'] = "\010\033[33;93m", // [i]important information (eye-catching bright yellow)
  ['n' - 'a'] = "\04\033[0m",      // [n]ormal colors. cancels all tags
  ['r' - 'a'] = "\04\033[7m",      // [r]everse video
  ['w' - 'a'] = "\05\033[91m",     // [w]arning message ( bright red )
  ['o' - 'a'] = "\05\033[33m",     // [o]ptional dark yellow
  ['u' - 'a'] = "\07\033[4;37m",   // [u]nderlined, normal white
  ['g' - 'a'] = "\05\033[92m",     // [g]reen. Bright green
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


// Memory type: a number from 0 to 15 to identify newly allocated memory block usage. 
// Newly allocated memory is assigned one of the types below. Command "sh mem" invokes
// q_memleaks() function to dump memory allocation information. Only works #if MEMTEST == 1

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
  MEM_TASKID,    // Task remap entry TODO: remove
  MEM_ALIAS,     // Aliases
  MEM_IFCOND,
  MEM_UNUSED14,
  MEM_UNUSED15
  // NOTE: only values 0..15 are allowed, do not add more!
};

// Check if memory address is in valid range. This function does not check memory access
// rights, only boundaries are checked.
// sizeof(unsigned int) == sizeof(void *) is ensured in espshell.c static_asserts section
// TODO: implement memory maps query (ESP32_Memory_Maps/maps.h)
// TODO: use esp_ptr_... API to get properties of the memory region (byte/dword access etc)
//
bool __attribute__((const)) is_valid_address(const void *addr, unsigned int count) {
  
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
  "UNUSED14",
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
  if (ptr != NULL)
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
    WD()
    q_printf("%%  % 5u | % 12s | % 7u | %p \r\n",++count,memtags[ml->type],ml->len,ml->ptr);
    WE()
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
  if (ptr != NULL) {
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
  if (p) {
    char pp;
    do {
      pp = *p; // pp undergo CSE removal, while *p is not :(
      if (pp >= 'A' && pp <= 'Z')
        *p = pp | (1 << 5);
      p++;
    } while( pp );
  }
}

// Check if given ascii string represents a decimal number. Ex.: "12345", "-12"
// "minus" sign is only accepted at the beginning (must be 1st symbol)
// Can be called with p = NULL, it is normal
static bool isnum(const char *p) {
  if (p && *p) {
    if (*p == '-')
      p++;
    //just single "-" is not a valid number
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
// not allowed. Functions below are intended to process small numbers, e.g. pin numbers 
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
    if (*p == '-')
      p++;
    while ((*p >= '0' && *p <= '9') || (*p == '.' && !dot))
      if (*p++ == '.')
        dot = true;
    return *p == '\0';  //if *p is 0 then all the chars were ok. (end of line reached).
  }
  return false;
}

// "to-lowercase" helper macro
// Only works with ANSI charset, single-byte encodings
//


// Check if given ascii string is a hex number
// String may or may not start with "0x"
// Strings "a" , "5a", "0x5" and "0x5Ac5" are valid input

static bool ishex(const char *p) {
  if (p && *p) {
    char c;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
      p += 2;
    while ((c = *p) != '\0') {
      
      if (c >= 'A' && c <= 'Z')
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
// 
static bool ishex2(const char *p) {

  if (p && *p) {
    if (p[0] == '0' && p[1] == 'x')
      p += 2;

    if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
      p++;
      if ((*p == 0) || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
        return true;
    }
  }
  return false;
}


// 
static bool isoct(const char *p) {

  if (p && *p) {

    if (*p != '0')
      return false;

    p++;

    while (*p >= '0' && *p < '8') 
      p++;

    return *p == '\0';  //if *p is 0 then all the chars were digits (end of line reached).
  }
  return false;
}

//
static bool isbin(const char *p) {

  if (p && *p) {

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

static bool q_isnumeric(const char *p) {
  if (p && *p) {
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

//convert hex ascii byte.
//strings "A", "5a" "0x5a" are all valid input

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
    f = f - (f >= 'A' && f <= 'F' ? 'A' 
                                  : (f >= 'a' && f <= 'f' ? 'a' 
                                                          : (f >= '0' && f <= '9' ? '0' 
                                                                                  : f )));
    f <<= 4;
  }
  // ..and the last
  l = *p++;
  // Code below expands either to a number or, if input was incorrect, zero is returned
  // This leads to equaliuty between "0", "0x" and "0x0" - all of these strings get converted to zero
  l = l - (l >= 'A' && l <= 'F' ? 'A' 
                                : (l >= 'a' && l <= 'f' ? 'a' 
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
      
    if (c >= 'A' && c <= 'Z')
      c |= 1 << 5;

    if (c >= '0' && c <= '9') four = c - '0'; else 
    if (c >= 'a' && c <= 'f') four = c - 'a' + 10; else break;

    value = (value << 4) | four;
    p++;
  }
  return value;
}

// Same as above but for octal numbers (e.g. "012346773" with leading 0)
// If string has symbols outside of the allowed range (`0`..`7`) then this
// function return zero. Empty strings ("") gets converted to zero as well
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

// q_atol() : extended version of atol()
// 1. Accepts decimal, hex,octal or binary numbers (0x for hex, 0 for octal, 0b for binary)
// 2. If conversion fails (bad symbols in string, empty string etc) the
//    "def" value is returned
//
#define DEF_BAD ((unsigned int)(-1))

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

static inline int q_atoi(const char *p, int def) {
  return isnum(p) ? atoi(p) : def;
}

#if WITH_IFCOND
// "1000" "seconds"
// "49" "days"
// "day" ""
// "minute" ""


static unsigned int q_rtime(const char *left, const char *right) {
  unsigned int val = 1;
  if (left && *left) {
    if (isnum(left))
      val = atol(left);
    else
      right = left;
  
    if (!q_strcmp(right,"millis")) val *= 1; else
    if (!q_strcmp(right,"seconds")) val *= 1000; else
    if (!q_strcmp(right,"minutes")) val *= 60*1000; else
    if (!q_strcmp(right,"hours")) val *= 3600*1000; else
    if (!q_strcmp(right,"days")) val *= 24*3600*1000; else return 0;

    return val;
  }
  return 0;
}
#endif //WITH_IDCOND

// Safe conversion to /float/ type. Returns /def/ if conversion can not be done
//
static inline float q_atof(const char *p, float def) {
  return isfloat(p) ? atof(p) : def;
}


// Loose strcmp() which performs a **partial** match. It is used to match commands and parameters which are shortened:
// e.g. user typed "seq" instead of "sequence" or "m w" instead of "mount wwwroot"
//
// /partial/ - string which expected to be incomplete/shortened. Can be NULL or empty string
// /full/    - full string to compare against. Can be null or empty string
//
// BIG FAT WARNING: If **both** /partial/ and /full/ are empty strings (i.e. strings containing only '\0')
//                  then behaviour of this function will be undefined. 
//
// q_strcmp("seq","sequence") == 0
// q_strcmp("sequence","seq") == 1
//
//
// RATIONALE
// ---------
// It is implemented as byte-by-byte compare which is very efficient way to compare **short** strings
// Longer strings are better to be compared as 32 bit chunks but this requires some calculation, string 
// length measurement and so on making this approach very slow for typical 2-4 letter strings
//
static int IRAM_ATTR q_strcmp(const char *partial, const char *full) {

  // quick reject: first symbols must match. if both are \0 then we will read beyound string buffers.
  if (partial && full && (*partial++ == *full++)) {
    // Run through every character of the /partial/ and compare them to characters of /full/
    while(*partial)
      if (*partial++ != *full++)
        return 1;
    return 0;
  }
  return 1;

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
//
static void q_printtable(const unsigned char *p, unsigned int count, unsigned char length, bool isu, bool isf, bool isp) {
    if (p && count && length) {
      if (count > 1)
        q_printf("%% Array of %u elements, %u bytes each\r\n%%  Address   :  Value    \r\n",count,length);
      while (count) {
        q_printf("%% %p : ", p);
        if (isp) {
          q_printf("0x%08x\r\n", *((unsigned int *)p));
        } else if (isf) {
          q_printf("%ff\r\n", *((float *)p));
        } else {
          if (length == 4) {
            if (isu)
              q_printf("%u (0x%x as hex)\r\n",*((unsigned int *)p),*((unsigned int *)p));
            else
              q_printf("%i\r\n",*((signed int *)p));
          } else if (length == 2) {
            if (isu)
              q_printf("%u (0x%x as hex)\r\n",*((unsigned short *)p),*((unsigned short *)p));
            else
              q_printf("%i\r\n",*((signed short *)p));
          } else if (length == 1) {
            if (isu)
              q_printf("%u (0x%x as hex)\r\n",*((unsigned char *)p),*((unsigned char *)p));
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

//static q_printhex_word_access(const unsigned char *p, unsigned int len) {
// TODO:
//}

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
// to a buffer.
//
// /argc/
// /argv/
// /i/    - first argv to start collecting text from
// /out/  - allocated buffer
// Returns number of bytes in buffer /*out/
//
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
                if (p[0] == '0' && p[1] == 'x')
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
// `duration` - delay time in milliseconds, or 0 for infinite delay
//  returns duration if everything was ok, 
// returns !=duration if was interrupted (returns real time spent in delay_interruptible())
//
// TODO: investigate why zero delays do not work as kill-points for "kill -15".
//       the only difference is that xTaskNotifyWait() is called with zero timeout
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
    return duration;         // Success!
  }

  // Called from the foreground task (i.e. called from main espshell task context)
  // Only foreground task can be interrupted by a keypress
  if (duration > TOO_LONG) {
    while (duration >= DELAY_POLL) {
      duration -= DELAY_POLL;
      q_delay(DELAY_POLL);
      if (anykey_pressed())
        return q_millis() - now;  // interrupted by a keypress
    }
  }
  q_delay(duration);

  // Success! Return exactly requested time, not the real one. Don't change this behaviour or if you do examine all calls to delay_interruptible()
  return duration0;
}

#endif //#if COMPILING_ESPSHELL


