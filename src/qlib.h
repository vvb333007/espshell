/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


// -- Q-Lib: helpful routines: ascii to number conversions, etc --
#if COMPILING_ESPSHELL

// Bunch of handy macros:

// gcc-specific size-optimization attempt ignored
#undef likely
#undef unlikely
#define unlikely(_X)     __builtin_expect(!!(_X), 0)
#define likely(X)     __builtin_expect(!!(_X), 1)

// millis() & micros() inlined versions
// TODO: may be use CCOUNT register for that? It will be super fast and it does not require any timer to be run
#define q_millis() ((unsigned int )esp_timer_get_time() / 1000)
#define q_micros() ((unsigned int )esp_timer_get_time())

// Mutex manipulation: declare, initialize, grab and release macros
#define MUTEX(_Name) xSemaphoreHandle _Name = NULL;   // e.g. static MUTEX(argv_mux);

// Grab a mutex. Blocks forever
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

  // TODO: mutex_destroy(_Name)


static bool ColorAuto = AUTO_COLOR;  // Autoenable coloring if terminal permits
static bool Color = false;           // Enable coloring
static bool Exit = false;            // True == close the shell and kill its FreeRTOS task.
static int  Echo = STARTUP_ECHO;  // Runtime echo flag: -1=silent,0=off,1=on

// -- Memory allocation wrappers --
//
// If MEMTEST is set to 0 (the default value) then q_malloc is simply malloc(),
// q_free() is free() and so on.
//
// If MEMTEST is non-zero then ESPShell tries to  load "extra/memlog.c" extension
// which provides its own versions of q_malloc, q_strdup, q_realloc and q_free
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
};

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

  // trivial cases
	if (ptr == NULL)
		return q_malloc(new_size,type);

	if (new_size == 0 && ptr != NULL) {
		q_free(ptr);
		return NULL;
	}

  //memlog_lock();
  mutex_lock(mem_mux);
  for (ml = head; ml != NULL; ml = (memlog_t *)(ml->li.next))
    if (ml->ptr == (unsigned char *)ptr)
      break;
  
  if (!ml) {
    //memlog_unlock();
    mutex_unlock(mem_mux);
    q_printf("<w>ERROR: q_realloc() : trying to realloc pointer %p which is not on the list</>\r\n",ptr);
    return NULL;
  }

	if (new_size == ml->len) {
    //memlog_unlock();
    mutex_unlock(mem_mux);
		return ptr;
  }

	if ((nptr = (char *)malloc(new_size + 2)) != NULL) {

    nptr[new_size + 0] = 0x55;
    nptr[new_size + 1] = 0xaa;

    memcpy(nptr, ptr, (new_size < ml->len) ? new_size : ml->len);
  	free(ptr);

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
              "%% Use command \"show mem ADDRESS [COUNT]\" to display data at memory address\r\n",count, count == 1 ? "" : "s");
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
static char *q_strdup256(const char *ptr, int type) {
  char *p = NULL;
  if (ptr != NULL) {
    int len = strlen(ptr);
    if ((p = (char *)q_malloc(len + 256 + 1, type)) != NULL)
      strcpy(p, ptr);
  }
  return p;
}


// Convert ascii (8biit per char) string to lowercase.
// Conversion is done for characters 'A'..'Z' by setting 5th bit
//
// /p/   - pointer to the string being converted (must be writeable memory)
// /len/ - if < 1, then string length is calculated by q_tolower. If > 0, then the value
//         is used as number of bytes to convert
//
static void q_tolower(char *p, int len) {
  if (p && *p) {
    if (len < 1)
      len = strlen(p);
    while (--len >= 0) {
      if (p[len] >= 'A' && p[len] <= 'Z')
        p[len] |= 1 << 5;
    }
  }
}

//check if given ascii string is a decimal number. Ex.: "12345", "-12"
// "minus" sign is only accepted if first in the string
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



// Check if given ascii string is a hex BYTE.
// String may or may not start with "0x"
// Strings "a" , "5a", "0x5" and "0x5A" are valid input
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

//convert hex ascii byte.
//strings "A", "5a" "0x5a" are valid input
//
static unsigned char hex2uint8(const char *p) {

  unsigned char f, l;  //first and last

  if (p[0] == '0' && p[1] == 'x')
    p += 2;

  f = *p++;

  //single character HEX?
  if (!(*p)) {
    l = f;
    f = '0';
  } else l = *p;

  // make it lowercase
  if (f >= 'A' && f <= 'F') f = f + 'a' - 'A';
  if (l >= 'A' && l <= 'F') l = l + 'a' - 'A';

  //convert first hex character to decimal
  if (f >= '0' && f <= '9') f = f - '0';
  else if (f >= 'a' && f <= 'f') f = f - 'a' + 10;
  else return 0;

  //convert second hex character to decimal
  if (l >= '0' && l <= '9') l = l - '0';
  else if (l >= 'a' && l <= 'f') l = l - 'a' + 10;
  else return 0;

  return (f << 4) | l;
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

// convert strings
// 0b10010101 and 10100101 (with or without leading "0b") to
// unsigned int values
//
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
// TAG:atol
#define DEF_BAD ((unsigned int)(-1))

static unsigned int q_atol(const char *p, unsigned int def) {
  if (p && *p) {
    if (isnum(p))  // decimal number?
      def = atol(p);
    else if (p[0] == '0') {  // leading "0" : either hexadecimal, binary or octal number
      if (p[1] == 'x') {     // hexadecimal
        if (ishex(p))
          def = hex2uint32(p);
      } else if (p[1] == 'b')  // binary (TODO: isbin())
        def = binary2uint32(p);
      else
        def = octal2uint32(p);  // octal  (TODO: isoct())
    }
  }
  return def;
}

// same for the atof():
static inline float q_atof(const char *p, float def) {
  if (p && *p)
    if (isfloat(p))
      def = atof(p);
  return def;
}



// Loose strcmp() which deoes partial match. It is used to match commands and parameters which are shortened:
// e.g. user typed "seq" instead of "sequence" or "m w" instead of "mount wwwroot"
//
// /partial/ - string which expected to be incomplete/shortened
// /full/    - full string to compare against
//
// q_strcmp("seq","sequence") == 0
// q_strcmp("sequence","seq") == 1
//
static int q_strcmp(const char *partial, const char *full) {
  int plen;
  if (partial && full && (*partial == *full))      // quick reject
    if ((plen = strlen(partial)) <= strlen(full))  //     OR
      return strncmp(partial, full, plen);         //  full test
  return 1;
}

static inline const char *q_findchar(const char *str, char sym) {
  if (str) {
    while (*str && sym != *str)
      str++;
    if (sym == *str)
      return str;
  }
  return NULL;
}


// adopted from esp32-hal-uart.c Arduino Core
// TODO: remake to either use non-static buffer OR use sync objects because we have async tasks which dp q_printf
//       thus reusing internal 128 bytes buffer
static int __printfv(const char *format, va_list arg) {

  static char buf[128 + 1];  // TODO: Mystery#2. Crashes without /static/.
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
  va_start(arg, format);
  len = __printfv(format, arg);
  va_end(arg);
  return len;
}


//Faster than q_printf() but only does non-formatted output
static int q_print(const char *str) {

  size_t len = 0;

  if (Echo < 0)  //"echo silent"
    return 0;

  if (str && *str) {

    const char *p, *pp = str;
    const char *ins;

    while (*pp) {
      if ((p = q_findchar(pp, '<')) == NULL)
        return console_write_bytes(pp, strlen(pp));
      if (p[1] && p[2] == '>') {
        ins = NULL;
        if (Color)
          switch (p[1]) {
            case 'i': ins = esc_i; break;
            case 'w': ins = esc_w; break;
            case 'e': ins = esc_e; break;
            case '/': ins = esc_n; break;
            case 'r': ins = esc_r; break;
            case '2': ins = esc_2; break;
            case '1': ins = esc_1; break;
            case '3': ins = esc_3; break;
            case 'b': ins = esc_b; break;
            case '*': ins = esc_ast; break;
            case '_': ins = esc__; break;
          }
        len += console_write_bytes(pp, p - pp);
        if (ins)
          len += console_write_bytes(ins, strlen(ins));
        pp = p + 3;
      } else {
        len += console_write_bytes(pp, p - pp + 1);
        pp = p + 1;
      }
    }
  }
  return len;
}

// make fancy hex data output: mixed hex values
// and ASCII. Useful to examine I2C EEPROM contents.
//
// data printed 16 bytes per line, a space between hex values, 2 spaces
// after each 4th byte. then separator and ascii representation are printed
//
static unsigned short tbl_min_len = 16;

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


  char ascii[16 + 1];
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
      // TODO: one space is lost somewhere when dumping sizes like 251
      if (j < 16) {
        unsigned char spaces = (16 - j) * 3 + (j <= 4 ? 3 : (j <= 8 ? 2 : (j <= 12 ? 1 : 0))) + 1;  // empirical :)
        char tmp[spaces + 1];
        memset(tmp, ' ', spaces);
        tmp[spaces] = '\0';
        q_print(tmp);
      }

      // print a separator and the same line but in ascii form
      q_print("|");
      ascii[j] = '\0';
      q_print(ascii);
      q_print("\r\n");
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
// keypress)or by a "kill" command
//
// `duration` - delay time in milliseconds
//  returns duration if everything was ok, 
// returns !=duration if was interrupted (returns real time spent in delay_interruptible())
//
#define TOO_LONG 2999
#define DELAY_POLL 250

static unsigned int delay_interruptible(unsigned int duration) {

  
  unsigned int now, duration0 = duration;

  now = millis();

  // Called from a background task? Wait for the signal from "kill" command, ignore keypresses
  if (!is_foreground_task()) {
    uint32_t note;
    if (task_wait_for_signal(&note, duration) == true)
      return millis() - now; // Interrupted 
    return duration;         // Success!
  }

  // Called from the foreground task (i.e. called from main espshell task context)
  // Only foreground task can be interrupted by a keypress
  if (duration > TOO_LONG) {
    while (duration >= DELAY_POLL) {
      duration -= DELAY_POLL;
      delay(DELAY_POLL);
      if (anykey_pressed())
        return millis() - now;  // interrupted by a keypress
    }
  }
  delay(duration);

  // Success! Return exactly requested time, not the real one. Don't change this behaviour or if you do examine all calls to delay_interruptible()
  return duration0;
}

// return "st", "nd", "rd" ot "th" depending on number
static __attribute__((const)) const char *number_english_ending(unsigned int n) {
  return n == 1 ? "st" : (n == 2 ? "nd" : (n == 3 ? "rd" : "th"));
}







#endif //#if COMPILING_ESPSHELL
