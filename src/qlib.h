/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


// -- Q-Lib: helpful routines: ascii to number conversions,platform abstraction, etc --
//
// 1. OS/Kernel lightweight abstraction layer (mutexes, time intervals, delays, etc. part of it is in task.h file as well )
// 2. Memory manager (for leaks detection)
// 3. Bunch of number->string and string->number conversion functions
// 4. Core functions like q_printf(), core variables etc
//
#if COMPILING_ESPSHELL

// gcc-specific size-optimization attempt ignored
#undef likely
#undef unlikely
#define unlikely(_X)     __builtin_expect(!!(_X), 0)
#define likely(_X)     __builtin_expect(!!(_X), 1)

// inlined version of millis() & delay() for better accuracy on small intervals
// because of decreased overhead. q_millis vs millis shows 196 vs 286 CPU cycles
#define q_millis() ((unsigned long )(esp_timer_get_time() / 1000ULL))
#define q_delay(_Delay) vTaskDelay(_Delay / portTICK_PERIOD_MS);


//  -- Mutex manipulation --
// declare, initialize, grab and release macros
// These are simple wrappers which do not increase code size but allow for unified names and better portability
//
#define MUTEX(_Name) xSemaphoreHandle _Name = NULL;   // e.g. static MUTEX(argv_mux);

// Grab a mutex. Blocks forever. Initializes mutex object on a first use
#define mutex_lock(_Name) \
  do { \
    if (unlikely(_Name == NULL)) _Name = xSemaphoreCreateMutex(); \
    if (likely(_Name != NULL))    xSemaphoreTake(_Name, portMAX_DELAY); \
  } while( 0 )

// Release
#define mutex_unlock(_Name) \
  do { \
    if (likely(_Name != NULL)) \
      xSemaphoreGive(_Name); \
  } while( 0 )

// Just for completeness. unused in espshell
#define mutex_destroy(_Name) \
  do { \
    if (likely(_Name != NULL)) { \
      vSemaphoreDelete(_Name); \
      _Name = NULL; \
    } \
  } while ( 0 )

// -- Memory access barrier -- :  a critical section on ESP32
#define BARRIER(_Name) portMUX_TYPE _Name = portMUX_INITIALIZER_UNLOCKED
#define barrier_lock(_Name) portENTER_CRITICAL(&_Name)
#define barrier_unlock(_Name) portEXIT_CRITICAL(&_Name)


// PPA(Number) generates 2 arguments for a printf ("%u%s",PPA(Number)), adding an "s" where its needed
// NEE(Number) are similar to the PPA above except it generates "st", "nd","rd" and "th" depending on /Number/
// returns "st", "nd", "rd" ot "th" depending on number
//
static inline __attribute__((const)) const char *number_english_ending(unsigned int n) {
  return n == 1 ? "st" : (n == 2 ? "nd" : (n == 3 ? "rd" : "th"));
}

#define PPA(_X) _X, (_X) == 1 ? "" : "s"
#define NEE(_X) _X, number_english_ending(_X)


// Some globals as well.
static bool Exit = false;            // True == close the shell and kill its FreeRTOS task. Can be restarted again with espshell_start()

static bool ColorAuto = AUTO_COLOR;  // Autoenable coloring if terminal permits
static bool Color = false;           // Coloring is enabled?
static int  Echo = STARTUP_ECHO;     // Runtime echo flag: -1=silent,0=off,1=on

// -- Coloring / ANSI sequences --
//
// Sequence below have their **length** encoded as the very first byte to save on strlen() later.
// These sequences are used by q_print() when decoding **color tags** (search for "<i> in the source code to find out where color tags are used")
// Unlike HTML, our tags are 1-character long, for easier processing.
//
static const char *ansi_tags[26] = {
  ['b' - 'a'] = "\07\033[1;97m",   // [b]old bright white
  ['e' - 'a'] = "\05\033[95m",     // [e]rror message (bright magenta)
  ['i' - 'a'] = "\010\033[33;93m", // [i]important information (eye-catching bright yellow)
  ['n' - 'a'] = "\04\033[0m",      // [n]ormal colors
  ['r' - 'a'] = "\04\033[7m",      // [r]everse video
  ['w' - 'a'] = "\05\033[91m",     // [w]arning message ( bright red )
  ['o' - 'a'] = "\05\033[33m",     // [o]ptional dark yellow
  ['u' - 'a'] = "\07\033[4;37m",   // [u]nderlined, normal white
  ['g' - 'a'] = "\05\033[92m",     // [g]reen. Bright green
};

// This is here to keep Arduino IDE's colorer happy
/*"*/

// Return an ANSI terminal sequence which corresponds to given tag.
//
static __attribute__((const)) const char *tag2ansi(char tag) {

  return tag == '/' ? ansi_tags['n' - 'a'] + 1
                    : (tag >= 'a' && tag <= 'z' ? ansi_tags[tag - 'a'] + 1
                                                : NULL);
}


// -- Memory allocation wrappers --
//
// If MEMTEST is set to 0 (the default value) then q_malloc is simply malloc(),
// q_free() is free() and so on.
//
// If MEMTEST is non-zero then ESPShell provides its own versions of q_malloc, q_strdup, q_realloc and q_free
// functions which do memory statistics/tracking and perform some checks on pointers
// being freed
//


// Memory type: a number from 0 to 15 to identify newly allocated memory block intended
// usage. Newly allocated memory is assigned one of the types below. Command "mem" invokes
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
  MEM_TASKID,    // Task remap entry
  MEM_UNUSED12,
  MEM_UNUSED13,
  MEM_UNUSED14,
  MEM_UNUSED15
  // NOTE: only values 0..15 are allowed, do not add more!
};

// Check if memory address is in valid range. This function does not check memory access
// rights, only boundaries are checked.
// sizeof(unsigned int) == sizeof(void *) is ensured in espshell.c static_asserts section
//
bool __attribute__((const)) is_valid_address(const void *addr, unsigned int count) {
  
  return  ((unsigned int)addr >= 0x20000000) && ((unsigned int)addr + count <= 0x80000000);
}


#if MEMTEST
// WARNING: not suitable for mallocing buffers larger than 512k.
// -- Memory wrappers for leaks hunting --
//
// memory calls (malloc,realloc,free and strdup) are wrapped to keep track of
// allocations and report ememory usage statistics.
//
// espshell stores all allocation in a list and creates 2 bytes overwrite detection
// zone at the end of the buffer allocated. these are checked upon q_free()
//
// statistics is displayed by "mem" command
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
  "UNUSED12",
  "UNUSED13",
  "UNUSED14",
  "UNUSED15"
};

// allocated blocks
static memlog_t *head = NULL;

// allocated memory total, and overhead added by memory logger
static unsigned int allocated = 0, internal = 0;

// memory logger mutex to access memory records list
static MUTEX(mem_mux);

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
        //memlog_lock();
        mutex_lock(mem_mux);
        ml->li.next = (list_t *)head;
        head = ml;
        allocated += size;
        internal += sizeof(memlog_t) + 2;
        //memlog_unlock();
        mutex_unlock(mem_mux);

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
    mutex_lock(mem_mux);
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
    mutex_unlock(mem_mux);
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
  mutex_lock(mem_mux);
  for (ml = head; ml != NULL; ml = (memlog_t *)(ml->li.next))
    if (ml->ptr == (unsigned char *)ptr)
      break;
  
  if (!ml) {
    mutex_unlock(mem_mux);
    q_printf("<w>ERROR: q_realloc() : trying to realloc pointer %p which is not on the list</>\r\n",ptr);
    return NULL;
  }

  // trivial case #2: requested size is the same as current size, so do nothing
  // TODO: should it be new_size <= ml->len ?
	if (new_size == ml->len) {
    mutex_unlock(mem_mux);
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
  mutex_unlock(mem_mux);
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
#pragma GCC diagnostic ignored "-Wformat"
    q_printf("%%  % 5u | % 12s | % 7u | %p \r\n",++count,memtags[ml->type],ml->len,ml->ptr);
#pragma GCC diagnostic warning "-Wformat"
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
// TODO: make generic q_strdup_tailroom(const char *string, int tailroom)
static char *q_strdup256(const char *ptr, int type) {
  char *p = NULL;
  if (ptr != NULL) {
    int len = strlen(ptr);
    if ((p = (char *)q_malloc(len + 256 + 1, type)) != NULL)
      strcpy(p, ptr);
  }
  return p;
}

// Check if condition ... is true and if it is - halt ESPShell
// Wrapper for the must_not_happen() function below
//
#define MUST_NOT_HAPPEN( ... ) do { \
  if ( __VA_ARGS__ ) \
    must_not_happen(#__VA_ARGS__, __FILE__, __LINE__ ); \
} while ( 0 )



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

//check if given ascii string is a decimal number. Ex.: "12345", "-12"
// "minus" sign is only accepted at the beginning (must be 1st symbol)
//
static bool isnum(const char *p) {
  if (p && *p) {
    if (*p == '-') p++;
    while (*p >= '0' && *p <= '9') p++;
    return *p == '\0';  //if *p is 0 then all the chars were digits (end of line reached).
  }
  return false;
}

// check if ascii string is a float number
// NOTE: "0.5" and ".5" are both valid inputs
static bool isfloat(const char *p) {
  if (p && *p) {
    bool dot = false;
    if (*p == '-') p++;
    while ((*p >= '0' && *p <= '9') || (*p == '.' && !dot)) {
      if (*p == '.')
        dot = true;
      p++;
    }
    return !(*p);  //if *p is 0 then all the chars were ok. (end of line reached).
  }
  return false;
}



// Check if given ascii string is a hex number
// String may or may not start with "0x"
// Strings "a" , "5a", "0x5" and "0x5Ac5" are valid input
//
static bool ishex(const char *p) {
  if (p && *p) {
    if (p[0] == '0' && p[1] == 'x')
      p += 2;
    while (*p != '\0') {
      if ((*p < '0' || *p > '9') && (*p < 'a' || *p > 'f') && (*p < 'A' || *p > 'F'))
        break;
      p++;
    }
    return *p == '\0';
  }
  return false;
}

// check only first 1-2 bytes (not counting "0x" if present)
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

    if (p[0] == '0' && p[1] == 'b')
      p += 2;

    while (*p == '0' || *p == '1')
      p++;

    return *p == '\0';  //if *p is 0 then all the chars were digits (end of line reached).
  }

  return false;
}




// Check if string can be converted to number, trying all possible formats: 
// floats, octal, binary or hexadecimal with leading 0x or without it
//
static bool q_isnumeric(const char *p) {
  if (p && *p) {
    if (p[0] == '0') {
      if (p[1] == 'x')
        return ishex(p);
      if (p[1] == 'b')
        return isbin(p);
      return isoct(p);
    }
    return isnum(p) || isfloat(p);
  }
  return false;
}

//convert hex ascii byte.
//strings "A", "5a" "0x5a" are all valid input
//
static unsigned char hex2uint8(const char *p) {

  unsigned char f = 0, l;  //first and last

  // Skip leading "0x" if any
  if (p[0] == '0' && p[1] == 'x')
    p += 2;

  // Single character hex? (i.e "a", "A" or "0xC")
  // Calculate the first nibble
  if (p[1] != '\0') {
    f = *p++;
    f = f - (f >= 'A' && f <= 'F' ? 'A' 
                                  : (f >= 'a' && f <= 'f' ? 'a' 
                                                          : (f >= '0' && f <= '9' ? '0' 
                                                                                  : f )));
    f <<= 4;
  }
  // ..and the last
  l = *p++;
  l = l - (l >= 'A' && l <= 'F' ? 'A' 
                                : (l >= 'a' && l <= 'f' ? 'a' 
                                                        : (l >= '0' && l <= '9' ? '0' 
                                                                                : l )));
  return f | l;
}

// convert a hex string to uint32_t
// if string is too long then number converted will be equal
// to last 4 bytes of the string
static unsigned int hex2uint32(const char *p) {

  unsigned int value = 0;
  unsigned int four = 0;

  if (p[0] == '0' && p[1] == 'x')
    p += 2;

  while (*p) {
    if (*p >= '0' && *p <= '9') four = *p - '0';
    else if (*p >= 'a' && *p <= 'f') four = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F') four = *p - 'A' + 10;
    else return 0;
    value <<= 4;
    value |= four;
    p++;
  }
  return value;
}

static unsigned int octal2uint32(const char *p) {
  unsigned int value = 0;
  unsigned int three = 0;
  while (*p) {
    if (*p >= '0' && *p <= '7') three = *p - '0';
    else return 0;
    value <<= 3;
    value |= three;
    p++;
  }
  return value;
}

// Convert strings 0b10010101 and 10100101 (with or without leading "0b") to unsigned int values
// If there are more than 32 bits in the string then only last 32 bits will be read, while some leading bits 
// will be ignored
static unsigned int binary2uint32(const char *p) {
  unsigned int value = 0;
  unsigned int one = 0;

  if (p[0] == '0' && p[1] == 'b')
    p += 2;

  while (*p) {
    if (*p == '0' || *p == '1') one = *p - '0';
    else return 0;
    value <<= 1;
    value |= one;
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
   // If condition is true -> continue to the right
   // If condition is false, continue from a "?" down to the first ":"
   return p && *p ? (p[0] == '0' ? (p[1] == 'x' ? (ishex(p) ? hex2uint32(p)
                                                            : def) 
                                                : (p[1] == 'b' ? (isbin(p) ? binary2uint32(p)
                                                                           : def)
                                                               : (isoct(p) ? octal2uint32(p)
                                                                           : def)))
                                : (isnum(p) ? atol(p) 
                                            : def))
                 : def;
}

// Safe conversion to /float/ type. Returns /def/ if conversion can not be done
//
static inline float q_atof(const char *p, float def) {
  return isfloat(p) ? atof(p) : def;
}


// Loose strcmp() which deoes partial match. It is used to match commands and parameters which are shortened:
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
// Longer strings are better to be compared as 32 bit chunks but this requires some calculation, string length measurement and so on
// making this approach very slow for typical 2-4 letter strings
//
//  TODO: Function below gets called alot from various parts of ESPShell, thats why IRAM_ATTR; Really need to profile the code and move most critical
//        functions to IRAM, but keep the IRAM usage small and optional (WITH_IRAM)
//
static int IRAM_ATTR q_strcmp(const char *partial, const char *full) {

  // quick reject: first symbols must match
  if (partial && full && (*partial++ == *full++)) {
    // Run through every character of the /partial/ and compare them to characters of /full/
    while(*partial)
      if (*partial++ != *full++)
        return 1;
    return 0;
  }
  return 1;

}

// TODO: used only once, probably have to get rid of this function
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
//
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
  if (temp != buf)
    q_free(temp);
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

// print Address : Value pairs, decoding the data according to data type
// 1,2 and 4 bytes long data types are supported
// TODO: signed/unsigned char is displayed as hex. this is wrong.
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
// `duration` - delay time in milliseconds
//  returns duration if everything was ok, 
// returns !=duration if was interrupted (returns real time spent in delay_interruptible())
//
#define TOO_LONG 2999
#define DELAY_POLL 250

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
  delay(duration);

  // Success! Return exactly requested time, not the real one. Don't change this behaviour or if you do examine all calls to delay_interruptible()
  return duration0;
}

#endif //#if COMPILING_ESPSHELL


