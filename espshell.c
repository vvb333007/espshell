/* 
 * ESP32Shell for the Arduino Framework by vvb333007 <vvb@nym.hush.com>
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 *
 * Uses editline library ( (c) 1992,1993 by Simmule Turner and Rich Salz)
 *
 * WHAT  IS THIS:
 * -------------
 * This is a debugging/development tool for use with Arduino projects on
 * ESP32 hardware. Provides a command line interface running in parallel
 * to user Arduino sketch (in a separate task). Allows pin manipulation,
 * PWM,UART,I2C and RMT operations. More information is in "docs/README.md"
 * and "docs/Commands.txt"
 *
 * DEVELOPERS: HOW TO NAVIGE THROUGH THE SOURCE CODE (THIS FILE):
 * --------------------------------------------------------------
 * It is huge.
 *
 * First 20-something Kb of this file is some ancient modified version of editline (readline).
 * Editline code is from the beginning till "EDITLINE CODE END", then ESPShell code starts.
 *
 * 1. Common functions are declared at the beginning of espshell code (q_printf(), isfloat(), 
 *    isnum(), etc)
 *
 * 2. Commands (syntax, help lines, handler function..) are declared in keywords_main[], 
 *    keywords_uart[] and other keywords_*[] arrays. Addition of a new command starts there.
 *
 * 3. Command handlers (functions which do the job when user enter commands) are prefixed 
 *    with "cmd_"  and are often named after commands: "pin" --> cmd_pin(), "reload" -->  cmd_reload()
 *    and so on.

 * 4. Function names are grouped: pin functions called from cmd_pin() are named "pin_..." so easily can be located.
      Similarily, UART functions are either cmd_uart...   or uart_... , i2c functions are cmd_i2c... and i2c_...
      and so on.

   5. Shell autostarts (search for AUTOSTART) and most of the time blocks in console_read(). User input is 
      processed by espshell_command() : input parsed, keywords[] array searched for keywords and corresponding
      cmd_... functions (command handlers) are called and process repeats
 *
 * 6. To create support for new type of console device (e.g. USB-CDC console, 
 *    not supported at the moment) one have to implement console_read_bytes(), 
 *    console_write_bytes() etc
 *
 * 7. Copyright information is at the end of this file
 */
#define COMPILING_ESPSHELL 1  // dont touch this!

// COMPILE TIME SETTINGS
// (User can override these with espshell.h, extra/espshell.h or just change default values below)
//
//TAG:settings
//#define SERIAL_IS_USB           // Not yet
//#define ESPCAM                  // Include ESP32CAM commands (read extra/README.md).
#define AUTOSTART       1          // Set to 0 for manual shell start via espshell_start().
#define WITH_COLOR      1          // Enable terminal colors
#define WITH_HELP       1          // Set to 0 to save some program space by excluding help strings/functions
#define WITH_HISTORY    1          // Set to 0 to when looking for memory leaks
#define WITH_FS         1          // Filesystems (fat/spiffs/littlefs) support (cp,mv,insert and delete are not implemented yet)
#define WITH_SPIFFS     1          // support SPIF filesystem
#define WITH_LITTLEFS   1          //   --    LittleFS
#define WITH_FAT        1          //   --    FAT
#define HIST_SIZE       20         // History buffer size (number of commands to remember)
#define STARTUP_PORT    UART_NUM_0 // Uart number (or 99 for USBCDC) where shell will be deployed at startup
#define SEQUENCES_NUM   10         // Max number of sequences available for command "sequence"
#define MOUNTPOINTS_NUM 5          // Max number of simultaneously mounted filesystems
#define STACKSIZE       (5*1024)   // Shell task stack size
#define DIR_RECURSION_DEPTH 127    // Max directory depth TODO: make a test with long "/a/a/a/.../a" path 
#define MEMTEST         1          // hunt for espshell's memory leaks   
#define DO_ECHO         1          // echo mode at espshell startup.
// ^^ ECHO ^^
// Automated processing (i.e. sending commands and parsing the resulting output by software) is supported by
// "echo" command: setting echo mode to -1 ("silent") disables all ESPShell output as if there is no shell at all. 
// This mode is used to stop ESPShell interfering sketch's output.
//
// Setting echo mode to 0 ("off") disables user input echo: everything you type is not displayed, but when <Enter> is
// pressed then command gets executed and its output is displayed. ESPShell prompt is displayed. This mode is an equivalent of
// a modem command "ATE0".
// Mode 1 ("on") is default mode when everything is displayed end echoed. This is used when you need human-friendly interface


//TAG:prompts
// Prompts used by command subdirectories
#define PROMPT "esp32#>"                 // Main prompt
#define PROMPT_I2C "esp32-i2c%u>"        // i2c prompt
#define PROMPT_UART "esp32-uart%u>"      // uart prompt   
#define PROMPT_SEQ "esp32-seq%u>"        // Sequence subtree prompt
#define PROMPT_FILES "esp32#(%s%s%s)>"   // File manager prompt
#define PROMPT_SEARCH "Search: "         // History search prompt

//TAG:includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <soc/soc_caps.h>
#include <soc/gpio_struct.h>
#include <soc/pcnt_struct.h>
#include <soc/efuse_reg.h>

#include <hal/gpio_ll.h>

#include <driver/gpio.h>
#include <driver/pcnt.h>
#include <driver/uart.h>

#include <rom/gpio.h>

#include <esp_timer.h>
#include <esp_chip_info.h>

#include <esp32-hal-periman.h>
#include <esp32-hal-ledc.h>
#include <esp32-hal-rmt.h>
#include <esp32-hal-uart.h>

// Support files for LittleFS, FAT and SPIFFS filesystems
#if WITH_FS
#  include <sys/unistd.h>
#  include <sys/stat.h>
#  include <dirent.h>
#  include <fcntl.h>
#  ifdef __cplusplus
   extern "C" {
#  endif
#  include <esp_vfs.h>
#  include <esp_partition.h>
#  if WITH_LITTLEFS
#    include <esp_littlefs.h>
#  endif
#  if WITH_SPIFFS
#    include <esp_spiffs.h>
#  endif
#  if WITH_FAT
#    include <esp_vfs_fat.h>
#    include <diskio.h>
#    include <diskio_wl.h>
#    include <vfs_fat_internal.h>
#    include <wear_levelling.h>
#  endif
#  ifdef __cplusplus
   };
#  endif // __cplusplus
#endif // WITH_FS

// Pickup compile-time setting overrides if any from "espshell.h" or "extra/espshell.h"
#if __has_include("espshell.h")
#  include "espshell.h"
#elif __has_include("extra/espshell.h")
#  include "extra/espshell.h"
#endif

// Compile basic AiThinker ESP32Cam support: ESPCam commands are arranged as "external"
// commands and their source code is in "extra/" folder. These are used for a side project
// and are left here for the reference on how to write external commands
#ifdef ESPCAM
#  if defined(EXTERNAL_PROTOTYPES) || defined(EXTERNAL_KEYWORDS) || defined(EXTERNAL_HANDLERS)
#    error "EXTERNAL_KEYWORDS and ESPCAM are both set"
#  endif
#  if __has_include("esp32cam_prototypes.h")
#    define EXTERNAL_PROTOTYPES "esp32cam_prototypes.h"
#    define EXTERNAL_KEYWORDS "esp32cam_keywords.c"
#    define EXTERNAL_HANDLERS "esp32cam_handlers.c"
#  elif __has_include("extra/esp32cam_prototypes.h")
#    define EXTERNAL_PROTOTYPES "extra/esp32cam_prototypes.h"
#    define EXTERNAL_KEYWORDS "extra/esp32cam_keywords.c"
#    define EXTERNAL_HANDLERS "extra/esp32cam_handlers.c"
#  else
#    warning "ESPCAM is defined, but no ESP32Cam source files were found"
#    warning "Disabling ESPCam support"
#    undef ESPCAM
#  endif  // Have esp32cam_* sources?
#endif  // ESPCAM?


// GCC-specific stuff
//TAG:gcc
#define UNUSED __attribute__((unused)) 
#define INLINE inline __attribute__((always_inline))
#define NORETURN __attribute__((noreturn))
#define PRINTF_LIKE __attribute__((format(printf, 1, 2)))
#define STARTUP_HOOK __attribute__((constructor))
#pragma GCC diagnostic warning "-Wformat"                   // enable -Wformat warnings. Turned off by Arduino IDE by default
#define xstr(s) ystr(s)
#define ystr(s) #s



// Autostart/start espshell.
// Called automatically by C runtime startup code if AUTOSTART == 1 (Default)
// otherwhise it is up to user sketch to call espshell_start().
#if AUTOSTART
void STARTUP_HOOK espshell_start();
#else
void espshell_start();
#endif

// Miscellaneous forwards
// TAG:forwards
static inline bool uart_isup(unsigned char u);       // Check if UART u is up and operationg (is driver installed?)
static int q_strcmp(const char *, const char *);     // loose strcmp
static int PRINTF_LIKE q_printf(const char *, ...);  // printf()
static int q_print(const char *);                    // puts()

// -- Memory allocation wrappers --
// If MEMTEST is set to 0 (the default value) then q_malloc is simply malloc(), 
// q_free() is free() and so on. 
// 
// If MEMTEST is non-zero then ESPShell tries to  load "extra/memlog.c" extension
// which provides its own versions of q_malloc, q_strdup, q_realloc and q_free  
// functions which do memory statistics/tracking and perform some checks on pointers
// being freed
//
// ENUM /Memory type/: a number from 0 to 15 to identify newly allocated memory block intended 
// usage. Newly allocated memory is assigned one of the types below. Command "mem" invokes 
// q_memleaks() function to dump memory allocation information. Only works #if MEMTEST == 1
enum {
  MEM_EDITLINE = 0,  // allocated by editline library (general)
  MEM_ARGV,          // parsed input string (argv[] array)
  MEM_ARGCARGV,      // parsed user input refcounted object
  MEM_LINE,
  MEM_SCREEN,
  MEM_HISTORY,

  MEM_TEXT2BUF,      // TEXT argument (fs commands write,append,insert or uart's write are examples) converted to byte array

  MEM_MOUNTPOINT,    // path (mountpoint)
  MEM_PATH,          // path (generic)
  MEM_CWD,           // path (current working directory)
  MEM_CAT,           // memory used by "cat" command
  MEM_GETLINE,       // memory allocated by files_getline()

  MEM_SEQUENCE,      // sequence bitstring
  MEM_RMT,           // sequence symbols array
  
  MEM_QPRINTF,       // printf internal buffer
  MEM_VAR            // memory used to track variables

};


// search & include memtest.c sources if MEMTEST is enabled
#if MEMTEST
#  if __has_include("extra/memtest.c")
#    include "extra/memtest.c"
#  else
#    warning "MEMTEST is defined, but no extra/memtest.c file were found. Disabling memory leaks tests"
#    undef MEMTEST
#    define MEMTEST 0
#  endif
#endif

// fallback to newlib
#if !MEMTEST
#  define q_malloc(_size, _type) malloc((_size))
#  define q_realloc(_ptr,_new_size,_type) realloc((_ptr),(_new_size))
#  define q_strdup(_ptr, _type) strdup((_ptr))
#  define q_free(_ptr) free((_ptr))
#endif

// espshell runs on this port:
static uart_port_t uart = STARTUP_PORT;              

// TAG:console
// --   SHELL TO CONSOLE HARDWARE GLUE --
// espshell uses console_read../console_write.. and some other functions to print data or read user input.
// In order to implement support for another type of hardware (say USBCDC) one have to implement functions
// below
#ifdef SERIAL_IS_USB
#  error "console_write_bytes() is not implemented"
#  error "console_read_bytes() is not implemented"
#  error "console_available() is not implemented"
static INLINE bool console_isup() { return uart == 99 ? true : uart_isup(uart); }
#else
// Send characters to user terminal
static INLINE int console_write_bytes(const void *buf, size_t len) { return uart_write_bytes(uart, buf, len); }
// How many characters are available for read without blocking?
static INLINE int console_available() { size_t av; return ESP_OK == uart_get_buffered_data_len(uart, &av) ? (int)av : -1; }
// read user input, with timeout
static INLINE int console_read_bytes(void *buf, uint32_t len, TickType_t wait) { return uart_read_bytes(uart, buf, len, wait); }
// is UART (or USBCDC) driver installed and can be used?
static INLINE bool console_isup() { return uart_isup(uart); }
#endif  //SERIAL_IS_USB

// Make ESPShell to use specified UART (or USB) for its IO. Default is uart0.
// Code below reads: return current uart number if i < 0. If i is valid uart number or i is 99
// then set current uart to that number and return the same number as well. If i is not a valid uart number then -1 is returned
static INLINE int console_here(int i) { return i < 0 ? uart : (i > UART_NUM_MAX ? (i == 99 ? (uart = i) : -1) : (uart = i)); }


//========>=========>======= EDITLINE CODE BELOW (modified ancient version) =======>=======>
// TAG:editline
#define CRLF "\r\n"

#define MEM_INC  64      // generic  buffer realloc increments
#define MEM_INC2 16      // dont touch this

#define SCREEN_INC 256  // "Screen" buffer realloc increments

#define DISPOSE(p) q_free((char *)(p))
#define NEW(T, c, Typ) ((T *)q_malloc((unsigned int)(sizeof(T) * (c)), Typ))
#define RENEW(p, T, c) (p = (T *)q_realloc((char *)(p), (unsigned int)(sizeof(T) * (c)),MEM_EDITLINE))
#define COPYFROMTO(_new, p, len) (void)memcpy((char *)(_new), (char *)(p), (int)(len))

#define NO_ARG (-1)
#define DEL 127
#define CTL(x) ((x)&0x1F)
#define ISCTL(x) ((x) && (x) < ' ')
#define UNCTL(x) ((x) + 64)
#define META(x) ((x) | 0x80)
#define ISMETA(x) ((x)&0x80)
#define UNMETA(x) ((x)&0x7F)


//  Command status codes.
typedef enum { CSdone, CSeof, CSmove, CSdispatch, CSstay, CSsignal } STATUS;

//  Key to command mapping.
typedef struct {
  unsigned char Key;
  STATUS (*Function)();
} KEYMAP;


// Command history structure.
typedef struct {
  int Size;
  int Pos;
  unsigned char *Lines[HIST_SIZE];
} HISTORY;

static portMUX_TYPE Input_mux = portMUX_INITIALIZER_UNLOCKED;

//static unsigned char NIL[] = "";    // Empty string
static unsigned char *Line = NULL;  // Raw user input
static const char *Prompt = NULL;   // Current prompt to use
static char *Screen = NULL;
static HISTORY H;
static int Repeat;
static int End;
static int Mark;
static int OldPoint;
static int Point;
static int PushBack;
static int Pushed;
static bool Color = false;     // Enable coloring
static bool Exit = false;      // True == close the shell and kill its FreeRTOS task.
static const char *Input = "";  // "Artificial input queue". if non empty then symbols are
                               // fed to TTYget as if it was user input. used by espshell_exec()
static unsigned int Length;
static unsigned int ScreenCount;
static unsigned int ScreenSize;


static int Echo = DO_ECHO;  // Runtime echo flag: -1=silent,0=off,1=on

static unsigned char *editinput();

static STATUS ring_bell();
static STATUS inject_exit();
static STATUS inject_suspend();
static STATUS tab_pressed();
static STATUS home_pressed();
static STATUS end_pressed();
static STATUS kill_line();
static STATUS enter_pressed();
static STATUS left_pressed();
static STATUS del_pressed();
static STATUS right_pressed();
static STATUS backspace_pressed();
static STATUS bk_kill_word();
static STATUS bk_word();

static STATUS h_next();
static STATUS h_prev();
static STATUS h_search();
static STATUS redisplay();
static STATUS clear_screen();
static STATUS meta();

static const KEYMAP Map[] = {
  //Key       Callback                 Action
  { CTL('C'), inject_suspend },     // "suspend" commands
  { CTL('Z'), inject_exit },        // "exit" command
  { CTL('A'), home_pressed },       // <HOME>
  { CTL('E'), end_pressed },        // <END>
  { CTL('B'), left_pressed },       // Arrow left. Terminal compatibility
  { CTL('F'), right_pressed },      // Arrow right. Terminal compatibility
  { CTL('D'), del_pressed },        // <DEL>
  { CTL('H'), backspace_pressed },  // <BACKSPACE>
  { CTL('J'), enter_pressed },      // <ENTER>
  { CTL('M'), enter_pressed },      // <ENTER>
  { CTL('K'), kill_line },          // Erase from cursor till the end
  { CTL('L'), clear_screen },       // Clear (erase) the screen, keep use input
  { CTL('O'), h_prev },             // Previous history entry. Terminal compatibility
  { CTL('P'), h_next },             // Next history entry. Terminal compatibility
  { CTL('R'), h_search },           // Reverse history search. Type few symbols and press <Enter>
  { CTL('['), meta },               // Arrows get processed there as well as other ESC sequences
  { CTL('I'), tab_pressed },        // <TAB>

//currently unused
#if 0  
  { CTL('\\'), ring_bell },
  { CTL('@'), ring_bell },  // ?
  { CTL('G'), ring_bell },
  { CTL('N'), ring_bell },
  { CTL('Q'), ring_bell },
  { CTL('S'), ring_bell },
  { CTL('T'), ring_bell },
  { CTL('U'), ring_bell },
  { CTL('V'), ring_bell },
  { CTL('X'), ring_bell },
  { CTL('Y'), ring_bell },
  { CTL(']'), ring_bell },  // ctrl+5 or ctrl+]
  { CTL('^'), ring_bell },  // ctrl+6 or ctrl+/
  { CTL('_'), ring_bell },  // ctrl+7
#endif
  { 0, NULL }
};

static const KEYMAP MetaMap[] = {
  { CTL('H'), bk_kill_word },  // <ESC>, <BACKSPACE> - deletes a word (undocumented)
#if 0  
  { DEL, ring_bell },
  { ' ', ring_bell },
  { '.', ring_bell },
  { '<', ring_bell },
  { '>', ring_bell },
  { 'b', ring_bell },
  { 'd', ring_bell },
  { 'f', ring_bell },
  { 'l', ring_bell },
  { 'm', ring_bell },
  { 'u', ring_bell },
  { 'y', ring_bell },
  { 'w', ring_bell },
#endif
  { 0, NULL }
};

// coloring macros.
// coloring is auto-enabled upon reception of certain symbols from user: arrow keys,
// <tab>, Ctrl+??? and such will enable syntax coloring
//
#define esc_i "\033[33;93m"             // [I]important information (eye-catching bright yellow)
#define esc_r "\033[38;5;0;48;5;255m"   // [R]eversed monochrome (black on white)
#define esc_w "\033[31;91m"             // [W]arning message ( failsafe red )
#define esc_e "\033[35;95m"             // [E]rror message (bright magenta)
#define esc_b "\033[1m"                 // [B]old
#define esc_n "\033[0m"                 // [N]ormal colors
#define esc_1 "\033[33m"                // Hint[1] dark yellow
#define esc_2 "\033[36m"                // Hint[2] dark cyan
#define esc_3 "\033[92m"                // Hint[3] dark cyan

// Queue an arbitrary asciiz string to simulate user input.
// String queued has higher priority than user input so console_read() would
// "read" from this string first.
static inline void
TTYqueue(const char *input) {
  portENTER_CRITICAL(&Input_mux);
  Input = input;
  portEXIT_CRITICAL(&Input_mux);
}

// Print buffered (by TTYputc/TTYputs) data. editline uses buffered IO
// so no actual data is printed until TTYflush() is called
// No printing is done if "echo off" or "echo silent" flag is set
//
static void
TTYflush() {
  if (ScreenCount) {
    if (Echo > 0)
      console_write_bytes(Screen, ScreenCount);
    ScreenCount = 0;
  }
}

// queue a char to be printed
static void
TTYput(unsigned char c) {

  Screen[ScreenCount] = c;
  if (++ScreenCount >= ScreenSize - 1) {
    ScreenSize += SCREEN_INC;
    RENEW(Screen, char, ScreenSize);
  }
}

//queue a string to be printed
static void
TTYputs(const unsigned char *p) {
  while (*p)
    TTYput(*p++);
}

// display a character in a human-readable form:
// normal chars are displayed as is
// Crel+Key or ESC+Key sequences are displayed as ^Key or M-Key
// DEL is "^?"
static void
TTYshow(unsigned char c) {
  if (c == DEL) {
    TTYput('^');
    TTYput('?');
  } else if (ISCTL(c)) {
    TTYput('^');
    TTYput(UNCTL(c));
  } else
    TTYput(c);
}

// same as above but for the string
static void
TTYstring(unsigned char *p) {
  while (*p)
    TTYshow(*p++);
}

//read a character from user.
//
static unsigned int
TTYget() {
  unsigned char c = 0;

  TTYflush();

  if (Pushed) {
    Pushed = 0;
    return PushBack;
  }
try_again:
  // read byte from a priority queue if any.
  portENTER_CRITICAL(&Input_mux);
  if (*Input)
    c = *Input++;
  portEXIT_CRITICAL(&Input_mux);
  if (c)
    return c;

  // read 1 byte from user. we use timeout to enable Input processing if it was set
  // mid console_read_bytes() call: i.e. Input polling happens every 500ms
  if (console_read_bytes(&c, 1, pdMS_TO_TICKS(500)) < 1)
    goto try_again;

#if WITH_COLOR
  // Trying to be smart:
  // if we receive lower keycodes from user that means his terminal
  // is not an Arduino IDE Serial Monitor or alike, so we can enable
  // syntax coloring.
  if (c < ' ' && c != '\n' && c != '\r' && c != '\t')
    Color = true;
#endif

  return c;
}

#define TTYback() TTYput('\b')

static void
TTYbackn(int n) {
  while (--n >= 0)
    TTYback();
}

static bool rl_history = true;

// enable/disable history saving. mostly for memory leaks detection
static void
rl_history_enable(bool enable) {
  if (!enable) {
    if (rl_history) {
      int i;
      for (i = 0; i < H.Size; i++) {
        if (H.Lines[i]) {
          q_free(H.Lines[i]);
          H.Lines[i] = NULL;
        }
      }
      H.Size = H.Pos = 0;
      rl_history = false;
#if WITH_HELP
      q_printf("%% Command history purged, history is disabled\r\n");
#endif
    }
  } else {
    if (!rl_history) {
      rl_history = true;
#if WITH_HELP
    q_printf("%% Command history is enabled\r\n");
#endif    
    }
  }
}



static void
reposition() {
  int i;
  unsigned char *p;

  TTYput('\r');
  TTYputs((const unsigned char *)Prompt);

  for (i = Point, p = Line; --i >= 0; p++)
    TTYshow(*p);
}

static void
left(STATUS Change) {
  TTYback();
  if (Point) {
    if (ISCTL(Line[Point - 1]))
      TTYback();
  }
  if (Change == CSmove)
    Point--;
}

static void
right(STATUS Change) {
  TTYshow(Line[Point]);
  if (Change == CSmove)
    Point++;
}

static STATUS
ring_bell() {
  TTYput('\07');
  TTYflush();
  return CSstay;
}

// Ctrl+Z hanlder: send "exit" commnd
static STATUS
inject_exit() {
  TTYqueue("exit\n");
  return CSstay;
}

// Ctrl+C handler: sends "suspend"
static STATUS
inject_suspend() {
  TTYqueue("suspend\n");
  return CSstay;
}


static STATUS
do_forward(STATUS move) {
  int i;
  unsigned char *p;

  i = 0;
  do {
    p = &Line[Point];
    for (; Point < End && (*p == ' ' || !isalnum(*p)); Point++, p++)
      if (move == CSmove)
        right(CSstay);

    for (; Point < End && isalnum(*p); Point++, p++)
      if (move == CSmove)
        right(CSstay);

    if (Point == End)
      break;
  } while (++i < Repeat);

  return CSstay;
}

//tab_pressed() was moved down 

static void
ceol() {
  int extras;
  int i;
  unsigned char *p;

  for (extras = 0, i = Point, p = &Line[i]; i <= End; i++, p++) {
    TTYput(' ');
    if (ISCTL(*p)) {
      TTYput(' ');
      extras++;
    }
  }

  for (i += extras; i > Point; i--)
    TTYback();
}

static void
clear_line() {
  Point = -strlen(Prompt);
  TTYput('\r');
  ceol();
  Point = 0;
  End = 0;
  Line[0] = '\0';
}

static STATUS
insert_string(unsigned char *p) {
  unsigned int len;
  int i;
  unsigned char *_new;
  unsigned char *q;

  len = strlen((char *)p);
  if (End + len >= Length) {
    if ((_new = NEW(unsigned char, Length + len + MEM_INC, MEM_LINE)) == NULL)
      return CSstay;
    if (Length) {
      COPYFROMTO(_new, Line, Length);
      DISPOSE(Line);
    }
    Line = _new;
    Length += len + MEM_INC;
  }

  for (q = &Line[Point], i = End - Point; --i >= 0;)
    q[len + i] = q[i];
  COPYFROMTO(&Line[Point], p, len);
  End += len;
  Line[End] = '\0';
  TTYstring(&Line[Point]);
  Point += len;

  return Point == End ? CSstay : CSmove;
}

static STATUS
redisplay() {
  const unsigned char *nl = (const unsigned char *)"\r\n";
  TTYputs(nl);
  TTYputs((const unsigned char *)Prompt);

  TTYstring(Line);
  return CSmove;
}


static STATUS
do_insert_hist(unsigned char *p) {
  if (p == NULL)
    return ring_bell();
  Point = 0;
  reposition();
  ceol();
  End = 0;
  return insert_string(p);
}

static STATUS
do_hist(unsigned char *(*move)()) {
  unsigned char *p;
  int i;

  i = 0;
  do {
    if ((p = (*move)()) == NULL)
      return ring_bell();
  } while (++i < Repeat);
  return do_insert_hist(p);
}

static unsigned char *next_hist() { return H.Pos >= H.Size - 1 ? NULL : H.Lines[++H.Pos];}
static unsigned char *prev_hist() { return H.Pos == 0 ? NULL : H.Lines[--H.Pos];}

static STATUS h_next() { return do_hist(next_hist);}
static STATUS h_prev() { return do_hist(prev_hist);}

/*
**  Return zero if pat appears as a substring in text.
*/
static int
substrcmp(char *text, char *pat, size_t len) {
  char c;

  if ((c = *pat) == '\0')
    return *text == '\0';
  for (; *text; text++)
    if (*text == c && strncmp(text, pat, len) == 0)
      return 0;
  return 1;
}

static unsigned char *
search_hist(unsigned char *search, unsigned char *(*move)()) {
  static unsigned char *old_search;
  int len;
  int pos;
  int (*match)(char *, char *, size_t);
  char *pat;

  /* Save or get remembered search pattern. */
  if (search && *search) {
    if (old_search)
      DISPOSE(old_search);
    old_search = (unsigned char *)q_strdup((char *)search,MEM_EDITLINE);
  } else {
    if (old_search == NULL || *old_search == '\0')
      return NULL;
    search = old_search;
  }

  /* Set up pattern-finder. */
  //TODO: document "^" symbol use in search request
  if (*search == '^') {
    match = (int (*)(char *, char *, size_t))strncmp;
    pat = (char *)(search + 1);
  } else {
    match = substrcmp;
    pat = (char *)search;
  }
  len = strlen(pat);

  for (pos = H.Pos; (*move)() != NULL;)
    if ((*match)((char *)H.Lines[H.Pos], pat, len) == 0)
      return H.Lines[H.Pos];
  H.Pos = pos;
  return NULL;
}


// CTRL+R : reverse history search
// start typing partial command and press <Enter>
static STATUS h_search() {

  static int Searching;
  const char *old_prompt;
  unsigned char *(*move)();
  unsigned char *p;

  if (Searching)
    return ring_bell();
  Searching = 1;

  clear_line();
  old_prompt = Prompt;
  Prompt = PROMPT_SEARCH;
#if WITH_COLOR
  if (Color) TTYputs((const unsigned char *)"\033[1;36m");
#endif
#if WITH_HELP
  const char *Hint = "% Command history search: start typing and press <Enter> to\r\n% find a matching command executed previously\r\n";
  TTYputs((const unsigned char *)Hint);
#endif
  TTYputs((const unsigned char *)Prompt);

  move = Repeat == NO_ARG ? prev_hist : next_hist;
  p = editinput();
  Prompt = old_prompt;
  Searching = 0;
  p = search_hist(p, move);
  clear_line();
  if (p == NULL) {
    (void)ring_bell();
    return redisplay();
  }
  return do_insert_hist(p);
}

static STATUS
right_pressed() {
  int i = 0;
  do {
    if (Point >= End)
      break;
    right(CSmove);
  } while (++i < Repeat);
  return CSstay;
}


static STATUS
delete_string(int count) {
  int i;
  unsigned char *p;

  if (count <= 0 || End == Point)
    return ring_bell();

  if (count == 1 && Point == End - 1) {
    /* Optimize common case of delete at end of line. */
    End--;
    p = &Line[Point];
    i = 1;
    TTYput(' ');
    if (ISCTL(*p)) {
      i = 2;
      TTYput(' ');
    }
    TTYbackn(i);
    *p = '\0';
    return CSmove;
  }
  if (Point + count > End && (count = End - Point) <= 0)
    return CSstay;

  for (p = &Line[Point], i = End - (Point + count) + 1; --i >= 0; p++)
    p[0] = p[count];
  ceol();
  End -= count;
  TTYstring(&Line[Point]);
  return CSmove;
}

static STATUS
left_pressed() {
  int i;

  i = 0;
  do {
    if (Point == 0)
      break;
    left(CSmove);
  } while (++i < Repeat);

  return CSstay;
}


static STATUS
clear_screen() {
  q_print("\033[H\033[2J");
  return redisplay();
}

static STATUS
kill_line() {
  int i;

  if (Repeat != NO_ARG) {
    if (Repeat < Point) {
      i = Point;
      Point = Repeat;
      reposition();
      (void)delete_string(i - Point);
    } else if (Repeat > Point) {
      right(CSmove);
      (void)delete_string(Repeat - Point - 1);
    }
    return CSmove;
  }

  Line[Point] = '\0';
  ceol();
  End = Point;
  return CSstay;
}

static STATUS
insert_char(int c) {
  STATUS s;
  unsigned char buff[2];
  unsigned char *p;
  unsigned char *q;
  int i;

  if (Repeat == NO_ARG || Repeat < 2) {
    buff[0] = c;
    buff[1] = '\0';
    return insert_string(buff);
  }

  if ((p = NEW(unsigned char, Repeat + 1, MEM_EDITLINE)) == NULL)
    return CSstay;
  for (i = Repeat, q = p; --i >= 0;)
    *q++ = c;
  *q = '\0';
  Repeat = 0;
  s = insert_string(p);
  DISPOSE(p);
  return s;
}

// ESC received. Arrows are encoded as ESC[A, ESC[B etc
// ESC+digits are decoded as character with code
//
static STATUS
meta() {
  unsigned int c;
  const KEYMAP *kp;

  if ((int)(c = TTYget()) == EOF)
    return CSeof;

  /* Also include VT-100 arrows. */
  if (c == '[' || c == 'O')
    switch ((int)(c = TTYget())) {
      default: return ring_bell();
      case EOF: return CSeof;
      case 'A': return h_prev();         // Arrow UP
      case 'B': return h_next();         // Arrow DOWN
      case 'C': return right_pressed();  // Arrow RIGHT
      case 'D': return left_pressed();   // Arrow LEFT
    }


  // ESC + NUMBER to enter an arbitrary ascii code
  if (isdigit(c)) {

    int i;
    unsigned char code = 0;

    for (i = 0; (i < 3) && isdigit(c); i++) {
      code = code * 10 + c - '0';
      c = TTYget();
    }
    Pushed = 1;
    PushBack = code;
    return CSstay;
  }

  if (isupper(c))
    return ring_bell();

  for (OldPoint = Point, kp = MetaMap; kp->Function; kp++)
    if (kp->Key == c)
      return (*kp->Function)();

  return ring_bell();
}

static STATUS
emacs(unsigned int c) {
  STATUS s;
  const KEYMAP *kp;

  for (kp = Map; kp->Function; kp++)
    if (kp->Key == c)
      break;
  s = kp->Function ? (*kp->Function)() : insert_char((int)c);
  if (!Pushed)
    /* No pushback means no repeat count; hacky, but true. */
    Repeat = NO_ARG;
  return s;
}

static STATUS
TTYspecial(unsigned int c) {
  if (ISMETA(c))
    return CSdispatch;

  if (c == DEL)
    return del_pressed();

  if (c == 0 && Point == 0 && End == 0) //TODO: investigate CSeof and CSsignal
    return CSeof;

  return CSdispatch;
}

static unsigned char *
editinput() {
  unsigned int c;

  Repeat = NO_ARG;
  OldPoint = Point = Mark = End = 0;
  Line[0] = '\0';

  while ((int)(c = TTYget()) != EOF) {
    switch (TTYspecial(c)) {
      case CSdone:   return Line;
      case CSeof:    return NULL;
      case CSsignal: return (unsigned char *)"";
      case CSmove:   reposition(); break;
      case CSdispatch:
        switch (emacs(c)) {
          case CSdone:   return Line;
          case CSeof:    return NULL;
          case CSsignal: return (unsigned char *)"";
          case CSmove:   reposition(); break;
          case CSdispatch:
          case CSstay:   break;
        }
        break;
      case CSstay: break;
    }
  }
  if (strlen((char *)Line))
    return Line;

  //Original code has a bug here: Line was not set to NULL causing random heap corruption
  //on ESP32
  // TODO: investigate
  q_free(Line);
  q_print("Wow\r\n");
  return (Line = NULL);
}

static void
hist_add(unsigned char *p) {
  int i;

  if ((p = (unsigned char *)q_strdup((char *)p,MEM_HISTORY)) == NULL)
    return;
  if (H.Size < HIST_SIZE)
    H.Lines[H.Size++] = p;
  else {
    DISPOSE(H.Lines[0]);
    for (i = 0; i < HIST_SIZE - 1; i++)
      H.Lines[i] = H.Lines[i + 1];
    H.Lines[i] = p;
  }
  H.Pos = H.Size - 1;
}


// old good readline()
static char *
readline(const char *prompt) {
  unsigned char *line;
  unsigned char nil[] = { 0 };

  if (Line == NULL) {
    Length = MEM_INC;
    if ((Line = NEW(unsigned char, Length, MEM_LINE)) == NULL)
      return NULL;
  }

  hist_add(nil); //TODO: how does it work?! 

  ScreenSize = SCREEN_INC;
  Screen = NEW(char, ScreenSize, MEM_SCREEN);  // TODO: allocate once, never DISPOSE()

  TTYputs((const unsigned char *)(Prompt = prompt));
  TTYflush();

  if ((line = editinput()) != NULL) {
    const unsigned char *nl = (const unsigned char *)"\r\n";
    line = (unsigned char *)q_strdup((char *)line,MEM_EDITLINE);
    TTYputs(nl);
    TTYflush();
  }

  DISPOSE(Screen);
  DISPOSE(H.Lines[--H.Size]);
  
  return (char *)line;
}

#if WITH_HISTORY
// Add an arbitrary string p to the command history.
//
static void rl_add_history(char *p) {
  if (p && *p)
    if (!H.Size || (H.Size && strcmp(p, (char *)H.Lines[H.Size - 1])))
      hist_add((unsigned char *)p);
}
#endif


static STATUS
del_pressed() {
  return delete_string(Repeat == NO_ARG ? 1 : Repeat);
}

static STATUS
backspace_pressed() {
  int i;

  i = 0;
  do {
    if (Point == 0)
      break;
    left(CSmove);
  } while (++i < Repeat);

  return delete_string(i);
}

static STATUS
home_pressed() {
  if (Point) {
    Point = 0;
    return CSmove;
  }
  return CSstay;
}

static STATUS
end_pressed() {
  if (Point != End) {
    Point = End;
    return CSmove;
  }
  return CSstay;
}

static STATUS
enter_pressed() {
  Line[End] = '\0';
  
#if WITH_COLOR  
  // user has pressed <Enter>: set colors to default
  if (Color) TTYputs((const unsigned char *)"\033[0m");
#endif  
  return CSdone;
}

static STATUS
bk_word() {
  int i;
  unsigned char *p;

  i = 0;
  do {
    for (p = &Line[Point]; p > Line && !isalnum(p[-1]); p--)
      left(CSmove);

    for (; p > Line && p[-1] != ' ' && isalnum(p[-1]); p--)
      left(CSmove);

    if (Point == 0)
      break;
  } while (++i < Repeat);

  return CSstay;
}

static STATUS
bk_kill_word() {
  (void)bk_word();
  if (OldPoint != Point)
    return delete_string(OldPoint - Point);
  return CSstay;
}

// Tokenize string p.
// p must be malloc()'ed (NOT CONST!) as it gets modified!
// Allocates array of pointers and fills it with pointers
// to individual tokens. Whitespace is the token separator.
// Original string p gets modified ('\0' are inserted)
//
// Usage:
//
// int argc;
// char **argv;
// argc = argify(p,&argv);
// ...
// if (argv)
//   free(argv);
static int
argify(unsigned char *line, unsigned char ***avp) {
  unsigned char *c;
  unsigned char **p;
  unsigned char **_new;
  int ac;
  int i;

  i = MEM_INC2;
  if ((*avp = p = NEW(unsigned char *, i, MEM_ARGV)) == NULL)
    return 0;

  /* skip leading whitespace */
  for (c = line; isspace(*c); c++)
    continue;

  if (*c == '\n' || *c == '\0')
    return 0;

  for (ac = 0, p[ac++] = c; *c && *c != '\n';) {
    if (isspace(*c)) {
      *c++ = '\0';
      if (*c && *c != '\n') {

        if (ac + 1 == i) {

          _new = NEW(unsigned char *, i + MEM_INC2, MEM_ARGV);

          if (_new == NULL) {
            p[ac] = NULL;
            return ac;
          }

          COPYFROMTO(_new, p, i * sizeof(char **));
          i += MEM_INC2;
          DISPOSE(p);
          *avp = p = _new;
        }

        /*skip spaces */
        for (; *c && isspace(*c); c++)
          continue;

        if (*c)
          p[ac++] = c;
      }
    } else
      c++;
  }

  *c = '\0';
  p[ac] = NULL;

  return ac;
}
////////////////////////////// EDITLINE CODE END

//common messages
static const char *Failed = "% <e>Failed</>\r\n";
static const char *Notset = "<1>not set</>\r\n";
#if WITH_HELP
static const char *SpacesInPath = "<e>% Too many arguments.\r\n% If your path contains spaces, please enter spaces as \"*\":\r\n% Examples: \"cd Path*With*Spaces\",  \"rm /ffat/Program*Files\"</>\r\n";
static const char *MultipleEntries = "<2>% Processing multiple paths.\r\n% Not what you want? Use asteriks (*) instead of spaces in the path</>\r\n";
static const char *VarOops = "<e>% Oops :-(\r\n"
            "% No registered variables to play with</>\r\n"
            "% <2>Try this:\r\n"
            "%  <i>1. Add include \"extra/espshell.h\" to your sketch</>\r\n"
            "%  <i>2. Use \"convar_add()\" macro to register your variables</>\r\n"
            "%\r\n"
            "% <2>Once registered, variables can be manipulated by the \"var\" command\r\n"
            "% while your sketch is running. More is in \"docs/Commands.txt\"</>\r\n";
#endif


// Context: an user-defined value (a number) which is set by change_command_directory()
// when switching to new command subtree. This is how command "uart 1" passes its argument 
// (the number "1") to the subtree commands like "write" or "read". Used to store: 
// sequence number, uart,i2c interface number, probably something else
static unsigned int Context = 0;

// Currently used prompt
static const char *prompt = PROMPT;

static TaskHandle_t shell_task = 0;  // Main espshell task ID
static int shell_core = 0;           // CPU core number ESPShell is running on. For single core systems it is always 0

// Tokenized user input:
// argc/argv are amount of tokens and pointers to tokens respectively.
// userinput is the raw user input with some zeros inserted by tokenizer.
//
typedef struct {
  int    ref;       // reference counter. normally 1 but async commands can increase it
  int    argc;      // number of tokens
  char **argv;      // tokenized input string (array of pointers to various locations withn userinput)
  char  *userinput; //original input string with '\0's inserted by tokenizer
} argcargv_t;

// Mutex to protect reference counters of argcargv_t structure.
// Yes it is global single lock
static xSemaphoreHandle argv_mux = NULL;

// User input which is currently processed. Normally command handlers have their hands on argv and argc
// but asyn commands do not. This one is used by async tasks to get argc/argv values
static argcargv_t *aa_current = NULL;

// Increase refcounter on argcargv structure.
static void userinput_ref(argcargv_t *a) {

  if (xSemaphoreTake(argv_mux, portMAX_DELAY)) {
    if (a)
      a->ref++;
    xSemaphoreGive(argv_mux);
  }
}

// Decrease refcounter
// When refcounter hits zero the whole argcargv gets freed
//
static void userinput_unref(argcargv_t *a) {

  if (xSemaphoreTake(argv_mux, portMAX_DELAY)) {
    if (a) {
      if (a->ref < 1)  //must not happen
        abort();
      a->ref--;
      // ref dropped to zero: delete everything
      if (a->ref == 0) {
        if (a->argv)
          q_free(a->argv);
        if (a->userinput)
          q_free(a->userinput);
        q_free(a);
      }
    }
    xSemaphoreGive(argv_mux);
  }
}

// Split user input string to tokens.
// Returns NULL if string is empty or there were memory allocation errors.
// Returns pointer to allocated argcargv_t structure on success, which contains tokenized user input.
// NOTE: ** Structure must be freed after use via userinput_unref() **;
//
static argcargv_t *userinput_tokenize(char *userinput) {
  argcargv_t *a = NULL;
  if (userinput && *userinput) {
    if ((a = (argcargv_t *)q_malloc(sizeof(argcargv_t),MEM_ARGCARGV)) != NULL) {
      a->argv = NULL;
      a->argc = argify((unsigned char *)userinput, (unsigned char ***)&(a->argv));
      if (a->argc > 0) {
        a->userinput = userinput;
        a->ref = 1;
      } else {
        if (a->argv)
          q_free(a->argv);
        q_free(a);
        //q_free(userinput);
        a = NULL;
      }
    }
  }
  return a;
}

// check if *this* task (a caller) is executed as separate (background) task
// or it is executed in context of ESPShell
//
static INLINE int is_foreground_task() {
  return shell_task == xTaskGetCurrentTaskHandle();
}


// Upcoming IDF has this function renamed.
// TODO: use arduino core version or IDF version number to find out which
//       is to use
#if 0
#define esp_gpio_is_pin_reserved esp_gpio_pin_reserved
#endif

// this is not in .h files of IDF so declare it here:
// check if ESP32 pin is **reserved**. Pins used internally for external ram or flash memory access
// are ** reserved **.
#ifdef __cplusplus
extern "C" bool esp_gpio_is_pin_reserved(unsigned int gpio);  //TODO: is it unsigned char arg?
#else
extern bool esp_gpio_is_pin_reserved(unsigned int gpio);
#endif

//TAG:util

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
// TODO: check all bytes, not just 2
static bool ishex(const char *p) {
  if (p && *p) {
    if (p[0] == '0' && p[1] == 'x')
      p += 2;
    while(*p != '\0') {
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
  if (f >= '0' && f <= '9') f = f - '0'; else 
  if (f >= 'a' && f <= 'f') f = f - 'a' + 10; else return 0;

  //convert second hex character to decimal
  if (l >= '0' && l <= '9') l = l - '0'; else 
  if (l >= 'a' && l <= 'f') l = l - 'a' + 10; else return 0;

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
    if (*p == '0' || *p == '1') one = *p - '0';  else return 0;
    value <<= 1;
    value |= one;
    p++;
  }
  return value;
}

// q_atol() : better version of atol()
// 1. Accepts decimal, hex or binary numbers
// 2. If conversion fails (bad symbols in string, empty string etc) the
//    "def" value is returned
//
// TAG:atol
static unsigned int q_atol(const char *p, unsigned int def) {
  if (p && *p) {
    if (isnum(p))                // decimal number?
      def = atol(p);
    else
    if (p[0] == '0') {         // leading "0" : either hexadecimal, binary or octal number
      if (p[1] == 'x') {       // hexadecimal
        if (ishex(p))
          def = hex2uint32(p);
      } else
      if (p[1] == 'b')          // binary (TODO: isbin())
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



// strcmp() which deoes partial match. It us used
// to match commands and parameters which are incomplete
//
// partial - string which expected to be incomplete
// full    - full string
// q_strcmp("seq","sequence") == 0
// q_strcmp("sequence","seq") == 1
//
static int q_strcmp(const char *partial, const char *full) {

  int plen = strlen(partial);

  if (plen > strlen(full))
    return 1;
  return strncmp(partial, full, plen);
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
// TODO: remake to either use non-static buffer OR use sync objects
//
static int __printfv(const char *format, va_list arg) {

  static char buf[128 + 1]; // TODO: Mystery#2. Crashes without /static/. 
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
    if ((temp = (char *)q_malloc(len + 1, MEM_QPRINTF)) == NULL)
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
    
    while(*pp) {
      if ((p = q_findchar(pp,'<')) == NULL)
        return console_write_bytes(pp,strlen(pp));
      if (p[1] && p[2] == '>') {
        ins = NULL;
        if (Color)
          switch(p[1]) {
            case 'i': ins = esc_i; break;
            case 'w': ins = esc_w; break;
            case 'e': ins = esc_e; break;
            case '/': ins = esc_n; break;
            case 'r': ins = esc_r; break;
            case '2': ins = esc_2; break;
            case '1': ins = esc_1; break;
            case '3': ins = esc_3; break;
            case 'b': ins = esc_b; break;
            
          }
        len += console_write_bytes(pp,p - pp);
        if (ins)
          len += console_write_bytes(ins,strlen(ins));
        pp = p + 3;
      } else {
        len += console_write_bytes(pp,p - pp + 1);
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
static void q_printhex(const unsigned char *p, unsigned int len) {

  if (!p || !len)
    return;

  if (len < 16) {
    // data array is too small. just do simple output
    do {
      q_printf("%02x ", *p++);
    } while (--len);
    q_print(CRLF);
    return;
  }


  char ascii[16 + 1];
  unsigned int space = 1;

  q_print("       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F  |0123456789ABCDEF\r\n");
  q_print("----------------------------------------------------------+----------------\r\n");

  for (unsigned int i = 0, j = 0; i < len; i++) {
    // add offset at the beginning of every line. it doesnt hurt to have it.
    // and it is useful when exploring eeprom contens
    if (!j)
      q_printf("%04x: ", i);

    //print hex byte value
    q_printf("%02x ", p[i]);

    // add an extra space after each 4 bytes printed
    if ((space++ & 3) == 0)
      q_print(" ");

    //add printed byte to ascii representation
    //dont print anything with codes less than 32
    ascii[j++] = (p[i] < ' ') ? '.' : p[i];

    // one complete line could be printed:
    // we had 16 bytes or we reached end of the buffer
    if ((j > 15) || (i + 1) >= len) {

      // end of buffer but less than 16 bytes:
      // pad line with spaces
      if (j < 16) {
        unsigned char spaces = (16 - j) * 3 + (j <= 4 ? 3 : (j <= 8 ? 2 : (j <= 12 ? 1 : 0)));  // fully agreed :-|
        char tmp[spaces + 1];
        memset(tmp, ' ', spaces);
        tmp[spaces] = '\0';
        q_print(tmp);
      }

      // print a separator and the same line but in ascii form
      q_print("|");
      ascii[j] = '\0';
      q_print(ascii);
      q_print(CRLF);
      j = 0;
    }
  }
}

#define ESPSHELL_MAX_INPUT_LENGTH 500

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
            case '\\': p++;  c = '\\';  break;
            case 'n':  p++;  c = '\n';  break;
            case 'r':  p++;  c = '\r';  break;
            case 't':  p++;  c = '\t';  break;
            case 'e':  p++;  c = '\e';  break;
            case 'v':  p++;  c = '\v';  break;
            case 'b':  p++;  c = '\b';  break;
            default:
              // Known issue (TODO:)
              // if user inputs \0xXY numbers then such numbers will pass ishex() validation,
              // will be read correctly by hex2uint8(), however /p/ will be advanced by 1 or 2, not 3 or 4
              if (ishex2(p)) {
                c = hex2uint8(p);
                p++;
                if (*p) p++;
              }
              else {
                // unknown escape sequence
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
      // input line length limiting
      if (size > 500)
        break;
    } while (i < argc);
  }
  return size;
}


// Console Variables.
//
// User sketch can register global or static variables to be accessible
// from ESPShell. Once registered, variables can be manipulated by
// "var" command. See "extra/espshell.h" for convar_add() definition

struct convar {
  struct convar *next;
  const char *name;
  void *ptr;
  unsigned int isf  : 1;
  unsigned int size : 31;
};

static struct convar *var_head = NULL;

// Register new sketch variable. 
// Memory allocated for variable is never free()'d. This function is not
// supposed to be called directly: unstead a macro from espshell.h must be used (convar_add())
//
// /name/ - variable name
// /ptr/  - pointer to the variable
// /size/ - variable size in bytes
//
void espshell_varadd(const char *name, void *ptr, int size, bool isf) {

  struct convar *var;

  if (size != 1 && size != 2 && size != 4)
    return;

  if ((var = (struct convar *)q_malloc(sizeof(struct convar),MEM_VAR)) != NULL) {
    var->next = var_head;
    var->name = name;
    var->ptr = ptr;
    var->size = size;
    var->isf = isf ? 1 : 0;
    var_head = var;
  }
}

// get registered variable value & length
//
static int convar_get(const char *name, void *value, char **fullname, bool *isf) {

  struct convar *var = var_head;
  while (var) {
    if (!q_strcmp(name,var->name)) {
      memcpy(value, var->ptr, var->size);
      if (fullname)
        *fullname = (char *)var->name;
      if (isf)
        *isf = var->isf;
      return var->size;
    }
    var = var->next;
  }
  return 0;
}

// set registered variable value
//
static int convar_set(const char *name, void *value) {
  struct convar *var = var_head;
  while (var) {
    if (!strcmp(var->name, name)) {
      memcpy(var->ptr, value, var->size);
      return var->size;
    }
    var = var->next;
  }
  return 0;
}




// version of delay() which can be interrupted by user input (terminal
// keypress) for delays longer than 5 seconds.
//
// for delays shorter than 5 seconds fallbacks to normal(delay)
//
// `duration` - delay time in milliseconds
//  returns duration if ok, <duration if was interrupted
#define TOO_LONG 4999
#define DELAY_POLL 250


//Detects if ANY key is pressed in serial terminal
//or any character was sent in Arduino IDE Serial Monitor.
// TODO: rewrite to use NotifyDelay only and make anykey_pressed sending notifications? Then we don't need 250ms polling thing
static bool anykey_pressed() {

  int av;
  if ((av = console_available()) > 0) {
    // read & discard a keypress
    unsigned char c;
    console_read_bytes(&c, 1, 0);
    return true;
  }
  return false;
}


static unsigned int delay_interruptible(unsigned int duration) {

  // if duration is longer than 4999ms split it in 250ms
  // intervals and check for a keypress or task "kill" ntofication from the "kill" command
  // in between
  unsigned int delayed = 0;
  if (duration > TOO_LONG) {
    while (duration >= DELAY_POLL) {
      duration -= DELAY_POLL;
      delayed += DELAY_POLL;

      if (xTaskNotifyWait(0, 0xffffffff, NULL, pdMS_TO_TICKS(DELAY_POLL)) == pdPASS)
        return delayed;

      if (anykey_pressed())
        return delayed;
    }
  }
  if (duration) {
    unsigned int now = millis();
    if (xTaskNotifyWait(0, 0xffffffff, NULL, pdMS_TO_TICKS(duration)) == pdPASS)
      duration = millis() - now;  // calculate how long we were in waiting state before notification was received
    delayed += duration;
  }
  return delayed;
}


// TAG:keywords
//
// Shell command.
struct keywords_t {
  const char *cmd;                   // Command keyword ex.: "pin"
  int (*cb)(int argc, char **argv);  // Callback to call (one of cmd_xxx functions)
  int argc;                          // Number of arguments required. Negative value means "any"
  const char *help;                  // Help text displayed on "? command"
  const char *brief;                 // Brief text displayed on "?". NULL means "use help text, not brief"
};

// KEYWORDS_BEGIN - common commands inserted in every command tree at the beginning
#if WITH_HELP
#define HELP(X) X
#define KEYWORDS_BEGIN { "?", cmd_question, -1, "% \"?\" - Show the list of available commands\r\n% \"<2>? comm</>\" - Get help on command \"<2>comm</>\"\r\n% \"<2>? keys</>\" - Get information on terminal keys used by ESPShell", "Commands list & help" },
#else
#define HELP(X) ""
#define KEYWORDS_BEGIN
#endif

// KEYWORDS_END - common commands inserted at the end of every command tree
#define KEYWORDS_END { "exit", cmd_exit, -1, "Exit", NULL }, { NULL, NULL, 0, NULL, NULL }

// Command flag to mark any keyword as "hidden" i.e. not displayable by "?"
// command.
#define HIDDEN_KEYWORD NULL, NULL

// Number of arguments accepted by command:
#define MANY_ARGS -1
#define NO_ARGS    0


#if WITH_HELP
static int cmd_question(int, char **);
#endif
static int cmd_pin(int, char **);

static int cmd_async(int, char **);  // "pin&" , "count&", ..

static int cmd_cpu(int, char **);
static int cmd_cpu_freq(int, char **);
static int cmd_uptime(int, char **);
static int cmd_mem(int, char **);
static int cmd_mem_read(int, char **);
static int NORETURN cmd_reload(int, char **);
static int cmd_nap(int, char **);

static int cmd_i2c_if(int, char **);
static int cmd_i2c_clock(int, char **);
static int cmd_i2c(int, char **);

static int cmd_uart_if(int, char **);
static int cmd_uart_baud(int, char **);
static int cmd_uart(int, char **);
#if WITH_FS
static int cmd_files_if(int, char **);
static int cmd_files_mount0(int, char **);
static int cmd_files_mount(int, char **);
static int cmd_files_unmount(int, char **);
static int cmd_files_cd(int, char **);
static int cmd_files_ls(int, char **);
static int cmd_files_rm(int, char **);
static int cmd_files_mv(int, char **);
static int cmd_files_cp(int, char **);
static int cmd_files_write(int, char **);
static int cmd_files_append(int, char **);
static int cmd_files_insdel(int, char **);
static int cmd_files_mkdir(int, char **);
static int cmd_files_cat(int, char **);
static int cmd_files_touch(int, char **);
static int cmd_files_format(int, char **);
#endif  //WITH_FS
static int cmd_tty(int, char **);
static int cmd_echo(int, char **);

static int cmd_suspend(int, char **);
static int cmd_resume(int, char **);
static int cmd_kill(int, char **argv);

static int cmd_pwm(int, char **);
static int cmd_count(int, char **);

static int cmd_seq_if(int, char **);
static int cmd_seq_eot(int argc, char **argv);
static int cmd_seq_modulation(int argc, char **argv);
static int cmd_seq_zeroone(int argc, char **argv);
static int cmd_seq_tick(int argc, char **argv);
static int cmd_seq_bits(int argc, char **argv);
static int cmd_seq_levels(int argc, char **argv);
static int cmd_seq_show(int argc, char **argv);

static int cmd_var(int, char **);
static int cmd_var_show(int, char **);

static int cmd_show(int, char **);

static int cmd_exit(int, char **);
static int cmd_history(int, char **);

// suport for user-defined commands
#ifdef EXTERNAL_PROTOTYPES
#  include EXTERNAL_PROTOTYPES
#endif

// Custom uart commands (uart subderictory or uart command tree)
// Those displayed after executing "uart 2" (or any other uart interface)
// TAG:keywords_uart
//
static const struct keywords_t keywords_uart[] = {

  KEYWORDS_BEGIN

  { "up", cmd_uart, 3, 
  HELP("% \"up RX TX BAUD\"\r\n"
       "%\r\n"
       "% Initialize uart interface X on pins RX/TX,baudrate BAUD, 8N1 mode\r\n"
       "% Ex.: up 18 19 115200 - Setup uart on pins rx=18, tx=19, at speed 115200"),"Initialize uart (pins/speed)" },

  { "baud", cmd_uart_baud, 1, 
  HELP("% \"baud SPEED\"\r\n"
       "%\r\n"
       "% Set speed for the uart (uart must be initialized)\r\n"
       "% Ex.: baud 115200 - Set uart baud rate to 115200"), "Set baudrate" },

  { "down", cmd_uart, NO_ARGS, 
  HELP("% \"down\"\r\n"
       "%\r\n"
       "% Shutdown interface, detach pins"), "Shutdown" },

  { "read", cmd_uart, NO_ARGS,
  HELP("% \"read\"\r\n"
       "%\r\n"
       "% Read bytes (available) from uart interface X"), "Read data from UART" },

  { "tap", cmd_uart, NO_ARGS, 
  HELP("% \"tap\\r\n"
       "%\r\n"
       "% Bridge the UART IO directly to/from shell\r\n"
       "% User input will be forwarded to uart X;\r\n"
       "% Anything UART X sends back will be forwarded to the user"), "Talk to device connected" },

  { "write", cmd_uart, MANY_ARGS, 
  HELP("% \"write TEXT\"\r\n"
       "%\r\n"
       "% Send an ascii/hex string(s) to UART X\r\n"
       "% TEXT can include spaces, escape sequences: \\n, \\r, \\\\, \\t and \r\n"
       "% hexadecimal numbers \\AB (A and B are hexadecimal digits)\r\n"
       "%\r\n"
       "% Ex.: \"write ATI\\n\\rMixed\\20Text and \\20\\21\\ff\""), "Send bytes over this UART" },

  KEYWORDS_END
};

//TAG:keywords_iic
//TAG_keywords_i2c
//i2c subderictory keywords list
//cmd_exit() and cmd_i2c_if are responsible for selecting keywords list
//to use
static const struct keywords_t keywords_i2c[] = {

  KEYWORDS_BEGIN

  { "up", cmd_i2c, 3, 
  HELP("% \"up SDA SCL CLOCK\"\r\n"
       "%\r\n"
       "% Initialize I2C interface X, use pins SDA/SCL, clock rate CLOCK\r\n"
       "% Ex.: up 21 22 100000 - enable i2c at pins sda=21, scl=22, 100kHz clock"), "initialize interface (pins and speed)" },

  { "clock", cmd_i2c_clock, 1, 
  HELP("% \"clock SPEED\"\r\n"
       "%\r\n"
       "% Set I2C master clock (i2c must be initialized)\r\n"
       "% Ex.: clock 100000 - Set i2c clock to 100kHz"), "Set clock" },


  { "read", cmd_i2c, 2, 
  HELP("% \"read ADDR SIZE\"\r\n"
       "%\r\n"
       "% Read SIZE bytes from a device at address ADDR\r\n"
       "% Ex.: read 0x68 7 - read 7 bytes from device address 0x68"), "Read data from an I2C device" },

  { "down", cmd_i2c, NO_ARGS, 
  HELP("% \"down\"\r\n"
       "%\r\n"
       "% Shutdown I2C interface X"), "Shutdown i2c interface" },

  { "scan", cmd_i2c, NO_ARGS,
  HELP("% \"scan\"\r\n"
       "%\r\n"
       "% Scan I2C bus X for devices. Interface must be initialized!"), "Scan i2c bus for devices" },

  { "write", cmd_i2c, MANY_ARGS,
  HELP("% \"write ADDR D1 [D2 ... Dn]\"\r\n"
       "%\r\n"
       "% Write bytes D1..Dn (hex values) to address ADDR on I2C bus X\r\n"
       "% Ex.: write 0x57 0 0xff - write 2 bytes to address 0x57: 0 and 255"), "Send bytes to the device" },

  KEYWORDS_END
};

//TAG:keywords_seq
//'sequence' subderictory keywords list
static const struct keywords_t keywords_sequence[] = {

  KEYWORDS_BEGIN

  { "eot", cmd_seq_eot, 1, 
  HELP("% \"eot high|low\"\r\n"
       "%\r\n"
       "% End of transmission: pull the line high or low at the\r\n"
       "% end of a sequence. Default is \"low\""), "End-of-Transmission pin state" },

  { "tick", cmd_seq_tick, 1,
  HELP("% \"tick TIME\"\r\n"
       "%\r\n"
       "% Set the sequence tick time: defines a resolution of a pulse sequence.\r\n"
       "% Expressed in microseconds, can be anything between 0.0125 and 3.2\r\n"
       "% Ex.: tick 0.1 - set resolution to 0.1 microsecond"),  "Set resolution" },

  { "zero", cmd_seq_zeroone, 2,
  HELP("% \"zero LEVEL/DURATION [LEVEL2/DURATION2]\"\r\n"
       "%\r\n"
       "% Define a logic \"0\"\r\n"
       "% Ex.: zero 0/50      - 0 is a level: LOW for 50 ticks\r\n"
       "% Ex.: zero 1/50 0/20 - 0 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks"), "Define a zero" },

  { "zero", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

  { "one", cmd_seq_zeroone, 2, 
  HELP("% \"one LEVEL/DURATION [LEVEL2/DURATION2]\"\r\n"
       "%\r\n"
       "% Define a logic \"1\"\r\n"
       "% Ex.: one 1/50       - 1 is a level: HIGH for 50 ticks\r\n"
       "% Ex.: one 1/50 0/20  - 1 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks"), "Define an one" },

  { "one", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

  { "bits", cmd_seq_bits, 1, 
  HELP("% \"bits STRING\"\r\n"
       "%\r\n"
       "% A bit pattern to be used as a sequence. STRING must contain only 0s and 1s\r\n"
       "% Overrides previously set \"levels\" command\r\n"
       "% See commands \"one\" and \"zero\" to define \"1\" and \"0\"\r\n"
       "%\r\n"
       "% Ex.: bits 11101000010111100  - 17 bit sequence"),
    "Set pattern to transmit" },

  { "levels", cmd_seq_levels, MANY_ARGS, 
  HELP("% \"levels L/D L/D ... L/D\"\r\n"
       "%\r\n"
       "% A bit pattern to be used as a sequnce. L is either 1 or 0 and \r\n"
       "% D is the duration measured in ticks [0..32767] \r\n"
       "% Overrides previously set \"bits\" command\r\n"
       "%\r\n"
       "% Ex.: levels 1/50 0/20 1/100 0/500  - HIGH 50 ticks, LOW 20, HIGH 100 and 0 for 500 ticks\r\n"
       "% Ex.: levels 1/32767 1/17233 0/32767 0/7233 - HIGH for 50000 ticks, LOW for 40000 ticks"),  "Set levels to transmit" },

  { "modulation", cmd_seq_modulation, 3,
  HELP("% \"modulation FREQ [DUTY [low|high]]\"\r\n"
       "%\r\n"
       "% Enables/disables an output signal modulation with frequency FREQ\r\n"
       "% Optional parameters are: DUTY (from 0 to 1) and LEVEL (either high or low)\r\n"
       "%\r\n"
       "% Ex.: modulation 100         - modulate all 1s with 100Hz, 50% duty cycle\r\n"
       "% Ex.: modulation 100 0.3 low - modulate all 0s with 100Hz, 30% duty cycle\r\n"
       "% Ex.: modulation 0           - disable modulation\r\n"),  "Enable/disable modulation" },

  { "modulation", cmd_seq_modulation, 2, HIDDEN_KEYWORD },
  { "modulation", cmd_seq_modulation, 1, HIDDEN_KEYWORD },

  { "show", cmd_seq_show, 0, "Show sequence", NULL },

  KEYWORDS_END
};

#if WITH_FS
// Filesystem commands. this commands subdirectory is enabled
// with "file" command /cmd_files_if()/
//TAG:keywords_files
//
static const struct keywords_t keywords_files[] = {

  KEYWORDS_BEGIN

  { "mount", cmd_files_mount, 2, 
  HELP("% \"mount LABEL [/MOUNT_POINT]\"\r\n"
       "%\r\n"
       "% Mount a filesystem located on built-in SPI FLASH\r\n"
       "%\r\n"
       "% LABEL        - SPI FLASH partition label\r\n"
       "% /MOUNT_POINT - A path, starting with \"/\" where filesystem will be mounted.\r\n"
       "%\r\n"
       "% Ex.: mount ffat /ffat - mount partition \"ffat\" at directory \"/ffat\""),"Mount partition/Show partition table" },
  
  { "mount", cmd_files_mount0, NO_ARGS, 
  HELP("% \"mount\"\r\n"
       "%\r\n"
       "% Command \"mount\" **without arguments** displays information about partitions\r\n"
       "% and mounted file systems (mount point, FS type, total/used counters)"), NULL },

  { "mount", cmd_files_mount, 1, HIDDEN_KEYWORD },


  { "unmount", cmd_files_unmount, 1, 
  HELP("% \"unmount /MOUNT_POINT\"\r\n"
       "%\r\n"
       "% Unmount a file system\r\n"),"Unmount partition" },

  { "unmount", cmd_files_unmount, NO_ARGS, HIDDEN_KEYWORD },
  { "umount", cmd_files_unmount, 1, HIDDEN_KEYWORD }, // for unix folks
  { "umount", cmd_files_unmount, NO_ARGS, HIDDEN_KEYWORD }, // for unix folks

  { "ls", cmd_files_ls, 1, 
  HELP("% \"ls [PATH]\"\r\n"
       "%\r\n"
       "% Show directory listing at PATH given\r\n"
       "% If PATH is omitted then current directory list is shown"),"List directory" },

  { "ls", cmd_files_ls, 0, HIDDEN_KEYWORD },

  { "cd", cmd_files_cd, MANY_ARGS, 
  HELP("% \"cd [PATH|..]\"\r\n"
       "%\r\n"
       "% Change current directory. Paths having .. (i.e \"../dir/\") are not supported\r\n"
       "%\r\n"
       "% Ex.: \"cd\"            - change current directory to filesystem's root\r\n"
       "% Ex.: \"cd ..\"         - go one directory up\r\n"
       "% Ex.: \"cd /ffat/test/  - change to \"/ffat/test/\"\r\n"
       "% Ex.: \"cd test2/test3/ - change to \"/ffat/test/test2/test3\"\r\n"),"Change directory" },
  
  { "rm", cmd_files_rm, MANY_ARGS, 
  HELP("% \"rm PATH1 [PATH2 PATH3 ... PATHn]\"\r\n"
       "%\r\n"
       "% Remove files or a directories with files.\r\n"
       "% When removing directories: removed with files and subdirs"), "Delete files/dirs" },

  { "mv", cmd_files_mv, 2, 
  HELP("% \"mv SOURCE DESTINATION\\r\n"
       "%\r\n"
       "% Move or Rename file or directory SOURCE to DESTINATION\r\n"
       "%\r\n"
       "% Ex.: \"mv /ffat/dir1 /ffat/dir2\"             - rename directory \"dir1\" to \"dir2\"\r\n"
       "% Ex.: \"mv /ffat/fileA.txt /ffat/fileB.txt\"   - rename file \"fileA.txt\" to \"fileB.txt\"\r\n"
       "% Ex.: \"mv /ffat/dir1/file1 /ffat/dir2\"       - move file to directory\r\n"
       "% Ex.: \"mv /ffat/fileA.txt /spiffs/fileB.txt\" - move file between filesystems\r\n"), "Move/rename files and/or directories" },

  { "cp", cmd_files_cp, 2, 
  HELP("% \"cp SOURCE DESTINATION\\r\n"
       "%\r\n"
       "% Copy file SOURCE to file DESTINATION.\r\n"
       "% Files SOURCE and DESTINATION can be on different filesystems\r\n"
       "%\r\n"
       "% Ex.: \"cp /ffat/test.txt /ffat/test2.txt\"       - copy file to file\r\n"
       "% Ex.: \"cp /ffat/test.txt /ffat/dir/\"            - copy file to directory\r\n"
       "% Ex.: \"cp /ffat/dir_src /ffat/dir/\"             - copy directory to directory\r\n"
       "% Ex.: \"cp /spiffs/test.txt /ffat/dir/test2.txt\" - copy between filesystems\r\n"), "Copy files/dirs" },

  { "write", cmd_files_write, MANY_ARGS, 
  HELP("% \"write FILENAME [TEXT]\"\r\n"
       "%\r\n"
       "% Write an ascii/hex string(s) to file\r\n"
       "% TEXT can include spaces, escape sequences: \\n, \\r, \\\\, \\t and \r\n"
       "% hexadecimal numbers \\AB (A and B are hexadecimal digits)\r\n"
       "%\r\n"
       "% Ex.: \"write /ffat/test.txt \\n\\rMixed\\20Text and \\20\\21\\ff\""), "Write strings/bytes to the file" },

  { "append", cmd_files_append, MANY_ARGS, 
  HELP("% \"append FILENAME [TEXT]\"\r\n"
       "%\r\n"
       "% Append an ascii/hex string(s) to file\r\n"
       "% Escape sequences & ascii codes are accepted just as in \"write\" command\r\n"
       "%\r\n"
       "% Ex.: \"append /ffat/test.txt \\n\\rMixed\\20Text and \\20\\21\\ff\""),"Append strings/bytes to the file" },

  { "insert", cmd_files_insdel, MANY_ARGS, 
  HELP("% \"insert FILENAME LINE_NUM [TEXT]\"\r\n"
       "% Insert TEXT to file FILENAME before line LINE_NUM\r\n"
       "% \"\\n\" is appended to the string being inserted, \"\\r\" is not\r\n"
       "% Escape sequences & ascii codes accepted just as in \"write\" command\r\n"
       "% Lines are numbered starting from 0. Use \"cat\" command to find out line numbers\r\n"
       "%\r\n"
       "% Ex.: \"insert 0 /ffat/test.txt Hello World!\""), "Insert lines to text file" },

  { "delete", cmd_files_insdel, 3, 
  HELP("% \"delete FILENAME LINE_NUM [COUNT]\"\r\n"
       "% Delete line LINE_NUM from a text file FILENAME\r\n"
       "% Optionsl COUNT argument is the number of lines to remove (default is 1)"
       "% Lines are numbered starting from 1. Use \"cat -n\" command to find out line numbers\r\n"
       "%\r\n"
       "% Ex.: \"delete 10 /ffat/test.txt\" - remove line #10 from \"/ffat/test.txt\""), "Delete lines from a text file" },

  { "delete", cmd_files_insdel, 2, HIDDEN_KEYWORD },

  { "mkdir", cmd_files_mkdir, MANY_ARGS,
  HELP("% \"mkdir PATH1 [PATH2 PATH3 ... PATHn]\"\r\n"
       "%\r\n"
       "% Create empty directories PATH1 ... PATHn\r\n"),"Create directory" },

  { "cat", cmd_files_cat, MANY_ARGS, 
  HELP("% \"cat [-n|-b] PATH [START [COUNT]] [uart NUM]\"\r\n"
       "%\r\n"
       "% Display (or send by UART) a binary or text file PATH\r\n"
       "% -n : display line numbers\r\n"
       "% -b : file is binary (mutually exclusive with \"-n\" option)\r\n"
       "% PATH  : path to the file\r\n"
       "% START : text file line number (OR binary file offset if \"-b\" is used)\r\n"
       "% COUNT : number of lines to display (OR bytes for \"-b\" option)\r\n"
       "% NUM   : UART interface number to transmit file to\r\n"
       "%\r\n"
       "% Examples:\r\n"
       "% cat file              - display file \"file\"\r\n"
       "% cat -n file           - display file \"file\" + line numbers\r\n"
       "% cat file 34           - display text file starting from line 34 \r\n"
       "% cat file 900 10       - 10 lines, starting from line 900 \r\n"
       "% cat -b file           - display binary file (formatted output)\r\n"
       "% cat -b file 0x1234    - display binary file starting from offset 0x1234\r\n"
       "% cat -b file 999 0x400 - 999 bytes starting from offset 1024 of binary file\r\n"
       "% cat file uart 1       - transmit a text file over UART1, strip \"\\r\" if any\r\n"
       "% cat -b file uart 1    - transmit file over UART1 \"as-is\" byte by byte"),"Display/transmit text/binary file" },

  { "touch", cmd_files_touch, MANY_ARGS, 
  HELP("% \"touch PATH1 [PATH2 PATH3 ... PATHn]\"\r\n"
       "%\r\n"
       "% Ceate new files or \"touch\" existing\r\n"), "Create/touch files" },

  { "format", cmd_files_format, 1, 
  HELP("% \"format [LABEL]\"\r\n"
       "%\r\n"
       "% Format partition LABEL. If LABEL is omitted then current working\r\n"
       "% directory is used to determine partition label"), "Erase old & create new filesystem" },

  { "format", cmd_files_format, 0, HIDDEN_KEYWORD },

  KEYWORDS_END
};
#endif  //WITH_FS

// root directory commands
//TAG:keywords_main
static const struct keywords_t keywords_main[] = {

  KEYWORDS_BEGIN

  { "uptime", cmd_uptime, NO_ARGS, 
  HELP("% \"uptime\" - Shows time passed since last boot"), "System uptime" },

  // System commands
  { "cpu", cmd_cpu_freq, 1, 
  HELP("% \"cpu FREQ\" : Set CPU frequency to FREQ Mhz"), "Set/show CPU parameters" },

  { "cpu", cmd_cpu, NO_ARGS, 
  HELP("% \"cpu\" : Show CPUID and CPU/XTAL/APB frequencies"), NULL },

  { "suspend", cmd_suspend, NO_ARGS, 
  HELP("% \"suspend\" : Suspend main loop()\r\n"), "Suspend sketch execution" },

  { "resume", cmd_resume, NO_ARGS, 
  HELP("% \"resume\" : Resume main loop()\r\n"), "Resume sketch execution" },

  { "kill", cmd_kill, 1, 
  HELP("% \"kill TASK_ID\" : Stop and delete task TASK_ID\r\n% CAUTION: wrong id will crash whole system :(\r\n% For use with \"pin&\" and \"count&\" tasks only!"), "Kill tasks" },

  { "kill", cmd_kill, 2, HIDDEN_KEYWORD },  //undocumented "kill TASK_ID terminate"

  { "reload", cmd_reload, NO_ARGS, 
  HELP("% \"reload\" - Restarts CPU"), "Reset CPU" },

  { "mem", cmd_mem, NO_ARGS, 
  HELP("% \"mem\"\r\n% Shows memory usage info & availability, no arguments"),"Memory commands" },

  { "mem", cmd_mem_read, 2, 
  HELP("% \"mem ADDR [LENGTH]\"\r\n"
       "% Display LENGTH bytes of memory starting from address ADDR\r\n"
       "% Address is either decimal or hex (with or without leading \"0x\")\r\n%\r\n"
       "% LENGTH is optional and its default value is 256 bytes. Can be decimal or hex\r\n"
       "% Ex.: mem 40078000 100 : display 100 bytes starting from address 40078000"), NULL },

  { "mem", cmd_mem_read, 1, HIDDEN_KEYWORD },

  { "nap", cmd_nap, 1, 
  HELP("% \"nap SEC\"\r\n%\r\n% Put the CPU into light sleep mode for SEC seconds."), "CPU sleep" },

  { "nap", cmd_nap, NO_ARGS, 
  HELP("% \"nap\"\r\n%\r\n% Put the CPU into light sleep mode, wakeup by console"), NULL },

  // Interfaces (UART,I2C, RMT, FileSystem..)
  { "iic", cmd_i2c_if, 1, 
  HELP("% \"iic X\" \r\n%\r\n"
       "% Enter I2C interface X configuration mode \r\n"
       "% Ex.: iic 0 - configure/use interface I2C 0"), "I2C commands" },

  { "uart", cmd_uart_if, 1,
  HELP("% \"uart X\"\r\n"
       "%\r\n"
       "% Enter UART interface X configuration mode\r\n"
       "% Ex.: uart 1 - configure/use interface UART 1"), "UART commands" },

  { "sequence", cmd_seq_if, 1, 
  HELP("% \"sequence X\"\r\n"
       "%\r\n"
       "% Create/configure a sequence\r\n"
       "% Ex.: sequence 0 - configure Sequence0"),"Sequence configuration" },

#if WITH_FS
  { "files", cmd_files_if, NO_ARGS, 
  HELP("% \"files\"\r\n"
       "%\r\n"
       "% Enter files & file system operations mode"), "File system access" },
#endif

  // Show funcions (more will be added)
  { "show", cmd_show, 2, 
  HELP("% \"show sequence X\" - display sequence X\r\n"), "Display information" },

  // Shell input/output settings
  { "tty", cmd_tty, 1,
  HELP("% \"tty X\" Use uart X for command line interface"), "IO redirect" },

  { "echo", cmd_echo, 1,
  HELP("% \"echo on|off|silent\" Echo user input on/off (default is on)"), "Enable/Disable user input echo" },

  { "echo", cmd_echo, NO_ARGS, HIDDEN_KEYWORD },  //hidden command, displays echo status

  // Generic pin commands
  { "pin", cmd_pin, 1, 
  HELP("% \"pin X\" - Show pin X configuration.\r\n% Ex.: \"pin 2\" - show GPIO2 information"), "Pins (GPIO) commands" },

  { "pin", cmd_pin, MANY_ARGS,
  HELP("% \"pin X (hold|release|up|down|out|in|open|high|low|save|load|read|aread|delay|loop|pwm|seq)...\"\r\n"
       "% Multifunction command which can:\r\n"
       "%  1. Set/Save/Load pin configuration and settings\r\n"
       "%  2. Enable/disable PWM and pattern generation on pin\r\n"
       "%  3. Set/read digital and/or analog pin values\r\n"
       "%\r\n"
       "% Multiple arguments must be separated with spaces, see examples below:\r\n%\r\n"
       "% Ex.: pin 1 read aread         -pin1: read digital and then analog values\r\n"
       "% Ex.: pin 1 out up             -pin1 is OUTPUT with PULLUP\r\n"
       "% Ex.: pin 1 save               -save pin state\r\n"
       "% Ex.: pin 1 high               -pin1 set to logic \"1\"\r\n"
       "% Ex.: pin 1 high delay 100 low -set pin1 to logic \"1\", after 100ms to \"0\"\r\n"
       "% Ex.: pin 1 pwm 2000 0.3       -set 5kHz, 30% duty square wave output\r\n"
       "% Ex.: pin 1 pwm 0 0            -disable generation\r\n"
       "% Ex.: pin 1 high delay 500 low delay 500 loop 10 - Blink a led 10 times\r\n%\r\n"
       "% Use \"<i>pin&</>\" instead of \"<i>pin</i>\" to execute in background\r\n"
       "% (see \"docs/Pin_Commands.txt\" for more details & examples)\r\n"),  NULL },

  // "pin&"" async (background) "pin" command
  { "pin&", cmd_async, MANY_ARGS, HIDDEN_KEYWORD },

  // PWM generation
  { "pwm", cmd_pwm, 3,
  HELP("% \"pwm X [FREQ [DUTY]]\"\r\n"
       "%\r\n"
       "% Start PWM generator on pin X, frequency FREQ Hz and duty cycle of DUTY\r\n"
       "% Maximum frequency is 312000Hz, and DUTY is in range [0..1] with 0.123 being\r\n"
       "% a 12.3% duty cycle\r\n"
       "%\r\n"
       "% DUTY is optional and its default value is 50% (if not specified) and\r\n"
       "% its resolution is 0.005 (0.5%)"
       "%\r\n"
       "% Ex.: pwm 2 1000     - enable PWM of 1kHz, 50% duty on pin 2\r\n"
       "% Ex.: pwm 2          - disable PWM on pin 2\r\n"
       "% Ex.: pwm 2 6400 0.1 - enable PWM of 6.4kHz, duty cycle of 10% on pin 2\r\n"), "PWM output" },

  { "pwm", cmd_pwm, 2, HIDDEN_KEYWORD },
  { "pwm", cmd_pwm, 1, HIDDEN_KEYWORD },

  // Pulse counting
  { "count", cmd_count, 3,
  HELP("% \"count PIN [DURATION [neg|pos|both]]\"\r\n%\r\n"
       "% Count pulses (negative/positive edge or both) on pin PIN within DURATION time\r\n"
       "% Time is measured in milliseconds, optional. Default is 1000\r\n"
       "% Pulse edge type is optional. Default is \"pos\"\r\n"
       "%\r\n"
       "% Ex.: \"count 4\"           - count positive edges on pin 4 for 1000ms\r\n"
       "% Ex.: \"count 4 2000\"      - count pulses (falling edge) on pin 4 for 2 sec.\r\n"
       "% Ex.: \"count 4 2000 both\" - count pulses (falling and rising edge) on pin 4 for 2 sec.\r\n%\r\n"
       "% Use \"<i>count&</>\" instead of \"<i>count</>\" to execute in background\r\n"), "Pulse counter" },

  { "count", cmd_count, 2, HIDDEN_KEYWORD },   //hidden "count" with 2 args
  { "count", cmd_count, 1, HIDDEN_KEYWORD },   //hidden with 1 arg
  { "count&", cmd_async, 3, HIDDEN_KEYWORD },  //hidden "count&" with 3 args
  { "count&", cmd_async, 2, HIDDEN_KEYWORD },  //hidden "count&" with 2 args
  { "count&", cmd_async, 1, HIDDEN_KEYWORD },  //hidden "count&" with 1 arg



  { "var", cmd_var, 2,
  HELP("% \"var [VARIABLE_NAME] [NUMBER]\"\r\n%\r\n"
       "% Set/display sketch variable \r\n"
       "% VARIABLE_NAME is the variable name, optional argument\r\n"
       "% NUMBER can be integer or float point values, positive or negative, optional argument\r\n"
       "%\r\n"
       "% Ex.: \"var\"             - List all registered sketch variables\r\n"
       "% Ex.: \"var button1\"     - Display current value of \"button1\" sketch variable\r\n"
       "% Ex.: \"var angle -12.3\" - Set sketch variable \"angle\" to \"-12.3\"\r\n"
       "% Ex.: \"var 1234\"        - Display a decimal number as hex, float, int etc.\r\n"
       "% Ex.: \"var 0x1234\"      - -- // hex // --\r\n"
       "% Ex.: \"var 01234\"       - -- // octal // --\r\n"
       "% Use prefix \"0x\" for hex, \"0\" for octal or \"0b\" for binary numbers"), "Sketch variables" },

  { "var", cmd_var_show, 1, HIDDEN_KEYWORD },
  { "var", cmd_var_show, NO_ARGS, HIDDEN_KEYWORD },

  
  { "history", cmd_history, 1, HIDDEN_KEYWORD },
  { "history", cmd_history, 0, HIDDEN_KEYWORD },

#ifdef EXTERNAL_KEYWORDS
#  include EXTERNAL_KEYWORDS
#endif

  KEYWORDS_END
};


//TAG:keywords
//current keywords list to use
static const struct keywords_t *keywords = keywords_main;



// Called by cmd_uart_if, cmd_i2c_if,cmd_seq_if, cam_settings and cmd_files_if to
// set new command list (command directory) and displays user supplied text
// /Context/ - arbitrary number which will be stored
// /dir/     - new keywords list (one of keywords_main[], keywords_uart[] tc)
// /prom/    - prompt to use
// /text/    - text to be displayed when switching command directory
//
static void change_command_directory(unsigned int context, const struct keywords_t *dir, const char *prom, const char *text) {

  Context = context;
  keywords = dir;
  prompt = prom;
#if WITH_HELP
  q_printf("%% Entering %s mode. Ctrl+Z or \"exit\" to return\r\n", text);
  q_print("% Hint: Main commands are still avaiable (but not visible in \"?\" command list)\r\n");
#endif
}


// TAG:pins
// Structure used to save/load pin states by "pin X save"/"pin X load".
//
static struct {
  uint8_t flags;    // INPUT,PULLUP,...
  bool value;       // digital value
  uint16_t sig_out; 
  uint16_t fun_sel;
  int bus_type;  //periman bus type.
} Pins[SOC_GPIO_PIN_COUNT];

static bool pin_is_input_only_pin(int pin);

// same as digitalRead() but reads all pins no matter what
// exported (not static) to enable its use in user sketch
//
int digitalForceRead(int pin) {
  gpio_ll_input_enable(&GPIO, pin);
  return gpio_ll_get_level(&GPIO, pin) ? HIGH : LOW;
}

// same as digitalWrite() but bypasses periman so no init/deinit
// callbacks are called. pin bus type remain unchanged
//
void digitalForceWrite(int pin, unsigned char level) {
  gpio_ll_output_enable(&GPIO, pin);
  gpio_set_level((gpio_num_t)pin, level == HIGH ? 1 : 0);
}



// same as pinMode() but calls IDF directly bypassing
// PeriMan's pin deinit/init. As a result it allows flags manipulation on
// reserved pins without crashing & rebooting
//
// exported (non-static) to allow use in a sketch (by including "extra/espshell.h" in
// user sketch .ino file)
//
void pinMode2(unsigned int pin, unsigned int flags) {

  // set ARDUINO flags to the pin using ESP-IDF functions
  if ((flags & PULLUP) == PULLUP) gpio_ll_pullup_en(&GPIO, pin);
  else gpio_ll_pullup_dis(&GPIO, pin);
  if ((flags & PULLDOWN) == PULLDOWN) gpio_ll_pulldown_en(&GPIO, pin);
  else gpio_ll_pulldown_dis(&GPIO, pin);
  if ((flags & OPEN_DRAIN) == OPEN_DRAIN) gpio_ll_od_enable(&GPIO, pin);
  else gpio_ll_od_disable(&GPIO, pin);
  if ((flags & INPUT) == INPUT) gpio_ll_input_enable(&GPIO, pin);
  else gpio_ll_input_disable(&GPIO, pin);

  // not every esp32 gpio is capable of OUTPUT
  if ((flags & OUTPUT) == OUTPUT) {
    if (!pin_is_input_only_pin(pin))
      gpio_ll_output_enable(&GPIO, pin);
  } else
    gpio_ll_output_disable(&GPIO, pin);
}


// checks if pin (GPIO) number is in valid range.
// display a message if pin is out of range
static bool pin_exist(int pin) {

  uint64_t mask = ~SOC_GPIO_VALID_GPIO_MASK;

  // pin number is in range and is a valid GPIO number?
  if ((pin < SOC_GPIO_PIN_COUNT) && (((uint64_t)1 << pin) & SOC_GPIO_VALID_GPIO_MASK))
    return true;
  else {
    int informed = 0;
    // pin number is incorrect, display help
    q_printf("%% Available pin numbers are 0..%d", SOC_GPIO_PIN_COUNT - 1);

    if (mask)
      for (pin = 63; pin >=0; pin--)
        if (mask & ((uint64_t)1 << pin)) {
          mask &= ~((uint64_t)1 << pin);
          if (pin < SOC_GPIO_PIN_COUNT) {
            if (!informed) {
              informed = 1;
              q_print(", except pins: ");
            } else
              q_print(", ");
            q_printf("%s<w>%d</>",mask ? "" : "and ", pin);
          }
        }
    

    // the function is not in .h files.
    // moreover its name has changed in recent ESP IDF
    for (pin = informed = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
      if (esp_gpio_is_pin_reserved(pin))
        informed++;

    if (informed) {
      q_print("\r\n% Reserved pins (used internally):");
      for (pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
        if (esp_gpio_is_pin_reserved(pin)) {
          informed--;
          q_printf("%s<w>%d</>", informed ? ", " : " and ",pin);
        }
    }

    q_print(CRLF);
    return false;
  }
}

//TAG:sequences
//  -- RMT Sequences --
//
// Sequences: the data structure defining a sequence of pulses (levels) to
// be transmitted. Sequences are used to generate an user-defined LOW/HIGH
// pulses train on an arbitrary GPIO pin using ESP32's hardware RMT peri
//
// These are used by "sequence X" command (and by a sequence subdirectory
// commands as well) (see seq_...() functions and cmd_sequence_if() function)


struct sequence {

  float tick;                  // uSeconds.  1000000 = 1 second.   0.1 = 0.1uS
  float mod_duty;              // modulator duty
  unsigned int mod_freq : 30;  // modulator frequency
  unsigned int mod_high : 1;   // modulate "1"s or "0"s
  unsigned int eot : 1;        // end of transmission level
  int seq_len;                 // how many rmt_data_t items is in "seq"
  rmt_data_t *seq;             // array of rmt_data_t
  rmt_data_t alph[2];          // alphabet. representation of "0" and "1"
  // TODO: bytes, head, tail (NEC protocol support)
  char *bits;  // asciiz "100101101"
};

// sequences
static struct sequence sequences[SEQUENCES_NUM] = { 0 };

// calculate frequency from tick length
// 0.1uS = 10Mhz
unsigned long __attribute__((const)) seq_tick2freq(float tick_us) {

  return tick_us ? (unsigned long)((float)1000000 / tick_us) : 0;
}

// free memory buffers associated with the sequence:
// ->"bits" and ->"seq"
static void seq_freemem(int seq) {

  if (sequences[seq].bits) {
    q_free(sequences[seq].bits);
    sequences[seq].bits = NULL;
  }
  if (sequences[seq].seq) {
    q_free(sequences[seq].seq);
    sequences[seq].seq = NULL;
  }
}

// initialize/reset sequences to default values
//
static void seq_init() {

  for (int i = 0; i < SEQUENCES_NUM; i++) {
    sequences[i].tick = 1;
    seq_freemem(i);
    sequences[i].alph[0].duration0 = sequences[i].alph[0].duration1 = 0;
    sequences[i].alph[1].duration0 = sequences[i].alph[1].duration1 = 0;
  }
}

// dump sequence content
static void seq_dump(unsigned int seq) {

  struct sequence *s;

  if (seq >= SEQUENCES_NUM) {
    q_printf("%% <e>Sequence %d does not exist</>\r\n", seq);
    return;
  }

  s = &sequences[seq];

  q_printf("%%\r\n%% Sequence #%d:\r\n%% Resolution : %.4fuS  (Frequency: %lu Hz)\r\n", seq, s->tick, seq_tick2freq(s->tick));
  q_print("% Levels are ");
  if (s->seq) {
    int i;
    unsigned long total = 0;

    for (i = 0; i < s->seq_len; i++) {
      if (!(i & 3))
        q_print("\r\n% ");
      q_printf("%d/%d, %d/%d, ", s->seq[i].level0, s->seq[i].duration0, s->seq[i].level1, s->seq[i].duration1);
      total += s->seq[i].duration0 + s->seq[i].duration1;
    }
    q_printf("\r\n%% Total: %d levels, duration: %lu ticks, (~%lu uS)\r\n", s->seq_len * 2, total, (unsigned long)((float)total * s->tick));
  } else
    q_print(Notset);

  q_print("% Modulation ");
  if (s->mod_freq)
    q_printf(" : yes, \"%s\" are modulated at %luHz, duty %.2f%%\r\n", s->mod_high ? "HIGH" : "LOW", (unsigned long)s->mod_freq, s->mod_duty * 100);
  else
    q_print("is not used\r\n");

  q_print("% Bit sequence is ");
  if (s->bits) {

    q_printf(": (%d bits) \"%s\"\r\n", strlen(s->bits), s->bits);
    q_print("% Zero is ");
    if (s->alph[0].duration0) {
      if (s->alph[0].duration1)
        q_printf("%d/%d %d/%d\r\n", s->alph[0].level0, s->alph[0].duration0, s->alph[0].level1, s->alph[0].duration1);
      else
        q_printf("%d/%d\r\n", s->alph[0].level0, s->alph[0].duration0);
    } else
      q_print(Notset);

    q_print("% One is ");
    if (s->alph[1].duration0) {
      if (s->alph[1].duration1)
        q_printf("%d/%d %d/%d\r\n", s->alph[1].level0, s->alph[1].duration0, s->alph[1].level1, s->alph[1].duration1);
      else
        q_printf("%d/%d\r\n", s->alph[1].level0, s->alph[1].duration0);
    } else
      q_print(Notset);
  } else
    q_print(Notset);

  q_printf("%% Hold %s after transmission is done\r\n", s->eot ? "HIGH" : "LOW");
}

// convert a level string to numerical values:
// "1/500" gets converted to level=1 and duration=500
// level is either 0 or 1, duration 0..32767
//
// called with first two arguments set to NULL performs
// syntax check on arguments only
//
// returns 0 on success, <0 - syntax error
//

static int seq_atol(int *level, int *duration, char *p) {
  unsigned int d;
  if (p && (p[0] == '0' || p[0] == '1') && (p[1] == '/' || p[1] == '\\'))
    if (isnum(p + 2) && ((d = atol(p + 2)) <= 32767)) {
      if (level) *level = *p - '0';
      if (duration) *duration = d;
      return 0;
    }
  return -1;
}


// check if sequence is configured and an be used
// to generate pulses. The criteria is:
// ->seq must be initialized
// ->tick must be set
static bool seq_isready(unsigned int seq) {

  if (seq >= SEQUENCES_NUM)
    return false;

  return (sequences[seq].seq != NULL) && (sequences[seq].tick != 0.0f);
}

// compile 'bits' to 'seq'
// compilation is done when following conditions are met:
//   1. both 'zero' and 'one' are set. Both must be of the
//      same type: either pulse (long form) or level (short form)
//   2. 'bits' are set
//   3. 'seq' is NULL : i.e. is not compiled yet
static int seq_compile(int seq) {

  struct sequence *s = &sequences[seq];

  if (s->seq)  //already compiled
    return 0;

  if (s->alph[0].duration0 && s->alph[1].duration0 && s->bits) {


    // if "0" is defined as a "pulse" (i.e. both parts of rmt_data_t used
    // to define a symbol (IR protocols-type encoding) then we need "1" to
    // be defined as a "pulse" as well.
    //
    // allocate strlen(bits) items for a 'seq', initialize 'seq' items with
    // either "alph[0]" or "alph[1]" according to "bits"
    if (s->alph[0].duration1) {
      //long form
      if (!s->alph[1].duration1) {
        q_print("% <e>\"One\" defined as a level, but \"Zero\" is a pulse</>\r\n");
        return -1;
      }

      // long-form. 1 rmt symbol = 1 bit;
      int j, i = strlen(s->bits);
      if (!i)
        return -2;

      s->seq = (rmt_data_t *)q_malloc(sizeof(rmt_data_t) * i, MEM_RMT);
      if (!s->seq)
        return -3;
      s->seq_len = i;
      for (j = 0; j < i; j++) {

        if (s->bits[j] == '0')
          s->seq[j] = s->alph[0];
        else
          s->seq[j] = s->alph[1];
      }
    } else {
      // short form
      // 1 rmt symbol can carry 2 bits so we want out "bits" to be of even size so
      // we add an extra '0' to the bits. It is probably not what user wants so
      // report it to user

      if (s->alph[1].duration1) {
        q_print("% <e>\"One\" defined as a pulse, but \"Zero\" is a level</>\r\n");
        return -4;
      }

      int k, j, i = strlen(s->bits);
      // add one extra bit is string length is uneven by copying last bit
      if (i & 1) {
        char *r = (char *)q_realloc(s->bits, i + 2,MEM_SEQUENCE);
        if (!r)
          return -5;
        s->bits = r;
        s->bits[i + 0] = s->bits[i - 1];
        s->bits[i + 1] = '\0';
#if WITH_HELP        
        q_printf("%% Bit string was padded with one extra \"%c\" (must be even number bits)\r\n",s->bits[i]);
#endif        
        i++;
      }
      s->seq_len = i / 2;
      s->seq = (rmt_data_t *)q_malloc(sizeof(rmt_data_t) * s->seq_len,MEM_RMT);

      if (!s->seq)
        return -6;

      j = 0;
      k = 0;

      while (j < i) {

        if (s->bits[j] == '1') {
          s->seq[k].level0 = s->alph[1].level0;
          s->seq[k].duration0 = s->alph[1].duration0;
        } else {
          s->seq[k].level0 = s->alph[0].level0;
          s->seq[k].duration0 = s->alph[0].duration0;
        }

        j++;

        if (s->bits[j] == '1') {
          s->seq[k].level1 = s->alph[1].level0;
          s->seq[k].duration1 = s->alph[1].duration0;
        } else {
          s->seq[k].level1 = s->alph[0].level0;
          s->seq[k].duration1 = s->alph[0].duration0;
        }

        j++;
        k++;
      }
    }  // short form end
  }    //if compilation criteria are met
  return 0;
}


//Send sequence 'seq' using GPIO 'pin'
//Sequence is fully configured
static int seq_send(unsigned int pin, unsigned int seq) {

  struct sequence *s = &sequences[seq];

  if (!rmtInit(pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, seq_tick2freq(s->tick)))
    return -1;
  if (!rmtSetCarrier(pin, s->mod_freq ? true : false, s->mod_high == 1 ? false : true, s->mod_freq, s->mod_duty))
    return -2;
  if (!rmtSetEOT(pin, s->eot))
    return -3;
  if (!rmtWrite(pin, s->seq, s->seq_len, RMT_WAIT_FOR_EVER))
    return -4;

  return 0;
}


// COMMAND HANDLERS
// ----------------
// Hanlers are the functions which get called by the parser
// when it matches first token (argv[0]) with the keyword[] array entry.
//
// Handlers receive two arguments: argc and argv (tokenized user input)
// argv[0] is the command name, argv[1] .. argv[argc-1] are arguments
//
// There is an array 'keywords[]' in a code below where all the handlers
// are registered
//



// "history [on|off]"
// disable/enable/show status for command history
//
static int cmd_history(int argc,char **argv) {
  if (argc < 2) q_printf("%% History is %s\r\n",rl_history ? "on" : "off"); else 
  if (!q_strcmp(argv[1],"off")) rl_history_enable(false); else
  if (!q_strcmp(argv[1],"on")) rl_history_enable(true); else return 1;
  return 0;
}


//"exit"
//"exit exit"
// exists from command subderictory or closes the shell ("exit exit")
//
static int cmd_exit(int argc, char **argv) {

  if (keywords != keywords_main) {
    // restore prompt & keywords list to use
    keywords = keywords_main;
    prompt = PROMPT;
  } else
    // close espshell. mounted filesystems are left mounted, background commands are left running
    // memory is not freed. It all can/will be reused on espshell restart via espshell_start() call
    if (argc > 1 && !q_strcmp(argv[1], "exit"))
      Exit = true;
    

  return 0;
}

//TAG:show
//"show seq NUMBER"

static int cmd_show(int argc, char **argv) {

  if (argc < 2)
    return -1;
  if (!q_strcmp(argv[1], "sequence"))
    return cmd_seq_show(argc, argv);
  return 1;
}

//TAG:seq
//"sequence X"
// save context, switch command list, change the prompt
static int cmd_seq_if(int argc, char **argv) {

  unsigned char seq;
  static char prom[16];
  if (argc < 2)
    return -1;

  if ((seq = q_atol(argv[1], SEQUENCES_NUM)) >= SEQUENCES_NUM) {
#if WITH_HELP    
    q_printf("%% <e>Sequence numbers are 0..%d</>\r\n", SEQUENCES_NUM - 1);
#endif    
    return 1;
  }

  // embed sequence number into prompt
  sprintf(prom,PROMPT_SEQ,seq);
  change_command_directory(seq, keywords_sequence,prom, "pulse sequence");
  return 0;
}


//TAG:eot
//eot high|low
//
// set End-Of-Transmission line status. Once transmission is finished
// the esp32 hardware will pull the line either HIGH or LOW depending
// on the EoT setting. Default is LOW
//
static int cmd_seq_eot(int argc, char **argv) {

  if (argc < 2)
    return -1;

  if (!q_strcmp(argv[1], "high") || argv[1][0] == '1')
    sequences[Context].eot = 1;
  else
    sequences[Context].eot = 0;

  return 0;
}

//TAG:modulation
//
//modulation FREQ [DUTY [low|high]]
//
static int cmd_seq_modulation(int argc, char **argv) {

  int high = 1;
  float duty = 0.5;
  unsigned int freq;

  // at least FREQ must be provided
  if (argc < 2)
    return -1;

  freq = q_atol(argv[1],0);
  if (!freq || freq > 40000000) {
#if WITH_HELP
    q_print("% Frequency must be between 1 and 40 000 000 Hz\r\n"); //TODO: find out real boundaries
#endif
    return 1;
  }

  // More arguments are available?
  if (argc > 2) {

    // read DUTY.
    // Duty cycle is a float number on range [0..1]
    duty = q_atof(argv[2],2.0f);

    if (duty < 0.0f || duty > 1.0f) {
#if WITH_HELP
      q_print("% <e>Duty cycle is a number in range [0..1] (0.01 means 1% duty)</>\r\n");
#endif
      return 2;
    }
  }
  //third argument: "high" or "1" means modulate when line is HIGH (modulate 1's)
  // "low" or "0" - modulate when line is LOW (modulate zeros)
  if (argc > 3) {
    if (!q_strcmp(argv[3], "low") || argv[3][0] == '1')
      high = 0;
    else if (!q_strcmp(argv[3], "high") || argv[3][0] == '0')
      high = 1;
    else
      return 3;  // 3rd argument was not understood
  }

  sequences[Context].mod_freq = freq;
  sequences[Context].mod_duty = duty;
  sequences[Context].mod_high = high;

  return 0;
}

//one 1/100 [0/10]
//zero 1/100 [0/10]
//
// Setup the alphabet to be used when encoding "bits". there are
// two symbols in alphabet: 0 and 1. Both of them can be defined
// as a level (short form) or a pulse (long form):
//
// short scheme example:
// one 1/50 , zero 0/50 : 1 is HIGH for 50 ticks, 0 is LOW for 50 ticks
// one RMT symbol is used to transmit 2 bits
//
// long scheme example:
// one 1/50 0/10 , zero 1/100 0/10 : 1 is "HIGH/50ticks then LOW for 10 tiks"
// and 0 is "HIGH for 100 ticks then LOW for 10 ticks"
// one RMT symbol is used to transmit 1 bit.
//
static int cmd_seq_zeroone(int argc, char **argv) {

  struct sequence *s = &sequences[Context];

  int i = 0;
  int level, duration;

  // which alphabet entry to set?
  if (!q_strcmp(argv[0], "one"))
    i = 1;

  //entry is short form by default
  s->alph[i].level1 = 0;
  s->alph[i].duration1 = 0;


  switch (argc) {
    // two arguments = a pulse
    // (long form)
    case 3:
      if (seq_atol(&level, &duration, argv[2]) < 0)
        return 2;
      s->alph[i].level1 = level;
      s->alph[i].duration1 = duration;


    //FALLTHRU
    // single value = a level
    // (short form)
    case 2:
      if (seq_atol(&level, &duration, argv[1]) < 0)
        return 1;
      s->alph[i].level0 = level;
      s->alph[i].duration0 = duration;
      break;


    default:
      return -1;  // wrong number of arguments
  };
  seq_compile(Context);
  return 0;
}

//TAG:tick
//
// tick TIME
// sets resolution of a sequence:
// the duration of pulses and levels are measured in 'ticks':
// level of "1/100" means "hold line HIGH for 100 ticks".
// ticks are measured in microseconds and can be <1: lower limit
// is 0.0125 microsecond/tick which corresponds to RMT hardware
// frequency of 80MHz
static int cmd_seq_tick(int argc, char **argv) {


  if (argc < 2)
    return -1;

  if (!isfloat(argv[1]))
    return 1;

  sequences[Context].tick = atof(argv[1]);

  if (sequences[Context].tick < 0.0125 || sequences[Context].tick > 3.2f) {
#if WITH_HELP
    q_print("% <e>Tick must be in range 0.0125..3.2 microseconds</>\r\n");
#endif
    return 1;
  }

  seq_compile(Context);

  return 0;
}



//TAG:bits
//
// sets a bit string as a sequence.
// "zero" and "one" must be set as well to tell the hardware
// what 1 and 0 are
//
// depending of values of "one" and "zero" (short or long form)
// the pulse or level train will be generated
//
// long form (pulses) are used mostly for IR remote control
// or similar application where 0 and 1 is not just levels but
// complete pulses: going up AND down when transmitting one single bit
static int cmd_seq_bits(int argc, char **argv) {

  struct sequence *s = &sequences[Context];

  if (argc < 2)
    return -1;

  char *bits = argv[1];

  while (*bits == '1' || *bits == '0')
    bits++;

  if (*bits != '\0')
    return 1;

  seq_freemem(Context);
  s->bits = q_strdup(argv[1],MEM_SEQUENCE);

  if (!s->bits)
    return -1;

  seq_compile(Context);

  return 0;
}

//TAG:levels
//
// instead setting up an alphabet ("zero", "one") and a bit string,
// the pattern can be set as simple sequence of levels.
//
// sequence of levels does not require compiling
//
static int cmd_seq_levels(int argc, char **argv) {

  int i, j;
  struct sequence *s = &sequences[Context];

  if (argc < 2)
    return -1;

  // check if all levels have correct syntax
  for (i = 1; i < argc; i++)
    if (seq_atol(NULL, NULL, argv[i]) < 0)
      return i;

  seq_freemem(Context);

  i = argc - 1;

  if (i & 1) {
    q_print("% <e>Uneven number of levels. Please add 1 more</>\r\n");
    return 0;
  }


  s->seq_len = i / 2;
  s->seq = (rmt_data_t *)q_malloc(sizeof(rmt_data_t) * s->seq_len,MEM_RMT);

  if (!s->seq)
    return -1;


  memset(s->seq, 0, sizeof(rmt_data_t) * s->seq_len);

  // each RMT symbol (->seq entry) can hold 2 levels
  // run thru all arguments and read level/duration pairs
  // into ->seq[].
  // i - index to arguments
  // j - index to ->seq[] (RMT entries)
  for (i = 0, j = 0; i < s->seq_len * 2; i += 2) {

    int level, duration;
    if (seq_atol(&level, &duration, argv[i + 1]) < 0)
      return i + 1;

    s->seq[j].level0 = level;
    s->seq[j].duration0 = duration;

    if (seq_atol(&level, &duration, argv[i + 2]) < 0)
      return i + 2;

    s->seq[j].level1 = level;
    s->seq[j].duration1 = duration;

    j++;
  }

  return 0;
}


//TAG:show
//
// display the sequence content.
// can be called either from 'sequence' command subderictory
// or from the root:
// esp32-seq#> show
// esp32#> show seq 0
//
static int cmd_seq_show(int argc, char **argv) {

  unsigned int seq;

  // command executed as "show" within sequence
  // command tree (no arguments)
  if (argc < 2) {
    seq_dump(Context);
    return 0;
  }

  // command executaed as "show seq NUMBER".
  // two arguments (argc=3)
  if (argc != 3)
    return -1;

  if ((seq = q_atol(argv[2],SEQUENCES_NUM)) >= SEQUENCES_NUM)
    return 2;

  seq_dump(seq);
  return 0;
}




#define PULSE_WAIT 1000
#define PCNT_OVERFLOW 20000


// PCNT interrupt handler. Called every 20 000 pulses 2 times :).
// i do not know why. should read docs better
static unsigned int count_overflow = 0;

static void IRAM_ATTR pcnt_interrupt(void *arg) {
  count_overflow++;
  PCNT.int_clr.val = BIT(PCNT_UNIT_0);
}


//TAG:count
//"count PIN [DELAY_MS [pos|neg|both]]"
//
static int cmd_count(int argc, char **argv) {

  pcnt_config_t cfg = { 0 }; 
  unsigned int pin, wait = PULSE_WAIT;
  int16_t count;

  if (!pin_exist((cfg.pulse_gpio_num = pin = q_atol(argv[1],999))))
    return 1;

  cfg.ctrl_gpio_num = -1;  // don't use "control pin" feature
  cfg.channel = PCNT_CHANNEL_0;
  cfg.unit = PCNT_UNIT_0;
  cfg.pos_mode = PCNT_COUNT_INC;
  cfg.neg_mode = PCNT_COUNT_DIS;
  cfg.counter_h_lim = PCNT_OVERFLOW;

  // user has provided second argument?
  if (argc > 2) {
    if ((wait = q_atol(argv[2], 0xffffffff)) == 0xffffffff)
      return 2;

    //user has provided 3rd argument?
    if (argc > 3) {
      if (!q_strcmp(argv[3], "pos")) { /* default*/
      } else 
      if (!q_strcmp(argv[3], "neg")) {
        cfg.pos_mode = PCNT_COUNT_DIS;
        cfg.neg_mode = PCNT_COUNT_INC;
      } else 
      if (!q_strcmp(argv[3], "both")) {
        cfg.pos_mode = PCNT_COUNT_INC;
        cfg.neg_mode = PCNT_COUNT_INC;
      } else 
      return 3;
    }
  }

  q_printf("%% Counting pulses on GPIO%d...", pin);
#if WITH_HELP
  if (is_foreground_task())
    q_print("(press <Enter> to stop counting)");
#endif
  q_print(CRLF);


  pcnt_unit_config(&cfg);
  pcnt_counter_pause(PCNT_UNIT_0);
  pcnt_counter_clear(PCNT_UNIT_0);
  pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_H_LIM);
  pcnt_isr_register(pcnt_interrupt, NULL, 0, NULL);
  pcnt_intr_enable(PCNT_UNIT_0);

  count_overflow = 0;
  pcnt_counter_resume(PCNT_UNIT_0);
  wait = delay_interruptible(wait);
  pcnt_counter_pause(PCNT_UNIT_0);

  pcnt_get_counter_value(PCNT_UNIT_0, &count);

  pcnt_event_disable(PCNT_UNIT_0, PCNT_EVT_H_LIM);
  pcnt_intr_disable(PCNT_UNIT_0);

  count_overflow = count_overflow / 2 * PCNT_OVERFLOW + count;
  q_printf("%% %u pulses in %.3f seconds (%.1f Hz)\r\n", count_overflow, (float)wait / 1000.0f, count_overflow * 1000.0f / (float)wait);
  return 0;
}


// To execute async version of the "count" command ("count&") we
// start a separate task which calls cmd_count()
//
static void count_async_task(void *arg) {

  argcargv_t *aa = (argcargv_t *)arg;
  if (aa != NULL) {
    int ret = cmd_count(aa->argc, aa->argv);
    userinput_unref(aa);
    if (ret != 0)
      q_print(Failed);
  }
  vTaskDelete(NULL);
}



// "var"     - display registered variables list
// "var X"   - display variable X value
static int cmd_var_show(int argc, char **argv) {

  int len;
  bool found_one = false;

  // composite variable value.
  // had to use union because of variables different sizeof()
  union {
    unsigned char uchar;  // unsigned char
    signed char ichar;    // signed --
    unsigned short ush;   // unsigned short
    signed short ish;     // signed --
    int ival;             // signed int
    unsigned int uval;    // unsigned --
    float fval;           // float
  } u;

  // "var": display variables list if no arguments were given
  if (argc < 2) {
    struct convar *var = var_head;

#if WITH_HELP    
    if (!var)
      q_print(VarOops);
    else
#endif
      q_print("% Registered variables:\r\n");

    while (var) {
#pragma GCC diagnostic ignored "-Wformat"
      q_printf("%% \"<i>% 16s</>\", %d bytes long (likely of <i>%s</> type)\r\n", var->name, var->size, var->size == 4 ? "float or int" : (var->size == 2 ? "short int" : "char"));
#pragma GCC diagnostic warning "-Wformat"            
      var = var->next;
    }
    return 0;
  }

  //"var X": display variable value OR
  //"var NUMBER" display different representaton of a number
  //
  if (argc < 3) {
    unsigned int unumber;
    signed int inumber;
    float fnumber;
    // argv[n] is at least 2 bytes long (1 symbol + '\0')

    // Octal, Binary or Hex number?
    if (argv[1][0] == '0') {
      if (argv[1][1] == 'x')
        unumber = hex2uint32(&argv[1][2]);
      else if (argv[1][1] == 'b')
        unumber = binary2uint32(&argv[1][2]);
      else
        unumber = octal2uint32(&argv[1][1]);
      memcpy(&fnumber, &unumber, sizeof(fnumber));
      memcpy(&inumber, &unumber, sizeof(inumber));
    } else {
      // Integer (signed or unsigned) or floating point number?
      if (isnum(argv[1])) {
        if (argv[1][0] == '-') {
          inumber = atoi(argv[1]);
          memcpy(&unumber, &inumber, sizeof(unumber));
        } else {
          unumber = atol(argv[1]);
          memcpy(&inumber, &unumber, sizeof(inumber));
        }
        memcpy(&fnumber, &unumber, sizeof(fnumber));
      } else 
      if (isfloat(argv[1])) {
        fnumber = atof(argv[1]);
        memcpy(&unumber, &fnumber, sizeof(unumber));
        memcpy(&inumber, &fnumber, sizeof(inumber));
      } else
        // no brother, this defenitely not a number
        goto process_as_variable_name;
    }

    // display a number in hex, octal, binary, integer or float representation
    q_printf("%% \"%s\" is a number, which can be written as\r\n"
             "%% unsigned : %u\r\n"
             "%%   signed : %i\r\n"
             "%% FP number: %f\r\n"
             "%% hex      : 0x%x\r\n"
             "%% oct      : 0%o\r\n"
             "%% bin      : \"0b", argv[1], unumber, inumber, fnumber, unumber, unumber);

    // display binary form with leading zeros omitted
    // TODO: use gcc's __builtin_ function to count leading zeros
    for (inumber = 0; inumber < 32; inumber++) {
      if (unumber & 0x80000000) {
        q_print("1");
        found_one = true;
      } else if (found_one)
        q_print("0");
      unumber <<= 1;
    }
    q_print(CRLF);

    return 0;

process_as_variable_name:

    char *fullname;
    bool isf;
    if ((len = convar_get(argv[1], &u, &fullname, &isf)) == 0) {
#if WITH_HELP
      q_printf("%% \"%s\" : No such variable\r\n",argv[1]);
      return 0;
#else
      return 1;
#endif      
    }
    switch (len) {
      case 1: 
        q_printf("%% // 0x%x in hex\r\n",u.uchar);
        q_printf("%% unsigned char %s = %u;\r\n"
                 "%%   signed char %s = %d;\r\n", fullname, u.uchar, fullname, u.ichar); 
        break;
      case 2: 
        q_printf("%% // 0x%x in hex\r\n",u.ush);
        q_printf("%% unsigned short %s = %u;\r\n"
                 "%%   signed short %s = %d;\r\n", fullname, u.ush, fullname, u.ish); 
        break;
      case 4: 
        q_printf("%% // 0x%x in hex\r\n",u.uval);
        if (isf)
          q_printf("%% float %s = %ff;\r\n",fullname,u.fval);
        else
          q_printf("%% unsigned int %s = %u;\r\n"
                   "%%   signed int %s = %d;\r\n", fullname, u.uval, fullname, u.ival); 
        break;
      default: 
        q_printf("%% FIXME: Variable \"%s\" has unsupported size of %d bytes\r\n", fullname, len);
        return 1;
    };
    return 0;
  }
  return -1;
}

// "var"
// "var X"
// "var X NUMBER"
static int cmd_var(int argc, char **argv) {

  int len;
  union {
    unsigned char uchar;
    signed char ichar;
    unsigned short ush;
    signed short ish;
    int ival;
    unsigned int uval;
    float fval;
  } u;

  // no variables were registered but user invoked "var" command:
  // give them a hint

  if (var_head == NULL) {
#if WITH_HELP    
    q_print(VarOops);
#endif  //WITH_HELP    
    return 0;
  }

  // "var": display variables list if no arguments were given
  if (argc < 3)
    return cmd_var_show(argc, argv);

  // Set variable
  // does variable exist? get its size
  char *fullname;
  bool isf;
  if ((len = convar_get(argv[1], &u, &fullname,&isf)) == 0)
    return 1;

  if (isf) {
    if (isfloat(argv[2])) {
      // floating point number
      q_print("% Floating point number\r\n");
      u.fval = q_atof(argv[2],0);
    } else {
      q_printf("%% Variable \"%s\" has type \"float\" and expects floating point argument\r\n",fullname);
      return 2;
    }   
  } else
  if (isnum(argv[2]) || (argv[2][0] == '0' && argv[2][0] == 'x')) {
    bool sign = argv[2][0] == '-';
    if (sign) {
      q_print("% Signed integer\r\n");
      u.ival = -q_atol(&(argv[2][1]),0);
      if (len == sizeof(char)) u.ichar = u.ival; else 
      if (len == sizeof(short)) u.ish = u.ival;
    } else {
      q_print("% Unsigned integer\r\n");
      u.uval = q_atol(argv[2],0);
      if (len == sizeof(char)) u.uchar = u.uval; else 
      if (len == sizeof(short)) u.ush = u.uval;
    }
  } else
    // unknown
    return 2;

  convar_set(fullname, &u);
  return 0;
}


#define MAGIC_FREQ 312000  // max allowed frequency for the "pwm" command

//TAG:pwm
// enable or disable (freq==0) tone generation on
// pin. freq is in (0..312kHz), duty is [0..1]
//
// TODO: there is a bug somewhere in this function. Sometimes, on a first use after
//       reboot it enables PWM but there is no output (as indicated by attached led).
//       calling this function again resolves the glitch.
//
static int pwm_enable(unsigned int pin, unsigned int freq, float duty) {

  int resolution = 8;

  if (!pin_exist(pin))
    return -1;

  if (freq > MAGIC_FREQ) freq = MAGIC_FREQ;
  if (duty > 1.0f)       duty = 1.0f;
  if (freq < 78722)      resolution = 10;   //higher duty parameter resolution on frequencies below 78 kHz

  pinMode2(pin, OUTPUT);
  ledcDetach(pin);

  if (freq) {
    ledcAttach(pin, freq, resolution);
    ledcWrite(pin, (unsigned int)(duty * ((1 << resolution) - 1)));
  }

  return 0;
}

//pwm PIN FREQ [DUTY]   - pwm on
//pwm PIN               - pwm off
static int cmd_pwm(int argc, char **argv) {

  unsigned int freq = 0;
  float duty = 0.5f;
  unsigned pin;

  if (argc < 2) return -1;   // missing arg
  pin = q_atol(argv[1],999); // first parameter is pin number

  //frequency is the second one (optional)
  if (argc > 2) {
    if ((freq = q_atol(argv[2],0)) == 0)
      return 2;    
#if WITH_HELP
    if (freq > MAGIC_FREQ)
      q_print("% Frequency will be adjusted to its maximum which is " xstr(MAGIC_FREQ) "] Hz\r\n");
#endif
  }

  // duty is the third argument (optional)
  if (argc > 3) {
    duty = q_atof(argv[3],-1);
    if (duty < 0 || duty > 1) {
#if WITH_HELP
      q_print("% <e>Duty cycle is a number in range [0..1] (0.01 means 1% duty)</>\r\n");
#endif
      return 3;
    }
  }

  // FIXME: TODO: evil hack. for unknown reason sometimes on fresh boot the first call to pwm_enable()
  //              has no effect. No errors reported also even at Verbose level. So we just call this function twice every time
  pwm_enable(pin, freq, duty);
  if (freq && pwm_enable(pin, freq, duty) < 0) {
#if WITH_HELP
    q_print(Failed);
#endif
  }
  return 0;
}

// save pin state.
// there is an array Pins[] which is used for that. Subsequent saves rewrite previous save.
// pin_load() is used to load pin state from Pins[]
//
static void pin_save(int pin) {

  bool pd, pu, ie, oe, od, slp_sel;
  uint32_t drv, fun_sel, sig_out;

  gpio_ll_get_io_config(&GPIO, pin, &pu, &pd, &ie, &oe, &od, &drv, &fun_sel, &sig_out, &slp_sel);

  //Pin peri connections:
  // fun_sel is either PIN_FUNC_GPIO and then sig_out is the signal ID
  // to route to the pin via GPIO Matrix.

  Pins[pin].sig_out = sig_out;
  Pins[pin].fun_sel = fun_sel;
  Pins[pin].bus_type = perimanGetPinBusType(pin);

  //save digital value for OUTPUT GPIO
  if (Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO && oe)
    Pins[pin].value = (digitalRead(pin) == HIGH);

  Pins[pin].flags = 0;
  if (pu) Pins[pin].flags |= PULLUP;
  if (pd) Pins[pin].flags |= PULLDOWN;
  if (ie) Pins[pin].flags |= INPUT;
  if (oe) Pins[pin].flags |= OUTPUT;
  if (od) Pins[pin].flags |= OPEN_DRAIN;
}

// Load pin state from Pins[] array
// Attempt is made to restore GPIO Matrix connections however it is not working as intended
//
static void pin_load(int pin) {

  // 1. restore pin mode
  pinMode2(pin, Pins[pin].flags);

  //2. attempt to restore peripherial connections:
  //   If pin was not configured or was simple GPIO function then restore it to simple GPIO
  //   If pin had a connection through GPIO Matrix, restore IN & OUT signals connection (use same
  //   signal number for both IN and OUT. Probably it should be fixed)
  //
  if (Pins[pin].fun_sel != PIN_FUNC_GPIO)
    q_printf("%% Pin %d IO MUX connection can not be restored\r\n", pin);
  else {
    if (Pins[pin].bus_type == ESP32_BUS_TYPE_INIT || Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO) {
      gpio_pad_select_gpio(pin);

      // restore digital value
      if ((Pins[pin].flags & OUTPUT) && (Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO))
        digitalWrite(pin, Pins[pin].value ? HIGH : LOW);
    } else {
      // unfortunately this will not work with Arduino :(
      if (Pins[pin].flags & OUTPUT)
        gpio_matrix_out(pin, Pins[pin].sig_out, false, false);
      if (Pins[pin].flags & INPUT)
        gpio_matrix_in(pin, Pins[pin].sig_out, false);
    }
  }
}

// Input-Only pins as per Tech Ref. Seems like only original
// ESP32 has these while newer models have all GPIO capable
// of Input & Output
//
static bool pin_is_input_only_pin(int pin) {
  return !GPIO_IS_VALID_OUTPUT_GPIO(pin);
}


// strapping pins as per Technical Reference
// TODO: add more targets
//
static bool pin_is_strapping_pin(int pin) {
  switch (pin) {
#ifdef CONFIG_IDF_TARGET_ESP32
    case 0: case 2: case 5: case 12: case 15: return true;
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S2
    case 0: case 45: case 46: return true;
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S3
    case 0: case 3: case 45: case 46: return true;
#endif
#ifdef CONFIG_IDF_TARGET_ESP32C3
    case 2: case 8: case 9: return true;
#endif
#ifdef CONFIG_IDF_TARGET_ESP32C6
    case 8: case 9: case 12: case 14: case 15: return true;
#endif
#ifdef CONFIG_IDF_TARGET_ESP32H2
    case 8: case 9: case 25: return true;
#endif
    default: return false;
  }
}


// IO_MUX function code --> human readable text mapping
// Each ESP32 variant has its own mapping but I made tables
// only for ESP32 and ESP32S3/2 simply because I have these boards
//
// Each pin of ESP32 can carry a function: be either a GPIO or be an periferial pin:
// SD_DATA0 or UART_TX etc.
//
//TODO: add support for other Espressif ESP32 variants
//

#ifdef CONFIG_IDF_TARGET_ESP32
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][6] = {
  // ESP32 pins can be assigned one of 6 functions via IO_MUX
  { "GPIO0", "CLK_OUT1", "GPIO0", "3", "4", "EMAC_TX_CLK" },
  { "U0TXD", "CLK_OUT3", "GPIO1", "3", "4", "EMAC_RXD2" },
  { "GPIO2", "HSPIWP", "GPIO2", "HS2_DATA0", "SD_DATA0" },
  { "U0RXD", "CLK_OUT2", "GPIO3", "3", "4", "5" },
  { "GPIO4", "HSPIHD", "GPIO4", "HS2_DATA1", "SD_DATA1", "EMAC_TX_ER" },
  { "GPIO5", "VSPICS0", "GPIO5", "HS1_DATA6", "4", "EMAC_RX_CLK" },
  { "SD_CLK", "SPICLK", "GPIO6", "HS1_CLK", "U1CTS", "5" },
  { "SD_DATA0", "SPIQ", "GPIO7", "HS1_DATA0", "U2RTS", "5" },
  { "SD_DATA1", "SPID", "GPIO8", "HS1_DATA1", "U2CTS", "5" },
  { "SD_DATA2", "SPIHD", "GPIO9", "HS1_DATA2", "U1RXD", "5" },
  { "SD_DATA3", "SPIWP", "GPIO10", "HS1_DATA3", "U1TXD", "5" },
  { "SD_CMD", "SPICS0", "GPIO11", "HS1_CMD", "U1RTS", "5" },
  { "MTDI", "HSPIQ", "GPIO12", "HS2_DATA2", "SD_DATA2", "EMAC_TXD3" },
  { "MTCK", "HSPID", "GPIO13", "HS2_DATA3", "SD_DATA3", "EMAC_RX_ER" },
  { "MTMS", "HSPICLK", "GPIO14", "HS2_CLK", "SD_CLK", "EMAC_TXD2" },
  { "MTDO", "HSPICS0", "GPIO15", "HS2_CMD", "SD_CMD", "EMAC_RXD3" },
  { "GPIO16", "1", "GPIO16", "HS1_DATA4", "U2RXD", "EMAC_CLK_OUT" },
  { "GPIO17", "1", "GPIO17", "HS1_DATA5", "U2TXD", "EMAC_CLK_180" },
  { "GPIO18", "VSPICLK", "GPIO18", "HS1_DATA7", "4", "5" },
  { "GPIO19", "VSPIQ", "GPIO19", "U0CTS", "4", "EMAC_TXD0" },
  { "GPIO20", "GPIO20(1)", "GPIO20(2)", "GPIO20(3)", "GPIO20(4)", "GPIO20(5)" },
  { "GPIO21", "VSPIHD", "GPIO21", "3", "4", "EMAC_TX_EN" },
  { "GPIO22", "VSPIWP", "GPIO22", "U0RTS", "4", "EMAC_TXD1" },
  { "GPIO23", "VSPID", "GPIO23", "HS1_STROBE", "4", "5" },
  { "GPIO24", "GPIO24(1)", "GPIO24(2)", "GPIO24(3)", "GPIO24(4)", "GPIO24(5)" },
  { "GPIO25", "1", "GPIO25", "3", "4", "EMAC_RXD0" },
  { "GPIO26", "1", "GPIO26", "3", "4", "EMAC_RXD1" },
  { "GPIO27", "1", "GPIO27", "3", "4", "EMAC_RX_DV" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "GPIO32", "1", "GPIO32", "3", "4", "5" },
  { "GPIO33", "1", "GPIO33", "3", "4", "5" },
  { "GPIO34", "1", "GPIO34", "3", "4", "5" },
  { "GPIO35", "1", "GPIO35", "3", "4", "5" },
  { "GPIO36", "1", "GPIO36", "3", "4", "5" },
  { "GPIO37", "1", "GPIO37", "3", "4", "5" },
  { "GPIO38", "1", "GPIO38", "3", "4", "5" },
  { "GPIO39", "1", "GPIO39", "3", "4", "5" },
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][5] = {
  // ESP32S3 MUX functions for pins
  { "GPIO0", "GPIO0", "2", "3", "4" },
  { "GPIO1", "GPIO1", "2", "3", "4" },
  { "GPIO2", "GPIO2", "2", "3", "4" },
  { "GPIO3", "GPIO3", "2", "3", "4" },
  { "GPIO4", "GPIO4", "2", "3", "4" },
  { "GPIO5", "GPIO5", "2", "3", "4" },
  { "GPIO6", "GPIO6", "2", "3", "4" },
  { "GPIO7", "GPIO7", "2", "3", "4" },
  { "GPIO8", "GPIO8", "2", "SUBSPICS1", "4" },
  { "GPIO9", "GPIO9", "2", "SUBSPIHD", "FSPIHD" },
  { "GPIO10", "GPIO10", "FSPIIO4", "SUBSPICS0", "FSPICS0" },
  { "GPIO11", "GPIO11", "FSPIIO5", "SUBSPID", "FSPID" },
  { "GPIO12", "GPIO12", "FSPIIO6", "SUBSPICLK", "FSPICLK" },
  { "GPIO13", "GPIO13", "FSPIIO7", "SUBSPIQ", "FSPIQ" },
  { "GPIO14", "GPIO14", "FSPIDQS", "SUBSPIWP", "FSPIWP" },
  { "GPIO15", "GPIO15", "U0RTS", "3", "4" },
  { "GPIO16", "GPIO16", "U0CTS", "3", "4" },
  { "GPIO17", "GPIO17", "U1TXD", "3", "4" },
  { "GPIO18", "GPIO18", "U1RXD", "CLK_OUT3", "4" },
  { "GPIO19", "GPIO19", "U1RTS", "CLK_OUT2", "4" },
  { "GPIO20", "GPIO20", "U1CTS", "CLK_OUT1", "4" },
  { "GPIO21", "GPIO21", "2", "3", "4" },
  { "1", "2", "3", "3", "4" },
  { "1", "2", "3", "3", "4" },
  { "1", "2", "3", "3", "4" },
  { "1", "2", "3", "3", "4" },
  { "SPICS1", "GPIO26", "2", "3", "4" },
  { "SPIHD", "GPIO27", "2", "3", "4" },
  { "SPIWP", "GPIO28", "2", "3", "4" },
  { "SPICS0", "GPIO29", "2", "3", "4" },
  { "SPICLK", "GPIO30", "2", "3", "4" },
  { "SPIQ", "GPIO31", "2", "3", "4" },
  { "SPID", "GPIO32", "2", "3", "4" },
  { "GPIO33", "GPIO33", "FSPIHD", "SUBSPIHD", "SPIIO4" },
  { "GPIO34", "GPIO34", "FSPICS0", "SUBSPICS0", "SPIIO5" },
  { "GPIO35", "GPIO35", "FSPID", "SUBSPID", "SPIIO6" },
  { "GPIO36", "GPIO36", "FSPICLK", "SUBSPICLK", "SPIIO7" },
  { "GPIO37", "GPIO37", "FSPIQ", "SUBSPIQ", "SPIDQS" },
  { "GPIO38", "GPIO38", "FSPIWP", "SUBSPIWP", "4" },
  { "MTCK", "GPIO39", "CLK_OUT3", "SUBSPICS1", "4" },
  { "MTDO", "GPIO40", "CLK_OUT2", "3", "4" },
  { "MTDI", "GPIO41", "CLK_OUT1", "3", "4" },
  { "MTMS", "GPIO42", "2", "3", "4" },
  { "U0TXD", "GPIO43", "CLK_OUT1", "3", "4" },
  { "U0RXD", "GPIO44", "CLK_OUT2", "3", "4" },
  { "GPIO45", "GPIO45", "2", "3", "4" },
  { "GPIO46", "GPIO46", "2", "3", "4" },
  { "SPICLK_P_DIFF", "GPIO47", "SUBSPICLK_P_DIFF", "3", "4" },
  { "SPICLK_N_DIFF", "GPIO48", "SUBSPICLK_N_DIFF", "3", "4" },
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][5] = {
  // ESP32S2 MUX functions for pins
  { "GPIO0", "GPIO0", "2", "3", "4" },
  { "GPIO1", "GPIO1", "2", "3", "4" },
  { "GPIO2", "GPIO2", "2", "3", "4" },
  { "GPIO3", "GPIO3", "2", "3", "4" },
  { "GPIO4", "GPIO4", "2", "3", "4" },
  { "GPIO5", "GPIO5", "2", "3", "4" },
  { "GPIO6", "GPIO6", "2", "3", "4" },
  { "GPIO7", "GPIO7", "2", "3", "4" },
  { "GPIO8", "GPIO8", "2", "SUBSPICS1", "4" },
  { "GPIO9", "GPIO9", "2", "SUBSPIHD", "FSPIHD" },
  { "GPIO10", "GPIO10", "FSPIIO4", "SUBSPICS0", "FSPICS0" },
  { "GPIO11", "GPIO11", "FSPIIO5", "SUBSPID", "FSPID" },
  { "GPIO12", "GPIO12", "FSPIIO6", "SUBSPICLK", "FSPICLK" },
  { "GPIO13", "GPIO13", "FSPIIO7", "SUBSPIQ", "FSPIQ", "" },
  { "GPIO14", "GPIO14", "FSPIDQS", "SUBSPIWP", "FSPIWP" },
  { "XTAL_32K_P", "GPIO15", "U0RTS", "3", "4" },
  { "XTAL_32K_N", "GPIO16", "U0CTS", "3", "4" },
  { "DAC_1", "GPIO17", "U1TXD", "3", "4" },
  { "DAC_2", "GPIO18", "U1RXD", "CLK_OUT3", "4" },
  { "GPIO19", "GPIO19", "U1RTS", "CLK_OUT2", "4" },
  { "GPIO20", "GPIO20", "U1CTS", "CLK_OUT1", "4" },
  { "GPIO21", "GPIO21", "2", "3", "4" },
  { "0", "1", "2", "3", "4" },
  { "0", "1", "2", "3", "4" },
  { "0", "1", "2", "3", "4" },
  { "0", "1", "2", "3", "4" },
  { "SPICS1", "GPIO26", "2", "3", "4" },
  { "SPIHD", "GPIO27", "2", "3", "4" },
  { "SPIWP", "GPIO28", "2", "3", "4" },
  { "SPICS0", "GPIO29", "2", "3", "4" },
  { "SPICLK", "GPIO30", "2", "3", "4" },
  { "SPIQ", "GPIO31", "2", "3", "4" },
  { "SPID", "GPIO32", "2", "3", "4" },
  { "GPIO33", "GPIO33", "FSPIHD", "SUBSPIHD", "SPIIO4" },
  { "GPIO34", "GPIO34", "FSPICS0", "SUBSPICS0", "SPIIO5" },
  { "GPIO35", "GPIO35", "FSPID", "SUBSPID", "SPIIO6" },
  { "GPIO36", "GPIO36", "FSPICLK", "SUBSPICLK", "SPIIO7" },
  { "GPIO37", "GPIO37", "FSPIQ", "SUBSPIQ", "SPIDQS" },
  { "GPIO38", "GPIO38", "FSPIWP", "SUBSPIWP", "4" },
  { "MTCK", "GPIO39", "CLK_OUT3", "SUBSPICS1", "4" },
  { "MTDO", "GPIO40", "CLK_OUT2", "3", "4" },
  { "MTDI", "GPIO41", "CLK_OUT1", "3", "4" },
  { "MTMS", "GPIO42", "2", "3", "4" },
  { "U0TXD", "GPIO43", "CLK_OUT1", "3", "4" },
  { "U0RXD", "GPIO44", "CLK_OUT2", "3", "4" },
  { "GPIO45", "GPIO45", "2", "3", "4" },
  { "GPIO46", "GPIO46", "2", "3", "4" },
#else
static const char *io_mux_func_name[13][6] = {
// unknown/unsupported target. make array big enough (6 functions)
#warning "Using dummy IO_MUX function name table"
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
#endif  // CONFIG_IDF_TARGET...
};      //static const char *io_mux_func_name[][] = {

// called by "pin X"
// moved to a separate function to offload giant cmd_pin() a bit
//
// Display pin information: function, direction, mode, pullup/pulldown etc
static int pin_show(int argc, char **argv) {

  unsigned int pin, informed = 0;

  if (argc < 2) return -1;
  if (!pin_exist((pin = q_atol(argv[1],999)))) return 1;

  bool pu, pd, ie, oe, od, sleep_sel, res;
  uint32_t drv, fun_sel, sig_out;
  peripheral_bus_type_t type;

  res = esp_gpio_is_pin_reserved(pin);
  q_printf("%% Pin %d is ", pin);

  if (res)
    q_print("<w>**RESERVED**</>, ");

  if (pin_is_strapping_pin(pin))
    q_print("strapping pin, ");

  if (pin_is_input_only_pin(pin))
    q_print("<i>**INPUT-ONLY**</>, ");

  if (!res)
    q_print("available, ");

  q_print("and is ");
  if ((type = perimanGetPinBusType(pin)) == ESP32_BUS_TYPE_INIT)
    q_print("not used by Arduino Core\r\n");
  else {
    
    if (type == ESP32_BUS_TYPE_GPIO)
      q_print("<i>configured as GPIO</>\r\n");
    else
      q_printf("used as \"<i>%s</>\"\r\n", perimanGetTypeName(type));
    
  }


  gpio_ll_get_io_config(&GPIO, pin, &pu, &pd, &ie, &oe, &od, &drv, &fun_sel, &sig_out, &sleep_sel);

  if (ie || oe || od || pu || pd || sleep_sel) {
    q_print("% Mode:<i> ");
    
    if (ie) q_print("INPUT, ");
    if (oe) q_print("OUTPUT, ");
    if (pu) q_print("PULL_UP, ");
    if (pd) q_print("PULL_DOWN, ");
    if (od) q_print("OPEN_DRAIN, ");
    if (sleep_sel) q_print("sleep mode selected,");
    if (!pu && !pd && ie) q_print(" input is floating!");
    
    q_print("</>\r\n");

    if (oe && fun_sel == PIN_FUNC_GPIO) {
      q_print("% Output via GPIO matrix, ");
      if (sig_out == SIG_GPIO_OUT_IDX)
        q_print("simple GPIO output\r\n");
      else
        q_printf("provides path for signal ID: %lu\r\n", sig_out);
    } else if (oe && fun_sel != PIN_FUNC_GPIO)
      q_printf("%% Output is done via IO MUX, (function: <i>%s</>)\r\n",io_mux_func_name[pin][fun_sel]);

    if (ie && fun_sel == PIN_FUNC_GPIO) {
      q_print("% Input via GPIO matrix, ");
      for (int i = 0; i < SIG_GPIO_OUT_IDX; i++) {
        if (gpio_ll_get_in_signal_connected_io(&GPIO, i) == pin) {
          if (!informed)
            q_print("provides path for signal IDs: ");
          informed++;
          q_printf("%d, ", i);
        }
      }

      if (!informed)
        q_print("simple GPIO input");
      q_print(CRLF);

    } else if (ie)
      q_printf("%% Input is done via IO MUX, (function: <i>%s</>)\r\n",io_mux_func_name[pin][fun_sel]);
  }


  // ESP32S3 has its pin 18 and 19 drive capability of 3 but the meaning is 2 and vice-versa
  // TODO:Other versions probably have the same behaviour on some other pins
#ifdef CONFIG_IDF_TAGET_ESP32S3
  if (pin == 18 || pin == 19) {
    if (drv == 2) drv == 3 else if (drv == 3) drv == 2;
  }
#endif
  q_printf("%% Maximum current is %u milliamps\r\n", !drv ? 5 : (drv == 1 ? 10 : (drv == 2 ? 20 : 40)));

  if (sleep_sel)
    q_print("% Sleep select: YES\r\n");

  // enable INPUT if was not enabled before
  //
  // As of Arduino Core 3.0.5 digitalRead() does not work following cases: 
  // 1. pin is interface pin (uart_tx as example),
  // 2. pin is not configured through PeriMan as "simple GPIO"
  // thats why IDF functions are used instead of digitalRead() and pinMode()
  if (!ie)
    gpio_ll_input_enable(&GPIO, pin);
  int val = gpio_ll_get_level(&GPIO, pin);
  if (!ie)
    gpio_ll_input_disable(&GPIO, pin);

  q_printf("%% Digital pin value is <i>%s</>\r\n",val ? "HIGH (1)" : "LOW (0)");
  return 0;
}

// "pin NUM arg1 arg2 .. argn"
// "pin NUM"
// Big fat "pin" command. Processes multiple arguments
// TODO: should I split into bunch of smaller functions?
static int cmd_pin(int argc, char **argv) {

  unsigned int flags = 0;
  unsigned int i = 2, pin;

  // repeat whole "pin ..." command "count" times.
  // this number can be changed by "loop" keyword
  unsigned int count = 1;

#if WITH_HELP
  bool informed = false;
#endif
  if (argc < 2) return -1;  //missing argument

  //first argument must be a decimal number: a GPIO number
  if (!pin_exist((pin = q_atol(argv[1], 999))))
    return 1;

  //"pin X" command is executed here
  if (argc == 2) return pin_show(argc, argv);

  //"pin arg1 arg2 .. argN"
  do {

    //Run through "pin NUM arg1, arg2 ... argN" arguments, looking for keywords
    // to execute.
    while (i < argc) {

      //1. "seq NUM" keyword found:
      if (!q_strcmp(argv[i], "seq")) {
        if ((i + 1) >= argc) {
#if WITH_HELP
          q_print("% <e>Sequence number expected after \"seq\"</>\r\n");
#endif
          return i;
        }
        i++;

        int seq, j;

        // enable RMT sequence 'seq' on pin 'pin'
        if (seq_isready((seq = q_atol(argv[i],999)))) {
#if WITH_HELP
          q_printf("%% Sending sequence %u over GPIO %u\r\n", seq, pin);
#endif
          if ((j = seq_send(pin, seq)) < 0)
            q_printf("%% <e>Failed. Error code is: %d</>\r\n", j);

        } else
          q_printf("%% <e>Sequence %u is not configured</>\r\n", seq);
      } else 
      //2. "pwm FREQ DUTY" keyword.
      // unlike global "pwm" command the duty and frequency are not an optional
      // parameter anymore. Both can be 0 which used to disable previous "pwm"
      if (!q_strcmp(argv[i], "pwm")) {

        unsigned int freq;
        float duty;
        // make sure that there are 2 extra arguments after "pwm" keyword
        if ((i + 2) >= argc) {
#if WITH_HELP
          q_print("% <e>Frequency and duty cycle are both expected</>\r\n");
#endif
          return i;
        }

        i++;

        // frequency must be an integer number and duty must be a float point number
        if ((freq = q_atol(argv[i++], MAGIC_FREQ+1)) > MAGIC_FREQ) {
#if WITH_HELP
          q_print("% <e>Frequency must be in range [1.." xstr(MAGIC_FREQ) "] Hz</>\r\n");
#endif
          return i - 1;
        }

        duty = q_atof(argv[i], -1.0f);
        if (duty < 0 || duty > 1) {
#if WITH_HELP
          q_print("% <e>Duty cycle is a number in range [0..1] (0.01 means 1% duty)</>\r\n");
#endif
          return i;
        }

        // enable/disable tone on given pin. if freq is 0 then tone is
        // disabled
        if (pwm_enable(pin, freq, duty) < 0) {
#if WITH_HELP
          q_print(Failed);
#endif
          return 0;
        }
      } else 
      //3. "delay X" keyword
      //creates delay for X milliseconds.
      if (!q_strcmp(argv[i], "delay")) {
        int duration;
        if ((i + 1) >= argc) {
#if WITH_HELP
          q_print("% <e>Delay value expected after keyword \"delay\"</>\r\n");
#endif
          return i;
        }
        i++;
        if ((duration = q_atol(argv[i],-1)) < 0)
          return i;
#if WITH_HELP
        if (!informed && (duration > 4999)) {
          informed = true;
          if (is_foreground_task())
            q_print("% Hint: Press <Enter> to interrupt the command\r\n");
        }
#endif
        // was interrupted by keypress or by "kill" command ? abort whole command
        if (delay_interruptible(duration) != duration) {
          q_print("% Aborted\r\n");
          return 0;
        }
      } else 
      //Now all the single-line keywords:
      // 5. "pin X save"
      if (!q_strcmp(argv[i], "save")) pin_save(pin); else 
      // 9. "pin X up" 
      if (!q_strcmp(argv[i], "up")) { flags |= PULLUP; pinMode2(pin, flags); } else  // set flags immediately as we read them 
      // 10. "pin X down"
      if (!q_strcmp(argv[i], "down")) {flags |= PULLDOWN; pinMode2(pin, flags); } else 
      // 12. "pin X in"
      if (!q_strcmp(argv[i], "in")) { flags |= INPUT; pinMode2(pin, flags);} else 
      // 13. "pin X out"
      if (!q_strcmp(argv[i], "out")) {flags |= OUTPUT; pinMode2(pin, flags); } else 
      // 11. "pin X open"
      if (!q_strcmp(argv[i], "open")) {flags |= OPEN_DRAIN;pinMode2(pin, flags); } else 
        // 14. "pin X low" keyword. only applies to I/O pins, fails for input-only pins
      if (!q_strcmp(argv[i], "low")) {
        if (pin_is_input_only_pin(pin)) {
abort_if_input_only:
          q_printf("%% <e>Pin %u is **INPUT-ONLY**, can not be set \"%s</>\"\r\n", pin, argv[i]);
          return i;
        }
        // use pinMode2/digitalForceWrite to not let the pin to be reconfigured
        // to GPIO Matrix pin. By default many GPIO pins are handled by IOMUX. However if
        // one starts to use that pin it gets configured as "GPIO Matrix simple GPIO". Code below
        // keeps the pin at IOMUX, not switching to GPIO Matrix
        flags |= OUTPUT;
        pinMode2(pin, flags);
        digitalForceWrite(pin, LOW);
      } else
      // 15. "pin X high" keyword. I/O pins only
      if (!q_strcmp(argv[i], "high")) {

        if (pin_is_input_only_pin(pin))
          goto abort_if_input_only;

        flags |= OUTPUT;
        pinMode2(pin, flags);
        digitalForceWrite(pin, HIGH);
       } else
       // 16. "pin X read"
       if (!q_strcmp(argv[i], "read")) q_printf("%% GPIO%d : logic %d\r\n", pin, digitalForceRead(pin)); else
       // 17. "pin X read"
       if (!q_strcmp(argv[i], "aread")) q_printf("%% GPIO%d : analog %d\r\n", pin, analogRead(pin)); else
       // 7. "pin X hold"
       if (!q_strcmp(argv[i], "hold")) gpio_hold_en((gpio_num_t)pin); else
       // 8. "pin X release"
       if (!q_strcmp(argv[i], "release")) gpio_hold_dis((gpio_num_t)pin); else
       // 6. "pin X load"
       if (!q_strcmp(argv[i], "load")) pin_load(pin); else
       //4. "loop" keyword
       if (!q_strcmp(argv[i], "loop")) {
        //must have an extra argument (loop count)
        if ((i + 1) >= argc) {
#if WITH_HELP
          q_print("% <e>Loop count expected after keyword \"loop\"</>\r\n");
#endif
          return i;
        }
        i++;

        // loop must be the last keyword, so we can strip it later
        if ((i + 1) < argc) {
#if WITH_HELP
          q_print("% <e>\"loop\" must be the last keyword</>\r\n");
#endif
          return i + 1;
        }
        if ((count = q_atol(argv[i],0)) == 0)
          return i;
        argc -= 2;  //strip "loop NUMBER" keyword
#if WITH_HELP
        if (!informed) {
          informed = true;
          q_printf("%% Repeating %u times", count);
          if (is_foreground_task())
            q_print(", press <Enter> to abort");
            q_print(CRLF);
        }
#endif
      } else
      //"X" keyword. when we see a number we use it as a pin number
      //for subsequent keywords. must be valid GPIO number.
      if (isnum(argv[i])) {
        if (!pin_exist((pin = q_atol(argv[i],9999))))
          return i;
      } else
      // argument i was not recognized
        return i;  
      i++;
    } //big fat "while (i < argc)" 
    i = 1;  // start over again

    //give a chance to cancel whole command
    // by anykey press
    if (anykey_pressed()) {
#if WITH_HELP
      q_print("% Key pressed, aborting..\r\n");
#endif
      break;
    }
  } while (--count > 0);  // repeat if "loop X" command was found
  return 0;
}

// async version of the "pin" command: "pin&"
//
// use with caution: here are no mutexes or refcoun on shared resources:
// running async "pin 2 seq 0" and simultaneous deletion of the sequence#0
// will result in undefined behaviour, most likely crash
//


// To execute async version of the "pin" command ("pin&") we
// start a separate task which calls cmd_pin()
//
static void pin_async_task(void *arg) {

  argcargv_t *aa = (argcargv_t *)arg;
  if (aa != NULL) {
    int ret = cmd_pin(aa->argc, aa->argv);
    userinput_unref(aa);
    if (ret != 0)
      q_print(Failed);
  }
  vTaskDelete(NULL);
}

// "pin& ARG1 ARG2 .. ARGn"
// "count& ... "
// and other async commands (commands which ends with "&")
//
static int cmd_async(int argc, char **argv) {

  TaskHandle_t ignored;

  if (aa_current == NULL)  //must not happen
    abort();

  TaskFunction_t cmd;

  // which command was called as async?
  if (!q_strcmp(argv[0], "pin&"))
    cmd = (TaskFunction_t)pin_async_task;
  else if (!q_strcmp(argv[0], "count&"))
    cmd = (TaskFunction_t)count_async_task;
  else {
    
    q_printf("%% <e>Don't know how to run \"%s\" in background</>\r\n", argv[0]);
    
    return 0;
  }

  //increase refcount on argcargv (tokenized user input) because it will be used by async task and
  // we dont want this memory to be freed immediately after this command finishes
  userinput_ref(aa_current);

  // Start async task
  if (pdPASS != xTaskCreatePinnedToCore(cmd, "Pin Async", STACKSIZE, aa_current, tskIDLE_PRIORITY, &ignored, shell_core)) {
    q_print("% <e>Can not start a new task. Resources low?</>\r\n");
    userinput_unref(aa_current);
  }

  //Hint user on how to stop bg command
  q_printf("%% Background task started\r\n%% Copy/paste \"kill %x\" command to stop execution\r\n", (unsigned int)ignored);

  return 0;
}

//TAG:mem
//"mem"
// Display memory amount (total/available) for different
// API functions: malloc() and heap_caps() allocator with different flags
//
static int cmd_mem(UNUSED int argc, UNUSED char **argv) {

  q_print("% -- Memory information --\r\n%\r\n"
          "% For \"malloc()\" (default allocator))\":\r\n");
  
  q_printf("%% <i>%u</> bytes total, <i>%u</> available, %u max per allocation\r\n%%\r\n", heap_caps_get_total_size(MALLOC_CAP_DEFAULT), heap_caps_get_free_size(MALLOC_CAP_DEFAULT), heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
  
  q_print("% For \"heap_caps_malloc(MALLOC_CAP_INTERNAL)\", internal SRAM:\r\n");
  
  q_printf("%% <i>%u</> bytes total,  <i>%u</> available, %u max per allocation\r\n%%\r\n", heap_caps_get_total_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  

  unsigned int total;
  if ((total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024) > 0)
    q_printf("%% External SPIRAM detected (available to \"malloc()\"):\r\n"
             "%% Total <i>%u</>Mbytes, free: <i>%u</> bytes\r\n", total / 1024, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  
#if MEMTEST
  q_memleaks("%% -- Memory currently used by ESPShell --\r\n");
#endif  
  return 0;
}
//"mem ADDR LENGTH"
// Display memory contens
// Only use with readable memory otherwise it will crash
//
static int cmd_mem_read(int argc, char **argv) {

  unsigned long length = 256;
  unsigned char *address;

  if (argc < 2)
    return -1;

  address = (unsigned char *)(hex2uint32(argv[1]));
  if (address == NULL)
    return 1;

  if (argc > 2)
    length = q_atol(argv[2],length);
  
  q_printhex(address, length);

  return 0;
}

//TAG:nap
//"nap NUM"
//"nap"
static int cmd_nap(int argc, char **argv) {

  static bool isen = false;
  // "nap" command: sleep until we receive at least 3 positive edges on UART pin
  // (press any key for a wakeup)
  if (argc == 1) {
    esp_sleep_enable_uart_wakeup(uart);  // wakeup by uart
    isen = true;
    uart_set_wakeup_threshold(uart, 3);  // 3 positive edges on RX pin to wake up ('spacebar' button two times)
  } else
    // "nap NUM" command: sleep NUM seconds. Wakeup by a timer
    if (argc == 2) {
      unsigned long sleep;
      if (isen) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_UART);  //disable wakeup by uart
        isen = false;
      }
      if ((sleep = q_atol(argv[1],0xffffffff)) == 0xffffffff) {
#if WITH_HELP
        q_printf("%% <e>Sleep time in seconds expected, instead of \"%s\"</>\r\n",argv[1]);
#endif        
        return 1;
      }
      esp_sleep_enable_timer_wakeup(1000000UL * (unsigned long)sleep);
    }
#if WITH_HELP
  q_print("% Light sleep..");
#endif
  esp_light_sleep_start();
#if WITH_HELP
  q_print("Resuming\r\n");
#endif
  return 0;
}


// unfortunately this one is not in header files
#ifdef __cplusplus
extern "C" bool i2cIsInit(uint8_t i2c_num);
#else
extern bool i2cIsInit(uint8_t i2c_num);
#endif


//TAG:iic
//check if I2C has its driver installed
// TODO: this is bad. need more low level call. esp32cams i2c is not detected as "up"
static inline bool i2c_isup(unsigned char iic) {
  return (iic >= SOC_I2C_NUM) ? false : i2cIsInit(iic);
}

//"iic X"
// save context, switch command list, change the prompt
static int cmd_i2c_if(int argc, char **argv) {

  unsigned int iic;
  static char prom[16];

  if (argc < 2)
    return -1;

  if ((iic = q_atol(argv[1],SOC_I2C_NUM)) >= SOC_I2C_NUM) {
#if WITH_HELP
    q_printf("%% <e>Valid I2C interface numbers are 0..%d</>\r\n", SOC_I2C_NUM - 1);
#endif
    return 1;
  }

  sprintf(prom,PROMPT_I2C,iic);
  change_command_directory(iic, keywords_i2c, prom, "I2C configuration");
  return 0;
}



//TAG:clock
//esp32-i2c#> clock FREQ
//
static int cmd_i2c_clock(int argc, char **argv) {

  unsigned char iic = (unsigned char)Context;

  if (argc < 2)
    return -1;

  if (!i2c_isup(iic)) {
    q_printf("%% <e>I2C%u is not initialized</>\r\n", iic);
#if WITH_HELP
    q_printf("%% Use command \"up\" to initialize</>\r\n");
#endif
    return 0;
  }

  // set clock to 100kHz if atol() fails
  if (ESP_OK != i2cSetClock(iic, q_atol(argv[1], 100000)))
    q_print(Failed);

  return 0;
}

// i2c commands handler, processes following commands in esp32-i2c#> category:
//"SDA SCL CLOCK"
//"down"
//"scan"
//"write ADDR A1 A2 A3 ... AN"
//"read ADDR NUM_BYTES"
#define I2C_RXTX_BUF 1024

static int cmd_i2c(int argc, char **argv) {

  unsigned char iic = (unsigned char)Context, sda, scl, addr;
  unsigned int clock = 0;
  int i,size;

  //"up" keyword: initialize i2c driver
  // on given pins with givent clockrate
  if (!q_strcmp(argv[0], "up")) {

    if (argc < 4)
      return -1;

    if (i2c_isup(iic)) {
#if WITH_HELP
      q_printf("%% <e>I2C%d is already initialized</>\r\n", iic);
#endif
      return 0;
    }

    
    if (!pin_exist((sda = q_atol(argv[1],999)))) return 1;  // SDA
    if (!pin_exist((scl = q_atol(argv[2],999)))) return 2;  // SCL
    if ((clock = q_atol(argv[3],0)) == 0)         return 3;  // CLOCK

    if (ESP_OK != i2cInit(iic, sda, scl, clock))
      q_print(Failed);
  } else if (!q_strcmp(argv[0], "down")) {
    if (!i2c_isup(iic))
      goto noinit;
    i2cDeinit(iic);
  }
  // "write ADDR byte1 byte2 ... byteN" keyword
  // send data to device address ADDR
  else if (!q_strcmp(argv[0], "write")) {  //write 4B 1 2 3 4

    // at least 1 but not more than 255 bytes
    if (argc < 3 || argc > I2C_RXTX_BUF)
      return -1;

    unsigned char data[argc];

    if (!i2c_isup(iic))
      goto noinit;

    // get i2c slave address
    if ((addr = q_atol(argv[1],0)) == 0)
      return 1;

    // read all bytes to the data buffer
    for (i = 2, size = 0; i < argc; i++) {
      if (!ishex2(argv[i]))
        return i;
      data[size++] = hex2uint8(argv[i]);
    }
    // send over
    q_printf("%% Sending %d bytes over I2C%d\r\n", size, iic);
    if (ESP_OK != i2cWrite(iic, addr, data, size, 2000))
      q_print(Failed);
  }
  // "read ADDR LENGTH" keyword:
  // read data from i2c device on address ADDR, request LENGTH
  // bytes to read
  else if (!q_strcmp(argv[0], "read")) {  //read 68 7

    size_t got;

    if (argc < 3) return -1;
    if ((addr = q_atol(argv[1],0)) == 0) return 1;
    // second parameter: requested size
    if ((size = q_atol(argv[2],I2C_RXTX_BUF+1)) > I2C_RXTX_BUF) {
      size = I2C_RXTX_BUF;
#if WITH_HELP
      q_printf("%% Size adjusted to the maxumum: %u bytes\r\n", size);
#endif
    }

    got = 0;
    unsigned char data[size];

    if (i2cRead(iic, addr, data, size, 2000, &got) != ESP_OK)
      q_print(Failed);
    else {
      if (got != size) {
        q_printf("%% <e>Requested %d bytes but read %d</>\r\n", size, got);
        got = size;
      }
      q_printf("%% I2C%d received %d bytes:\r\n", iic, got);
      q_printhex(data, got);
    }
  }
  // "scan" keyword
  //
  else if (!q_strcmp(argv[0], "scan")) {
    if (!i2c_isup(iic)) {
#if WITH_HELP
      q_printf("%% <e>I2C %d is not initialized</>\r\n", iic);
#endif
      return 0;
    }

    q_printf("%% Scanning I2C bus %d...\r\n", iic);

    for (addr = 1, i = 0; addr < 128; addr++) {
      unsigned char b;
      if (ESP_OK == i2cWrite(iic, addr, &b, 0, 500)) {
        i++;
        q_printf("%% Device found at <i>address %02X</>\r\n", addr);
      }
    }

    if (!i)
      q_print("% Nothing found\r\n");
    else
      q_printf("%% <i>%d</> devices found\r\n", i);
  } else return 0;

  return 0;
noinit:
  // love gotos
#if WITH_HELP
  q_printf("%% <e>I2C %d is not initialized</>\r\n", iic);
#endif
  return 0;
}


//defined in HardwareSerial and must be kept in sync
//unfortunately HardwareSerial.h can not be included directly in a .c code
#define SERIAL_8N1 0x800001c

//check if UART has its driver installed
static inline bool uart_isup(unsigned char u) {

  return u >= SOC_UART_NUM ? false : uart_is_driver_installed(u);
}

// Change to uart command tree
// "uart X"
static int cmd_uart_if(int argc, char **argv) {

  unsigned int u;
  static char prom[16];

  if (argc < 2)
    return -1;

  if ((u = q_atol(argv[1],SOC_UART_NUM)) >= SOC_UART_NUM) {
#if WITH_HELP
    q_printf("%% <e>Valid UART interface numbers are 0..%d</>\r\n", SOC_UART_NUM - 1);
#endif
    return 1;
  }
#if WITH_HELP
  if (uart == u)
    q_print("% <w>You are configuring Serial interface shell is running on!</> BE CAREFUL :)\r\n");
#endif

  sprintf(prom,PROMPT_UART,u);
  change_command_directory(u, keywords_uart, prom, "UART configuration");
  return 0;
}


//TAG:baud
static int cmd_uart_baud(int argc, char **argv) {

  unsigned char u = Context;

  if (argc < 2)
    return -1;

  if (!uart_isup(u)) {
    q_printf("%% <e>uart%u is not initialized</>\r\n", u);
#if WITH_HELP
    q_print("%% Use command \"up\" to initialize</>\r\n");
#endif
    return 0;
  }
  // set baud rate to 115200 if atol() fails
  if (ESP_OK != uart_set_baudrate(u, q_atol(argv[1],115200)))
    q_print(Failed);

  return 0;
}


//TAG:tap
// create uart-to-uart bridge between user's serial and "remote"
// everything that comes from the user goes to "remote" and
// vice versa
//
// returns  when BREAK_KEY is pressed

#define BREAK_KEY       3              // Keycode of an "Exit" key: CTRL+C to exit uart "tap" mode

static void
uart_tap(int remote) {

#define UART_RXTX_BUF 512
  size_t av;

  while (1) {

    // 1. read all user input and send it to remote
    while (1) {

      // fails when interface is down. must not happen.
      if ((av = console_available()) <= 0)
        break;

      // must not happen unless UART FIFO sizes were changed in ESP-IDF
      if (av > UART_RXTX_BUF)
        av = UART_RXTX_BUF;

      char buf[av];

      console_read_bytes(buf, av, portMAX_DELAY);
      // CTRL+C. most likely sent as a single byte (av == 1), so get away with
      // just checking if buf[0] == CTRL+C
      if (buf[0] == BREAK_KEY)
        return;
      uart_write_bytes(remote, buf, av);
      yield();
    }

    // 2. read all the data from remote uart and echo it to the user
    while (1) {

      // return here or we get flooded by driver messages
      if (ESP_OK != uart_get_buffered_data_len(remote, &av)) {
#if WITH_HELP
        q_printf("%% <e>UART%d is not initialized</>\r\n", remote);
#endif
        return;
      }

      if (av > UART_RXTX_BUF)
        av = UART_RXTX_BUF;
      else if (!av)
        break;

      char *buf[av];

      uart_read_bytes(remote, buf, av, portMAX_DELAY);
      console_write_bytes(buf, av);
      delay(1);  //TODO: yield() ?
    }
  }
}

//TAG:uart
//"down"
//"up RX TX BAUD"
//"write TEXT"
//"read"
//"tap"
//TODO: split to separate functions
static int cmd_uart(int argc, char **argv) {

  int sent = 0;
  unsigned char u = Context;


  // 1. "tap" command
  if (!q_strcmp(argv[0], "tap")) {
    if (uart == u) {
      //do not tap to the same uart we are running on
      q_print("% <e>Can not bridge to itself</>\r\n");
      return 0;
    }

    if (!uart_isup(u))
      goto noinit;

    q_printf("%% Tapping to UART%d, CTRL+C to exit\r\n", u);
    uart_tap(u);
    q_print("\r\n% Ctrl+C, exiting\r\n");
    return 0;
  } else
  // 2. "up" command
  if (!q_strcmp(argv[0], "up")) {  //up RX TX SPEED

    unsigned int rx, tx, speed;
    if (argc < 4)
      return -1;

    // sanity checks for arguments
    if (!pin_exist((rx = q_atol(argv[1],999)))) return 1;
    if (!pin_exist((tx = q_atol(argv[2],999)))) return 2;
    if ((speed = q_atol(argv[3],0)) == 0)    return 3;

    if (NULL == uartBegin(u, speed, SERIAL_8N1, rx, tx, 256, 0, false, 112))
      q_print(Failed);
#if WITH_HELP
    else
      q_printf("%% UART%u is initialized (RX=pin%u, TX=pin%u, speed=%u, bits: 8N1)\r\n",u,rx,tx,speed);
#endif      
  } else
    // "down" command
  if (!q_strcmp(argv[0], "down")) {  // down
    if (!uart_isup(u))
      goto noinit;
    else
      uartEnd(u);
  } else
    // "write TEXT" command
  if (!q_strcmp(argv[0], "write")) {
    int size;
    char *out = NULL;          

    if (argc < 2)
      return -1;

    if (!uart_isup(u))
      goto noinit;

    size = text2buf(argc,argv,1,&out);
          
    if (size > 0)
      if ((size = uart_write_bytes(u, out, size)) > 0)
        sent+=size;
    if (out)
      q_free(out);

    q_printf("%% %u bytes sent\r\n", sent);
  } else
  // "read" command
  if (!q_strcmp(argv[0], "read")) {
    size_t available = 0, tmp;
    if (ESP_OK != uart_get_buffered_data_len(u, &available))
      goto noinit;
    tmp = available;
    while (available--) {
      unsigned char c;
      if (uart_read_bytes(u, &c, 1, portMAX_DELAY /* TODO: make short delay! */) == 1) {
        if (c >= ' ' || c == '\r' || c == '\n' || c == '\t')
          q_printf("%c", c);
        else
          q_printf("\\x%02x", c);
      }
    }
    q_printf("\r\n%% %d bytes read\r\n", tmp);
  }
  return 0;
noinit:
  q_printf("%% <e>UART%d is not initialized</>\r\n", u);
  return 0;
}

//TAG:tty
//"tty NUM"
//
// Set UART (or USBCDC) to use by this shell.
static int cmd_tty(int argc, char **argv) {

  unsigned char tty;

  if (argc < 2)
    return -1;

  if ((tty = q_atol(argv[1],100)) < 100) {
    // if not USB then check if requested UART is up & running
    if ((tty == 99) || ((tty < 99) && uart_isup(tty))) {
#if WITH_HELP
      q_print("% See you there\r\n");
#endif
      console_here(tty);
      return 0;
    }
  } else
    q_print("%% <e>Uart number expected. (use 99 for USB CDC)</>\r\n");

  if (tty < 99) {
    q_printf("%% <e>UART%u is not initialized</>.\r\n", tty);
#if WITH_HELP
    q_printf("%% Use commands \" uart %u\" and \"up\" commands to initialize it\r\n", tty);
#endif    
  }
  return 0;
}

//TAG:echo
//"echo on|off|silent"
//
// Enable/disable local echo. Normally enabled, it lets software
// like TeraTerm and PuTTY to be used. Turning echo off supresses
// all shell output (except for command handlers output)
//
// Setting "echo silent" has effect of "echo off" + all command output
// is supressed as well. commands executed do not output anything
//
static int cmd_echo(int argc, char **argv) {

  if (argc < 2)
    q_printf("%% Echo %s\r\n", Echo ? "on" : "off");  //if echo is silent we can't see it anyway so no bother printing
  else {
    if (!q_strcmp(argv[1], "on")) Echo = 1;
    else if (!q_strcmp(argv[1], "off")) Echo = 0;
    else if (!q_strcmp(argv[1], "silent")) Echo = -1;
    else return 1;
  }
  return 0;
}


//TAG:reload
//"reload"
static int NORETURN cmd_reload(int argc, char **argv) {
  esp_restart();
  /* NOT REACHED */
  //return 0;
}


extern uint32_t getCpuFrequencyMhz();
extern bool setCpuFrequencyMhz(uint32_t cpu_freq_mhz);
extern uint32_t getXtalFrequencyMhz();
extern uint32_t getApbFrequency();


//"cpu"
//
// Display CPU ID information, frequencies and
// chip temperature
//


static int cmd_cpu(int argc, char **argv) {

  esp_chip_info_t chip_info;

  uint32_t chip_ver;
  uint32_t pkg_ver;
  const char *chipid = "ESP32-(Unknown)>";

  esp_chip_info(&chip_info);

#if CONFIG_IDF_TARGET_ESP32
  chip_ver = REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_PACKAGE);
  pkg_ver = chip_ver & 0x7;

  switch (pkg_ver) {

    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6:
      if (chip_info.revision / 100 == 3) chipid = "ESP32-D0WDQ6-V3";
      else chipid = "ESP32-D0WDQ6";
      break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5:
      if (chip_info.revision / 100 == 3) chipid = "ESP32-D0WD-V3";
      else chipid = "ESP32-D0WD";
      break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5: chipid = "ESP32-D2WD-Q5"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD2: chipid = "ESP32-PICO-D2"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4: chipid = "ESP32-PICO-D4"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302: chipid = "ESP32-PICO-V3-02"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3: chipid = "ESP32-D0WDR2-V3"; break;
    default: q_printf("%% Detected PKG_VER=%04x\r\n", (unsigned int)pkg_ver);
  }
#elif CONFIG_IDF_TARGET_ESP32S2
  pkg_ver = REG_GET_FIELD(EFUSE_RD_MAC_SPI_SYS_3_REG, EFUSE_PKG_VERSION);
  switch (pkg_ver) {
    case 0: chipid = "ESP32-S2"; break;
    case 1: chipid = "ESP32-S2FH16"; break;
    case 2: chipid = "ESP32-S2FH32"; break;
  }
#else
  switch (chip_info.model) {
    case CHIP_ESP32S3: chipid = "ESP32-S3"; break;
    case CHIP_ESP32C3: chipid = "ESP32-C3"; break;
    case CHIP_ESP32C2: chipid = "ESP32-C2"; break;
    case CHIP_ESP32C6: chipid = "ESP32-C6"; break;
    case CHIP_ESP32H2: chipid = "ESP32-H2"; break;
  }
#endif  //CONFIG_IDF_TARGET_XXX

  q_printf("\r\n%% CPU ID: %s, Rev.: %d.%d\r\n%% CPU frequency is %luMhz, Xtal %luMhz, APB bus %luMhz\r\n%% Chip temperature: %.1f\xe8 C\r\n",
           chipid,
           (chip_info.revision >> 8) & 0xf,
           chip_info.revision & 0xff,
           getCpuFrequencyMhz(),
           getXtalFrequencyMhz(),
           getApbFrequency() / 1000000,
           temperatureRead());

  q_printf("%%\r\n%% Sketch is running on " ARDUINO_BOARD "/(" ARDUINO_VARIANT "), uses Arduino Core v%s, based on\r\n%% Espressif ESP-IDF version \"%s\"\r\n", ESP_ARDUINO_VERSION_STR, esp_get_idf_version());
  cmd_uptime(argc, argv);
  return 0;
}

//"cpu CLOCK"
//
// Set cpu frequency.
//
static int cmd_cpu_freq(int argc, char **argv) {

  if (argc < 2)
    return -1;  // not enough arguments

  unsigned int freq;
  
  if ((freq = q_atol(argv[1],0)) == 0) {
#if WITH_HELP
    q_print("% Numeric value is expected (e.g. 240): frequency in MHz\r\n");
#endif
    return 1;
  }

  while (freq != 240 && freq != 160 && freq != 120 && freq != 80) {

    unsigned int xtal = getXtalFrequencyMhz();

    if ((freq == xtal) || (freq == xtal / 2)) break;
    if ((xtal >= 40) && (freq == xtal / 4)) break;
#if WITH_HELP
    q_print("% Supported frequencies are: 240, 160, 120, 80, ");

    if (xtal >= 40)
      q_printf("%u, %u and %u\r\n", xtal, xtal / 2, xtal / 4);
    else
      q_printf("%u and %u\r\n", xtal, xtal / 2);
#endif
    return 1;
  }

  if (!setCpuFrequencyMhz(freq))
    q_print(Failed);

  return 0;
}

// external user-defined command handler functions here
#ifdef EXTERNAL_HANDLERS
#  include EXTERNAL_HANDLERS
#endif

// "uptime"
//
// Displays system uptime as returned by esp_timer_get_time() counter
// Displays last reboot cause
static int
cmd_uptime(UNUSED int argc, UNUSED char **argv) {

  unsigned int sec, min = 0, hr = 0, day = 0;
  sec = (unsigned int)(esp_timer_get_time() / 1000000);

  const char *rr;

  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: rr = "power-on event"; break;
    case ESP_RST_SW: rr = "reload command"; break;
    case ESP_RST_PANIC: rr = "panic()!"; break;
    case ESP_RST_INT_WDT: rr = "an interrupt watchdog"; break;
    case ESP_RST_TASK_WDT: rr = "a task watchdog"; break;
    case ESP_RST_WDT: rr = "an unspecified watchdog"; break;
    case ESP_RST_DEEPSLEEP: rr = "coming up from deep sleep"; break;
    case ESP_RST_BROWNOUT: rr = "brownout"; break;
    case ESP_RST_SDIO: rr = "SDIO"; break;
    case ESP_RST_USB: rr = "USB event"; break;
    case ESP_RST_JTAG: rr = "JTAG"; break;
    case ESP_RST_EFUSE: rr = "eFuse errors"; break;
    case ESP_RST_PWR_GLITCH: rr = "power glitch"; break;
    case ESP_RST_CPU_LOCKUP: rr = "lockup (double exception)"; break;
    default: rr = "no idea";
  };

  q_print("% Last boot was ");
  if (sec > 60 * 60 * 24) {
    day = sec / (60 * 60 * 24);
    sec = sec % (60 * 60 * 24);
    q_printf("%u day%s ", day, day == 1 ? "" : "s");
  }
  if (sec > 60 * 60) {
    hr = sec / (60 * 60);
    sec = sec % (60 * 60);
    q_printf("%u hour%s ", hr, hr == 1 ? "" : "s");
  }
  if (sec > 60) {
    min = sec / 60;
    sec = sec % 60;
    q_printf("%u minute%s ", min, min == 1 ? "" : "s");
  }

  q_printf("%u second%s ago\r\n%% Restart reason was \"%s\"\r\n", sec, sec == 1 ? "" : "s", rr);


  return 0;
}


//TAG:suspend
// task handle of a task which calls Arduino's loop()
// defined somewhere in the ESP32 Arduino Core
extern TaskHandle_t loopTaskHandle;

//"suspend"
// suspends the Arduino loop() task
static int cmd_suspend(int argc, char **argv) {

  vTaskSuspend(loopTaskHandle);

  return 0;
}

//"resume"
// Resume previously suspended loop() task
static int cmd_resume(int argc, char **argv) {

  vTaskResume(loopTaskHandle);

  return 0;
}

//"kill TASK_ID"
// kill a espshell task.
// TODO: make async tasks have their TASK_ID mapped to simple numbers starting from 1.
static int cmd_kill(int argc, char **argv) {

  if (argc < 2)
    return -1;

  unsigned int taskid = hex2uint32(argv[1]);
  if (taskid == 0) {
#if WITH_HELP
    q_print("% Task id is a hex number, something like \"3fff0030\"\r\n");
#endif
    return 1;
  }

  TaskHandle_t handle = (TaskHandle_t)taskid;
  if (shell_task == handle) {
    q_print(Failed);
    return 0;
  }

  // Try to finish the task via notification. This allows commands
  // to print their intermediate results: one of examples is "count&" command:
  // being killed it will print out packets count arrived so far
  //
  xTaskNotify(handle, 0, eNoAction);

  //undocumented
  if (argc > 2) {
    if (!q_strcmp(argv[2], "terminate")) {
      vTaskDelete(handle);
#if WITH_HELP
      q_printf("%% Terminated: \"%p\"\r\n", handle);
#endif
    }
  }
  return 0;
}

//            -- Filesystem --
// Filesystems support. So far ESPShell supports FAT, LittleFS and SPIFFS file systems
// Code below (within #if WITH_FS .. #endif) is all about that.
//  Command handlers names start with "cmd_files_...", utility & helper functions are
// all have names which start with "files_"
//

#if WITH_FS
static char *Cwd = NULL;  // Current working directory. Must end with "/"

// espshell allows for simultaneous mounting up to MOUNTPOINTS_NUM partitions
// mountpoints[] holds information about mounted filesystems.
//
static struct {
  char *mp;             // mount point e.g. "/a/b/c/d"
  char label[16 + 1];   // partition label e.g. "ffat", "spiffs" or "littlefs"  TODO: use idf macros or sizeof(something) to get rid of "16+1"
  unsigned char type;   // partition subtype
#if WITH_FAT  
  wl_handle_t wl_handle;// FAT wear-levelling library handle
#endif  
} mountpoints[MOUNTPOINTS_NUM] = { 0 };

// remove trailing path separators
static void files_strip_trailing_slash(char *p) {
  if (p && *p) {
    int i = strlen(p);
    if (p[i - 1] == '\\' || p[i - 1] == '/') {
      p[i - 1] = '\0';
      files_strip_trailing_slash(p);
    }
  }
}

// is path == "/" ?
static INLINE bool files_path_is_root(const char *path) {
  return (path && (path[0] == '/' || path[0] == '\\') && (path[1] == '\0'));
}

// Check if path is ok for file/directory creation
// Any objects in "/" are impossible except for mountpoint dirs
// TODO: add more checks here: double dots, bad characters, too long and so on
//
static bool files_path_impossible(const char *path) {
  int separators = 0;
  while (*path && (separators < 2)) {
    if (*path == '\\' || *path == '/')
      separators++;
    path++;
  }
  return *path == '\0';
}

// read lines from a text file
// \n is the line separator, (\r and \n arediscarded).
//
// returns number of bytes read (0 means end of file is reached)
//
// on the first call set buf = NULL, don't change buf & size on subsequent
// calls to files_getline(). When done, free(buf) if it is non-zero
//
static int files_getline(char **buf, unsigned int *size, FILE *fp) {

  int c;
  char *wp, *end;

  // no buffer provided? allocate our own
  if (*buf == NULL)
    if ((*buf = (char *)q_malloc((*size = 128),MEM_GETLINE)) == NULL)
      return -1;

  if (feof(fp))
    return -1;

  wp  = *buf;          // buffer write pointer
  end = *buf + *size;  // buffer end pointer

  while ( true ) {
    
    c = fgetc(fp);

    // end of line or end of file? return what was read so far
    if ((c < 0) || (c == '\n')) {
      *wp ='\0';
      return (wp - *buf);
    }

    //skip Microsoft-style line endings and save byte to the buffer
    if (c == '\r')
      continue;
    *wp++ = c;

    // do we still have space to store 1 character + '\0'?
    // if not - increase buffer size two times
    if (wp + sizeof(char) + sizeof(char) >= end) {

      char   *tmp;
      int     written_so_far = wp - *buf;

      if ((tmp = (char *)q_realloc(*buf, (*size *= 2),MEM_GETLINE)) == NULL)
        return -1;

      // update pointers
      *buf = tmp;
      end  = tmp + *size;
      wp   = tmp + written_so_far;
    }
  }
}

// convert time_t to "Jun-10-2022 10:40:07"
// not reentrant
static char *files_time2text(time_t t) {
  static char buf[32];
  struct tm *info;
  info = localtime( &t );
  sprintf(buf,"%u-%02u-%02u %02u:%02u:%02u",info->tm_year + 1900, info->tm_mon + 1,info->tm_mday, info->tm_hour,info->tm_min,info->tm_sec);
  return buf;
}

// set CWD
// path must include a mountpoint as well: "/ffat/my_dir" if
// one wishes to read/write files
//
static const char *files_set_cwd(const char *cwd) {

  static char prom[256+16] = { 0 };

  if (Cwd != cwd) {
    if (Cwd) {
      q_free(Cwd);
      Cwd = NULL;
    }
    if (cwd) {
      int len = strlen(cwd);
      if (len) {
        Cwd = (char *)q_malloc(len + 2,MEM_CWD);
        if (Cwd) {
          strcpy(Cwd, cwd);

          len--;
          if (Cwd[len] != '/' && cwd[len] != '\\')
            strcat(Cwd, "/");
        }
      }
    }
  }

  sprintf(prom,PROMPT_FILES,
    Color ? "\033[33;93m" : "",  // enable standart ANSI yellow and then bright color. One of them is supported for sure :)
    Cwd ? Cwd : "?",
    Color ? "\033[0m" : "");

  prompt = prom;

  return Cwd;
}

// return current working directory or NULL if there
// were memory allocation errors
//
static inline const char *files_get_cwd() {
  return Cwd ? Cwd : files_set_cwd("/");
}

// Convert "*" to spaces in paths. Spaces in paths are entered as asteriks
// "path" must be writeable memory.
//"Program*Files*(x64)" gets converted to "Program Files (x64)"
//
static void files_asteriks2spaces(char *path) {
  if (path) {
    while (*path != '\0') {
      if (*path == '*')
        *path = ' ';
      path++;
    }
  }
}


// Subtype of DATA entries in human-readable form
static const char *files_subtype2text(unsigned char subtype) {

  switch (subtype) {

    // Supported filesystems:
    case ESP_PARTITION_SUBTYPE_DATA_FAT: return " FAT/exFAT ";
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: return "    SPIFFS ";
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS: return "  LittleFS ";

    // Not supported file systems:
    case ESP_PARTITION_SUBTYPE_DATA_OTA: return "  OTA data ";
    case ESP_PARTITION_SUBTYPE_DATA_PHY: return "  PHY data ";
    case ESP_PARTITION_SUBTYPE_DATA_NVS: return " NVStorage ";
    case ESP_PARTITION_SUBTYPE_DATA_COREDUMP: return " Core dump ";
    case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS: return "  NVS keys ";
    case ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM: return " eFuse emu ";
    case ESP_PARTITION_SUBTYPE_DATA_UNDEFINED: return " Undefined ";
    case ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD: return " ESP HTTPD ";

    default: return " *Unknown* ";
  }
}
// -- Mountpoints: allocation & query --
// These are represented by mountpoints[MOUNTPOINTS_NUM] global array which contains mount point path,
// partition label name and some other information
//
// Mountpoints are referenced by their index rather than pointers.



// Find a mountpoint (index) by partition label (accepts shortened label names)
// If /label/ is NULL, then this function returns first unused entry in mountpoints[] array
// If there is no mountpoint which serves partition /label/ then -1 is returned
// On success the mountpoint index returned
//
static int files_mountpoint_by_label(const char *label) {
  int i;
  for (i = 0; i < MOUNTPOINTS_NUM; i++)
    if ((!label && !mountpoints[i].label[0]) ||
        (label && !q_strcmp(label, mountpoints[i].label)))
      return i;
  return -1;
}

// Find mountpoint index by arbitrary path.
// Path must include mount point (be absolute)
// Similar to files_mountpoint_by_label()
//
static int files_mountpoint_by_path(const char *path, bool reverse) {
  int i;
  for (i = 0; i < MOUNTPOINTS_NUM; i++)
    if ((!path && !mountpoints[i].mp) ||
        (path && mountpoints[i].mp && !q_strcmp(mountpoints[i].mp,path)) ||
        (reverse && path && mountpoints[i].mp && !q_strcmp(path,mountpoints[i].mp)))
      return i;
  return -1;
}

// All this code is just to make esp_partition_find() be able to
// find a partition by incomplete (shortened) label name
// TODO: rewrite partition lookup code to use this function
const esp_partition_t *files_partition_by_label(const char *label) {

  esp_partition_iterator_t it;

  if ((it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL)) != NULL)
    do {
      const esp_partition_t *part = esp_partition_get(it);
      if (part && (part->type == ESP_PARTITION_TYPE_DATA) && !q_strcmp(label, part->label)) {
        esp_partition_iterator_release(it);
        return part;
      }
  } while ((it = esp_partition_next(it)) != NULL);
  return NULL;
}


// make full path from path and return pointer to resulting string.
// function uses static buffer to store result so it is cannot be called in recursive function
// without copying the result to stack
//
// If /path/ starts from "/" then it is used as is
//                otherwhise
// path is appended to cwd and this full path is used
// Asteriks (*), if present, are converted to spaces ( )
//
static char *files_full_path(const char *path) {

  static char out[256+16];
  int len, cwd_len;

  // is cwd ok?
  if ((Cwd == NULL) && (files_set_cwd("/") == NULL))
      return NULL;

  len = strlen(path);

  if (path[0] == '/' || path[0] == '\\') {
    if (len >= sizeof(out))
      return NULL;
    strcpy(out, path);
    goto done;
  }

  cwd_len = strlen(Cwd);
  if ((len + cwd_len) >= sizeof(out))
    return NULL;
  strcpy(out, Cwd);
  strcat(out, path);
done:
  files_asteriks2spaces(out);
  return out;
}

// check if given path (directory or file) exists
// FIXME: spiffs allows for a/b/c/d and says its a valid path: it has then many consequences :(
static bool files_path_exist(const char *path, bool directory) {

  // LittleFS & FAT have proper stat() while SPIFFs doesn't
  // that why we do double check here
  struct stat st;
  DIR *d;

  if (!(path && *path))
    return false;

  // report that directory "/" does exist (actually it doesn't)
  if (files_path_is_root(path))
    return directory;

  int len = strlen(path);
  char path0[len + 1];

  strcpy(path0,path);
  files_strip_trailing_slash(path0);
  
  // try stat().. (FAT & LittleFS)
  if (0 == stat(path0, &st))
     return directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode);

  // try opendir()..(SPIFFS workaround: stat(path_to_directory) returns crap on SPIFFS)
  if (directory && (d = opendir(path0)) != NULL) {
    closedir(d);
    return true;
  }

  return false;
}

// return total bytes available on mounted filesystem index i (index in mountpoints[i])
//
static unsigned int files_space_total(int i) {

  switch (mountpoints[i].type) {
#if WITH_FAT    
    case ESP_PARTITION_SUBTYPE_DATA_FAT:
      FATFS *fs;
      DWORD free_clust, tot_sect, sect_size;
      BYTE pdrv = ff_diskio_get_pdrv_wl(mountpoints[i].wl_handle);
      char drv[3] = { (char)(48 + pdrv), ':', 0 };
      if (f_getfree(drv, &free_clust, &fs) != FR_OK)
        return 0;
      tot_sect = (fs->n_fatent - 2) * fs->csize;
      sect_size = CONFIG_WL_SECTOR_SIZE;
      return tot_sect * sect_size;
#endif
#if WITH_LITTLEFS
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:
      size_t total, used;
      if (esp_littlefs_info(mountpoints[i].label, &total, &used))
        return 0;
      return total;
#endif
#if WITH_SPIFFS
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
      if (esp_spiffs_info(mountpoints[i].label, &total, &used))
        return 0;
      return total;
#endif      
    default:
  }
  return 0;
}

// return amount of space available for allocating
//
static unsigned int files_space_free(int i) {
  switch (mountpoints[i].type) {
#if WITH_FAT    
    case ESP_PARTITION_SUBTYPE_DATA_FAT:
      FATFS *fs;
      DWORD free_clust, free_sect, sect_size;

      BYTE pdrv = ff_diskio_get_pdrv_wl(mountpoints[i].wl_handle);
      char drv[3] = { (char)(48 + pdrv), ':', 0 };
      if (f_getfree(drv, &free_clust, &fs) != FR_OK)
        return 0;

      free_sect = free_clust * fs->csize;
      sect_size = CONFIG_WL_SECTOR_SIZE;
      return free_sect * sect_size;
#endif
#if WITH_LITTLEFS
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:
      size_t total, used;
      if (esp_littlefs_info(mountpoints[i].label, &total, &used))
        return 0;
      return total - used;
#endif
#if WITH_SPIFFS
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
      if (esp_spiffs_info(mountpoints[i].label, &total, &used))
        return 0;
      return total - used;
#endif
    default:
  }
  return 0;
}

// handy macro
#define files_space_used(I) (files_space_total(I) - files_space_free(I))

// callback which is called by files_walkdir() on every entry it founds.
typedef int (* files_walker_t)(const char *);

// Walk thru the directory tree starting at /path/ (i.e. /path/ itself and all its subdirs)
// on every file entry file_cb() is called, on every directory entry dir_cb() is called
//
static unsigned int files_dirwalk(const char *path0, files_walker_t files_cb, files_walker_t dirs_cb, int depth) {

  char path[256+16], *p;
  int len;
  DIR *dir;
  unsigned int processed = 0;

  if (depth < 1)
    return 0;

  // figure out full path, if needed
  if ((p = files_full_path(path0)) == NULL)
    return 0;
  
  // save a copy, files_full_path's buffer is not reentrat 
  len = strlen(p);
  if (len < 1 || len > (sizeof(path) - 8)) // 8 - reserve some space "/" appending
    return 0;
  strcpy(path,p);

  // directory exists?
  if (files_path_exist(path,true)) {

    // append "/"" to the path if it was not there already
    if (path[len-1] != '\\' && path[len-1] != '/') {
      path[len++] = '/';
      path[len] = '\0';
    }

    // Walk through the directory, entering all subdirs in recursive way
    if ((dir = opendir(path)) != NULL) {
      struct dirent *de;
      while((de = readdir(dir)) != NULL) {

        path[len] = '\0';        // cut off previous addition
        strcat(path,de->d_name); // add entry name to our path

        // if its a directory - call recursively
        if (de->d_type == DT_DIR)
          processed += files_dirwalk(path,files_cb,dirs_cb, depth - 1);
        else 
          if (files_cb)
            processed += files_cb(path);
      }
      closedir(dir);
      path[len] = '\0';
      if (dirs_cb)
        processed += dirs_cb(path);
    }
  }
  return processed;
}

// Callback to be used by files_remove() when it calls to files_dirwalk()
// This callback gets called for every FILE that needs to be removed
static int remove_file_callback(const char *path) {
  if (0 != unlink(path)) {
#if WITH_HELP    
    q_printf("%% <e>Failed to delete: \"%s\"</>\r\n",path);
#endif    
    return 0;
  }
#if WITH_HELP  
  q_printf("%% Deleted file: \"%s\"\r\n",path);
#endif  
  return 1;
}

// Callback to be used by files_remove() when it calls to files_dirwalk()
// This callback gets called for every DIRECTORY that needs to be removed
static int remove_dir_callback(const char *path) {
  if (rmdir(path) == 0) {
#if WITH_HELP        
    q_printf("%% Directory removed: \"%s\"\r\n",path);
#endif        
    return 1;
  }
#if WITH_HELP  
  q_printf("%% <e>Failed to delete: \"%s\"</>\r\n",path);
#endif  
  return 0;
}

// Remove file/directory with files recursively
// /path/  is file or directory path
// /depth/ is max recursion depth
// Returns number of items removed (files+directories)
//
static int files_remove(const char *path0, int depth) {
  char path[256+16], *p;    // TODO: use some MAX_PATH idf macro

  if (depth < 1)
    return 0;
  
  // make full path if necessary
  if ((p = files_full_path(path0)) == NULL)
    return 0;

  // make a copy of full path as files_full_path()'s buffer is not reentrant (static)
  strcpy(path,p);
  
  
  if (files_path_exist(path,false))     // a file?
    return unlink(path) == 0 ? 1 : 0;
  else if (files_path_exist(path,true)) // a directory?
    return files_dirwalk(path,remove_file_callback, remove_dir_callback, DIR_RECURSION_DEPTH);
  else                                  // bad path
    q_printf("%% <e>File/directory \"%s\" does not exist</>\r\n",path);
  return 0;
}


// Callback to be used by files_size() when it calls to files_dirwalk()
// This callback gets called for every FILE which size was requested
static int size_file_callback(const char *p) {
  struct stat st;
  if (stat(p,&st) == 0)
    return st.st_size;
  return 0;
}

// get file/directory size in bytes
// /path/ is the path to the file or to the directory
//
static unsigned int files_size(const char *path) {

  struct stat st;
  char p[256+16];
  char *path0 = files_full_path(path);
  if (!path0)
    return 0;
  strcpy(p,path0);

  // size of file requested
  if (files_path_exist(p, false)) {
    if (stat(p,&st) == 0)
      return st.st_size;
    q_printf("files_size() : stat() failed on an existing file \"%s\"\r\n",p);
    return 0;
  }

  // size of a directory requested
  if (files_path_exist(p, true))
    return files_dirwalk(path,size_file_callback, NULL, DIR_RECURSION_DEPTH);

#if WITH_HELP
  q_printf("%% <e>Path \"%s\" does not exist\r\n",p);
#endif
  return 0;
}

// display (or send over uart interface) binary file content starting from byte offset "line"
// "count" is either 0xffffffff (means "whole file") or data length
//
// When displayed the file content is formatted as a table so it is easy to read. When file is
// transferred to another UART raw content is sent instead so file can be saved on the remote side
//
// /device/ is either uart nunmber (to send raw data) or -1 to do fancy human readable output
// /path/ is the full path
//
static int files_cat_binary(const char *path, unsigned int line, unsigned int count, unsigned char device) {

  unsigned int size, sent = 0, plen = 5*1024; //TODO: use 64k blocks if we have SPI RAM
  unsigned char *p;
  FILE *f;
  size_t r;

  if ((size = files_size(path)) > 0) {
    if (line < size) {
      if (size < plen)
        plen = size;
      if ((p = (unsigned char *)q_malloc(plen + 1,MEM_CAT)) != NULL) {
        if ((f = fopen(path,"rb")) != NULL) {
          if (line) {
            if (fseek(f, line, SEEK_SET) != 0) {
              q_printf("%% <e>Can't position to offset %u (0x%x)\r\n",line,line);
              goto fail;
            }
          }
          while (!feof(f) && (count > 0)) {
            if ((r = fread(p,1,count < plen ? count : plen,f)) > 0) {
              count -= r;
              if (device == (unsigned char)(-1))
                  q_printhex(p,r);
              else
                uart_write_bytes(device,p,r);
              sent += r;
            }
          }
#if WITH_HELP          
          q_printf("%% EOF (%u bytes)\r\n",sent);
#endif          
fail:          
          fclose(f);
        } else q_printf("%% <e>Failed to open \"%s\" for reading</>\r\n",path);
        q_free(p);
      } else q_print("%% Out of memory\r\n");
    } else q_printf("%% <e>Offset %u (0x%x) is beyound the file end. File size is %u</>\r\n",line,line,size);
  } else q_print("%% Empty file\r\n");
  return 0;
}

// read file 'path' line by line and display it as that
// /line/ & /count/ here stand for starting line and line count to display
// if /numbers/ is true then line numbers are added to output stream
//
static int files_cat_text(const char *path,unsigned int line,unsigned int count,unsigned char device, bool numbers) {

  FILE *f;
  char *p = NULL;
  unsigned int plen = 0, cline = 0;
  int r;

  if ((f = fopen(path,"rb")) != NULL) {
    while (count && (r = files_getline(&p,&plen,f)) >= 0) {
      cline++;
      if (line <= cline) {
        count--;
        if (device == (unsigned char )(-1)) {
          if (numbers)
#pragma GCC diagnostic ignored "-Wformat"
            q_printf("% 4u: ",cline);
#pragma GCC diagnostic warning "-Wformat"            
          q_print(p);
          q_print(CRLF);
        }
        else {
          char tmp[16];
          if (numbers) {
#pragma GCC diagnostic ignored "-Wformat"            
            sprintf(tmp,"% 4u: ",cline);
#pragma GCC diagnostic warning "-Wformat"            
            uart_write_bytes(device,tmp,strlen(tmp));
          }
          uart_write_bytes(device,p,r);
          tmp[0] = '\n';
          uart_write_bytes(device,tmp,1);
        }
      }
    }
    if (p)
      q_free(p);
    fclose(f);
  } else
    q_printf("%% <e>Can not open file \"%s\" for reading</>\r\n",path);
  return 0;
}

// <TAB> (Ctrl+I) handler. Jump to next argument
// until end of line is reached. start to jump back
//
// In file manager mode try to perform basic autocomplete (TODO: not implemented yet)
//
static STATUS tab_pressed() {

  if (Point < End)
    return do_forward(CSmove);
  else {
    if (Point) {
      Point = 0;
      return CSmove;
    }
    return CSstay;
  }
}

// "files"
// FileManager commands subtree
//
static int cmd_files_if(int argc, char **argv) {

  // file manager actual prompt is set by files_set_cwd()
  change_command_directory(0, keywords_files, PROMPT, "filesystem");

  //initialize CWD if not initialized previously. update user prompt.
  files_set_cwd(files_get_cwd()); 
  return 0;
}

// "unmount /Mount_point"
// "unmount"
// 
// Unmount a filesystem
//
static int cmd_files_unmount(int argc, char **argv) {

  int i;
  esp_err_t err = -1;
  char *path;
  char path0[512];

  // no mountpoint provided:
  // use CWD to find out mountpoint
  if (argc < 2) {
    if ((path = (char *)files_get_cwd()) == NULL)
      return 0;
    if (strlen(path) >= sizeof(path0)) // must not happen
      abort();
    strcpy(path0,path);
    path = path0;
  } else
    path = argv[1];

  // mount/unmount fails if path ends with slash
  files_strip_trailing_slash(path);

  // expand name if needed
  if ((path = files_full_path(path)) == NULL)
    return 1;

  // find a corresponding mountpoint
  if ((i = files_mountpoint_by_path(path,true)) < 0) {
    q_printf("%% <e>Unmount failed: nothing is mounted on \"%s\"</>\r\n", path);
    return 0;
  }

  // Process "unmount" depending on filesystem type
  switch (mountpoints[i].type) {
#if WITH_FAT
    case ESP_PARTITION_SUBTYPE_DATA_FAT:
      if (mountpoints[i].wl_handle != WL_INVALID_HANDLE)
        if ((err = esp_vfs_fat_spiflash_unmount_rw_wl(mountpoints[i].mp, mountpoints[i].wl_handle)) == ESP_OK)
          goto finalize_unmount;
      goto failed_unmount;
#endif
#if WITH_SPIFFS
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
      if (esp_spiffs_mounted(mountpoints[i].label))
        if ((err = esp_vfs_spiffs_unregister(mountpoints[i].label)) == ESP_OK)
          goto finalize_unmount;
      goto failed_unmount;
#endif
#if WITH_LITTLEFS
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:
      if (esp_littlefs_mounted(mountpoints[i].label))
        if ((err = esp_vfs_littlefs_unregister(mountpoints[i].label)) == ESP_OK)
          goto finalize_unmount;
      // FALLTHRU
#endif      
    default:
      goto failed_unmount;
  }

finalize_unmount:
#if WITH_HELP
  q_printf("%% Unmounted %s partition \"%s\"\r\n", files_subtype2text(mountpoints[i].type), mountpoints[i].mp);
#endif
#if WITH_FAT
  mountpoints[i].wl_handle = WL_INVALID_HANDLE;
#endif  
  q_free(mountpoints[i].mp);
  mountpoints[i].mp = NULL;
  mountpoints[i].label[0] = '\0';

  // adjust our CWD after unmount: our working directory may be not existent anymore
  if (!files_path_exist(files_get_cwd(),true))
    files_set_cwd("/");
  return 0;

failed_unmount:
#if WITH_HELP
  q_printf("%% <e>Unmount failed, error code is \"0x%x\"</>\r\n", err);
#endif
  return 0;
}

// "mount LABEL [/MOUNTPOINT"]
// mount a filesystem. filesystem type is defined by its label (see partitions.csv file).
// supported filesystems: fat, littlefs, spiffs
//

static int cmd_files_mount(int argc, char **argv) {

  int i;
  char mp0[ESP_VFS_PATH_MAX * 2]; // just in case
  char *mp = NULL;
  const esp_partition_t *part = NULL;
  esp_partition_iterator_t it;
  esp_err_t err = 0;

  // enough arguments?
  if (argc < 2)
    return -1;

  // is mountpoint specified?
  // is mountpoint starts with "/"?
  if (argc > 2) {
    mp = argv[2];
    if (mp[0] != '/') {
#if WITH_HELP      
      q_print("% <e>Mount point must start with \"/\"</>\r\n");
#endif      
      return 2;
    }
  } else {
    // mountpoint is not specified: use partition label and "/"
    // to make a mountpoint
    if (strlen(argv[1]) >= sizeof(mp0)) {
#if WITH_HELP      
      q_print("% <e>Invalid partition name (too long)</>\r\n");
#endif      
      return 1;
    }

    // following is wrong for cases when user enters incomplete label name. it is
    // fixed later
    sprintf(mp0, "/%s", argv[1]);
    mp = mp0;
  }

  files_strip_trailing_slash(mp); // or mount fails :-/
  if (!*mp) {
#if WITH_HELP    
    q_print("% <e>Directory name required: can't mount to \"/\"</>\r\n");
#endif    
    return 2;
  }
  
  // due to VFS internals there are restrictions on mount point length.
  // longer paths will work for mounting but fail for unmount so we just
  // restrict it here
  if (strlen(mp) >= sizeof(mp0)) {
    q_printf("%% <e>Mount point path max length is %u characters</>\r\n", sizeof(mp0) - 1);
    return 0;
  }

  // find free slot in mountpoints[] to mount new partition
  if ((i = files_mountpoint_by_path(NULL,false)) < 0) {
    q_print("% <e>Too many mounted filesystems, increase MOUNTPOINTS_NUM</>\r\n");
    return 0;
  }

  // find requested partition on flash and mount it:
  // run through all partitions
  it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it) {

    part = esp_partition_get(it);

    // skip everything except for DATA-type partitions
    if (part && (part->type == ESP_PARTITION_TYPE_DATA))
      // label name match?
      if (!q_strcmp(argv[1], part->label)) {

        int tmp;

        // We have found the partition user wants to mount.
        // reassign 1st arg to real partition name (the case when user enters shortened label name)
        argv[1] = (char *)part->label;  
        if (mp == mp0)
          sprintf(mp0, "/%s", argv[1]);

        // check if selected mount point is not used
        if ((tmp = files_mountpoint_by_path(mp,false)) >= 0) {
#if WITH_HELP  
          q_printf("%% <e>Mount point \"%s\" is already used by partition \"%s\"</>\r\n", mp, mountpoints[tmp].label);
#endif    
          goto mount_failed;
        }



        // Mount/Format depending on FS type
        switch (part->subtype) {
#if WITH_FAT
          // Mount FAT partition
          case ESP_PARTITION_SUBTYPE_DATA_FAT:
            esp_vfs_fat_mount_config_t conf = { 0 };

            conf.format_if_mount_failed = true;
            conf.max_files = 2;
            conf.allocation_unit_size = CONFIG_WL_SECTOR_SIZE;
            
            if ((err = esp_vfs_fat_spiflash_mount_rw_wl(mp, part->label, &conf, &mountpoints[i].wl_handle)) != ESP_OK)
              goto mount_failed;
            goto finalize_mount;
#endif
#if WITH_SPIFFS
          // Mount SPIFFS partition
          case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
            if (esp_spiffs_mounted(part->label)) {
              q_printf("%% <e>Partition \"%s\" is already mounted</>\r\n", part->label);
              goto mount_failed;
            }
            esp_vfs_spiffs_conf_t conf2 = { 0 };

            conf2.base_path = mp;
            conf2.partition_label = part->label;
            conf2.max_files = 2;
            conf2.format_if_mount_failed = true;

            
            if ( (err = esp_vfs_spiffs_register(&conf2)) != ESP_OK )
              goto mount_failed;
            goto finalize_mount;
#endif
#if WITH_LITTLEFS
          // Mount LittleFS partition
          case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:

            if (esp_littlefs_mounted(part->label)) {
              q_printf("%% <e>Partition \"%s\" is already mounted</>\r\n", part->label);
              goto mount_failed;
            }
            esp_vfs_littlefs_conf_t conf1 = { 0 };

            conf1.base_path = mp;
            conf1.partition_label = part->label;
            conf1.format_if_mount_failed = true;
            conf1.grow_on_mount = true;
            
            if ((err = esp_vfs_littlefs_register(&conf1)) != ESP_OK)
              goto mount_failed;
            goto finalize_mount;
#endif
          default:
            q_print("% <e>Unsupported file system</>\r\n");
            goto mount_failed;
        }
      }
    it = esp_partition_next(it);
  }

  // Matching partition was not found
  q_printf("%% <e>Partition label \"%s\" is not found</>\r\n", argv[1]);

mount_failed:
  q_printf("%% <e>Mount partition \"%s\" failed (error: %d)</>\r\n", argv[1], err);
#if WITH_FAT  
  mountpoints[i].wl_handle = WL_INVALID_HANDLE;
#endif  
  if (it)
    esp_partition_iterator_release(it);
  return 0;

finalize_mount:
  // 'part', 'i', 'mp' are valid pointers/inidicies
  if (it)
    esp_partition_iterator_release(it);

  if ((mountpoints[i].mp = q_strdup(mp,MEM_MOUNTPOINT)) == NULL)
    q_print(Failed);
  else {
    mountpoints[i].type = part->subtype;
    static_assert(sizeof(mountpoints[0].label) >= sizeof(part->label), "Increase mountpoints[].label array size");
    strcpy(mountpoints[i].label, part->label);

    q_printf("%% %s on partition \"%s\" is mounted under \"%s\"\r\n", files_subtype2text(part->subtype), part->label, mp);
  }
  return 0;
}

// "mount"
// Without arguments display currently mounted filesystems and partition table
//
#pragma GCC diagnostic ignored "-Wformat"
static int cmd_files_mount0(int argc, char **argv) {

  int usable = 0, i;
  bool mountable;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);

  if (!it) {
    q_print("% <e>Can not read partition table</>\r\n");
    return 0;
  }

  q_print("<r>% Disk partition |M|File system| Size on |    Mounted on    |Capacity |  Free   \r\n"
          "%    label       |?|   type    |  flash  |                  |  total  |  space  </>\r\n");
  q_print("% ---------------+-+-----------+---------+------------------+---------+---------\r\n");
  while (it) {
    const esp_partition_t *part = esp_partition_get(it);
    if (part && (part->type == ESP_PARTITION_TYPE_DATA)) {

      if (part->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT || 
          part->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS || 
          part->subtype == ESP_PARTITION_SUBTYPE_DATA_LITTLEFS) {
        usable++;
        mountable = true;
      } else
        mountable = false;

#if WITH_COLOR
      if (mountable)
        q_print("<i>");
#endif
        //"label" "fs type" "partition size"
      q_printf("%%% 16s|%s|%s| % 6uK | ",part->label,mountable ? "+" : " ", files_subtype2text(part->subtype),part->size / 1024);
      
      if ((i = files_mountpoint_by_label(part->label)) >= 0)
        // "mountpoint" "total fs size" "available fs size"
        q_printf("% 16s | % 6uK | % 6uK\r\n",mountpoints[i].mp, files_space_total(i)/1024, files_space_free(i)/1024);
      else
        q_print("                 |         |\r\n");
#if WITH_COLOR
      if (mountable)
        q_print("</>");
#endif
    }
    it = esp_partition_next(it);
  }
#if WITH_HELP
  q_print("%\r\n");
  if (!usable)
    q_print("% <2>No usable partitions were found. Use (Tools->Partition Scheme) in Arduino IDE</>\r\n");
  else
    q_printf("%% <i>%u</> mountable partition%s found. (+) - mountable partition\r\n", usable, usable == 1 ? "" : "s");
#endif  //WITH_HELP
  if (it)
    esp_partition_iterator_release(it);
  return 0;
}
#pragma GCC diagnostic warning "-Wformat"



// "cd"
// "cd .."
// "cd /full/path"
// "cd relative/path"
//
//  Change current working directory. Double dots (relative
//  paths) are not supported on purpose: paths like "some/../../path" are considered invalid
//  Paths which starts with double dot are treated as if user typed "cd .." i.e. change to upper
//  level directory.
//  Single dot (a reference to *this* directory) is not supported. Path "some/./path" is not
//  valid for "cd" command
//
static int cmd_files_cd(int argc, char **argv) {

  if (files_get_cwd() == NULL)
    return 0;

  //"cd" no args, go to the mountpoint
  if (argc < 2) {
    int i;
    if ((i = files_mountpoint_by_path(files_get_cwd(), false)) < 0)
      files_set_cwd("/");
    else
      files_set_cwd(mountpoints[i].mp);
    return 0;
  }

#if WITH_HELP
  //"cd Path With Spaces"
  if (argc > 2) { q_print(SpacesInPath); return 0; }
#endif  

  int i;
  // just in case. 
  if (argv[1][0] == '\0')
    return 1;

  // Case#1:
  // Two leading dots - go 1 level up if possible.
  // Paths like "../some/path" will be processed as if it was simply ".."
  // No support of relative paths
  if (argv[1][0] == '.' && argv[1][1] == '.') {

    char *p;
    if ((i = strlen(Cwd)) < 3)  //  minimal path "/a/". root directory reached
      return 0;

    // remove trailing path separator and reverse search
    // for another one
    files_strip_trailing_slash(Cwd);

    if (NULL == (p = strrchr(Cwd, '/')))
      if (NULL == (p = strrchr(Cwd, '\\')))
        abort(); //must not happen

    // strip everything after it
    p[1] = '\0';
    if (Cwd[0] == '\0')
      files_set_cwd("/");
    else {
      // repeat "cd .."" until we reach path that exists.
      // partition can be mounted under /a/b/c but /a, /a/b are not exist so
      // "cd .." from "/a/b/c/" should not end up at "/a/b/"
      if (!files_path_exist(Cwd, true))
        return cmd_files_cd(argc,argv);
    }
    files_set_cwd(Cwd); //update prompt
    return 0;
  }

  // Case#2:
  // Relative/absolute path
  i = 0;
  // Sanity check: must be no double dots in path
  while (argv[1][i]) {
    if (argv[1][i] == '.' && argv[1][i + 1] == '.') {
      q_printf("%% <e>Two dots (..) are not supported in path</>\r\n");
      return 1;
    }
    i++;
  }

  // Replace all "*" with spaces " "
  files_asteriks2spaces(argv[1]);

  // Path is absolute: check if it exists and
  // store it as current working directory
  if (argv[1][0] == '/') {
    if (files_path_exist(argv[1], true)) {
      files_set_cwd(argv[1]);
      return 0;
    }
    goto path_does_not_exist;
  }

  char tmp[512] = { 0 };

  // Path is relative: append path to the CWD
  // and check if path exists as well
  if (strlen(Cwd) + strlen(argv[1]) > sizeof(tmp)) {
    q_print("% <e>Path is too long</>\r\n");
    return 1;
  }

  // tmp = Cwd+arg1
  strcpy(tmp, Cwd);
  
  if ((i = strlen(tmp)) < 1) //must not happen
    abort();  

  strcat(tmp, argv[1]);

  // if resulting path does not end with "/" - add it, we have enough space
  // in our tmp
  i = strlen(tmp);
  if (tmp[i - 1] != '\\' && tmp[i - 1] != '/') {
    tmp[i] = '/';
    tmp[i + 1] = '\0';
  }

  // Set new CWD if path exists
  if (files_path_exist(tmp, true)) {
    if (files_set_cwd(tmp))
      return 0;
    else
      q_print(Failed);
  } else
path_does_not_exist:
    q_print("% <e>Path does not exist</>\r\n");
  return 1;
}

// "ls [PATH]"
// Directory listing for current working directory or PATH if specified
//
static int cmd_files_ls(int argc, char **argv) {
  char path[256+16],*p;
  int plen;
  
  if ((p = (argc > 1) ? files_full_path(argv[1]) : files_full_path(Cwd)) == NULL)
    return 0;

  if ((plen = strlen(p)) == 0)
    return 0;

  if (plen > (256+8))
    return 0;

  strcpy(path,p);
    
  // if it is Cwd then it MUST end with "/" so we dont touch it
  // if it is full_path then it MAY or MAY NOT end with "/" but full_path is writeable and expandable
  if (path[plen - 1] != '\\' && path[plen - 1] != '/') {
    path[plen++] = '/';
    path[plen] = '\0';
  }

  // "ls /" -  root directory listing,
  if (files_path_is_root(path)) {
    bool found = false;
    for (int i = 0; i < MOUNTPOINTS_NUM; i++)
      if (mountpoints[i].mp) {
        if (!found) {
          q_print("%-- USED --        *  Mounted on\r\n");
          found = true;
        }
#pragma GCC diagnostic ignored "-Wformat"        
        q_printf("%% <b>% 9u</>       MP  [<3>%s</>]\r\n",files_space_used(i), mountpoints[i].mp);
#pragma GCC diagnostic warning "-Wformat"        
      }
    if (!found)
      q_printf("%% <i>Root (\"%s\") directory is empty</>: no fileystems mounted\r\n%% Use command \"mount\" to list & mount available partitions\r\n",path);
    return 0;
  }
  
  // real directory listing
  if (!files_path_exist(path,true))
    q_printf("%% <e>Path \"%s\" does not exist</>\r\n",path);
  else {
    unsigned int total_f = 0, total_d = 0, total_fsize = 0;
    DIR *dir;

    if ((dir = opendir(path)) != NULL) {
      struct dirent *ent;

      q_print("%    Size        Modified          *  Name\r\n"
              "%               -- level up --    DIR [<i>..</>]\r\n");
      while ((ent = readdir(dir)) != NULL) {
        
        struct stat st;
        char path0[256+16] = { 0 };

        if (strlen(ent->d_name) + 1 + plen > (sizeof(path0) - 8)) {
          q_print("% <e>Path is too long</>\r\n");
          continue;
        }

        // d_name entries are simply file/directory names without path so
        // we need to prepend a valid path to d_name 
        strcpy(path0,path);
        strcat(path0,ent->d_name);

        if (0 == stat(path0,&st)) {
          if (ent->d_type == DT_DIR) {
            unsigned int dir_size = files_size(path0);
            total_d++;
            total_fsize += dir_size;
#pragma GCC diagnostic ignored "-Wformat"            
            q_printf("%% % 9u  %s  DIR [<i>%s</>]\r\n", dir_size , files_time2text(st.st_mtime), ent->d_name);
#pragma GCC diagnostic warning "-Wformat"            
          }
          else {
            total_f++;
            total_fsize += st.st_size;
#pragma GCC diagnostic ignored "-Wformat"            
            q_printf("%% % 9u  %s      <3>%s</>\r\n",(unsigned int)st.st_size,files_time2text(st.st_mtime), ent->d_name);
#pragma GCC diagnostic warning "-Wformat"            
          }
        } else
          q_printf("<e>stat() : failed %d, name %s</>\r\n",errno,path0);
      }
      closedir(dir);
    }
    q_printf("%%\r\n%% <i>%u</> director%s, <i>%u</> file%s, <i>%u</> byte%s\r\n",
             total_d, total_d == 1 ? "y" : "ies", 
             total_f, total_f == 1 ? "" : "s",
             total_fsize,total_fsize == 1 ? "" : "s");
  }
  return 0;
}

// "rm PATH1 [PATH2 ... PATHn]"
// removes file or directory with its content (recursively)
//
static int cmd_files_rm(int argc, char **argv) {
  
  if (argc < 2) return -1;
#if WITH_HELP  
  if (argc > 2)
    q_print(MultipleEntries);
#endif  
  
  int i, num;
  for (i = 1, num = 0; i < argc; i++) {
    files_asteriks2spaces(argv[i]);
    num += files_remove(argv[i],DIR_RECURSION_DEPTH);
  }

  if (num)
    q_printf("%% <i>%d</> files/directories were deleted\r\n",num);

  return 0;
}

// "write FILENAME TEXT"
// Write TEXT to file FILENAME. File is created if does not exist
//
static int cmd_files_write(int argc, char **argv) {

  int fd,size;
  char *path, *out = NULL;
  char empty[] = { '\n' , '\0' };

  if (argc < 2) return -1;
  if ((path = files_full_path(argv[1])) == NULL)
    return 1;
#if MUST_TOUCH
  if (!files_path_exist(path,false)) {
    q_printf("%% <e>File \"%s\" does not exist, \"touch\" it first</>\r\n",path);
    return 0;
  }
#endif

  if (argc > 2)
    size = text2buf(argc,argv,2,&out);
  else {
    out = empty;
    size = 1;
  }
  

  unsigned flags = O_CREAT|O_WRONLY;

  // are we running as "write" or as "append" command?
  if (!q_strcmp(argv[0],"append"))
    flags |= O_APPEND;
  else
    flags |= O_TRUNC;

  if (size > 0) {
    if ((fd = open(path,flags)) > 0) {
      size = write(fd,out,size);
      if (size < 0)
        q_printf("%% <e>Write to file \"%s\" failed</>\r\n",path);
      else
        q_printf("%% <i>%u</> bytes written to <2>%s</>\r\n",size,path);
      close(fd);
    }
  }

  if (out && (out != empty))
    q_free(out);

  return 0;
}

// "append FILENAME TEXT"
// same as "write" but appends text to the end of the file
//
static int cmd_files_append(int argc, char **argv) {
  return cmd_files_write(argc,argv);
}

// "insert FILENAME LINE_NUMBER [TEXT]"
// "delete FILENAME LINE_NUMBER [COUNT]"
// insert TEXT before line number LINE_NUMBER
//
static int cmd_files_insdel(int argc, char **argv) {

  char *path, *upath = NULL;
  FILE *f = NULL, *t = NULL;
  unsigned char *p = NULL;
  char *text = NULL;
  unsigned int   plen, tlen = 0, cline = 0, line;
  bool insert = true; // default action is insert
  int count = 1;
  char empty[] = { '\n', '\0' };

  if (!q_strcmp(argv[0],"delete"))
    insert = false;

  if (argc < 3)
    return -1;

  if ((line = q_atol(argv[2],(unsigned int)(-1))) == (unsigned int)(-1)) {
#if WITH_HELP
    q_printf("%% Line number expected instead of \"%s\"\r\n",argv[2]);
#endif
    return 2;
  }

  if ((path = files_full_path(argv[1])) == NULL)
    return 1;
  
  if (!files_path_exist(path, false)) {
#if WITH_HELP
    q_printf("%% <e>Path \"%s\" does not exist</>\r\n",path);  //TODO: Path does not exist is a common string.
#endif    
    return 1;
  }

  if ((f = fopen(path,"rb")) == NULL) {
#if WITH_HELP
    q_printf("%% <e>File \"%s\" does exist but failed to open</>\r\n",path);
#endif    
    return 0;
  }

  // path is the files_full_path's buffer which has some extra bytes beyound MAX_PATH boundary which are
  // safe to use.
  strcat(path,"~");
  upath = q_strdup(path,MEM_PATH);
  if (!upath)
    goto free_memory_and_return;

  int tmp = strlen(path);
  path[tmp-1] = '\0'; // remove "~""

  if ((t = fopen(upath,"wb")) == NULL) {
#if WITH_HELP
    q_printf("%% <e>Failed to create temporary file \"%s\"</>\r\n",upath);
#endif    
    goto free_memory_and_return;
  }


  if (insert) {
    if (argc > 3) {
      tlen = text2buf(argc,argv,3,&text);
      if (!tlen)
        goto free_memory_and_return;
    } else {
      tlen = 1;
      text = empty;
    }
  } else
    count = q_atol(argv[3],1);

  while (!feof(f)) {
    int r = files_getline((char **)&p,&plen,f);
    if (r >= 0) {
      if ((r == 0) && feof(f))
        break;
      cline++;
      if (cline == line) {
        if (!insert) {
#if WITH_HELP
          q_printf("%% Line %u deleted\r\n",line);
#endif
          if (--count > 0)
            line++;
          continue;
        }
        fwrite(text,1,tlen,t);
        if (text != empty)
          fwrite("\n",1,1,t); // Add \n only if it was not empty string
#if WITH_HELP        
        q_printf("%% Line %u inserted\r\n",line);
#endif        
      }
      fwrite(p,1,r,t);
      fwrite("\n",1,1,t);
    }
  }
  fclose(f);
  fclose(t);
  t = f = NULL;

  unlink(path);
  if (rename(upath,path) == 0) {
    q_free(upath);
    upath = NULL;
  }

free_memory_and_return:  
  if (p) q_free(p);
  if (f) fclose(f);
  if (t) fclose(t);
  if (text && text != empty) q_free(text);
  if (upath) { unlink(upath);q_free(upath); }

  return 0;
}

// "mkdir PATH1 [PATH2 ... PATHn]"
// Create new directory PATH
//
static int cmd_files_mkdir(int argc, char **argv) {

  int i;
  if (argc < 2) return -1;
#if WITH_HELP  
  if (argc > 2)
    q_print(MultipleEntries);
#endif  

  for (i = 1; i < argc; i++) {
    files_strip_trailing_slash(argv[i]);
    if (argv[i][0] == '\0')
      return i;
    files_asteriks2spaces(argv[i]);
    if ((argv[i] = files_full_path(argv[i])) != NULL)
      if (!files_path_impossible(argv[i]))
        if (mkdir(argv[i],0777) != 0)
          q_printf("%% <e>Failed to create directory \"%s\", error %d</>\r\n",argv[i],errno);
  }
  return 0;
}

// "touch PATH1 [PATH2 ... PATHn]"
// Create new files or update existing's timestamp
//
static int cmd_files_touch(int argc, char **argv) {
  
  int fd,i;

  if (argc < 2) return -1;
#if WITH_HELP  
  if (argc > 2)
    q_print(MultipleEntries);
#endif  

  for (i = 1; i < argc; i++) {

    // create path from user input (arg1..argN) and current working directory set by "cd"
    files_asteriks2spaces(argv[i]);
    argv[i] = files_full_path(argv[i]);

    // try to open file, creating it if it doesn't exist
    if ((fd = open(argv[i], O_CREAT | O_WRONLY, 0666)) > 0)
      close(fd);
    else
      q_printf("%% <e>Failed to create file \"%s\", error code is %d</>\r\n",argv[i],errno);
  }
  return 0;
}

// "format [LABEL]"
// Format partition (current partition or LABEL partition if specified)
// If LABEL is not given (argc < 2) then espshell attempts to derive
// label name from current working directory
//
#define disableCore0WDT()
#define enableCore0WDT()

static int cmd_files_format(int argc, char **argv) {

  int i;
  esp_err_t err = ESP_OK;
  const char *label;
  const esp_partition_t *part;
  char path0[32] = { 0 };
  const char *reset_dir = "/";

  // this will be eliminated at compile time if verything is ok
  if (sizeof(path0) < sizeof(part[0].label))
    abort();

  // find out partition name (label): it is either set as 1st argument of "format"
  // command, or must be derived from current working directory
  if (argc > 1)
    label = argv[1];
  else {
    const char *path;

    if ((path = files_get_cwd()) == NULL)
      return 0;

    if (files_path_is_root(path)) {
      q_print("% <e>Root partition can not be formatted, \"cd\" first</>\r\n");
      return 0;
    }
    
    // disable reverse lookup on this: we don't want wrong partition to be formatted
    if ((i = files_mountpoint_by_path(path, false)) < 0) { 
      // normally happen when currently used partition is unmounted: reset CWD to root directory
      files_set_cwd("/");
      return 0;
    }
    label = mountpoints[i].label;
    reset_dir = mountpoints[i].mp;
  }
  
  // find partition user wants to format
  if ((part = files_partition_by_label(label)) == NULL) {
    q_printf("%% <e>Partition \"%s\" does not exist</>\r\n",label);
    return argc > 1 ? 1 : 0;
  }

  // handle shortened label names
  label = part->label;

#if WITH_HELP
  q_printf("%% Formatting partition \"%s\", file system type is \"%s\"\r\n",label,files_subtype2text(part->subtype));
#endif
  switch (part->subtype) {
#if WITH_FAT    
    case ESP_PARTITION_SUBTYPE_DATA_FAT: sprintf(path0,"/%s",label); err = esp_vfs_fat_spiflash_format_rw_wl(path0,label); break;
#endif
#if WITH_LITTLEFS      
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS: disableCore0WDT(); err = esp_littlefs_format(label); enableCore0WDT(); break;
#endif
#if WITH_SPIFFS    
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: disableCore0WDT(); err = esp_spiffs_format(label); enableCore0WDT(); break;
#endif    
    default:
      q_printf("%% <e>Unsupported filesystem type 0x%02x</>\r\n",part->subtype);
  }
  if (err != ESP_OK)
    q_printf("%% <e>There were errors during formatting (code: %u)</>\r\n",err);
  else
    q_print("% done\r\n");
  
  // update CWD if we were formatting current filesystem
  if (!files_path_exist(files_get_cwd(), true))
    files_set_cwd(reset_dir);
  
  return 0;
}

// "mv FILENAME1 FILENAME2"
// "mv DIRNAME1 DIRNAME2"
// "mv FILENAME DIRNAME"
// Move/rename files or directories
//
static int cmd_files_mv(int argc, char **argv) {
  q_print("% Not implemented yet\r\n");
  return 0;
}

// "cp FILENAME1 FILENAME2"
// "cp FILENAME DIRNAME"
// "cp DIRNAME1 DIRNAME2"
//
// Copy files/directories
static int cmd_files_cp(int argc, char **argv) {
  q_print("% Not implemented yet\r\n");
  return 0;
}

// "cat [-n|-b] PATH [START [COUNT]] [uart NUM]
// cat /wwwroot/text_file.txt
// cat /wwwroot/text_file.txt 10
// cat /wwwroot/text_file.txt 10 10
// cat /wwwroot/text_file.txt uart1
// cat /wwwroot/text_file.txt 10 10 usb0
//
// Display a text file content
//
static int cmd_files_cat(int argc, char **argv) {

  bool binary = false, numbers = false;
  char *path;
  int i =1;
  unsigned int line = (unsigned int)(-1), count = (unsigned int)(-1);
  unsigned char device = (unsigned char )(-1);

  if (argc < 2) return -1;
  
  // -b & -n options
  if (!strcmp("-b",argv[i])) {
    binary = true;
    i++;
  } else if (!strcmp("-n",argv[i])) {
    numbers = true;
    i++;
  }

  if (i >= argc)
    return -1;
    
  if (!files_path_exist((path = files_full_path(argv[i])), false)) {
#if WITH_HELP
    q_printf("%% File not found:\"<e>%s</>\"\r\n",path);
    return 0;
#else
    return 1;
#endif
  }
  i++;

  // pickup other arguments.
  // these are: one or two numbers (start & count) and possibly an uart interface number (a number as well)
  while (i < argc) {
    if (isnum(argv[i]) || ishex(argv[i])) {
      if (line == (unsigned int)(-1))
        line = q_atol(argv[i],0);
      else if (count == (unsigned int)(-1))
        count = q_atol(argv[i],0xffffffff);
      else {
#if WITH_HELP        
        q_print("% Unexpected 3rd numeric argument\r\n");
#endif        
        return i;
      }
    } else 
    // "uart" keyword? must be a valid uart interface number in the next argument then
    if (!q_strcmp(argv[i],"uart")) {
      if (i + 1 >= argc) {
#if WITH_HELP        
        q_print("% <e>UART number is missing</>\r\n");
#endif        
        return i;
      }
      i++;
      if (!isnum(argv[i])) {
#if WITH_HELP        
        q_print("% <e>Numeric value (UART number) is expected</>\r\n");
#endif        
        return i;
      }
      
      if (!uart_isup((device = atol(argv[i])))) {
        q_printf("%% <e>UART%d is not initialized</>\r\n",device);
#if WITH_HELP
        q_printf("%% Configure it by command \"uart %d\"</>\r\n",device);
#endif        
        return 0;
      }
    } else
    // unexpected keyword
      return i;

    // to the next keyword
    i++;
  }

  if (line == (unsigned int)(-1))
    line = 0;

  if (binary)
    files_cat_binary(path,line,count,device);
  else
    files_cat_text(path,line,count,device,numbers);

  return 0;
}


#endif  //WITH_FS

#if WITH_HELP
// "? keys"
// display keyboard usage help page
static int help_keys(UNUSED int argc, UNUSED char **argv) {

  // 25 lines maximum to fit in default terminal window without scrolling
  q_print("%             -- ESPShell Keys -- \r\n\r\n"
          "% <ENTER>         : Execute command.\r\n"
          "% <- -> /\\ \\/     : Arrows: move cursor left or right. Up and down to scroll\r\n"
          "%                   through command history\r\n"
          "% <DEL>           : As in Notepad\r\n"
          "% <BACKSPACE>     : As in Notepad\r\n"
          "% <HOME>, <END>   : Use Ctrl+A instead of <HOME> and Ctrl+E as <END>\r\n"
          "% <TAB>           : Move cursor to the next word/argument: press <TAB> multiple\r\n"
          "%                   times to cycle through words in the line\r\n"
          "% Ctrl+R          : Command history search\r\n"
          "% Ctrl+K          : [K]ill line: clear input line from cursor to the end\r\n"
          "% Ctrl+L          : Clear screen\r\n"
          "% Ctrl+Z          : Same as entering \"exit\" command\r\n"
          "% Ctrl+C          : Suspend sketch execution\r\n"
          "% <ESC>,NUM,<ESC> : Same as entering letter with decimal ASCII code NUM\r\n%\r\n"
          "% -- Terminal compatibility workarounds (alternative key sequences) --\r\n%\r\n"
          "% Ctrl+B and Ctrl+F work as \"<-\" and \"->\" ([B]ack & [F]orward arrows)>\r\n"
          "% Ctrl+O or P   : Go through the command history: O=backward, P=forward\r\n"
          "% Ctrl+D works as <[D]elete> key\r\n"
          "% Ctrl+H works as <BACKSPACE> key\r\n");

  return 0;
}

// "? pinout"
// display some known interfaces pin numbers
static int help_pinout(UNUSED int argc, UNUSED char **argv) {
  //TODO: basic pin numvers are in pins_arduino.h.
  //      problems with pins_arduino.h: it is a bunch of static const unsigned chars, not #defines;
  //      there are only basic pins, no pins for 2nd UART or I2C or SPI.
  //      Looks like I have to go through all TechRefs :(. Next time.
  q_print("% Sorry brother, not yet implemented\r\n");
  return 0;
}

// "? COMMAND_NAME"
//
// display a command usage details ("? some_command")
//
static int help_command(int argc, char **argv) {

  int i = 0;
  bool found = false;

  // go through all matched commands (only name is matched) and print their
  // help lines. hidden commands are ignored
  while (keywords[i].cmd) {
    if (keywords[i].help || keywords[i].brief) {  //skip hidden commands
      if (!q_strcmp(argv[1], keywords[i].cmd)) {

        // print common header for the first entry
        if (!found && keywords[i].brief)
          q_printf("\r\n -- %s --\r\n", keywords[i].brief);

        if (keywords[i].help)
          q_printf("\r\n%s\r\n", keywords[i].help);  //display long help
        else if (keywords[i].brief)
          q_printf("\r\n%s\r\n", keywords[i].brief);  //display brief (must not happen)
        else
          q_print("% FIXME: no help lines?\r\n");
        found = true;
      }
    }
    i++;
  }
  // if we didnt find anything, return 1 (index of failed argument)
  return found ? 0 : 1;
}

//"?"
// display command list
//
#define INDENT 10
static int help_command_list(int argc, char **argv) {
  int i = 0;
  const char *prev = "";
  char indent[INDENT + 1];

  // commands which are shorter than INDENT will be padded with extra spaces to be INDENT bytes long
  memset(indent, ' ', INDENT);
  indent[INDENT] = 0;
  char *spaces;

  q_print("% Enter \"? command\" to get details about specific command.\r\n"
          "% Enter \"? keys\" to display the espshell keyboard help page\r\n"
          "%\r\n");

  //run through the keywords[] and print brief info for every entry
  // 1. for repeating entries (same command name) only the first entry's description
  //    used. Such entries in keywords[] must be grouped to avoid undefined bahaviour
  //    Ex.: see "count" command entries
  // 2. entries with both help lines (help and brief) set to NULL are hidden commands
  //    and are not displayed (but are executed if requested).
  while (keywords[i].cmd) {

    if (keywords[i].help || keywords[i].brief) {
      if (strcmp(prev, keywords[i].cmd)) {  // skip entry if its command name is the same as previous
        const char *brief;
        if (!(brief = keywords[i].brief))  //use "brief" or fallback to "help"
          if (!(brief = keywords[i].help))
            brief = "% FIXME: No description";

        // indent: commands with short names are padded with spaces so
        // total length is always INDENT bytes. Longer commands are not padded
        int clen;
        spaces = &indent[INDENT];  //points to \0
        if ((clen = strlen(keywords[i].cmd)) < INDENT)
          spaces = &indent[clen];
        // "COMMAND" PADDING_SPACES : DESCRIPTION
        q_printf("%% \"%s\"%s : %s\r\n", keywords[i].cmd, spaces, brief);
      }
    }

    prev = keywords[i].cmd;
    i++;
  }

  return 0;
}


// "?"
//
// question mark command: display general help pages ("? keys", "? pinout")
// display a command usage details ("? some_command") or display the list
// of available commands ("?" with no arguments)
//
static int cmd_question(int argc, char **argv) {

  //"? arg"
  if (argc > 1) {
    // use strcmp, not q_strcmp to distibuish from "? pin" command
    //"? keys"
    if (!strcmp(argv[1], "keys"))
      return help_keys(argc, argv);

    //"? pinout"
    if (!strcmp("pinout", argv[1]))
      return help_pinout(argc, argv);

    //"? command"
    return help_command(argc, argv);
  }
  return help_command_list(argc, argv);
}
#endif  // WITH_HELP



// Parse & execute: split user input "p" to tokens, find an appropriate
// entry in keywords[] array and execute coresponding callback.
// String p - is the user input as returned by readline()
//
// returns 0 on success

static int
espshell_command(char *p) {

  char **argv = NULL;
  int argc, i, bad;
  bool found;
  argcargv_t *aa = NULL;

  // sanity checks
  if (!p) goto early_fail;
  if (!p[0]) goto early_fail;

  //tokenize user input
  if ((aa = userinput_tokenize(p)) == NULL)
    goto early_fail;

#if WITH_HISTORY
  //make a history entry
  if (rl_history)
    rl_add_history(p);
#endif  


  //make it global so async functions can increase the refcounter
  aa_current = aa;

  //shortcuts
  argc = aa->argc;
  argv = aa->argv;

  {
    // Process a command
    //
    // Find a corresponding entry in a keywords[] : match the name and the number of arguments.
    // For commands executed in a command subdirectory both main command directory and current
    // directory are searched.
    // This allows all command from the main tree to be accessible while in sequence, uart or i2c
    // subdirectory
    const struct keywords_t *key = keywords;
    found = false;
one_more_try:
    i = 0;
    bad = -1;
    while (key[i].cmd) {
      // command name matches user input?
      if (!q_strcmp(argv[0], key[i].cmd)) {
        found = true;

        // command & user input match when both name & number of arguments match.
        // one special case is command with argc < 0 : these are matched always
        // (-1 == "take any number of arguments")
        if (((argc - 1) == key[i].argc) || (key[i].argc < 0)) {

          // execute the command.
          // if nonzero value is returned then it is an index of the "failed" argument (value of 3 means argv[3] was bad (for values > 0))
          // value of zero means successful execution, return code <0 means argument number was wrong (missing argument)
          if (key[i].cb) {
            bad = key[i].cb(argc, argv);
            // keywords[i] is not a valid pointer anymore as cb() can change the keywords pointer
            // keywords[0] is always valid
            if (bad > 0)
              q_printf("%% <e>Invalid argument \"%s\" (\"? %s\" for help)</>\r\n", argv[bad], argv[0]);
            else if (bad < 0)
              q_printf("%% <e>Missing argument (\"? %s\" for help)</>\r\n", argv[0]);
            else
              i = 0;  // make sure keywords[i] is a valid pointer
            break;
          }  // if callback is provided
        }    // if argc matched
      }      // if name matched
      i++;   // process next entry in keywords[]
    }        // until all keywords[] are processed

    // reached the end of the list and didn't find any exact match?
    if (!key[i].cmd) {

      // try keywords_main if we are currently in a subdirectory
      // so we can execute "pin" command while are in uart config mode
      if (key != keywords_main) {
        key = keywords_main;
        goto one_more_try;
      }

      
      if (found)  //we had a name match but number of arguments was wrong
        q_printf("%% <e>\"%s\": wrong number of arguments</> (\"? %s\" for help)\r\n", argv[0], argv[0]);
      else
        q_printf("%% <e>\"%s\": command not found</>\r\n", argv[0]);
      
    }
#if WITH_HELP
    if (!found)
      q_print("% <e>Type \"?\" to show the list of commands available</>\r\n");
#endif
  }

  // free memory associated wih user input
  userinput_unref(aa);
  return bad;
early_fail:
  if (p) q_free(p);
  return -1;
}


// Execute an arbitrary shell command (\n are allowed for multiline).
// it is an async call: it returns immediately. one can use espshell_exec_finished()
// to check if actual shell command has finished its execution
void espshell_exec(const char *p) {
  TTYqueue(p);
}

// check if last espshell_exec() call has completed its
// execution
bool espshell_exec_finished() {
  bool ret;
  portENTER_CRITICAL(&Input_mux);
  ret = (*Input == '\0');
  portEXIT_CRITICAL(&Input_mux);
  return ret;
}


// shell task. reads and processes user input.
//
// only one shell task can be started!
// this task started from espshell_start()
//
static void espshell_task(const void *arg) {

  // arg is not NULL - first time call: start a task and return immediately
  if (arg) {
    if (shell_task != NULL) {
      q_print("% ESPShell is started already\r\n");
      return;
    }

    
    // on multicore processors use another core: if Arduino uses Core1 then
    // espshell will be on core 0.
    shell_core = xPortGetCoreID();
    if (portNUM_PROCESSORS > 1)
      shell_core = shell_core ? 0 : 1;
    if (pdPASS != xTaskCreatePinnedToCore((TaskFunction_t)espshell_task, NULL, STACKSIZE, NULL, tskIDLE_PRIORITY, &shell_task, shell_core))
      q_print("% ESPShell failed to start its task\r\n");
  } else {

    // wait until user code calls Serial.begin()
    while (!console_isup())
      delay(1000);

#if WITH_HELP
    q_print("% ESPShell. Type \"?\" and press <Enter> for help\r\n% Press <Ctrl>+L to clear the screen and to enable colors\r\n");
#endif

    while (!Exit) {
      espshell_command(readline(prompt));
      delay(1);
    }
#if WITH_HELP
    q_print("% Bye!\r\n");
#endif
    Exit = false;
    vTaskDelete((shell_task = NULL));
  }
}

// Start ESPShell
// Normally (AUTOSTART==1) starts automatically. With autostart disabled EPShell can
// be started by calling espshell_start(). 
//
// This function also cqan start the shell which was terminated by command "exit ex"
//
#if AUTOSTART
void STARTUP_HOOK espshell_start() {
#else
void espshell_start() {
#endif
  if (!argv_mux) argv_mux = xSemaphoreCreateMutex();
#if MEMTEST
  q_meminit();
#endif
  seq_init(); 
  espshell_task((const void *)1);
}

// TAG:copyrights
// ESPShell copyright 2024, by vvb333007 <vvb@nym.hush.com> https://github/vvb33007/espshell
// EditLine copyright 1992,1993 Simmule Turner and Rich Salz.  All rights reserved
