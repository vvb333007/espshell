
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
 *
 * 4. Structures and #includes are all over the file: they are in close proximity
 *    to the code which needs/uses them.
 *
 * 5. You can search for "TAG:" lines to get the idea on what is where and how
 *    to find it.
 *
 * 6. To create support for new type of console device (e.g. USB-CDC console, 
 *    not supported at the moment) one have to implement console_read_bytes(), 
 *    console_write_bytes(),anykey_pressed() and console_isup() functions
 *
 * 7. Copyright information is at the end of this file
 */

#undef WITH_HELP
#undef WITH_VAR
#undef UNIQUE_HISTORY
#undef HIST_SIZE
#undef STACKSIZE
#undef BREAK_KEY
#undef USE_UART
#undef DO_ECHO

// COMPILE TIME SETTINGS
// ---------------------
//#define SERIAL_IS_USB        //Not yet
//#define ESPCAM               //include ESP32CAM commands (read extra/README.md).

#define WITH_COLOR     0          // Enable terminal colors
#define WITH_HELP      1          // Set to 0 to save some program space by excluding help strings/functions
#define WITH_VAR       0          // Enable or disable "var" command. Default is "disabled"
#define UNIQUE_HISTORY 1          // Wheither to discard repeating commands from the history or not
#define HIST_SIZE      20         // History buffer size (number of commands to remember)
#define STACKSIZE      5000       // Shell task stack size
#define BREAK_KEY      3          // Keycode of an "Exit" key: CTRL+C to exit uart "tap" mode
#define SEQUENCES_NUM  10         // Max number of sequences available for command "sequence"
#define DO_ECHO        1          // -1 - espshell is completely silent, commands are executed but all screen output is disabled
                                  //  0 - espshell does not do echo for user input (as modem ATE0 command). commands are executed and their output is displayed
                                  //  1 - espshell default behaviour (do echo, enable all output)
#define USE_UART       UART_NUM_0 // Uart where shell will be deployed at startup, or UART_NUM_MAX for USB-CDC

#define COMPILING_ESPSHELL 1      // dont touch this!
                            
// include user-defined commands and their handlers
// if any.
#ifdef ESPCAM
#  define EXTERNAL_PROTOTYPES  "esp32cam_prototypes.h"
#  define EXTERNAL_KEYWORDS  "esp32cam_keywords.c"
#  define EXTERNAL_HANDLERS  "esp32cam_handlers.c"
#else
#  undef EXTERNAL_PROTOTYPES
#  undef EXTERNAL_KEYWORDS
#  undef EXTERNAL_HANDLERS
#endif


#define PROMPT      "esp32#>"
#define PROMPT_I2C  "esp32-i2c#>"
#define PROMPT_UART "esp32-uart#>"
#define PROMPT_SEQ  "esp32-seq#>"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <Arduino.h>

//autostart espshell
static void __attribute__((constructor)) espshell_start();

// misc forwards
static int __attribute__((format (printf, 1, 2))) q_printf(const char *, ...);
static int q_print(const char *);


#ifdef SERIAL_IS_USB // Serial is class USBCDC
static int console_here(int i) {
#  error "console_here() is not implemented"  
}
static int console_write_bytes(const void *buf, size_t len) {
#  error "console_write_bytes() is not implemented"  
  return 0;
}
static int console_read_bytes(void *buf, uint32_t len, TickType_t wait) {
#  error "console_read_bytes() is not implemented"  
  return 0;
}
static bool console_isup(int u) {
#  error "console_isup() is not implemented"
  return false;
}
#else // Serial is class HardwareSerial
#include <driver/uart.h>
static uart_port_t uart = USE_UART;

static inline bool uart_isup(int u);

static inline __attribute__((always_inline)) 
int console_write_bytes(const void *buf, size_t len) {
  return uart_write_bytes(uart,buf,len);
}
static inline __attribute__((always_inline)) 
int console_read_bytes(void *buf, uint32_t len, TickType_t wait) {
  return uart_read_bytes(uart,buf,len,wait);
}
static inline __attribute__((always_inline)) 
bool console_isup() {
  return uart_isup(uart);
}
// Make ESPShell to use specified UART for its IO. Default is uart0.
// console_here(-1) returns uart port number occupied by espshell
static inline __attribute__((always_inline)) 
int console_here(int i) {
  return i < 0 ? uart : (i > UART_NUM_MAX ? -1 : (uart = i));
}
#endif //SERIAL_IS_USB

// external command queue. 
// commands placed on this queue gets executed first



//========>=========>======= EDITLINE CODE BELOW (modified ancient version) =======>=======>



#define CRLF "\r\n"

#define MEM_INC 64      // "Line" buffer realloc increments
#define SCREEN_INC 256  // "Screen" buffer realloc increments


/* Memory buffer macros */
#define DISPOSE(p) free((char *)(p))
#define NEW(T, c) ((T *)malloc((unsigned int)(sizeof(T) * (c))))
#define RENEW(p, T, c) (p = (T *)realloc((char *)(p), (unsigned int)(sizeof(T) * (c))))
#define COPYFROMTO(_new, p, len) (void)memcpy((char *)(_new), (char *)(p), (int)(len))

//#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/termios.h>

#define NO_ARG (-1)
#define DEL 127
#define CTL(x) ((x)&0x1F)
#define ISCTL(x) ((x) && (x) < ' ')
#define UNCTL(x) ((x) + 64)
#define META(x) ((x) | 0x80)
#define ISMETA(x) ((x)&0x80)
#define UNMETA(x) ((x)&0x7F)

/*
**  Command status codes.
*/
typedef enum _STATUS {
  CSdone,
  CSeof,
  CSmove,
  CSdispatch,
  CSstay,
  CSsignal
} STATUS;

/*
**  The type of case-changing to perform.
*/
typedef enum _CASE {
  TOupper,
  TOlower
} CASE;

/*
**  Key to command mapping.
*/
typedef struct _KEYMAP {
  unsigned char Key;
  STATUS(*Function)
  ();
} KEYMAP;

/*
**  Command history structure.
*/
typedef struct _HISTORY {
  int Size;
  int Pos;
  unsigned char *Lines[HIST_SIZE];
} HISTORY;

/*
 *  Globals.
 */



static unsigned rl_eof = 0;

static unsigned char NIL[] = "";
static const unsigned char *Input = NIL;
static unsigned char *Line = NULL;
static const char *Prompt = NULL;
static unsigned char *Yanked;
static char *Screen = NULL;
static const char NEWLINE[] = CRLF;
static HISTORY H;
static int Repeat;
static int End;
static int Mark;
static int OldPoint;
static int Point;
static int PushBack;
static int Pushed;
static int Signal = 0;

static bool Color = false; //enable coloring


static const char *TTYq = NULL;    //"command queue". if non-null then symbols are 
                                   // fed to TTYget as if it was user input. No locking so far
static unsigned int TTYq_len = 0;
static unsigned int TTYq_txi = 0;

static unsigned int Length;
static unsigned int ScreenCount;
static unsigned int ScreenSize;
static char *backspace = NULL;
static int Echo = DO_ECHO;

/* Display print 8-bit chars as `M-x' or as the actual 8-bit char? */
static int rl_meta_chars = 0;

static unsigned char *editinput();
static STATUS ring_bell();
static STATUS inject_exit();

static STATUS beg_line();
static STATUS end_line();
static STATUS kill_line();
static STATUS accept_line();

static STATUS bk_char();
static STATUS del_char();
static STATUS fd_char();
static STATUS bk_del_char();
static STATUS move_to_char();

static STATUS bk_kill_word();
static STATUS bk_word();
static STATUS fd_kill_word();
static STATUS fd_word();
static STATUS case_down_word();
static STATUS case_up_word();

static STATUS h_next();
static STATUS h_prev();
static STATUS h_search();
static STATUS h_first();
static STATUS h_last();

static STATUS redisplay();
static STATUS clear_screen();

static STATUS transpose();
static STATUS quote();
static STATUS wipe();
static STATUS exchange();
static STATUS yank();
static STATUS meta();
static STATUS mk_set();
static STATUS last_argument();
static STATUS toggle_meta_mode();
static STATUS copy_region();

static const KEYMAP Map[] = {
  { CTL('@'), ring_bell },
  { CTL('A'), beg_line },
  { CTL('B'), bk_char },
  { CTL('D'), del_char },
  { CTL('E'), end_line },
  { CTL('F'), fd_char },
  { CTL('G'), ring_bell },
  { CTL('H'), bk_del_char },
  { CTL('J'), accept_line },
  { CTL('K'), kill_line },
  { CTL('L'), /*case_down_word*/ clear_screen },
  { CTL('M'), accept_line },
  { CTL('N'), h_next },
  { CTL('O'), bk_word },
  { CTL('P'), fd_word },
  { CTL('Q'), ring_bell },
  { CTL('R'), h_search },
  { CTL('S'), ring_bell },
  { CTL('T'), transpose },
  { CTL('U'), case_up_word },
  { CTL('V'), quote },
  { CTL('W'), wipe },
  { CTL('X'), exchange },
  { CTL('Y'), yank },
  { CTL('Z'), inject_exit },
  { CTL('['), meta },
  { CTL(']'), move_to_char },
  { CTL('^'), ring_bell },
  { CTL('_'), ring_bell },
  { 0, NULL }
};

static const KEYMAP MetaMap[16] = {
  { CTL('H'), bk_kill_word },
  { DEL, bk_kill_word },
  { ' ', mk_set },
  { '.', last_argument },
  { '<', h_first },
  { '>', h_last },
  { 'b', bk_word },
  { 'd', fd_kill_word },
  { 'f', fd_word },
  { 'l', case_down_word },
  { 'm', toggle_meta_mode },
  { 'u', case_up_word },
  { 'y', yank },
  { 'w', copy_region },
  { 0, NULL }
};

// coloring macros.
// coloring is auto-enabled upon reception of ESC code
//
#define font_color(COLOR_NUM) if (Color) q_printf("\033[3%dm",(COLOR_NUM));
#define font_bold() if (Color) q_print("\033[1m")
#define font_normal() if (Color) q_print("\033[0m")


static int
TTYqueue(const char *input) {
  if (TTYq || TTYq_txi)
    return 0;
  if ((TTYq_len = strlen(input)) > 0)
    TTYq = input;
  return TTYq_len;
}

// Print buffered (by TTYputc/TTYputs) data
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
// CTRL/ALT+Key sequences are displayed as ^Key or M-Key
// DEL is "^?"
static void
TTYshow(unsigned char c) {
  if (c == DEL) {
    TTYput('^');
    TTYput('?');
  } else if (ISCTL(c)) {
    TTYput('^');
    TTYput(UNCTL(c));
  } else if (rl_meta_chars && ISMETA(c)) {
    TTYput('M');
    TTYput('-');
    TTYput(UNMETA(c));
  } else
    TTYput(c);
}

// same as above but for the string
static void
TTYstring(unsigned char *p) {
  while (*p)
    TTYshow(*p++);
}

//read a character from user
static unsigned int
TTYget() {

  unsigned char c;
  

  TTYflush();

  if (Pushed) {
    Pushed = 0;
    return PushBack;
  }

  if (*Input)
    return *Input++;

  // read byte from a priority queue if any.
  if (TTYq) {
    c = TTYq[TTYq_txi++];
    if (TTYq_txi >= TTYq_len) {
      TTYq = NULL;
      TTYq_txi = TTYq_len = 0;
    }
    return c;
  }
  // read 1 byte from user
  if (console_read_bytes(&c, 1, portMAX_DELAY) < 1)
    return EOF;

#if WITH_COLOR   
  if (c == '\033' && Color == false) //Not an ArduinoIDE Serial Monitor
    Color = true;
#endif    

  return c;
}

#define TTYback() (backspace ? TTYputs((unsigned char *)backspace) : TTYput('\b'))

static void
TTYbackn(int n) {
  while (--n >= 0)
    TTYback();
}



static void
reposition() {
  int i;
  unsigned char *p;

  TTYput('\r');
  if (Color) TTYputs("\033[1;37m");
  TTYputs((const unsigned char *)Prompt);
  //if (Color) TTYputs("\033[0m");
  for (i = Point, p = Line; --i >= 0; p++)
    TTYshow(*p);
}

static void
left(STATUS Change) {
  TTYback();
  if (Point) {
    if (ISCTL(Line[Point - 1]))
      TTYback();
    else if (rl_meta_chars && ISMETA(Line[Point - 1])) {
      TTYback();
      TTYback();
    }
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

static STATUS
inject_exit() {
  TTYqueue("exit\n");
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

static STATUS
do_case(CASE type) {
  int i;
  int end;
  int count;
  unsigned char *p;

  (void)do_forward(CSstay);
  if (OldPoint != Point) {
    if ((count = Point - OldPoint) < 0)
      count = -count;
    Point = OldPoint;
    if ((end = Point + count) > End)
      end = End;
    for (i = Point, p = &Line[i]; i < end; i++, p++) {
      if (type == TOupper) {
        if (islower(*p))
          *p = toupper(*p);
      } else if (isupper(*p))
        *p = tolower(*p);
      right(CSmove);
    }
  }
  return CSstay;
}

static STATUS
case_down_word() {
  return do_case(TOlower);
}

static STATUS
case_up_word() {
  return do_case(TOupper);
}

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
    } else if (rl_meta_chars && ISMETA(*p)) {
      TTYput(' ');
      TTYput(' ');
      extras += 2;
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
    if ((_new = NEW(unsigned char, Length + len + MEM_INC)) == NULL)
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
  TTYputs((const unsigned char *)NEWLINE);
  if (Color) TTYputs("\033[1;37m");
  TTYputs((const unsigned char *)Prompt);
  //if (Color) TTYputs("\033[0m");
  TTYstring(Line);
  return CSmove;
}

static STATUS
toggle_meta_mode() {
  rl_meta_chars = !rl_meta_chars;
  return redisplay();
}


static unsigned char *
next_hist() {
  return H.Pos >= H.Size - 1 ? NULL : H.Lines[++H.Pos];
}

static unsigned char *
prev_hist() {
  return H.Pos == 0 ? NULL : H.Lines[--H.Pos];
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

static STATUS
h_next() {
  return do_hist(next_hist);
}

static STATUS
h_prev() {
  return do_hist(prev_hist);
}

static STATUS
h_first() {
  return do_insert_hist(H.Lines[H.Pos = 0]);
}

static STATUS
h_last() {
  return do_insert_hist(H.Lines[H.Pos = H.Size - 1]);
}

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
    old_search = (unsigned char *)strdup((char *)search);
  } else {
    if (old_search == NULL || *old_search == '\0')
      return NULL;
    search = old_search;
  }

  /* Set up pattern-finder. */
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
static STATUS
h_search() {
  static int Searching;
  const char *old_prompt;
  unsigned char *(*move)();
  unsigned char *p;

  if (Searching)
    return ring_bell();
  Searching = 1;

  clear_line();
  old_prompt = Prompt;
  Prompt = "Search: ";
  if (Color) TTYputs("\033[1;37m");
  TTYputs((const unsigned char *)Prompt);
  //if (Color) TTYputs("\033[0m");
  move = Repeat == NO_ARG ? prev_hist : next_hist;
  p = editinput();
  Prompt = old_prompt;
  Searching = 0;
  //imperfection on CTRL+R: prompt is displayed at the end of the line
  //TTYputs((const unsigned char *)Prompt);
  if (p == NULL && Signal > 0) {
    Signal = 0;
    clear_line();
    return redisplay();
  }
  p = search_hist(p, move);
  clear_line();
  if (p == NULL) {
    (void)ring_bell();
    return redisplay();
  }
  return do_insert_hist(p);
}

static STATUS
fd_char() {
  int i;

  i = 0;
  do {
    if (Point >= End)
      break;
    right(CSmove);
  } while (++i < Repeat);
  return CSstay;
}

static void
save_yank(int begin, int i) {
  if (Yanked) {
    DISPOSE(Yanked);
    Yanked = NULL;
  }

  if (i < 1)
    return;

  if ((Yanked = NEW(unsigned char, (unsigned int)i + 1)) != NULL) {
    COPYFROMTO(Yanked, &Line[begin], i);
    Yanked[i] = '\0';
  }
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
    } else if (rl_meta_chars && ISMETA(*p)) {
      i = 3;
      TTYput(' ');
      TTYput(' ');
    }
    TTYbackn(i);
    *p = '\0';
    return CSmove;
  }
  if (Point + count > End && (count = End - Point) <= 0)
    return CSstay;

  if (count > 1)
    save_yank(Point, count);

  for (p = &Line[Point], i = End - (Point + count) + 1; --i >= 0; p++)
    p[0] = p[count];
  ceol();
  End -= count;
  TTYstring(&Line[Point]);
  return CSmove;
}

static STATUS
bk_char() {
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
bk_del_char() {
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

  save_yank(Point, End - Point);
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

  if ((p = NEW(unsigned char, Repeat + 1)) == NULL)
    return CSstay;
  for (i = Repeat, q = p; --i >= 0;)
    *q++ = c;
  *q = '\0';
  Repeat = 0;
  s = insert_string(p);
  DISPOSE(p);
  return s;
}

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
      case 'A': return h_prev();   // Arrow UP
      case 'B': return h_next();   // Arrow DOWN
      case 'C': return fd_char();  // Arrow RIGHT
      case 'D': return bk_char();  // Arrow LEFT
    }


  if (isdigit(c)) {
    for (Repeat = c - '0'; (int)(c = TTYget()) != EOF && isdigit(c);)
      Repeat = Repeat * 10 + c - '0';
    Pushed = 1;
    PushBack = c;
    return CSstay;
  }

  if (isupper(c))
    return ring_bell();

  //FIXME:
  for (OldPoint = Point, kp = MetaMap; kp->Function; kp++)
    if (kp->Key == c)
      return (*kp->Function)();

  return ring_bell();
}

static STATUS
emacs(unsigned int c) {
  STATUS s;
  const KEYMAP *kp;

  if (rl_meta_chars && ISMETA(c)) {
    Pushed = 1;
    PushBack = UNMETA(c);
    return meta();
  }
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
    return del_char();

/*
  if (c == rl_kill) {
    if (Point != 0) {
      Point = 0;
      reposition();
    }
    Repeat = NO_ARG;
    return kill_line();
  }
*/
  if (c == rl_eof && Point == 0 && End == 0)
    return CSeof;

  return CSdispatch;
}

static unsigned char *
editinput() {
  unsigned int c;
  //char tmp[2] = { 0, 0 };


  Repeat = NO_ARG;
  OldPoint = Point = Mark = End = 0;
  Line[0] = '\0';

  Signal = -1;
  while ((int)(c = TTYget()) != EOF) {

    //printf("%02x ",c);
    switch (TTYspecial(c)) {
      case CSdone:
        return Line;
      case CSeof:
        return NULL;
      case CSsignal:
        return (unsigned char *)"";
      case CSmove:
        reposition();
        break;
      case CSdispatch:
        switch (emacs(c)) {
          case CSdone:
            return Line;
          case CSeof:
            return NULL;
          case CSsignal:
            return (unsigned char *)"";
          case CSmove:
            reposition();
            break;
          case CSdispatch:
          case CSstay:
            break;
        }
        break;
      case CSstay:
        break;
    }
  }
  if (strlen((char *)Line))
    return Line;

  //Original code has a bug here: Line was not set to NULL causing random heap corruption
  //on ESP32
  free(Line);
  return (Line = NULL);
}

static void
hist_add(unsigned char *p) {
  int i;

  if ((p = (unsigned char *)strdup((char *)p)) == NULL)
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
  //int s;

  if (Line == NULL) {
    Length = MEM_INC;
    if ((Line = NEW(unsigned char, Length)) == NULL)
      return NULL;
  }


  
  hist_add(NIL);
  ScreenSize = SCREEN_INC;
  Screen = NEW(char, ScreenSize);
  Prompt = prompt ? prompt : (char *)NIL;
  if (Color) TTYputs("\033[1;37m");
  TTYputs((const unsigned char *)Prompt);
  //if (Color) TTYputs("\033[0m");
  TTYflush();

  if ((line = editinput()) != NULL) {
    line = (unsigned char *)strdup((char *)line);
    TTYputs((const unsigned char *)NEWLINE);
    TTYflush();
  }

  
  DISPOSE(Screen);
  DISPOSE(H.Lines[--H.Size]);
  //TODO: remove signal processing
  if (Signal > 0) {
    //s = Signal;
    Signal = 0;
    //(void)kill(getpid(), s);
  }
  return (char *)line;
}

// add an arbitrary string p to the command history
// ARROW UP and ARROW DOWN are history navigation keys
// CTRL+R is history search key
//
static void rl_add_history(char *p) {
  if (p == NULL || *p == '\0')
    return;

#if UNIQUE_HISTORY
  if (H.Size && strcmp(p, (char *)H.Lines[H.Size - 1]) == 0)
    return;
#endif
  hist_add((unsigned char *)p);
}


static STATUS
beg_line() {
  if (Point) {
    Point = 0;
    return CSmove;
  }
  return CSstay;
}

static STATUS
del_char() {
  return delete_string(Repeat == NO_ARG ? 1 : Repeat);
}

static STATUS
end_line() {
  if (Point != End) {
    Point = End;
    return CSmove;
  }
  return CSstay;
}

static STATUS
accept_line() {
  Line[End] = '\0';
  if (Color) q_print("\033[0m");
  return CSdone;
}

static STATUS
transpose() {
  unsigned char c;

  if (Point) {
    if (Point == End)
      left(CSmove);
    c = Line[Point - 1];
    left(CSstay);
    Line[Point - 1] = Line[Point];
    TTYshow(Line[Point - 1]);
    Line[Point++] = c;
    TTYshow(c);
  }
  return CSstay;
}

static STATUS
quote() {
  unsigned int c;

  return (int)(c = TTYget()) == EOF ? CSeof : insert_char((int)c);
}

static STATUS
wipe() {
  int i;

  if (Mark > End)
    return ring_bell();

  if (Point > Mark) {
    i = Point;
    Point = Mark;
    Mark = i;
    reposition();
  }

  return delete_string(Mark - Point);
}

static STATUS
mk_set() {
  Mark = Point;
  return CSstay;
}

static STATUS
exchange() {
  unsigned int c;

  if ((c = TTYget()) != CTL('X'))
    return (int)c == EOF ? CSeof : ring_bell();

  if ((int)(c = Mark) <= End) {
    Mark = Point;
    Point = c;
    return CSmove;
  }
  return CSstay;
}

static STATUS
yank() {
  if (Yanked && *Yanked)
    return insert_string(Yanked);
  return CSstay;
}

static STATUS
copy_region() {
  if (Mark > End)
    return ring_bell();

  if (Point > Mark)
    save_yank(Mark, Point - Mark);
  else
    save_yank(Point, Mark - Point);

  return CSstay;
}

static STATUS
move_to_char() {
  unsigned int c;
  int i;
  unsigned char *p;

  if ((int)(c = TTYget()) == EOF)
    return CSeof;
  for (i = Point + 1, p = &Line[i]; i < End; i++, p++)
    if (*p == c) {
      Point = i;
      return CSmove;
    }
  return CSstay;
}

static STATUS
fd_word() {
  return do_forward(CSmove);
}

static STATUS
fd_kill_word() {
  int i;

  (void)do_forward(CSstay);
  if (OldPoint != Point) {
    i = Point - OldPoint;
    Point = OldPoint;
    return delete_string(i);
  }
  return CSstay;
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
// Use: int argc; char **argv; argc = argify(p,&argv);
static int
argify(unsigned char *line, unsigned char ***avp) {
  unsigned char *c;
  unsigned char **p;
  unsigned char **_new;
  int ac;
  int i;

  i = MEM_INC;
  if ((*avp = p = NEW(unsigned char *, i)) == NULL)
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

          _new = NEW(unsigned char *, i + MEM_INC);

          if (_new == NULL) {
            p[ac] = NULL;
            return ac;
          }

          COPYFROMTO(_new, p, i * sizeof(char **));
          i += MEM_INC;
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


static STATUS
last_argument() {
  unsigned char **av;
  unsigned char *p;
  STATUS s;
  int ac;

  if (H.Size == 1 || (p = H.Lines[H.Size - 2]) == NULL)
    return ring_bell();

  if ((p = (unsigned char *)strdup((char *)p)) == NULL)
    return CSstay;
  ac = argify(p, &av);

  if (Repeat != NO_ARG)
    s = Repeat < ac ? insert_string(av[Repeat]) : ring_bell();
  else
    s = ac ? insert_string(av[ac - 1]) : CSstay;

  if (ac)
    DISPOSE(av);
  DISPOSE(p);
  return s;
}



////////////////////////////// EDITLINE CODE END


//TAG:forwards
//GCC stringify magic.
#define xstr(s) ystr(s)
#define ystr(s) #s

#define MAGIC_FREQ 312000  // max allowed frequency for the "pwm" command

static bool Exit = false; // True == close the shell and kill its FreeRTOS task


int espshell_exec(const char *p); //execute 1 command line


static int q_strcmp(const char *, const char *); // loose strcmp
#if WITH_HELP
static int cmd_question(int, char **);
#endif
static int cmd_pin(int, char **);

static int cmd_cpu(int, char **);
static int cmd_cpu_freq(int, char **);
static int cmd_uptime(int, char **);
static int cmd_mem(int, char **);
static int cmd_reload(int, char **);
static int cmd_nap(int, char **);

static int cmd_i2c_if(int, char **);
static int cmd_i2c_clock(int, char **);
static int cmd_i2c(int, char **);

static int cmd_uart_if(int, char **);
static int cmd_uart_baud(int, char **);
static int cmd_uart(int, char **);

static int cmd_tty(int, char **);
static int cmd_echo(int,char **);

static int cmd_suspend(int, char **);
static int cmd_resume(int, char **);

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
#if WITH_VAR
static int cmd_var(int, char **);
#endif
static int cmd_show(int, char **);

static int cmd_exit(int, char **);

// suport for user-defined commands
#ifdef EXTERNAL_PROTOTYPES
#include EXTERNAL_PROTOTYPES
#endif


//TAG:util

//check if given ascii string is a decimal number. Ex.: "12345", "-12"
// "minus" sign is only accepted if first in the string
//
static bool isnum(const char *p) {
  if (*p == '-')
    p++;
  while (*p >= '0' && *p <= '9')
    p++;
  return !(*p);  //if *p is 0 then all the chars were digits (end of line reached).
}

// check if ascii string is a float number
// NOTE: "0.5" and ".5" are both valid inputs
static bool isfloat(const char *p) {
  bool dot = false;
  if (*p == '-')
    p++;
  while ((*p >= '0' && *p <= '9') || (*p == '.' && !dot)) {
    if (*p == '.')
      dot = true;
    p++;
  }
  return !(*p);  //if *p is 0 then all the chars were ok. (end of line reached).
}



// check if given ascii string is a hex BYTE.
// first 1 or 2 characters are checked
static bool ishex(const char *p) {
  if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
    p++;
    if ((*p == 0) || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
      return true;
  }
  return false;
}

//convert hex ascii byte.
//input strings are 1 or 2 chars long:  Ex.:  "0A", "A","6E"
//TODO: rewrite. as of now it accepts whole latin1 set not just ABCDEF
static unsigned char ascii2hex(const char *p) {

  unsigned char f, l;  //first and last

  f = *p++;

  //single character HEX?
  if (!(*p)) {
    l = f;
    f = '0';
  } else l = *p;

  // make it lowercase
  if (f >= 'A' && f <= 'Z') f = f + 'a' - 'A';
  if (l >= 'A' && l <= 'Z') l = l + 'a' - 'A';

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
  return strncmp(partial,full, plen);
}

static inline char *q_findchar(char *str, char sym) {
  while(*str && sym != *str++) ;
  return *str ? str : NULL;
}


// adopted from esp32-hal-uart.c Arduino Core
// Modified to support multiple uarts via uart_write_bytes()
//
static int __printfv(const char *format, va_list arg) {

  static char buf[128 + 1];
  char *temp = buf;
  uint32_t len;
  int ret;
  va_list copy;

  if (Echo < 0) // "echo silent" mode
    return 0;

  va_copy(copy, arg);
  len = vsnprintf(NULL, 0, format, copy);
  va_end(copy);

  if (len >= sizeof(buf))
    if ((temp = (char *)malloc(len + 1)) == NULL)
	    return 0;

  vsnprintf(temp, len + 1, format, arg);

  ret = console_write_bytes(temp, len);

  if (len >= sizeof(buf))
    free(temp);
 
//  while (!uart_ll_is_tx_idle(UART_LL_GET_HW(uart)));   // flushes TX - make sure that the log message is completely sent.
 

  return ret;
}

// same as printf() but uses global var 'uart' to direct
// its output to different uarts
// NOTE: add -Wall or at least -Wformat to Arduino's c_flags for __attribute__ to have
//       effect.
static int __attribute__((format (printf, 1, 2))) q_printf(const char *format, ...) {
  int len;
  va_list arg;
  va_start(arg, format);
  len =__printfv(format, arg);
  va_end(arg);
  return len;
}

//Faster than q_printf() but only does non-formatted output
static int q_print(const char *str) {
  size_t len;
  if (Echo < 0) //"echo silent"
    return 0;
  if ((len = strlen(str)) > 0)
    len = console_write_bytes(str, len);
  return len;
}

static inline void __attribute__((always_inline)) q_println(const char *str) {
  q_print(str);
  q_print(CRLF);
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

  
  char ascii[16+1];
  unsigned int space = 1;

  q_print("       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F  |0123456789ABCDEF\r\n");
  q_print("----------------------------------------------------------+----------------\r\n");

  for (unsigned int i = 0, j = 0; i < len; i++) {
    // add offset at the beginning of every line. it doesnt hurt to have it.
    // and it is useful when exploring eeprom contens
    if (!j)
      q_printf("%04x: ",i);
    
    //print hex byte value
    q_printf("%02x ", p[i]);

    // add an extra space after each 4 bytes printed
    if ((space++ & 3) == 0) 
      q_print(" ");

    //add printed byte to ascii representation
    //dont print anything with codes less than 32
    ascii[j++] = (p[i] <  ' ') ? '.' : p[i]; 
    
    // one complete line could be printed:
    // we had 16 bytes or we reached end of the buffer
    if ((j > 15) || (i + 1) >= len) { 

      // end of buffer but less than 16 bytes:
      // pad line with spaces 
      if (j < 16) {
        unsigned char spaces = (16 - j)*3 + (j <= 4 ? 3 : (j <=8 ? 2 : (j <= 12 ? 1 : 0))); // fully agreed :-|
        char tmp[spaces + 1];
        memset(tmp,' ',spaces);
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

#if WITH_VAR
// Console Variables.
//
// User sketch can register global or static variables to be accessible
// from ESPShell. Once registered, variables can be manipulated by
// "var" command. See "extra/espshell.h" for convar_add() definition

struct convar {
	struct convar *next;
	const char    *name;
	void          *ptr;
	int            size;
};

static struct convar *var_head = NULL;

// register new sketch variable.
//
void espshell_varadd(const char *name, void *ptr, int size) {

	struct convar *var;

	if (size != 1 && size != 2 && size != 4)
		return;

	if ((var = (struct convar *)malloc(sizeof(struct convar))) != NULL) {
		var->next = var_head;
		var->name = name;
		var->ptr  = ptr;
		var->size = size;
		var_head = var;
	}
	
}

// get registered variable value
//
static int convar_get(const char *name, void *value) {

	struct convar *var = var_head;
	while (var) {
		if (!strcmp(var->name,name)) {
      memcpy(value,var->ptr,var->size);
      return 0;
		}
		var = var->next;
	}
  return -1;
}

// set registered variable value
//
static int convar_set(const char *name, void *value) {
	struct convar *var = var_head;
	while (var) {
		if (!strcmp(var->name,name)) {
			memcpy(var->ptr, value, var->size);
      return 0;
    }
		var = var->next;
	}
  return -1;
}
#endif //WITH_VAR


#if SERIAL_IS_USB
static bool anykey_pressed() {
#error "anykey_pressed() is not implemented"  
  return false;
}
#else
//detects if ANY key is pressed in serial terminal
// or any character was sent in Arduino IDE Serial Monitor
//
static bool anykey_pressed() {

  size_t av = 0;

  if (ESP_OK == uart_get_buffered_data_len(uart, &av))
    if (av > 0)
      return true;

  return false;
}
#endif //SERIAL_IS_USB

// version of delay() which can be interrupted by user input (terminal 
// keypress) for delays longer than 5 seconds. 
//
// for delays shorter than 5 seconds fallbacks to normal(delay)
//
// `duration` - delay time in milliseconds
//  returns duration if ok, <duration if was interrupted
//
static unsigned int delay_interruptible(unsigned int duration) {

  // if duration is longer than 4999ms split it in 250ms
  // intervals and check for user input in between these
  // intervals.
  unsigned int delayed = 0;
  if (duration > 4999) {
    while (duration >= 250) {
      duration -= 250;
      delayed += 250;
      delay(250); 
      if (anykey_pressed())
        return delayed;
    }
  }
  if (duration) {
    delayed += duration;
    delay(duration);
  }
  return delayed;
}


//TAG:sequences
//
// Sequences: the data structure defining a sequence of pulses (levels) to
// be transmitted. Sequences are used to generate an user-defined LOW/HIGH
// pulses train on an arbitrary GPIO pin using ESP32's hardware RMT peri
//
// These are used by "sequence X" command (and by a sequence subdirectory
// commands as well)
//
// TODO: bytes, head, tail (NEC protocol support)
//
#include <esp32-hal-rmt.h>

struct sequence {

  float tick;                  // uSeconds.  1000000 = 1 second.   0.1 = 0.1uS
  float mod_duty;              // modulator duty
  unsigned int mod_freq : 30;  // modulator frequency
  unsigned int mod_high : 1;   // modulate "1"s or "0"s
  unsigned int eot : 1;        // end of transmission level
  int seq_len;                 // how may rmt_data_t items is in "seq"
  rmt_data_t *seq;             // array of rmt_data_t
  rmt_data_t alph[2];          // alphabet. representation of "0" and "1"
  char *bits;                  // asciiz
};


// TAG:keywords
//
// Shell commands list structure
struct keywords_t {
  const char *cmd;                 // Command keyword ex.: "pin"
  int (*cb)(int argc, char **argv);// Callback to call (one of cmd_xxx functions)
  int         argc;                // Number of arguments required. Negative values mean "any"  
  const char *help;                // Help text displayed on "command ?"
  const char *brief;               // Brief text displayed on "?". NULL means "use help text, not brief"
};

#if WITH_HELP
#  define HELP(X) X
#  define KEYWORDS_BEGIN { "?", cmd_question, -1, "% \"?\" - Show the list of available commands\r\n% \"? comm\" - Get help on command \"comm\"\r\n% \"? keys\" - Get information on terminal keys used by ESPShell", "Commands list & help" },
#else
#  define HELP(X) ""
#  define KEYWORDS_BEGIN
#endif


#define KEYWORDS_END {"exit", cmd_exit, -1, "Exit", NULL}, {NULL, NULL, 0, NULL, NULL}

#define HIDDEN_KEYWORD NULL,NULL


//Custom uart commands (uart subderictory)
//Those displayed after executing "uart 2" (or any other uart interface)
//TAG:keywords_uart
//
static const struct keywords_t keywords_uart[] = {

  KEYWORDS_BEGIN

  { "up", cmd_uart, 3,HELP("% \"up RX TX BAUD\"\r\n" \
    "%\r\n" \
    "% Initialize uart interface X on pins RX/TX,baudrate BAUD, 8N1 mode\r\n" \
    "% Ex.: up 18 19 115200 - Setup uart on pins rx=18, tx=19, at speed 115200"), "Initialize uart (pins/speed)" },

  { "baud", cmd_uart_baud, 1,HELP("% \"baud SPEED\"\r\n" \
    "%\r\n" \
    "% Set speed for the uart (uart must be initialized)\r\n" \
    "% Ex.: baud 115200 - Set uart baud rate to 115200"), "Set baudrate" },

  { "down", cmd_uart, 0,HELP("% \"down\"\r\n" \
    "%\r\n" \
    "% Shutdown interface, detach pins"), "Shutdown" },

  // overlaps with "uart X down", never called, here is for the help text only
  { "read", cmd_uart, 0,HELP("% \"read\"\r\n" \
    "%\r\n" \
    "% Read bytes (available) from uart interface X"), "Read data from UART" },

  { "tap", cmd_uart, 0,HELP("% \"tap\\r\n" \
    "%\r\n" \
    "% Bridge the UART IO directly to/from shell\r\n" \
    "% User input will be forwarded to uart X;\r\n" \
    "% Anything UART X sends back will be forwarded to the user"), "Talk to UARTs device" },

  { "write", cmd_uart, -1, HELP("% \"write TEXT\"\r\n" \
    "%\r\n" \
    "% Send an ascii/hex string(s) to UART X\r\n" \
    "% TEXT can include spaces, escape sequences: \\n, \\r, \\\\, \\t and \r\n" \
    "% hexadecimal numbers \\AB (A and B are hexadecimal digits)\r\n" \
    "%\r\n" \
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

  { "up", cmd_i2c, 3,HELP("% \"up SDA SCL CLOCK\"\r\n" \
    "%\r\n" \
    "% Initialize I2C interface X, use pins SDA/SCL, clock rate CLOCK\r\n" \
    "% Ex.: up 21 22 100000 - enable i2c at pins sda=21, scl=22, 100kHz clock"), "initialize interface (pins and speed)" },

  { "clock", cmd_i2c_clock, 1,HELP("% \"clock SPEED\"\r\n" \
    "%\r\n" \
    "% Set I2C master clock (i2c must be initialized)\r\n" \
    "% Ex.: clock 100000 - Set i2c clock to 100kHz"), "Set clock" },


  { "read", cmd_i2c, 2,HELP("% \"read ADDR SIZE\"\r\n"
    "%\r\n"
    "% I2C bus X : read SIZE bytes from a device at address ADDR (hex)\r\n"
    "% Ex.: read 68 7 - read 7 bytes from device address 0x68"), "Read data from a device" },

  { "down", cmd_i2c, 0,HELP("% \"down\"\r\n" \
    "%\r\n" \
    "% Shutdown I2C interface X"), "Shutdown i2c interface" },

  { "scan", cmd_i2c, 0,HELP("% \"scan\"\r\n" \
    "%\r\n" \
    "% Scan I2C bus X for devices. Interface must be initialized!"),"Scan i2c bus" },

  { "write", cmd_i2c, -1,HELP("% \"write ADDR D1 [D2 ... Dn]\"\r\n" \
    "%\r\n" \
    "% Write bytes D1..Dn (hex values) to address ADDR (hex) on I2C bus X\r\n" \
    "% Ex.: write 78 0 1 FF - write 3 bytes to address 0x78: 0,1 and 255"), "Send bytes to the device" },

  KEYWORDS_END
};

//TAG:keywords_seq
//'sequence' subderictory keywords list
static const struct keywords_t keywords_sequence[] = {

  KEYWORDS_BEGIN

  { "eot", cmd_seq_eot, 1,HELP("% \"eot high|low\"\r\n" \
    "%\r\n" \
    "% End of transmission: pull the line high or low at the\r\n" \
    "% end of a sequence. Default is \"low\""), "End-of-Transmission pin state" },

  { "tick", cmd_seq_tick, 1, HELP("% \"tick TIME\"\r\n" \
    "%\r\n" \
    "% Set the sequence tick time: defines a resolution of a pulse sequence.\r\n" \
    "% Expressed in microseconds, can be anything between 0.0125 and 3.2\r\n" \
    "% Ex.: tick 0.1 - set resolution to 0.1 microsecond"), "Set resolution" },

  { "zero", cmd_seq_zeroone, 2,HELP("% \"zero LEVEL/DURATION [LEVEL2/DURATION2]\"\r\n" \
    "%\r\n" \
    "% Define a logic \"0\"\r\n" \
    "% Ex.: zero 0/50      - 0 is a level: LOW for 50 ticks\r\n" \
    "% Ex.: zero 1/50 0/20 - 0 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks"), "Define a zero" },

  { "zero", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

  { "one", cmd_seq_zeroone, 2,HELP("% \"one LEVEL/DURATION [LEVEL2/DURATION2]\"\r\n" \
    "%\r\n" \
    "% Define a logic \"1\"\r\n" \
    "% Ex.: one 1/50       - 1 is a level: HIGH for 50 ticks\r\n" \
    "% Ex.: one 1/50 0/20  - 1 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks"), "Define an one" },

  { "one", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

  { "bits", cmd_seq_bits, 1,HELP("% \"bits STRING\"\r\n" \
    "%\r\n" \
    "% A bit pattern to be used as a sequence. STRING must contain only 0s and 1s\r\n" \
    "% Overrides previously set \"levels\" command\r\n" \
    "% See commands \"one\" and \"zero\" to define \"1\" and \"0\"\r\n" \
    "%\r\n" \
    "% Ex.: bits 11101000010111100  - 17 bit sequence"), "Set pattern to transmit" },

  { "levels", cmd_seq_levels, -1,HELP("% \"levels L/D L/D ... L/D\"\r\n" \
    "%\r\n" \
    "% A bit pattern to be used as a sequnce. L is either 1 or 0 and \r\n" \
    "% D is the duration measured in ticks [0..32767] \r\n" \
    "% Overrides previously set \"bits\" command\r\n" \
    "%\r\n" \
    "% Ex.: levels 1/50 0/20 1/100 0/500  - HIGH 50 ticks, LOW 20, HIGH 100 and 0 for 500 ticks\r\n" \
    "% Ex.: levels 1/32767 1/17233 0/32767 0/7233 - HIGH for 50000 ticks, LOW for 40000 ticks"), "Set levels to transmit" },

  { "modulation", cmd_seq_modulation, 3,HELP("% \"modulation FREQ [DUTY [low|high]]\"\r\n" \
    "%\r\n" \
    "% Enables/disables an output signal modulation with frequency FREQ\r\n" \
    "% Optional parameters are: DUTY (from 0 to 1) and LEVEL (either high or low)\r\n" \
    "%\r\n" \
    "% Ex.: modulation 100         - modulate all 1s with 100Hz, 50%% duty cycle\r\n" \
    "% Ex.: modulation 100 0.3 low - modulate all 0s with 100Hz, 30%% duty cycle\r\n" \
    "% Ex.: modulation 0           - disable modulation\r\n"), "Enable/disable modulation" },

  { "modulation", cmd_seq_modulation, 2, HIDDEN_KEYWORD },
  { "modulation", cmd_seq_modulation, 1, HIDDEN_KEYWORD },

  { "show", cmd_seq_show, 0, "Show sequence" , NULL},

  KEYWORDS_END
};

#if 0 //not yet
//Custom SPI commands (spi subderictory)
//TAG:keywords_spi
static struct keywords_t keywords_spi[] = {

  KEYWORDS_BEGIN

  KEYWORDS_END
};
#endif


// root directory commands
//TAG:keywords_main
static const struct keywords_t keywords_main[] = {

  KEYWORDS_BEGIN

  { "uptime", cmd_uptime, 0, HELP("% \"uptime\" - Shows time passed since last boot"), "System uptime" },
  { "show", cmd_show, 2, HELP("\"show seq X\" - display sequence X\r\n"), "Display information" },
  { "pin", cmd_pin, 1,HELP("% \"pin X\" - Show pin X configuration"), "Pins (GPIO) commands" },
  { "pin", cmd_pin, -1, HELP("% \"pin X (hold|release|up|down|out|in|open|high|low|save|load|read|aread|delay|loop|pwm|seq)...\"\r\n" \
    "%\r\n" \
    "% Set/Save/Load pin X configuration: pull/direction/digital value\r\n" \
    "% Multiple arguments must be separated with spaces, see examples below:\r\n" \
    "% Ex.: pin 1 read aread         -pin1: read digital and then analog values\r\n" \
    "% Ex.: pin 1 out up             -pin1 is OUTPUT with PULLUP\r\n" \
    "% Ex.: pin 1 save               -save pin state\r\n" \
    "% Ex.: pin 1 out high           -pin1 set to logic \"1\"\r\n" \
    "% Ex.: pin 1 high delay 100 low -set pin1 to logic \"1\", after 100ms to \"0\"\r\n" \
    "% Ex.: pin 1 pwm 2000 0.3      -set 5kHz, 30% duty square wave output\r\n" \
    "% Ex.: pin 1 pwm 0 0           -disable generation\r\n" \
    "% Ex.: pin 1 out high delay 500 low delay 500 loop 10 \r\n" \
    "% (see \"docs/Pin_commands.txt\" for more details & examples)\r\n"),NULL },

  
  { "cpu", cmd_cpu_freq, 1, HELP("% \"cpu FREQ\" : Set CPU frequency to FREQ Mhz"), "Set/show CPU parameters" },
  { "cpu", cmd_cpu, 0, HELP("% \"cpu\" : Show CPUID and CPU/XTAL/APB frequencies"), NULL },
  { "reload", cmd_reload, 0, HELP("% \"reload\" - Restarts CPU"), "Reset CPU" },
  { "mem", cmd_mem, 0, HELP("% Show memory usage info"), "Memory usage" },
  

  { "nap", cmd_nap, 1,HELP("% \"nap SEC\"\r\n%\r\n% Put the CPU into light sleep mode for SEC seconds."),"CPU sleep" },
  { "nap", cmd_nap, 0,HELP("% \"nap\"\r\n%\r\n% Put the CPU into light sleep mode, wakeup by console"),NULL },

  { "iic", cmd_i2c_if, 1,HELP("% \"iic X\" \r\n%\r\n" \
    "% Enter I2C interface X configuration mode \r\n" \
    "% Ex.: iic 0 - configure/use interface I2C 0"),"I2C commands" },

  { "uart", cmd_uart_if, 1,HELP("% \"uart X\"\r\n" \
    "%\r\n" \
    "% Enter UART interface X configuration mode\r\n" \
    "% Ex.: uart 1 - configure/use interface UART 1"), "UART commands" },

  { "sequence", cmd_seq_if, 1,HELP("% \"sequence X\"\r\n" \
    "%\r\n" \
    "% Create/configure a sequence\r\n" \
    "% Ex.: sequence 0 - configure Sequence0"), "Sequence configuration" },

  { "tty", cmd_tty, 1, HELP("% \"tty X\" Use uart X for command line interface"), "IO redirect" },

  { "echo", cmd_echo, 1, HELP("% \"echo on|off|silent\" Echo user input on/off (default is on)"), "Enable/Disable user input echo" },
  { "echo", cmd_echo, 0, HIDDEN_KEYWORD}, //hidden command, displays echo status (on / off)

  { "suspend", cmd_suspend, 0, HELP("% \"suspend\" : Suspend main loop()\r\n"), "Suspend sketch execution" },
  { "resume", cmd_resume, 0, HELP("% \"resume\" : Resume main loop()\r\n"), "Resume sketch execution" },

  { "pwm", cmd_pwm, 3,HELP("% \"pwm X FREQ DUTY\"\r\n" \
    "%\r\n" \
    "% Start PWM generator on pin X, frequency FREQ Hz and duty cycle of DUTY\r\n" \
    "% Max frequency is 312000 Hz\r\n" \
    "% Value of DUTY is in range [0..1] with 0.123 being a 12.3% duty cycle" \
    "% Duty resolution is 0.005 (0.5%)"), "PWM output" },

  { "pwm", cmd_pwm, 2,HELP("% \"pwm X FREQ\"\r\n"
    "% Start squarewave generator on pin X, frequency FREQ Hz\r\n"
    "% duty cycle is  set to 50%"), NULL },

  { "pwm", cmd_pwm, 1,HELP("% \"pwm X\" Stop generator on pin X"), NULL },

  { "count", cmd_count, 3,HELP("% \"count PIN [DURATION [neg|pos|both]]\"\r\n%\r\n" \
    "% Count pulses (negative/positive edge or both) on pin PIN within DURATION time\r\n" \
    "% Time is measured in milliseconds, optional. Default is 1000\r\n" \
    "% Pulse edge type is optional. Default is \"pos\"\r\n" \
    "%\r\n" \
    "% Ex.: \"count 4\"           - count positive edges on pin 4 for 1000ms\r\n" \
    "% Ex.: \"count 4 2000\"      - count pulses (falling edge) on pin 4 for 2 sec.\r\n" \
    "% Ex.: \"count 4 2000 both\" - count pulses (falling and rising edge) on pin 4 for 2 sec."), "Pulse counter" },
  { "count", cmd_count, 2, HIDDEN_KEYWORD },  //hidden "count" with 2 args
  { "count", cmd_count, 1, HIDDEN_KEYWORD },  //hidden with 1 arg
#if WITH_VAR
  { "var", cmd_var, -1,HELP("% \"var [VARIABLE_NAME[ NUMBER]]\r\n%\r\n" \
    "% Set sketch variable to new value.\r\n" \
    "% NUMBER can be integer or float point values, positive or negative\r\n" \
    "%\r\n" \
    "% Ex.: \"var button1\" - Display current value of \"button1\" sketch variable\r\n" \
    "% Ex.: \"var a -12.3\" - Set sketch variable \"a\"to new value \"-12.3\"\r\n" \
    "% Ex.: \"var\"         - List all variables"), "Sketch variables" },
#endif //WITH_VAR

#ifdef EXTERNAL_KEYWORDS
#include EXTERNAL_KEYWORDS
#endif

  KEYWORDS_END
};


//TAG:keywords
//current keywords list to use
static const struct keywords_t  *keywords = keywords_main;

//common Failed message
static const char *Failed = "% Failed\r\n";

// prompt
static const char *prompt = PROMPT;

// sequences
static struct sequence sequences[SEQUENCES_NUM];

// interface unit number when entering a subderictory.
// also sequence number when entering "sequence" subdir
static int Context = 0;


// checks if pin (GPIO) number is in valid range.
// display a message if pin is out of range
static bool pin_exist(int pin) {

  // pin number is in range and is a valid GPIO number?
  if ((pin >= 0) && (pin < SOC_GPIO_PIN_COUNT) && (((uint64_t )1 << pin) & SOC_GPIO_VALID_GPIO_MASK))
    return true;
  else {
    int informed = 0;
    // pin number is incorrect, display help
    q_printf("%% Available pin numbers are 0..%d",SOC_GPIO_PIN_COUNT-1);

    for (pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++) {
      if (!(((uint64_t )1 << pin) & SOC_GPIO_VALID_GPIO_MASK)) {
        if (!informed) {
          informed = 1;
          q_print(", except pins: ");
        }
        q_printf("%d,",pin);
      }
    }

    q_printf("\r\n%% Reserved pins (used internally): ");  

    for (pin = informed = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
      if (esp_gpio_is_pin_reserved(pin)) {
        informed++;
        q_printf("%d, ",pin);
      }

    if (!informed)
      q_print("none");

    q_print(CRLF);
    return false;
  }
}

// calculate frequency from tick length
// 0.1uS = 10Mhz
unsigned long __attribute__((const)) seq_tick2freq(float tick_us) {

  return tick_us ? (unsigned long)((float)1000000 / tick_us) : 0;
}

// initialize sequences to default values
//
static void seq_init() {
  int i;

  for (i = 0; i < SEQUENCES_NUM; i++) {
    sequences[i].tick = 1;
    sequences[i].seq = NULL;
    sequences[i].bits = NULL;

    sequences[i].alph[0].duration0 = 0;
    sequences[i].alph[0].duration1 = 0;
    sequences[i].alph[1].duration0 = 0;
    sequences[i].alph[1].duration1 = 0;
  }
}

static const char *Notset = "is not set yet\r\n";

// dump sequence content
static void seq_dump(int seq) {

  struct sequence *s;

  if (seq < 0 || seq >= SEQUENCES_NUM) {
    q_printf("%% Sequence %d does not exist\r\n",seq);
    return;
  }

  s = &sequences[seq];

  q_printf("%%\r\n%% Sequence #%d:\r\n%% Resolution ", seq);

  if (s->tick)
    q_printf(": %.4fuS  (Frequency: %lu Hz)\r\n", s->tick, seq_tick2freq(s->tick));
  else
    q_print(Notset);

  q_print("% Levels ");
  if (s->seq) {

    int i;
    unsigned long total = 0;

    q_print("are :");

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

  q_print("% Bit sequence ");

  if (s->bits)
    q_printf(": (%d bits) \"%s\"\r\n", strlen(s->bits), s->bits);
  else
    q_print(Notset);

  q_print("% Zero ");

  if (s->alph[0].duration0) {
    if (s->alph[0].duration1)
      q_printf("%d/%d %d/%d\r\n", s->alph[0].level0, s->alph[0].duration0, s->alph[0].level1, s->alph[0].duration1);
    else
      q_printf("%d/%d\r\n", s->alph[0].level0, s->alph[0].duration0);
  } else
    q_print(Notset);

  q_print("% One ");

  if (s->alph[1].duration0) {
    if (s->alph[1].duration1)
      q_printf("%d/%d %d/%d\r\n", s->alph[1].level0, s->alph[1].duration0, s->alph[1].level1, s->alph[1].duration1);
    else
      q_printf("%d/%d\r\n", s->alph[1].level0, s->alph[1].duration0);
  } else
    q_print(Notset);
  q_printf("%% Pull pin %s after transmission is done\r\n", s->eot ? "HIGH" : "LOW");
}

#if 0
static int seq_atol(int *level, int *duration, char *p) {

  char *lp = p;
  char *dp = NULL;
  unsigned int l,d;

  while (*p) {
    if (*p == '/' || *p == '\\') {
      if (dp)
        return -1;  // separator was found already
      //if (level)
      *p = '\0';  // split the string for atol
      dp = p + 1;
    } else if (*p < '0' || *p > '9')
      return -1;
    p++;
  }
  if (!dp)  // no separator found
    return -2;
  if (!*lp)  { // separator was the first symbol
    *lp = '/'; // make parser happy: "invalid argument" message will contain failed element, not ""
    return -3;
}
#else

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
      if (level)    *level    = *p - '0';
      if (duration) *duration =  d;
      return 0;
    }
  return -1;
}
#endif

// free memory buffers associated with the sequence:
// ->"bits" and ->"seq"
static void seq_freemem(int seq) {

  if (sequences[seq].bits) {
    free(sequences[seq].bits);
    sequences[seq].bits = NULL;
  }
  if (sequences[seq].seq) {
    free(sequences[seq].seq);
    sequences[seq].seq = NULL;
  }
}

// check if sequence is configured and an be used
// to generate pulses. The criteria is:
// ->seq must be initialized
// ->tick must be set
static bool seq_isready(int seq) {

  if (seq < 0 || seq >= SEQUENCES_NUM)
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
        q_print("% \"One\" defined as a level, but \"Zero\" is a pulse\r\n");
        return -1;
      }

      // long-form. 1 rmt symbol = 1 bit;
      int j, i = strlen(s->bits);
      if (!i)
        return -2;

      s->seq = (rmt_data_t *)malloc(sizeof(rmt_data_t) * i);
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
        q_print("% \"One\" defined as a pulse, but \"Zero\" is a level\r\n");
        return -4;
      }

      int k, j, i = strlen(s->bits);
      if (i & 1) {
        char *r = (char *)realloc(s->bits, i + 2);
        if (!r)
          return -5;
        s->bits = r;
        s->bits[i + 0] = '0';
        s->bits[i + 1] = '\0';
        q_print("% Bitstring was padded with one extra \"0\"\r\n");
        i++;
      }



      s->seq_len = i / 2;
      s->seq = (rmt_data_t *)malloc(sizeof(rmt_data_t) * s->seq_len);

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
static int seq_send(int pin, int seq) {

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


//"exit"
// exists from a 2nd level subderictory
static int cmd_exit(int argc, char **argv) {

  if (keywords != keywords_main) {
    // restore prompt & keywords list to use
    keywords = keywords_main;
    prompt = PROMPT;
  } else // "exit exit" secret command. kills shell task, does not remove history tho
    if (argc > 1 && !q_strcmp(argv[1],"exit"))
      Exit = true;

  return 0;
}

//TAG:show
//show KEYWORD ARG1 ARG2 .. ARGN
static int cmd_show(int argc, char **argv) {

  if (argc < 2)
    return -1;
  if (!q_strcmp(argv[1],"seq"))
    return cmd_seq_show(argc, argv);
  else
    return 1;

  //NOTREACHED
  return 1;
}

//TAG:seq
//"sequence X"
// save context, switch command list, change the prompt
static int cmd_seq_if(int argc, char **argv) {

  int seq;
  if (argc < 2)
    return -1;

  if (!isnum(argv[1]))
    return 1;

  seq = atoi(argv[1]);
  if (seq < 0 || seq >= SEQUENCES_NUM) {
    q_printf("%% Sequence numbers are 0..%d\r\n", SEQUENCES_NUM - 1);
    return 1;
  }

  Context = seq;
  keywords = keywords_sequence;
  prompt = PROMPT_SEQ;

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
  int freq;

  // at least FREQ must be provided
  if (argc < 2)
    return -1;

  // must be a number
  if (!isnum(argv[1]))
    return 1;

  freq = atoi(argv[1]);

  // More arguments are available
  if (argc > 2) {

    // read DUTY.
    // Duty cycle is a float number on range (0..1]
    if (!isfloat(argv[2]))
      return 2;

    duty = atof(argv[2]);

    if (duty < 0.0f || duty > 1.0f) {
#if WITH_HELP      
      q_print("% Duty cycle is a number in range [0..1] (0.01 means 1% duty)\r\n");
#endif      
      return 2;
    }
  }


  //third argument: "high" or "1" means moduleate when line is HIGH (modulate 1's)
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
    q_print("% Tick must be in range 0.0125..3.2 microseconds\r\n");
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
  s->bits = strdup(argv[1]);

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
    q_print("% Uneven number if levels. Please add 1 more\r\n");
    return 0;
  }


  s->seq_len = i / 2;
  s->seq = (rmt_data_t *)malloc(sizeof(rmt_data_t) * s->seq_len);

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

  int seq;

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

  if (q_strcmp(argv[1], "seq"))
    return 1;

  if (!isnum(argv[2]))
    return 2;

  seq = atoi(argv[2]);
  if (seq < 0 || seq >= SEQUENCES_NUM)
    return 2;

  seq_dump(seq);
  return 0;
}



#include "driver/gpio.h"
#include "driver/pcnt.h"
#include "soc/pcnt_struct.h"
#include "esp_timer.h"

#define PULSE_WAIT    1000
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

  pcnt_config_t cfg;
  unsigned int pin, wait = PULSE_WAIT;
  int16_t count;

  //pin number
  if (!isnum(argv[1]))
    return 1;

  memset(&cfg, 0, sizeof(cfg));

  cfg.pulse_gpio_num = pin = atol(argv[1]);

  if (!pin_exist(pin))
    return 1;

  cfg.ctrl_gpio_num = -1;  // don't use "control pin" feature
  cfg.channel = PCNT_CHANNEL_0;
  cfg.unit = PCNT_UNIT_0;
  cfg.pos_mode = PCNT_COUNT_INC;  
  cfg.neg_mode = PCNT_COUNT_DIS;
  cfg.counter_h_lim = PCNT_OVERFLOW;
 

  // user has provided second argument?
  if (argc > 2) {
      // delay must be a number
      if (!isnum(argv[2]))
        return 2;
      wait = atol(argv[2]);
    
    //user has provided 3rd argument?
    if (argc > 3) {
      if (!q_strcmp(argv[3], "pos"))       { /* default*/ }
      else if (!q_strcmp(argv[3], "neg"))  { cfg.pos_mode = PCNT_COUNT_DIS;  cfg.neg_mode = PCNT_COUNT_INC; }
      else if (!q_strcmp(argv[3], "both")) { cfg.pos_mode = PCNT_COUNT_INC;  cfg.neg_mode = PCNT_COUNT_INC; }
      else return 3;
    }
  } 

  q_printf("%% Counting pulses on GPIO%d (any key to abort)..\r\n", pin);

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

  count_overflow = count_overflow/2 * PCNT_OVERFLOW + count;
  q_printf("%u pulses in %.3f sec\r\n", count_overflow, (float)wait / 1000.0f);
  return 0;
}

#if WITH_VAR
// "var"
// "var X"
// "var X NUMBER"
static int cmd_var(int argc, char **argv) {

  union {
    int ival;
    unsigned int uval;
    float fval;
  } u;

  // "var": display variables list if no arguments were given
  if (argc < 2) {
    struct convar *var = var_head;
    q_print("% Registered variables:\r\n");
    while (var) {
      q_printf("\"%s\", %d bytes long\r\n",var->name,var->size);
      var = var->next;
    }
    return 0;
  }

  //"var X": display variable value
  
  if (argc < 3) {
    if (convar_get(argv[1],&u) < 0)
      return 1;
    q_printf("\"%s\" == Unsigned: %u, Signed: %d, Float: %f (0x%x)\r\n",argv[1],u.uval,u.ival,u.fval,u.uval);
    return 0;
  }

  if (argc < 4) {
    if (isnum(argv[2])) {
        if (argv[2][0] == '-')
          u.ival = atoi(argv[2]);
        else
          u.uval = atol(argv[2]);
      } else if (isfloat(argv[2]))
        u.fval = atof(argv[2]);
      else
        return 2;

    if (convar_set(argv[1],&u) < 0)
      return 1;
    return 0;  
  }

  return -1;  
}
#endif //WITH_VAR


//TAG:pwm
#include "soc/soc_caps.h"
#include "esp32-hal-ledc.h"

// enable or disable (freq==0) tone generation on
// pin. freq is in (0..312kHz), duty is [0..1]
//
static int pwm_enable(unsigned int pin, unsigned int freq, float duty) {

  int resolution = 8;

  if (!pin_exist(pin))
    return -1;

  if (freq > MAGIC_FREQ) freq = MAGIC_FREQ;
  if (duty > 1.0f)       duty = 1.0f;
  if (freq < 78722)
    resolution = 10;

  pinMode(pin,OUTPUT);
  ledcWriteTone(pin, 0); //disable ledc at pin.

  if (freq) {
    if (ledcAttach(pin, freq, resolution) == 0)
      return -1;
    ledcWrite(pin, (unsigned int)(duty * ((1 << resolution) - 1)));
  }

  return 0;
}

//pwm PIN FREQ [DUTY]   - pwm on
//pwm PIN               - pwm off
static int cmd_pwm(int argc, char **argv) {

  unsigned int  freq = 0;
  float         duty = 0.5f;
  unsigned char pin;

  if (argc < 2)        return -1;  // missing arg
  if (!isnum(argv[1])) return  1;  // arg 1 is bad

  // first parameter is pin number
  pin  = (unsigned char)(atol(argv[1]));

  //frequency is the second one (optional)
  if (argc > 2) {
    if (!isnum(argv[2])) return 2;
    freq = atol(argv[2]);
#if WITH_HELP
    if (freq > MAGIC_FREQ)
      q_print("% Frequency will be adjusted to maximum which is " xstr(MAGIC_FREQ) "] Hz\r\n");
#endif      
  }

  // duty is the third argument (optional)
  if (argc > 3) {
    if (!isfloat(argv[3])) 
      return 3;
    duty = atof(argv[3]);
    if (duty < 0 || duty > 1) {
#if WITH_HELP      
      q_print("% Duty cycle is a number in range [0..1] (0.01 means 1% duty)\r\n");
#endif
      return 3;
    }
  }

  if (pwm_enable(pin,freq,duty) < 0) {
#if WITH_HELP
    q_print(Failed);
#endif    
  }
  return 0;
}

#include <soc/gpio_struct.h>
#include <hal/gpio_ll.h>
#include <driver/gpio.h>
#include <rom/gpio.h>
#include <esp32-hal-periman.h>

extern gpio_dev_t GPIO;

static struct {
  uint8_t  flags;
  bool     value;
  uint16_t sig_out;
  uint16_t fun_sel;
  int      bus_type;  //periman bus type. 
} Pins[SOC_GPIO_PIN_COUNT];

static void pin_save(int pin) {

	bool pd,pu,ie,oe,od,slp_sel;
	uint32_t drv,fun_sel,sig_out;

	gpio_ll_get_io_config(&GPIO,pin,&pu, &pd, &ie, &oe, &od,&drv, &fun_sel, &sig_out, &slp_sel);

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


static void pin_load(int pin) {

  // 1. restore pin mode 
  pinMode(pin,Pins[pin].flags);

  //2. attempt to restore peripherial connections:
  //   If pin was not configured or was simple GPIO function then restore it to simple GPIO
  //   If pin had a connection through GPIO Matrix, restore IN & OUT signals connection (use same
  //   signal number for both IN and OUT. Probably it should be fixed)
  //
  if (Pins[pin].fun_sel != PIN_FUNC_GPIO)
    q_printf("%% Pin %d IO MUX connection can not be restored\r\n",pin);
  else {
    if (Pins[pin].bus_type == ESP32_BUS_TYPE_INIT || Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO) {
      gpio_pad_select_gpio(pin);
      
      // restore digital value
      if ((Pins[pin].flags & OUTPUT) && (Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO))
        digitalWrite(pin, Pins[pin].value ? HIGH : LOW);
    }
    else {
      // unfortunately this will not work with Arduino :(
      if (Pins[pin].flags & OUTPUT) 
        gpio_matrix_out(pin,Pins[pin].sig_out,false,false);
      if (Pins[pin].flags & INPUT) 
        gpio_matrix_in(pin,Pins[pin].sig_out,false);
    }
  }
}

// strapping pins as per Technical Reference
//
static bool pin_is_strapping_pin(int pin) {
  switch (pin) {
#ifdef CONFIG_IDF_TARGET_ESP32
    case 0: case 2: case 5: case 12: case 15: 
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S2
    case 0: case 45: case 46:
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S3
    case 0: case 3: case 45: case 46:
#endif
#ifdef CONFIG_IDF_TARGET_ESP32C3
    case 2: case 8: case 9:
#endif
#ifdef CONFIG_IDF_TARGET_ESP32C6
    case 8: case 9: case 12: case 14: case 15:
#endif
#ifdef CONFIG_IDF_TARGET_ESP32H2
    case 8: case 9: case 25:
#endif
      return true;
default:
      return false;
  }
}

static int digitalForceRead(int pin) {
  gpio_ll_input_enable(&GPIO,pin);
  return gpio_ll_get_level(&GPIO,pin);

}

// called by "pin X"
// moved to a separate function to offload giant cmd_pin() a bit
// 
// Display pin information: function, direction, mode, pullup/pulldown etc
static int pin_show(int argc, char **argv) {

  unsigned int pin, informed = 0;

  if (argc < 2) return -1;
  if (!isnum(argv[1])) return 1;
  if (!pin_exist((pin = atol(argv[1])))) return 1;

  bool pu, pd, ie, oe, od, sleep_sel, res;
  uint32_t drv, fun_sel, sig_out;
  int type;

  res = esp_gpio_is_pin_reserved(pin);
  q_printf("%% Pin %d is %s",pin, res ? "**RESERVED**, " : "");
  if (pin_is_strapping_pin(pin))
    q_print("strapping pin, ");

  if (!res)
    q_print("available, ");
  
  q_print("and is ");
  if ((type = perimanGetPinBusType(pin)) == ESP32_BUS_TYPE_INIT)
    q_print("not used by Arduino Core\r\n");
  else {
    if (type == ESP32_BUS_TYPE_GPIO)
      q_print("configured as GPIO\r\n");
    else
      q_printf("used as \"%s\"\r\n",perimanGetTypeName(type));
  }


  gpio_ll_get_io_config(&GPIO, pin, &pu, &pd, &ie, &oe, &od, &drv, &fun_sel, &sig_out, &sleep_sel);

  if (ie || oe || od || pu || pd || sleep_sel) {
    q_print("% Mode: ");
    if (ie) q_print("INPUT, ");
    if (oe) q_print("OUTPUT, ");
    if (pu) q_print("PULL_UP, ");
    if (pd) q_print("PULL_DOWN, ");
    if (od) q_print("OPEN_DRAIN, ");
    if (sleep_sel) q_print("sleep mode selected,");
    if (!pu && !pd && ie) q_print(" input is floating!");

    q_print(CRLF);

    if (oe && fun_sel == PIN_FUNC_GPIO) {
      q_print("% Output via GPIO matrix, ");
      if (sig_out == SIG_GPIO_OUT_IDX)
        q_print("simple GPIO output\r\n");
      else
        q_printf("provides path for signal ID: %lu\r\n",sig_out);
    } else if (oe && fun_sel != PIN_FUNC_GPIO) {
      q_print("% Output is done via IO MUX, ");
      //TODO: get IOMUX function
      q_print(CRLF);
    }

    if (ie && fun_sel == PIN_FUNC_GPIO) {
      q_print("% Input via GPIO matrix, ");
      for (int i = 0; i < SIG_GPIO_OUT_IDX; i++) {  // FIXME: SIG_GPIO_IN_IDX ?
        if (gpio_ll_get_in_signal_connected_io(&GPIO, i) == pin) {
          if (!informed)
            q_print("provides path for signal IDs: ");
          informed++;
          q_printf("%d, ",i);
        }
      }

      if (!informed)
        q_print("simple GPIO input");
      q_print(CRLF);

    } else if (ie) {
      q_print("% Input is routed through IO MUX\r\n");
      //TODO: get IOMUX function
    }
  } else
    q_print(CRLF);

  
  //TODO: ESP32S3 has its pin 18 and 19 drive capability of 3 but the meaning is 2 and vice-versa
  //      Other versions probably have the same behaviour on some other pins
  q_printf("%% Maximum current is %u milliamps\r\n", !drv ? 5 : (drv == 1 ? 10 : (drv == 2 ? 20 : 40)));
  
  if (sleep_sel)
    q_print("% Sleep select: YES\r\n");
#if 0
  if (type == ESP32_BUS_TYPE_GPIO)
    q_printf("%% Digital pin value is %s\r\n",digitalRead(pin) == HIGH ? "HIGH (1)" : "LOW (0)");
#else
  gpio_ll_input_enable(&GPIO,pin);
  q_printf("%% Digital pin value is %s\r\n",gpio_ll_get_level(&GPIO,pin) ? "HIGH (1)" : "LOW (0)");
  if (!ie)
    gpio_ll_input_disable(&GPIO,pin);
#endif    

  return 0;
}

// "pin NUM arg1 arg2 .. argn"
// "pin NUM"
// Big fat "pin" command. Processes multiple arguments
//
static int cmd_pin(int argc, char **argv) {

  unsigned int flags = 0;
  int i = 2, pin;

  // repeat whole "pin ..." command "count" times.
  // this number can be changed by "loop" keyword
  unsigned int count = 1; 

#if WITH_HELP  
  bool informed = false;
#endif
  if (argc < 2) return -1;  //missing argument

  //first argument must be a decimal number: a GPIO number
  if (!isnum(argv[1]) || !pin_exist((pin = atoi(argv[1])))) return 1;

  //"pin X" command is executed here
  if (argc == 2) return pin_show(argc, argv);

  //"pin arg1 arg2 .. argN"
  do {

    //Run through "pin NUM arg1, arg2 ... argN" arguments, looking for keywords
    // to execute.
    while (i < argc) {

      //1. "seq NUM" keyword found:
      if (!q_strcmp(argv[i],"seq")) {
        if ((i + 1) >= argc) {
#if WITH_HELP
          q_print("% Sequence number expected after \"seq\"\r\n");
#endif          
          return i;
        }
        i++;

        //sequence number
        if (!isnum(argv[i])) 
          return i;

        int seq,j;

        // enable RMT sequence 'seq' on pin 'pin'
        if (seq_isready((seq = atol(argv[i])))) {
#if WITH_HELP          
          q_printf("%% Sending sequence %d over GPIO %d\r\n", seq, pin);
#endif
          if ((j = seq_send(pin, seq)) < 0)
            q_printf("%% Failed. Error code is: %d\r\n",j);

        } else
          q_printf("%% Sequence %d is not configured\r\n", seq);
      } 
      //2. "pwm FREQ DUTY" keyword. 
      // unlike global "pwm" command the duty and frequency are not an optional 
      // parameter anymore. Both can be 0 which used to disable previous "pwm"
      else if (!q_strcmp(argv[i],"pwm")) {

        unsigned int freq; float duty;
        // make sure that there are 2 extra arguments after "pwm" keyword
        if ((i+2) >= argc) {
#if WITH_HELP          
          q_print("% Frequency and duty cycle are both expected\r\n");
#endif          
          return i;
        }

        i++;

        // frequency must be an integer number and duty must be a float point number
        if (!isnum(argv[i]))
          return i; 
        
        
        if ((freq = atol(argv[i++])) > MAGIC_FREQ) {
#if WITH_HELP
          q_print("% Frequency must be in range [1.." xstr(MAGIC_FREQ) "] Hz\r\n");
#endif
          return i - 1;
        }

        if (!isfloat(argv[i]))
          return i; 
        
        duty = atof(argv[i]);
        if (duty < 0 || duty > 1) {
#if WITH_HELP      
          q_print("% Duty cycle is a number in range [0..1] (0.01 means 1% duty)\r\n");
#endif
          return i;
        }

        // enable/disable tone on given pin. if freq is 0 then tone is
        // disabled
        if (pwm_enable(pin,freq,duty) < 0) {
#if WITH_HELP
          q_print(Failed);
#endif          
          return 0;
        }
      }
      //3. "delay X" keyword
      //creates delay for X milliseconds. 
      else if (!q_strcmp(argv[i],"delay")) {
        unsigned int duration;
        if ((i + 1) >= argc) {
#if WITH_HELP          
          q_print("% Delay value expected after keyword \"delay\"\r\n");
#endif          
          return i;
        }
        i++;
        if (!isnum(argv[i]))
          return i;
        // delays of more than 5 seconds are sensetive to
        //user input: anykey will interrupt the delay
        duration = atol(argv[i]);
#if WITH_HELP        
        if (!informed && (duration > 4999)) {
            informed = true;
            q_print("% Hint: Press/Enter any key to interrupt the command\r\n");
          }
#endif          
          if (delay_interruptible(duration) != duration) // was interrupted by keypress? abort whole command
            return 0;
      } 
      //4. "loop" keyword
      else if (!q_strcmp(argv[i],"loop")) {
        //must have an extra argument (loop count)
        if ((i + 1) >= argc) {
#if WITH_HELP
          q_print("% Loop count expected after keyword \"loop\"\r\n");
#endif          
          return i;
        }
        i++;
        // loop count must be a number
        if (!isnum(argv[i]))
          return i;

        // loop must be the last keyword, so we can strip it later
        if ((i + 1) < argc) {
#if WITH_HELP          
          q_print("% \"loop\" must be the last keyword\r\n");
#endif          
          return i + 1;
        }
        count = atol(argv[i]);
        argc -= 2;//strip "loop NUMBER" keyword
#if WITH_HELP
        if (!informed) {
          informed = true;
          q_printf("%% Repeating the command %u times. Press/Enter any key to abort\r\n",count);
        }
#endif
      }
      //Now all the single-line keywords:
      else if (!q_strcmp(argv[i], "save"))    pin_save(pin);
      else if (!q_strcmp(argv[i], "load"))    pin_load(pin);
      else if (!q_strcmp(argv[i], "hold"))    gpio_hold_en((gpio_num_t)pin);
      else if (!q_strcmp(argv[i], "release")) gpio_hold_dis((gpio_num_t)pin);
      else if (!q_strcmp(argv[i], "up"))      { flags |= PULLUP;     pinMode(pin, flags); } // set flags immediately as we read them
      else if (!q_strcmp(argv[i], "down"))    { flags |= PULLDOWN;   pinMode(pin, flags); }
      else if (!q_strcmp(argv[i], "open"))    { flags |= OPEN_DRAIN; pinMode(pin, flags); }
      else if (!q_strcmp(argv[i], "in"))      { flags |= INPUT;      pinMode(pin, flags); }
      else if (!q_strcmp(argv[i], "out"))     { flags |= OUTPUT;     pinMode(pin, flags); }
      else if (!q_strcmp(argv[i], "low"))     { flags |= OUTPUT;     pinMode(pin,flags); digitalWrite(pin, LOW); }
      else if (!q_strcmp(argv[i], "high"))    { flags |= OUTPUT;     pinMode(pin,flags); digitalWrite(pin, HIGH); }
      else if (!q_strcmp(argv[i], "read"))    q_printf("%% GPIO%d : logic %d\r\n", pin, digitalForceRead(pin));
      else if (!q_strcmp(argv[i], "aread"))   q_printf("%% GPIO%d : analog %d\r\n", pin, analogRead(pin));
      //"new pin number" keyword. when we see a number we use it as a pin number
      //for subsequent keywords. must be valid GPIO number.
      else if (isnum(argv[i])) { 
        pin = atoi(argv[i]);
        if (!pin_exist(pin))
          return i;
      }
      else
        return i;  // argument i was not recognized
      i++;
    }
    i = 1; // start over again

    //give a chance to cancel whole command
    // by anykey press
    if (anykey_pressed()) {
#if WITH_HELP
      q_print("% Key pressed, aborting..\r\n");
#endif
      break;
    }
  } while (--count > 0); // repeat if "loop X" command was found

  return 0;
}

//TAG:mem
//"mem"
static int cmd_mem(int argc, char **argv) {

  unsigned int total;

  q_print("% -- Memory information --\r\n%\r\n");

  q_printf("%% For \"malloc()\" and \"heap_caps_malloc(MALLOC_CAP_DEFAULT)\":\r\n%% %u bytes total, %u available, %u max per allocation\r\n%%\r\n", heap_caps_get_total_size(MALLOC_CAP_DEFAULT), heap_caps_get_free_size(MALLOC_CAP_DEFAULT),heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
  q_printf("%% For \"heap_caps_malloc(MALLOC_CAP_INTERNAL)\", internal SRAM:\r\n%% %u bytes total,  %u available, %u max per allocation\r\n%%\r\n", heap_caps_get_total_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  if ((total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM)/1024) > 0)
    q_printf("%% External SPIRAM total: %uMbytes, free: %u bytes\r\n",total / 1024,heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

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

      if (!isnum(argv[1]))  //arg1 must be a number
        return 1;
      if (isen) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_UART);  //disable wakeup by uart
        isen = false;
      }
      esp_sleep_enable_timer_wakeup(1000000UL * (unsigned long)atol(argv[1]));
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


//TAG:iic
//check if I2C has its driver installed
static inline bool i2c_isup(int iic) {

  extern bool i2cIsInit(uint8_t i2c_num);  //not a public API, no .h file
  return (iic < 0 || iic >= SOC_I2C_NUM) ? false : i2cIsInit(iic);
}



//"iic X"
// save context, switch command list, change the prompt
static int cmd_i2c_if(int argc, char **argv) {

  unsigned int iic;
  if (argc < 2)
    return -1;

  if (!isnum(argv[1]))
    return 1;

  iic = atol(argv[1]);
  if (iic >= SOC_I2C_NUM) {
#if WITH_HELP    
    q_printf("%% Valid I2C interface numbers are 0..%d\r\n", SOC_I2C_NUM - 1);
#endif    
    return 1;
  }

  Context = iic;
  keywords = keywords_i2c;
  prompt = PROMPT_I2C;

  return 0;
}

// same as the above but for uart commands
// "uart X"
static int cmd_uart_if(int argc, char **argv) {

  unsigned int u;
  if (argc < 2)
    return -1;

  if (!isnum(argv[1]))
    return 1;

  u = atol(argv[1]);
  if (u >= SOC_UART_NUM) {
#if WITH_HELP    
    q_printf("%% Valid UART interface numbers are 0..%d\r\n", SOC_UART_NUM - 1);
#endif    
    return 1;
  }
#if WITH_HELP
  if (uart == u)
    q_print("% You are configuring Serial interface shell is running on! BE CAREFUL :)\r\n");
#endif

  Context = u;
  keywords = keywords_uart;
  prompt = PROMPT_UART;
  return 0;
}


//TAG:clock
//esp32-i2c#> clock FREQ
//
static int cmd_i2c_clock(int argc, char **argv) {

  int iic = Context;

  if (argc < 2)
    return -1;

  if (!isnum(argv[1]))
    return 1;

  if (!i2c_isup(iic)) {
#if WITH_HELP    
    q_printf("%% I2C %d is not initialized. use command \"up\" to initialize\r\n", iic);
#endif    
    return 0;
  }

  if (ESP_OK != i2cSetClock(iic, atol(argv[1])))
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

  unsigned char iic, sda, scl;
  unsigned int clock = 0;
  int i;
  unsigned char addr;
  
  int size;

  iic = Context;

  //"up" kaeyword: initialize i2c driver
  // on given pins with givent clockrate
  if (!q_strcmp(argv[0], "up")) {

    if (argc < 4)
      return -1;

    if (i2c_isup(iic)) {
#if WITH_HELP      
      q_printf("%% I2C%d is already initialized\r\n", iic);
#endif      
      return 0;
    }

    if (!isnum(argv[1]))                   return 1; // sda must be a number
    if (!pin_exist((sda = atoi(argv[1])))) return 1; // and be a valid pin
    if (!isnum(argv[2]))                   return 2; // same for scl
    if (!pin_exist((scl = atoi(argv[2])))) return 2;
    if (!isnum(argv[3]))                   return 3; // clock must be a number
    clock = atol(argv[3]);

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
    if (!ishex(argv[1]))
      return 1;

    addr = ascii2hex(argv[1]);
    if (addr < 1 || addr > 127)
      return 1;

    // read all bytes to the data buffer
    for (i = 2, size = 0; i < argc; i++) {
      if (!ishex(argv[i]))
        return i;
      data[size++] = ascii2hex(argv[i]);
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

    if (argc < 3)
      return -1;

    if (!ishex(argv[1]))
      return 1;

    addr = ascii2hex(argv[1]);

    if (addr < 1 || addr > 127)
      return 1;

    // second parameter: requested size
    if (!isnum(argv[2]))
      return 2;

    size = atol(argv[2]);

    if (size > I2C_RXTX_BUF) {
      size = I2C_RXTX_BUF;
#if WITH_HELP      
      q_printf("%% Max read size buffer is %d bytes\r\n", size);
#endif      
    }

    got = 0;
    unsigned char data[size];

    if (i2cRead(iic, addr, data, size, 2000, &got) != ESP_OK)
      q_print(Failed);
    else {
      if (got != size) {
        q_printf("%% Requested %d bytes but read %d\r\n",size,got);
        got = size;
      }
      q_printf("%% I2C%d received %d bytes:\r\n", iic, got);
      q_printhex(data,got);

    }
  } 
  // "scan" keyword
  //
  else if (!q_strcmp(argv[0], "scan")) {
    if (!i2c_isup(iic)) {
#if WITH_HELP      
      q_printf("%% I2C %d is not initialized\r\n", iic);
#endif      
      return 0;
    }

    q_printf("%% Scanning I2C bus %d...\r\n", iic);

    for (addr = 1, i = 0; addr < 128; addr++) {
      unsigned char b;
      if (ESP_OK == i2cWrite(iic, addr, &b, 0, 500)) {
        i++;
        q_printf("%% Device found at address %02X\r\n", addr);
      }
    }

    if (!i)
      q_print("% Nothing found\r\n");
    else
      q_printf("%% %d devices found\r\n", i);
  } else return 0;

  return 0;
noinit:
  // love gotos
#if WITH_HELP  
  q_printf("%% I2C %d is not initialized\r\n", iic);
#endif  
  return 0;
}



#include <esp32-hal-uart.h>

//defined in HardwareSerial and must be kept in sync
//unfortunately HardwareSerial.h can not be included directly in a .c code
#define SERIAL_8N1 0x800001c

//check if UART has its driver installed
static inline bool uart_isup(int u) {

  return (u < 0 || u >= SOC_UART_NUM) ? 0 : uart_is_driver_installed(u);
}


//TAG:baud
static int cmd_uart_baud(int argc, char **argv) {

  int u = Context;

  if (argc < 2)
    return -1;

  if (!isnum(argv[1]))
    return 1;

  if (!uart_isup(u)) {
#if WITH_HELP    
    q_printf("%% uart %d is not initialized. use command \"up\" to initialize\r\n", u);
#endif    
    return 0;
  }

  if (ESP_OK != uart_set_baudrate(u, atol(argv[1])))
    q_print(Failed);

  return 0;
}


//TAG:tap
// create uart-to-uart bridge between user's serial and "remote"
// everything that comes from the user goes to "remote" and
// vice versa
//
// returns  when BREAK_KEY is pressed
static void
uart_tap(int remote) {

#define UART_RXTX_BUF 512
  size_t av;

  while (1) {

    // 1. read all user input and send it to remote
    while (1) {
      av = 0;
      // fails when interface is down. must not happen.
      if (ESP_OK != uart_get_buffered_data_len(uart, &av))
        break;

      // must not happen unless UART FIFO sizes were changed in ESP-IDF
      if (av > UART_RXTX_BUF)
        av = UART_RXTX_BUF;

      else if (!av)  //nothing to read?
        break;

      char buf[av];

      console_read_bytes(buf, av, portMAX_DELAY);
      // CTRL+C. most likely sent as a single byte (av == 1), so get away with
      // just checking if buf[0] == CTRL+C
      if (buf[0] == BREAK_KEY)
        return;
      uart_write_bytes(remote, buf, av);
      // make WDT happy if we get flooded.
      delay(1);
    }

    // 2. read all the data from remote uart and echo it to the user
    while (1) {

      // return here or we get flooded by driver messages
      if (ESP_OK != uart_get_buffered_data_len(remote, &av)) {
#if WITH_HELP        
        q_printf("%% UART%d is not initialized\r\n", remote);
#endif        
        return;
      }

      if (av > UART_RXTX_BUF)
        av = UART_RXTX_BUF;
      else if (!av)
        break;
      
      char *buf[av];

      uart_read_bytes(remote, buf, av, portMAX_DELAY);
      console_write_bytes(buf, av); //TODO: should it be affected by "echo silent"?
      delay(1);
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

  int u, sent = 0;


  u = Context;

  // UART TAP
  if (!q_strcmp(argv[0], "tap")) {
    //do not tap to the same uart
    if (uart == u) {
      q_print("% Can not tap on itself\r\n");
      return 0;
    }

    if (!uart_isup(u))
      goto noinit;

    q_printf("%% Tapping to UART%d, CTRL+C to exit\r\n", u);
    uart_tap(u);
    q_print("\r\n% Ctrl+C, exiting\r\n");
    return 0;
  } else if (!q_strcmp(argv[0], "up")) {  //up RX TX SPEED
    if (argc < 4)
      return -1;
    // uart number, rx/tx pins and speed must be numbers

    unsigned int rx, tx, speed;
    if (!isnum(argv[1])) return 1; else rx = atol(argv[1]);
    if (!pin_exist(rx))  return 1;
    if (!isnum(argv[2])) return 2; else tx = atol(argv[2]);
    if (!pin_exist(tx))  return 2;
    if (!isnum(argv[3])) return 3;
    else speed = atol(argv[3]);  //TODO: check speed

    if (NULL == uartBegin(u, speed, SERIAL_8N1, rx, tx, 256, 0, false, 112))
      q_print(Failed);
  } else if (!q_strcmp(argv[0], "down")) {  // down
    if (!uart_isup(u))
      goto noinit;
    else
      uartEnd(u);
  } else if (!q_strcmp(argv[0], "write")) {  //write TEXT
    if (argc < 2)
      return -1;

    int i = 1;

    if (!uart_isup(u))
      goto noinit;

    // go thru all the arguments and send them. the space is inserted between arguments
    do {
      char *p = argv[i];

      // char by char. parse c-style escape sequences if any
      // XY are hexadecimal characters
      // hexadecimals at the end of every token allowed to be in a short form as well: \X
      // valid: "write Hello\20World!\9"
      // valid: "write Hello\9 World"
      // invalid: "write Hello\9World" (Must be \09)
      while (*p) {
        char c = *p;
        p++;
        if (c == '\\') {

          switch (*p) {
            case '\\': p++; c = '\\'; break;
            case 'n' : p++; c = '\n'; break;
            case 'r' : p++; c = '\r'; break;
            case 't' : p++; c = '\t'; break;
            default:
              if (ishex(p))
                c = ascii2hex(p);
              else {
                q_printf("%% Unknown escape sequence: \"\\%s\"\r\n", p);
                return i;
              }
              p++;
              if (*p) p++;
          }
        }
        if (uart_write_bytes(u, &c, 1) == 1)
          sent++;
      }

      i++;
      //if there are more arguments - insert a space
      if (i < argc) {
        char space = ' ';
        
        if (uart_write_bytes(u, &space, 1) == 1)
          sent++;
      }
    } while (i < argc);

    q_printf("%% %u bytes sent\r\n", sent);
  } else 
  if (!q_strcmp(argv[0], "read")) {  // read
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

  // command executed or was not understood
  return 0;
noinit:
  q_printf("%% UART%d is not initialized\r\n", u);
  return 0;
}


//TAG:tty
//"tty NUM"
//
// Set UART to use by this shell. By default it
// is set to UART_0 which corresponds to Serial in Arduino (inless it
// is an USB-enabled board )
static int cmd_tty(int argc, char **argv) {


  int u;
  if (!isnum(argv[1]))
    return 1;
  u = atoi(argv[1]);
  if (!uart_isup(u))
    q_printf("%% UART%d is not initialized\r\n", u);
  else {
#if WITH_HELP
    q_printf("%% See you on UART%d. Bye!\r\n", u);
#endif
    uart = u;
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
    q_printf("%% Echo %s\r\n",Echo ? "on" : "off"); //if echo is silent we can't see it anyway so no bother printing
  else {
    if (!q_strcmp(argv[1],"on")) Echo = 1; else
    if (!q_strcmp(argv[1],"off")) Echo = 0; else
    if (!q_strcmp(argv[1],"silent")) Echo = -1; else return 1;
  }
  return 0;
}


//TAG:reload
//"reload"
static int cmd_reload(int argc, char **argv) {
  esp_restart();
  /* NOT REACHED */
  return 0;
}


#include <soc/efuse_reg.h>
#include <esp_chip_info.h>

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

    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6: if (chip_info.revision / 100 == 3) chipid = "ESP32-D0WDQ6-V3"; else chipid = "ESP32-D0WDQ6"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5: if (chip_info.revision / 100 == 3) chipid = "ESP32-D0WD-V3";   else chipid = "ESP32-D0WD";   break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5:   chipid = "ESP32-D2WD-Q5"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD2:   chipid = "ESP32-PICO-D2"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4:   chipid = "ESP32-PICO-D4"; break;
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
#endif //CONFIG_IDF_TARGET_XXX

  q_printf("\r\n%% CPU ID: %s, Rev.: %d.%d\r\n%% CPU frequency is %luMhz, Xtal %luMhz, APB bus %luMhz\r\n%% Chip temperature: %.1f\xe8 C\r\n",
             chipid,
             (chip_info.revision >> 8) & 0xf,
             chip_info.revision & 0xff,
             getCpuFrequencyMhz(),
             getXtalFrequencyMhz(),
             getApbFrequency() / 1000000,
             temperatureRead());

  q_printf("%%\r\n%% Sketch is running on " ARDUINO_BOARD "/(" ARDUINO_VARIANT "), uses Arduino Core v%s, based on\r\n%% Espressif ESP-IDF version \"%s\"\r\n",ESP_ARDUINO_VERSION_STR,esp_get_idf_version());
  cmd_uptime(argc,argv);
  return 0;
}

//"cpu CLOCK"
//
// Set cpu frequency. 
//
static int cmd_cpu_freq(int argc, char **argv) {

  if (argc < 2)
    return -1; // not enough arguments

  if (!isnum(argv[1]))
    return 1;

  unsigned int freq = atol(argv[1]);

  while (freq != 240 && freq != 160 && freq != 120 && freq != 80) {

    unsigned int xtal = getXtalFrequencyMhz();

    if ((freq == xtal) || (freq == xtal / 2)) break;
    if ((xtal >= 40) && (freq == xtal / 4))   break;

    q_print("% Supported frequencies are: 240, 160, 120, 80, ");

    if (xtal >= 40)
      q_printf("%u, %u and %u\r\n", xtal, xtal / 2, xtal / 4);
    else
      q_printf("%u and %u\r\n", xtal, xtal / 2);
      return 1;
  }

  if (!setCpuFrequencyMhz(freq))
    q_print(Failed);

  return 0;
}

// external user-defined command handler functions here
#ifdef EXTERNAL_HANDLERS
#include EXTERNAL_HANDLERS
#endif

//Time counter value (in seconds) right before entering
//main()/app_main()
static unsigned int uptime = 0;


// pre-main() hook
//
// the function gets called right before entering main()
// FreeRTOS is initialized but task scheduler is not started yet.
static void espshell_task(const void *arg);
static void __attribute__((constructor)) espshell_start() {

  // save the counter value (seconds) on program start
  // must be 0 but just in case
  uptime = (uint32_t)(esp_timer_get_time() / 1000000);

  //initialize sequences
  seq_init();

  //start shell task.
  //task have to wait until Serial.begin() in order to start
  //reading and process user input
  espshell_task((const void *)1);
}


// "uptime"
//
// Displays system uptime as returned by esp_timer_get_time() counter.
//
static int
cmd_uptime(int argc, char **argv) {

  unsigned int sec, min = 0, hr = 0, day = 0;
  sec = (unsigned int)(esp_timer_get_time() / 1000000) - uptime;

  const char *rr;

  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:    rr = "power-on event"; break;
    case ESP_RST_SW:         rr = "reload command"; break;
    case ESP_RST_PANIC:      rr = "panic()!"; break;
    case ESP_RST_INT_WDT:    rr = "an interrupt watchdog"; break;
    case ESP_RST_TASK_WDT:   rr = "a task watchdog"; break;
    case ESP_RST_WDT:        rr = "an unspecified watchdog"; break;
    case ESP_RST_DEEPSLEEP:  rr = "coming up from deep sleep"; break;
    case ESP_RST_BROWNOUT:   rr = "brownout"; break;
    case ESP_RST_SDIO:       rr = "SDIO"; break;
    case ESP_RST_USB:        rr = "USB event"; break;
    case ESP_RST_JTAG:       rr = "JTAG"; break;
    case ESP_RST_EFUSE:      rr = "eFuse errors"; break;
    case ESP_RST_PWR_GLITCH: rr = "power glitch"; break;
    case ESP_RST_CPU_LOCKUP: rr = "lockup (double exception)"; break;
    default:                 rr = "no idea";
  };

  q_print("% Last boot was ");
  if (sec > 60 * 60 * 24) {
    day = sec / (60 * 60 * 24);
    sec = sec % (60 * 60 * 24);
    q_printf("%u days ", day);
  }
  if (sec > 60 * 60) {
    hr = sec / (60 * 60);
    sec = sec % (60 * 60);
    q_printf("%u hours ", hr);
  }
  if (sec > 60) {
    min = sec / 60;
    sec = sec % 60;
    q_printf("%u minutes ", min);
  }

  q_printf("%u seconds ago\r\n%% Restart reason was \"%s\"\r\n", sec,rr);
  

  return 0;
}


//TAG:suspend
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

#if WITH_HELP
// "?"
//
// question mark command: display all commands available
// along with their brief description.
//
static int cmd_question(int argc, char **argv) {

#define INDENT 10  //TODO: use a variable calculated at shell task startup
  int i = 0;
  const char *prev = "";
  char indent[INDENT + 1];

  //"? arg"
  if (argc > 1) {
    if (!q_strcmp(argv[1],"keys")) {
      // 25 lines maximum to fit in default terminal window without scrolling
      q_print("%             -- ESPShell Keys -- \r\n\r\n" \
              "% <ENTER>       : Execute command.\r\n" \
              "% <- -> /\\ \\/   : Arrows: move cursor left or right. Up and down to scroll\r\n" \
              "%                 through command history\r\n" \
              "% <DEL>         : As in Notepad\r\n" \
              "% <BACKSPACE>   : As in Notepad\r\n" \
              "% <HOME>, <END> : Use Ctrl+A instead of <HOME> and Ctrl+E as <END>\r\n" \
              "% Ctrl+R        : Command History search. Enter text to search and press <ENTER>\r\n" \
              "%                 to search for a command executed before\r\n" \
              "% Ctrl+W        : Wipe : clear input line\r\n" \
              "% Ctrl+O        : Move cursor to previous argument\r\n" \
              "% Ctrl+P        : Move cursor to the next argument\r\n" \
              "% Ctrl+L        : Clear screen\r\n" \
              "% Ctrl+Z        : Same as entering \"exit\" command\r\n%\r\n" \
              "% -- Terminal compatibility workarounds (alternative key sequences) --\r\n%\r\n" \
              "% Ctrl+J and Ctrl+M work as <ENTER>\r\n" \
              "% Ctrl+B and Ctrl+F work as <- and -> (left & right arrows)>\r\n" \
              "% Ctrl+D works as <Delete> key\r\n" \
              "% Ctrl+H works as <BACKSPACE> key\r\n"
              "% Ctrl+A is <HOME> and Ctrl+E is <END>\r\n");
      return 0;
    } else { // "? command"
      i = 0;
      bool found = false;
      // go through all matched commands (only name is matched) and print their
      // help lines. hidden commands are ignored
      while (keywords[i].cmd) {
        if (keywords[i].help || keywords[i].brief) {  //skip hidden commands
          if (!q_strcmp(argv[1], keywords[i].cmd)) {
            if (keywords[i].help)
              q_printf("\r\n%s\r\n", keywords[i].help);  //display long help
            else if (keywords[i].brief)
              q_printf("\r\n%s\r\n", keywords[i].brief);  //display brief (must not happen)
            found = true;
          }
        }
        i++;
      }
      // return 1 if arguemnt #1 was not understood
      return found ? 0 : 1; 
    }
    //NOTREACHED
    abort();
  } // if (argc > 1)

  // process "?" command (no arguments given)
  // commands which are shorter than INDENT will be padded with extra spaces to be INDENT bytes long
  memset(indent, ' ', INDENT);
  indent[INDENT] = 0;
  char *spaces;

  q_print("% Enter \"? command\" to get details about specific command.\r\n" \
          "% Enter \"? keys\" to learn keys used in ESPShell.\r\n" \
          "%\r\n");

  //run through the keywords[] and print brief info for every entry
  // 1. for repeating entries (same command name) only the first entry's description
  //    used. Such entries in keywords[] must be grouped to avoid undefined bahaviour
  // 2. entries with both help lines (help and brief) set to NULL are hidden commands
  //    and are not displayed. 
  while (keywords[i].cmd) {

    if (keywords[i].help || keywords[i].brief) { // process only non-hidden entries
      if (strcmp(prev, keywords[i].cmd)) {       // skip entry if its command name is the same as previous
        const char *brief;
        if (!(brief = keywords[i].brief))        //use "brief" or fallback to "help"
          if (!(brief = keywords[i].help))
            brief = "";

        // indent list: short commands are padded with spaces so
        // total length is always INDENT bytes. Commands which size
        // is bigger than INDENT will not be padded
        int clen;
        spaces = &indent[INDENT];  //points to \0
        if ((clen = strlen(keywords[i].cmd)) < INDENT)
          spaces = &indent[clen];
        q_printf("%% \"%s\"%s : %s\r\n", keywords[i].cmd, spaces, brief);
      }
    }

    prev = keywords[i].cmd;
    i++;
  }

  return 0;
}
#endif // WITH_HELP

// Parse & execute: split user input "p" to tokens, find an appropriate 
// entry in keywords[] array and execute coresponding callback.
// String p - is the user input as returned by readline()
//
// returns 0 on success
//
static int
espshell_command(char *p) {

  char **argv = NULL;
  int argc;
  int i = 0, bad = -1;

  bool found = false; //have found something. maybe not exact match but name is matched

  if (!p)
    return bad;

  //make a history entry
  rl_add_history(p);

  // tokenize a string. 
  // note that resulting argv array consists of pointers within "p"
  // while "p" itself is modified by inserting '\0' between tokens.
  argc = argify((unsigned char *)p, (unsigned char ***)&argv);

  //argc must be at least 1 if tokenization was successful. the very first token is a command while
  //others are arguments. argc of zero means there was a memory allocation error, or input string was
  //empty/consisting completely of whitespace
  if (argc < 1)
    return bad;

  {
    //process command
    //find a corresponding entry in a keywords[] : match the name and minimum number of arguments
    while (keywords[i].cmd) {
    // command name matches user input?
      if (!q_strcmp(argv[0],keywords[i].cmd)) {
        found = true;
        // command & user input match when both name & number of arguments match.
        // one special case is command with argc < 0 : these are matched always
        // (-1 == "take any number of arguments")
        if (((argc - 1) == keywords[i].argc) || (keywords[i].argc < 0)) {

          // execute the command. 
          // if nonzero value is returned then it is an index of the "failed" argument (value of 3 means argv[3] was bad (for values > 0))
          // value of zero means successful execution, return code <0 means argument number was wrong (missing argument)
            
          if (keywords[i].cb) {
            //!!! handler MAY change keywords pointer! keywords[i] may be invalid pointer after
            // cb() call with return code 0
            bad = keywords[i].cb(argc, argv);
            font_color(5);
            font_bold();
            if (bad > 0)
              q_printf("%% Invalid argument \"%s\" (\"? %s\" for help)\r\n", argv[bad], argv[0]);
            else if (bad < 0)
              q_printf("%% Missing argument (\"? %s\" for help)\r\n", argv[0]);
            else
              i = 0;  // make sure keywords[i] is valid pointer
            font_normal();
            break;
          }  // if callback is provided
        }    // if argc matched
      }      // if name matched
      i++;     // process next entry in keywords[]
    }          // until all keywords[] are processed

    // reached the end of the list and didn't find any exact match?
    if (!keywords[i].cmd) {
      font_color(5);
      font_bold();
      if (found)  //we had a name match but number of arguments was wrong
        q_printf("%% \"%s\": wrong number of arguments (\"? %s\" for help)\r\n", argv[0], argv[0]);
      else
        q_printf("%% \"%s\": command not found\r\n", argv[0]);
    font_normal();
    }
#if WITH_HELP
    if (!found)
      q_print("% Type \"?\" to show the list of commands available\r\n");
#endif
  }

  // free the argv list & input string
  if (argv)
    free(argv);

  free(p);
  return bad;
}


// Execute an arbitrary shell command (1 string per call).
// Basically it is just a wrapper for espshell_command() dealing with 
// const char * input. Return 0 on success, -1 on failure, >0 - failed 
// argument index
//
// TODO: rewrite. make it queue strings to the espshell_task or it all doomed without
//       any mutexes
int
espshell_exec(const char *p) {

  int err = -1;
  char *pp = strdup(p);
  if (pp) {
    err = espshell_command(pp);
    free(pp);
  }
  return err;
}


static TaskHandle_t shell_task = 0;

// shell task. reads and processes user input.
//
// only one shell task can be started!
// this task started from espshell_start()
//
static void espshell_task(const void *arg) {

  //start task and return
  if (arg) {
    if (shell_task != NULL) {
      q_print("% ESPShell is already started\r\n");
      return ;
    }
    if (pdPASS != xTaskCreate((TaskFunction_t)espshell_task, NULL, STACKSIZE, NULL, tskIDLE_PRIORITY, &shell_task))
      q_print("% ESPShell failed to start task\r\n");
  } else {

    // wait until user code calls Serial.begin()
    while (!console_isup())
      delay(100);

#if WITH_HELP
    
    q_print("% ESP Shell. Type \"?\" and press enter for help\r\n");
    q_print("% Type \"? keys\" to learn this shell keys\r\n");
#endif

    while (!Exit) {
      espshell_command(readline(prompt));
      delay(1);
    }
#if WITH_HELP    
    q_print("% Bye!\r\n");
#endif    
    vTaskDelete(NULL);
    shell_task = NULL;
  }
}

/* espshell code copyright 2024, vvb33007 <vvb@nym.hush.com> */

/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.
 */

/*
 * Copyright 1992,1993 Simmule Turner and Rich Salz.  All rights reserved.
 *
 * This software is not subject to any license of the American Telephone
 * and Telegraph Company or of the Regents of the University of California.
 *
 * Permission is granted to anyone to use this software for any purpose on
 * any computer system, and to alter it and redistribute it freely, subject
 * to the following restrictions:
 * 1. The authors are not responsible for the consequences of use of this
 *    software, no matter how awful, even if they arise from flaws in it.
 * 2. The origin of this software must not be misrepresented, either by
 *    explicit claim or by omission.  Since few users ever read sources,
 *    credits must appear in the documentation.
 * 3. Altered versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.  Since few users
 *    ever read sources, credits must appear in the documentation.
 * 4. This notice may not be removed or altered.
 */
