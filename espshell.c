
/* 
 * ESP32Shell for the Arduino Framework by vvb333007 <vvb@nym.hush.com>
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Uses editline library ( (c) 1992,1993 by Simmule Turner and Rich Salz)
 *
 * WHAT IS THIS:
 * -------------
 * This is a debugging/development tool for use with Arduino projects on
 * ESP32 hardware. Provides a command line interface running in parallel
 * to user Arduino sketch (in a separate task). 
 * More information is README.md
 *
 * NAVIGATING THROUGH THE SOURCE CODE (THIS FILE):
 * ----------------------------------------------
 * Search for "TAG:" lines (TAG:pin TAG:uart etc)  to locate corresponding command handlers
 * Search for TAG:keywords to locate the command list
 *            TAG:shell to find the command line parser code
 */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

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

////////////////// COMPILE TIME SETTINGS
#undef AUTOSTART
#undef WITH_HELP
#undef UNIQUE_HISTORY
#undef HIST_SIZE
#undef SCREEN_WIDTH
#undef SCREEN_ROWS
#undef STACKSIZE
#undef BREAK_KEY

#define AUTOSTART      1     // start the shell automatically (no extra code needed to user sketch) \
                             // if set to 0, then the user sketch must call espshell_task("my-prompt") \
                             // (usually at the end of sketch's setup() function)
#define WITH_HELP      1     // set to 0 to save some program space by excluding help strings/functions
#define UNIQUE_HISTORY 1     // wheither to discard repeating commands from the history or not
#define HIST_SIZE      20    // history buffer size (number of commands to remember)
#define SCREEN_WIDTH   80    // terminal width
#define SCREEN_ROWS    24    // terminal height
#define STACKSIZE      10240 //Shell task stack size
#define BREAK_KEY      3     // Keycode of a "Exit" key: CTRL+C to exit "uart NUM tap" mode
#define SEQUENCES_NUM  10    // max number of sequences available for command "sequence"

#define PROMPT      "esp32#>"
#define PROMPT_I2C  "esp32-i2c#>"
#define PROMPT_UART "esp32-uart#>"
#define PROMPT_SEQ  "esp32-seq#>"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <Arduino.h>

// defined in IDF-Arduino glue code ( esp32-hal-uart.c, arduino core )
// !!! add -Wformat to compiler options
extern int __attribute__((format(printf, 1, 2))) log_printf(const char *format, ...);

// start espshell:
// called automatically if AUTOSTART==1 (default). if autostart is disabled (==0) then use
// must declare this function in ther sketch .ino file as extern "C" espshell_task(const void *);
// and call it explicitly
// arg == NULL : blocking call. does not return.
// arg != NULL : async call. starts the task and returns immediately
void espshell_task(const void *arg);

////////////////// EDITLINE CODE BELOW (modified ancient version)

typedef unsigned char CHAR;

#define CRLF "\r\n"

#define SIZE_T unsigned int
#define STATIC static
#define FORWARD static
#define CONST const

#define MEM_INC 64      // "Line" buffer realloc increments
#define SCREEN_INC 256  // "Screen" buffer granularity


/* Memory buffer macros */
#define DISPOSE(p) free((char *)(p))
#define NEW(T, c) ((T *)malloc((unsigned int)(sizeof(T) * (c))))
#define RENEW(p, T, c) (p = (T *)realloc((char *)(p), (unsigned int)(sizeof(T) * (c))))
#define COPYFROMTO(new, p, len) (void)memcpy((char *)(new), (char *)(p), (int)(len))

#include <signal.h>  //TODO: remove
#include <ctype.h>
#include <unistd.h>
#include <sys/termios.h>

#include <driver/uart.h>
static uart_port_t uart = UART_NUM_0;

/* Get/Set UART to use: UART_NUM_0, UART_NUM_1,... etc.
 * By default all the IO happens on UART0 but it can be changed to any other
 * UART. After this call the shell will use uart "i" to perform all the IO
 * leaving previously used uart to the sketch
 *
 * Used by "tty NUM" shell command
 */
int rl_set_uart(uart_port_t i) {

  if (i >= UART_NUM_MAX)
    return -1;

  if (i < 0)
    return uart;
  return (uart = i);
}




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
  CHAR Key;
  STATUS(*Function)
  ();
} KEYMAP;

/*
**  Command history structure.
*/
typedef struct _HISTORY {
  int Size;
  int Pos;
  CHAR *Lines[HIST_SIZE];
} HISTORY;

/*
 *  Globals.
 */


/* 5 variables below supposed to hold TERMIOS values for EOF, ERASE, INTERRUPT, KILL and QUIT
 * keycodes. However on ESP-IDF these are all set to 0
 */
static unsigned rl_eof = 0;
static unsigned rl_erase = 0;
static unsigned rl_intr = 0;
static unsigned rl_kill = 0;
static unsigned rl_quit = 0;

STATIC CHAR NIL[] = "";
STATIC CONST CHAR *Input = NIL;
STATIC CHAR *Line = NULL;
STATIC CONST char *Prompt = NULL;
STATIC CHAR *Yanked;
STATIC char *Screen = NULL;
STATIC CONST char NEWLINE[] = CRLF;
STATIC HISTORY H;
STATIC int Repeat;
STATIC int End;
STATIC int Mark;
STATIC int OldPoint;
STATIC int Point;
STATIC int PushBack;
STATIC int Pushed;
STATIC int Signal = 0;
FORWARD CONST KEYMAP Map[32];
FORWARD CONST KEYMAP MetaMap[16];
STATIC SIZE_T Length;
STATIC SIZE_T ScreenCount;
STATIC SIZE_T ScreenSize;
STATIC char *backspace = NULL;
STATIC int TTYwidth = SCREEN_WIDTH;
STATIC int TTYrows = SCREEN_ROWS;

/* Display print 8-bit chars as `M-x' or as the actual 8-bit char? */
static int rl_meta_chars = 0;

/*
**  Declarations.
*/
STATIC CHAR *editinput();

/*
 * Termios stuff
 * if Reset !=0 then restore previous termios settings.
 * if Reset == 0 then save previous termios settings, read control characters, set VMIN and VTIME
 * for non-blocking IO. First call to this function must be rl_ttyset(0)
 */
static void
rl_ttyset(int Reset) {
  static struct termios old;
  struct termios new;

  //no point to use termios: we are not using stdin/stdout
  //we also don't do read()/write() to the serial port
#if 0

  if (Reset == 0) {

    tcgetattr(0, &old);

    rl_erase = old.c_cc[VERASE];
    rl_kill = old.c_cc[VKILL];
    rl_eof = old.c_cc[VEOF];
    rl_intr = old.c_cc[VINTR];
    rl_quit = old.c_cc[VQUIT];

    new = old;

    new.c_cc[VINTR] = -1;
    new.c_cc[VQUIT] = -1;
    new.c_lflag &= ~(ECHO | ICANON);  // no echo
    new.c_iflag &= ~(ISTRIP | INPCK);

    /* Non-blocking IO, min 1 character */
    new.c_cc[VMIN] = 1;
    new.c_cc[VTIME] = 0;

    tcsetattr(0, TCSADRAIN, &new);
  } else
    tcsetattr(0, TCSADRAIN, &old);
#endif
}

// Print buffered (by TTYputc/TTYputs) data
STATIC void
TTYflush() {

  if (ScreenCount) {
#if 1
    uart_write_bytes(uart, Screen, ScreenCount);
#else
    write(1, Screen, ScreenCount);
#endif
    ScreenCount = 0;
  }
}

// queue a char to be printed
STATIC void
TTYput(CHAR c) {

  Screen[ScreenCount] = c;
  if (++ScreenCount >= ScreenSize - 1) {
    ScreenSize += SCREEN_INC;
    RENEW(Screen, char, ScreenSize);
  }
}

//queue a string to be printed
STATIC void
TTYputs(CONST CHAR *p) {
  while (*p)
    TTYput(*p++);
}

// display a character in a human-readable form:
// normal chars are displayed as is
// CTRL/ALT+Key sequences are displayed as ^Key or M-Key
// DEL is "^?"
STATIC void
TTYshow(CHAR c) {
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
STATIC void
TTYstring(CHAR *p) {
  while (*p)
    TTYshow(*p++);
}

//read a character from user
STATIC unsigned int
TTYget() {

  CHAR c;
  int i;

  TTYflush();

  if (Pushed) {
    Pushed = 0;
    return PushBack;
  }

  if (*Input)
    return *Input++;
#if 1
  // read 1 byte from UART
  if (uart_read_bytes(uart, &c, 1, portMAX_DELAY) < 1)
    return EOF;
#else
  // poll stdin and read 1 byte when it becomes available
  while ((i = read(0, &c, (SIZE_T)1)) < 1)
    delay(1);  //make WDT happy
#endif
  return c;
}

#define TTYback() (backspace ? TTYputs((CHAR *)backspace) : TTYput('\b'))

STATIC void
TTYbackn(int n) {
  while (--n >= 0)
    TTYback();
}



STATIC void
reposition() {
  int i;
  CHAR *p;

  TTYput('\r');
  TTYputs((CONST CHAR *)Prompt);
  for (i = Point, p = Line; --i >= 0; p++)
    TTYshow(*p);
}

STATIC void
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

STATIC void
right(STATUS Change) {
  TTYshow(Line[Point]);
  if (Change == CSmove)
    Point++;
}

STATIC STATUS
ring_bell() {
  TTYput('\07');
  TTYflush();
  return CSstay;
}


//TODO: remove
STATIC STATUS
do_macro(unsigned int c) {
  CHAR name[4];

  name[0] = '_';
  name[1] = c;
  name[2] = '_';
  name[3] = '\0';

  if ((Input = (CHAR *)getenv((char *)name)) == NULL) {
    Input = NIL;
    return ring_bell();
  }
  return CSstay;
}

STATIC STATUS
do_forward(STATUS move) {
  int i;
  CHAR *p;

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

STATIC STATUS
do_case(CASE type) {
  int i;
  int end;
  int count;
  CHAR *p;

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

STATIC STATUS
case_down_word() {
  return do_case(TOlower);
}

STATIC STATUS
case_up_word() {
  return do_case(TOupper);
}

STATIC void
ceol() {
  int extras;
  int i;
  CHAR *p;

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

STATIC void
clear_line() {
  Point = -strlen(Prompt);
  TTYput('\r');
  ceol();
  Point = 0;
  End = 0;
  Line[0] = '\0';
}

STATIC STATUS
insert_string(CHAR *p) {
  SIZE_T len;
  int i;
  CHAR *new;
  CHAR *q;

  len = strlen((char *)p);
  if (End + len >= Length) {
    if ((new = NEW(CHAR, Length + len + MEM_INC)) == NULL)
      return CSstay;
    if (Length) {
      COPYFROMTO(new, Line, Length);
      DISPOSE(Line);
    }
    Line = new;
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

STATIC STATUS
redisplay() {
  TTYputs((CONST CHAR *)NEWLINE);
  TTYputs((CONST CHAR *)Prompt);
  TTYstring(Line);
  return CSmove;
}

STATIC STATUS
toggle_meta_mode() {
  rl_meta_chars = !rl_meta_chars;
  return redisplay();
}


STATIC CHAR *
next_hist() {
  return H.Pos >= H.Size - 1 ? NULL : H.Lines[++H.Pos];
}

STATIC CHAR *
prev_hist() {
  return H.Pos == 0 ? NULL : H.Lines[--H.Pos];
}

STATIC STATUS
do_insert_hist(CHAR *p) {
  if (p == NULL)
    return ring_bell();
  Point = 0;
  reposition();
  ceol();
  End = 0;
  return insert_string(p);
}

STATIC STATUS
do_hist(CHAR *(*move)()) {
  CHAR *p;
  int i;

  i = 0;
  do {
    if ((p = (*move)()) == NULL)
      return ring_bell();
  } while (++i < Repeat);
  return do_insert_hist(p);
}

STATIC STATUS
h_next() {
  return do_hist(next_hist);
}

STATIC STATUS
h_prev() {
  return do_hist(prev_hist);
}

STATIC STATUS
h_first() {
  return do_insert_hist(H.Lines[H.Pos = 0]);
}

STATIC STATUS
h_last() {
  return do_insert_hist(H.Lines[H.Pos = H.Size - 1]);
}

/*
**  Return zero if pat appears as a substring in text.
*/
STATIC int
substrcmp(char *text, char *pat, size_t len) {
  char c;

  if ((c = *pat) == '\0')
    return *text == '\0';
  for (; *text; text++)
    if (*text == c && strncmp(text, pat, len) == 0)
      return 0;
  return 1;
}

STATIC CHAR *
search_hist(CHAR *search, CHAR *(*move)()) {
  static CHAR *old_search;
  int len;
  int pos;
  int (*match)(char *, char *, size_t);
  char *pat;

  /* Save or get remembered search pattern. */
  if (search && *search) {
    if (old_search)
      DISPOSE(old_search);
    old_search = (CHAR *)strdup((char *)search);
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
STATIC STATUS
h_search() {
  static int Searching;
  CONST char *old_prompt;
  CHAR *(*move)();
  CHAR *p;

  if (Searching)
    return ring_bell();
  Searching = 1;

  clear_line();
  old_prompt = Prompt;
  Prompt = "Search: ";
  TTYputs((CONST CHAR *)Prompt);
  move = Repeat == NO_ARG ? prev_hist : next_hist;
  p = editinput();
  Prompt = old_prompt;
  Searching = 0;
  //imperfection on CTRL+R: prompt is displayed at the end of the line
  //TTYputs((CONST CHAR *)Prompt);
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

STATIC STATUS
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

STATIC void
save_yank(int begin, int i) {
  if (Yanked) {
    DISPOSE(Yanked);
    Yanked = NULL;
  }

  if (i < 1)
    return;

  if ((Yanked = NEW(CHAR, (SIZE_T)i + 1)) != NULL) {
    COPYFROMTO(Yanked, &Line[begin], i);
    Yanked[i] = '\0';
  }
}

STATIC STATUS
delete_string(int count) {
  int i;
  CHAR *p;

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

STATIC STATUS
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

STATIC STATUS
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

STATIC STATUS
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

STATIC STATUS
insert_char(int c) {
  STATUS s;
  CHAR buff[2];
  CHAR *p;
  CHAR *q;
  int i;

  if (Repeat == NO_ARG || Repeat < 2) {
    buff[0] = c;
    buff[1] = '\0';
    return insert_string(buff);
  }

  if ((p = NEW(CHAR, Repeat + 1)) == NULL)
    return CSstay;
  for (i = Repeat, q = p; --i >= 0;)
    *q++ = c;
  *q = '\0';
  Repeat = 0;
  s = insert_string(p);
  DISPOSE(p);
  return s;
}

STATIC STATUS
meta() {
  unsigned int c;
  CONST KEYMAP *kp;

  if ((int)(c = TTYget()) == EOF)
    return CSeof;

  /* Also include VT-100 arrows. */
  if (c == '[' || c == 'O')
    switch (c = TTYget()) {
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

  //TODO: remove macro
  if (isupper(c))
    return do_macro(c);

  for (OldPoint = Point, kp = MetaMap; kp->Function; kp++)
    if (kp->Key == c)
      return (*kp->Function)();

  return ring_bell();
}

STATIC STATUS
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

STATIC STATUS
TTYspecial(unsigned int c) {
  if (ISMETA(c))
    return CSdispatch;

  if (c == rl_erase || (int)c == DEL)
    return bk_del_char();

  if (c == rl_kill) {
    if (Point != 0) {
      Point = 0;
      reposition();
    }
    Repeat = NO_ARG;
    return kill_line();
  }

  if (c == rl_eof && Point == 0 && End == 0)
    return CSeof;

  //TODO: remove
  if (c == rl_intr) {
    Signal = SIGINT;
    return CSsignal;
  }

  //TODO: remove
  if (c == rl_quit) {
    Signal = SIGQUIT;
    return CSeof;
  }

  return CSdispatch;
}

STATIC CHAR *
editinput() {
  unsigned int c;
  char tmp[2] = { 0, 0 };


  Repeat = NO_ARG;
  OldPoint = Point = Mark = End = 0;
  Line[0] = '\0';

  Signal = -1;
  while ((int)(c = TTYget()) != EOF) {

    switch (TTYspecial(c)) {
      case CSdone:
        return Line;
      case CSeof:
        return NULL;
      case CSsignal:
        return (CHAR *)"";
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
            return (CHAR *)"";
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

STATIC void
hist_add(CHAR *p) {
  int i;

  if ((p = (CHAR *)strdup((char *)p)) == NULL)
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
char *
readline(CONST char *prompt) {
  CHAR *line;
  int s;

  if (Line == NULL) {
    Length = MEM_INC;
    if ((Line = NEW(CHAR, Length)) == NULL)
      return NULL;
  }


  rl_ttyset(0);
  hist_add(NIL);
  ScreenSize = SCREEN_INC;
  Screen = NEW(char, ScreenSize);
  Prompt = prompt ? prompt : (char *)NIL;
  TTYputs((CONST CHAR *)Prompt);
  TTYflush();

  if ((line = editinput()) != NULL) {
    line = (CHAR *)strdup((char *)line);
    TTYputs((CONST CHAR *)NEWLINE);
    TTYflush();
  }

  rl_ttyset(1);
  DISPOSE(Screen);
  DISPOSE(H.Lines[--H.Size]);
  //TODO: remove signal processing
  if (Signal > 0) {
    s = Signal;
    Signal = 0;
    //(void)kill(getpid(), s);
  }
  return (char *)line;
}

// add an arbitrary string p to the command history
// ARROW UP and ARROW DOWN are history navigation keys
// CTRL+R is history search key
//
void rl_add_history(char *p) {
  if (p == NULL || *p == '\0')
    return;

#if UNIQUE_HISTORY
  if (H.Size && strcmp(p, (char *)H.Lines[H.Size - 1]) == 0)
    return;
#endif
  hist_add((CHAR *)p);
}


STATIC STATUS
beg_line() {
  if (Point) {
    Point = 0;
    return CSmove;
  }
  return CSstay;
}

STATIC STATUS
del_char() {
  return delete_string(Repeat == NO_ARG ? 1 : Repeat);
}

STATIC STATUS
end_line() {
  if (Point != End) {
    Point = End;
    return CSmove;
  }
  return CSstay;
}

STATIC STATUS
accept_line() {
  Line[End] = '\0';
  return CSdone;
}

STATIC STATUS
transpose() {
  CHAR c;

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

STATIC STATUS
quote() {
  unsigned int c;

  return (int)(c = TTYget()) == EOF ? CSeof : insert_char((int)c);
}

STATIC STATUS
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

STATIC STATUS
mk_set() {
  Mark = Point;
  return CSstay;
}

STATIC STATUS
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

STATIC STATUS
yank() {
  if (Yanked && *Yanked)
    return insert_string(Yanked);
  return CSstay;
}

STATIC STATUS
copy_region() {
  if (Mark > End)
    return ring_bell();

  if (Point > Mark)
    save_yank(Mark, Point - Mark);
  else
    save_yank(Point, Mark - Point);

  return CSstay;
}

STATIC STATUS
move_to_char() {
  unsigned int c;
  int i;
  CHAR *p;

  if ((int)(c = TTYget()) == EOF)
    return CSeof;
  for (i = Point + 1, p = &Line[i]; i < End; i++, p++)
    if (*p == c) {
      Point = i;
      return CSmove;
    }
  return CSstay;
}

STATIC STATUS
fd_word() {
  return do_forward(CSmove);
}

STATIC STATUS
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

STATIC STATUS
bk_word() {
  int i;
  CHAR *p;

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

STATIC STATUS
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
STATIC int
argify(CHAR *line, CHAR ***avp) {
  CHAR *c;
  CHAR **p;
  CHAR **new;
  int ac;
  int i;

  i = MEM_INC;
  if ((*avp = p = NEW(CHAR *, i)) == NULL)
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

          new = NEW(CHAR *, i + MEM_INC);

          if (new == NULL) {
            p[ac] = NULL;
            return ac;
          }

          COPYFROMTO(new, p, i * sizeof(char **));
          i += MEM_INC;
          DISPOSE(p);
          *avp = p = new;
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
  p[ac] = NULL;  // NULL? TODO: check & replace with '\0'

  return ac;
}


STATIC STATUS
last_argument() {
  CHAR **av;
  CHAR *p;
  STATUS s;
  int ac;

  if (H.Size == 1 || (p = H.Lines[H.Size - 2]) == NULL)
    return ring_bell();

  if ((p = (CHAR *)strdup((char *)p)) == NULL)
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

STATIC CONST KEYMAP Map[32] = {
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
  { CTL('L'), redisplay },
  { CTL('M'), accept_line },
  { CTL('N'), h_next },
  { CTL('O'), ring_bell },
  { CTL('P'), h_prev },
  { CTL('Q'), ring_bell },
  { CTL('R'), h_search },
  { CTL('S'), ring_bell },
  { CTL('T'), transpose },
  { CTL('U'), ring_bell },
  { CTL('V'), quote },
  { CTL('W'), wipe },
  { CTL('X'), exchange },
  { CTL('Y'), yank },
  { CTL('Z'), ring_bell },
  { CTL('['), meta },
  { CTL(']'), move_to_char },
  { CTL('^'), ring_bell },
  { CTL('_'), ring_bell },
  { 0, NULL }
};

STATIC CONST KEYMAP MetaMap[16] = {
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


////////////////////////////// SHELL CODE BELOW
//TAG:forwards
//GCC stringify magic.
#define xstr(s) ystr(s)
#define ystr(s) #s

#define MAGIC_FREQ 78227  // max allowed frequency for the "tone" command

static int cmd_question(int, char **);

static int cmd_uptime(int, char **);
static int cmd_pin(int, char **);
static int cmd_cpu(int, char **);
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
static int cmd_suspend(int, char **);
static int cmd_resume(int, char **);
static int cmd_tone(int, char **);
static int cmd_count(int, char **);
static int cmd_exit(int, char **);
static int cmd_show(int, char **);

static int cmd_seq_if(int, char **);
static int cmd_seq_eot(int argc, char **argv);
static int cmd_seq_modulation(int argc, char **argv);
static int cmd_seq_zeroone(int argc, char **argv);
static int cmd_seq_tick(int argc, char **argv);
static int cmd_seq_bits(int argc, char **argv);
static int cmd_seq_levels(int argc, char **argv);
static int cmd_seq_show(int argc, char **argv);

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
  const char *cmd;                   // command keyword ex.: "pin"
  int (*cb)(int argc, char **argv);  // callback to call
  int min_argc;                      // minimum number of arguments required. Negative values mean "any"
  const char *help;                  // help text displayed on "command ?"
  const char *brief;                 // brief text displayed on "?". NULL means "use help text, not brief"
};

#define KEYWORDS_BEGIN \
  { "?", cmd_question, -1, "Show the list of available commands", NULL }
#define KEYWORDS_END \
  { "exit", cmd_exit, 0, "Exit", NULL }, { \
    NULL, NULL, 0, NULL, NULL \
  }


//Custom uart commands (uart subderictory)
//Those displayed after executing "uart 2" (or any other uart interface)
//TAG:kuart
static struct keywords_t keywords_uart[] = {
  //{ "?", cmd_question, -1, "Show the list of available commands", NULL },
  KEYWORDS_BEGIN,
  { "up", cmd_uart, 3,
#if WITH_HELP
    "% \"up RX TX BAUD\"\n\r"
    "%\n\r"
    "% Initialize uart interface X on pins RX/TX,baudrate BAUD, 8N1 mode\n\r"
    "% Ex.: up 18 19 115200 - Setup uart on pins rx=18, tx=19, at speed 115200",
#else
    "",
#endif
    "Initialize uart (pins/speed)" },
  { "baud", cmd_uart_baud, 1,
#if WITH_HELP
    "% \"baud SPEED\"\n\r"
    "%\n\r"
    "% Set speed for the uart (uart must be initialized)\n\r"
    "% Ex.: baud 115200 - Set uart baud rate to 115200",
#else
    "",
#endif
    "Set baudrate" },

  { "down", cmd_uart, 0,
#if WITH_HELP
    "% \"down\"\n\r"
    "%\n\r"
    "% Shutdown interface, detach pins",
#else
    "",
#endif
    "Shutdown" },

  // overlaps with "uart X down", never called, here are for the help text
  { "read", cmd_uart, 0,
#if WITH_HELP
    "% \"read\"\n\r"
    "%\n\r"
    "% Read bytes (available) from uart interface X",
#else
    "",
#endif
    "Read data from UART" },

  { "tap", cmd_uart, 0,
#if WITH_HELP
    "% \"tap\\n\r"
    "%\n\r"
    "% Bridge the UART IO directly to/from shell\n\r"
    "% User input will be forwarded to uart X;\n\r"
    "% Anything UART X sends back will be forwarded to the user",
#else
    "",
#endif
    "Talk to UARTs device" },

  { "write", cmd_uart, -1,
#if WITH_HELP
    "% \"write TEXT\"\n\r"
    "%\n\r"
    "% Send an ascii/hex string(s) to UART X\n\r"
    "% TEXT can include spaces, escape sequences: \\n, \\r, \\\\, \\t and \n\r"
    "% hexadecimal numbers \\AB (A and B are hexadecimal digits)\n\r"
    "%\n\r"
    "% Ex.: \"write ATI\\n\\rMixed\\20Text and \\20\\21\\ff\"",
#else
    "",
#endif
    "Send bytes over this UART" },

  KEYWORDS_END
};


//i2c subderictory keywords list
//cmd_exit() and cmd_i2c_if are responsible for selecting keywords list
//to use
static struct keywords_t keywords_i2c[] = {

  //{ "?", cmd_question, -1, "Show the list of available commands", NULL },
  KEYWORDS_BEGIN,
  { "up", cmd_i2c, 3,
#if WITH_HELP
    "% \"up SDA SCL CLK\"\n\r"
    "%\n\r"
    "% Initialize I2C interface X, use pins SDA/SCL, clock rate CLK\n\r"
    "% Ex.: up 21 22 100000 - enable i2c at pins sda=21, scl=22, 100kHz clock",
#else
    "",
#endif
    "initialize interface (pins and speed)" },
  { "clock", cmd_i2c_clock, 1,
#if WITH_HELP
    "% \"clock SPEED\"\n\r"
    "%\n\r"
    "% Set I2C master clock (i2c must be initialized)\n\r"
    "% Ex.: clock 100000 - Set i2c clock to 100kHz",
#else
    "",
#endif
    "Set clock" },


  { "read", cmd_i2c, 2,
#if WITH_HELP
    "% \"read ADDR SIZE\"\n\r"
    "%\n\r"
    "% I2C bus X : read SIZE bytes from a device at address ADDR (hex)\n\r"
    "% Ex.: read 68 7 - read 7 bytes from device address 0x68",
#else
    "",
#endif
    "Read data from a device" },

  { "down", cmd_i2c, 0,
#if WITH_HELP
    "% \"down\"\n\r"
    "%\n\r"
    "% Shutdown I2C interface X",
#else
    "",
#endif
    "Shutdown i2c interface" },

  { "scan", cmd_i2c, 0,
#if WITH_HELP
    "% \"scan\"\n\r"
    "%\n\r"
    "% Scan I2C bus X for devices. Interface must be initialized!",
#else
    "",
#endif
    "Scan i2c bus" },

  { "write", cmd_i2c, -1,
#if WITH_HELP
    "% \"write ADDR D1 [D2 ... Dn]\"\n\r"
    "%\n\r"
    "% Write bytes D1..Dn (hex values) to address ADDR (hex) on I2C bus X\n\r"
    "% Ex.: write 78 0 1 FF - write 3 bytes to address 0x78: 0,1 and 255",
#else
    "",
#endif
    "Send bytes to the device" },
  KEYWORDS_END
};


//'sequence' subderictory keywords list
static struct keywords_t keywords_sequence[] = {

  KEYWORDS_BEGIN,
  { "eot", cmd_seq_eot, 1,
#if WITH_HELP
    "% \"eot high|low\"\n\r"
    "%\n\r"
    "% End of transmission: pull the line high or low at the\n\r"
    "% end of a sequence. Default is \"low\"",
#else
    "",
#endif
    "End-of-Transmission pin state" },
  { "tick", cmd_seq_tick, 1,
#if WITH_HELP
    "% \"tick TIME\"\n\r"
    "%\n\r"
    "% Set the sequence tick time: defines a resolution of a pulse sequence.\n\r"
    "% Expressed in microseconds, can be anything between 0.0125 and 3.2\n\r"
    "% Ex.: tick 0.1 - set resolution to 0.1 microsecond",
#else
    "",
#endif
    "Set resolution" },


  { "zero", cmd_seq_zeroone, 2,
#if WITH_HELP
    "% \"zero LEVEL/DURATION [LEVEL2/DURATION2]\"\n\r"
    "%\n\r"
    "% Define a logic \"0\"\n\r"
    "% Ex.: zero 0/50      - 0 is a level: LOW for 50 ticks\n\r"
    "% Ex.: zero 1/50 0/20 - 0 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks",

#else
    "",
#endif
    "Define a zero" },
  { "zero", cmd_seq_zeroone, 1, NULL, NULL },  //HIDDEN

  { "one", cmd_seq_zeroone, 2,
#if WITH_HELP
    "% \"one LEVEL/DURATION [LEVEL2/DURATION2]\"\n\r"
    "%\n\r"
    "% Define a logic \"1\"\n\r"
    "% Ex.: one 1/50       - 1 is a level: HIGH for 50 ticks\n\r"
    "% Ex.: one 1/50 0/20  - 1 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks",

#else
    "",
#endif
    "Define an one" },

  { "one", cmd_seq_zeroone, 1, NULL, NULL },  //HIDDEN


  { "bits", cmd_seq_bits, 1,
#if WITH_HELP
    "% \"bits STRING\"\n\r"
    "%\n\r"
    "% A bit pattern to be used as a sequence. STRING must contain only 0s and 1s\n\r"
    "% Overrides previously set \"levels\" command\n\r"
    "% See commands \"one\" and \"zero\" to define \"1\" and \"0\"\n\r"
    "%\n\r"
    "% Ex.: bits 11101000010111100  - 17 bit sequence",
#else
    "",
#endif
    "Set pattern to transmit" },

  { "levels", cmd_seq_levels, -1,
#if WITH_HELP
    "% \"levels L/D L/D ... L/D\"\n\r"
    "%\n\r"
    "% A bit pattern to be used as a sequnce. L is either 1 or 0 and \n\r"
    "% D is the duration measured in ticks [0..32767] \n\r"
    "% Overrides previously set \"bits\" command\n\r"
    "%\n\r"
    "% Ex.: levels 1/50 0/20 1/100 0/500  - HIGH 50 ticks, LOW 20, HIGH 100 and 0 for 500 ticks\n\r"
    "% Ex.: levels 1/32767 1/17233 0/32767 0/7233 - HIGH for 50000 ticks, LOW for 40000 ticks",
#else
    "",
#endif
    "Set levels to transmit" },

  { "modulation", cmd_seq_modulation, 3,
#if WITH_HELP
    "% \"modulation FREQ [DUTY [low|high]]\"\n\r"
    "%\n\r"
    "% Enables/disables an output signal modulation with frequency FREQ\n\r"
    "% Optional parameters are: DUTY (from 0 to 1) and LEVEL (either high or low)\n\r"
    "%\n\r"
    "% Ex.: modulation 100         - modulate all 1s with 100Hz, 50%% duty cycle\n\r"
    "% Ex.: modulation 100 0.3 low - modulate all 0s with 100Hz, 30%% duty cycle\n\r"
    "% Ex.: modulation 0           - disable modulation\n\r",
#else
    "",
#endif
    "Enable/disable modulation" },

  { "modulation", cmd_seq_modulation, 2, NULL, NULL },  // HIDDEN
  { "modulation", cmd_seq_modulation, 1, NULL, NULL },  // HIDDEN

  { "show", cmd_seq_show, 0, "Show sequence" },

  KEYWORDS_END
};


//Custom SPI commands (spi subderictory)
//TAG:kspi
static struct keywords_t keywords_spi[] = {

  KEYWORDS_BEGIN,

  KEYWORDS_END
};



// root directory commands
static struct keywords_t keywords_main[] = {

  //{ "?", cmd_question, -1, "Show the list of available commands", NULL },
  KEYWORDS_BEGIN,

  { "uptime", cmd_uptime, 0, "% \"uptime\" - Shows time passed since last boot", "System uptime" },

  { "show", cmd_show, 2,
#if WITH_HELP
    "\"show seq X\" - display sequence X\n\r",
#else
    "",
#endif
    "Display information" },
  { "pin", cmd_pin, -1,
#if WITH_HELP
    "% \"pin X (up|down|out|in|open|high|low|save|load|read|aread)...\"\n\r" \
    "%\n\r" \
    "% Set/Save/Load pin X configuration: pull/direction/digital value\n\r" \
    "% Multiple arguments must be separated with spaces, see examples below:\n\r" \
    "%\n\r" \
    "% Ex.: pin 18 read aread         - read digital and then analog values\n\r" \
    "% Ex.: pin 18 save               - save pin state\n\r" \
    "% Ex.: pin 18 save out high load - save pin state and set it to OUTPUT HIGH then restore\n\r" \
    "% Ex.: pin 18 out high           - set pin18 to OUTPUT logic \"1\"\n\r" \
    "% Ex.: pin 18 in up down open    - set pin18 INPUT, PULL-UP, PULL-DOWN, OPEN_DRAIN\n\r",
#else
    "",
#endif
    "GPIO commands" },


  //never called, shadowed by previous entry. for helptext only
  { "pin", cmd_pin, 3,
#if WITH_HELP
    "% \"pin X seq Y\"\n\r"
    "%\n\r"
    "% Set GPIO pin X to output the sequence Y\n\r"
    "% Sequence must be configured in advance (see command \"sequence\")\n\r"
    "% Ex.: pin 18 seq 0       - start Sequence0 on GPIO18",
#else
    "",
#endif
    "GPIO commands" },


  //never called, shadowed by previous entry. for helptext only
  { "pin", cmd_pin, 1,
#if WITH_HELP
    "% \"pin X\"\n\r"
    "%\n\r"
    "% Show pin X configuration",
#else
    "",
#endif
    NULL },

  { "cpu", cmd_cpu, 1, "% \"cpu FREQ\" : Set CPU frequency to FREQ Mhz",
    "Set/show CPU parameters" },

  { "cpu", cmd_cpu, 0, "% \"cpu\" : Show CPUID and CPU/XTAL/APB frequencies", NULL },

  { "mem", cmd_mem, 0, "% Show memory usage info", "Memory usage" },

  { "reload", cmd_reload, 0, "% \"reload\" - Restarts CPU", "Reset CPU" },

  { "nap", cmd_nap, 1,
#if WITH_HELP
    "% \"nap SEC\"\n\r"
    "%\n\r"
    "% Put the CPU into light sleep mode for SEC seconds.",
#else
    "",
#endif
    "CPU sleep" },
  { "nap", cmd_nap, 0,
#if WITH_HELP
    "% \"nap\"\n\r"
    "%\n\r"
    "% Put the CPU into light sleep mode, wakeup by console",
#else
    "",
#endif
    NULL },

  { "iic", cmd_i2c_if, 1,
#if WITH_HELP
    "% \"iic X\" \n\r"
    "%\n\r"
    "% Enter I2C interface X configuration mode \n\r"
    "% Ex.: iic 0 - configure/use interface I2C 0",
#else
    "",
#endif
    "I2C commands" },

  { "uart", cmd_uart_if, 1,
#if WITH_HELP
    "% \"uart X\"\n\r"
    "%\n\r"
    "% Enter UART interface X configuration mode\n\r"
    "% Ex.: uart 1 - configure/use interface UART 1",
#else
    "",
#endif
    "UART commands" },
  { "sequence", cmd_seq_if, 1,
#if WITH_HELP
    "% \"sequence X\"\n\r"
    "%\n\r"
    "% Create/configure a sequence\n\r"
    "% Ex.: sequence 0 - configure Sequence0",
#else
    "",
#endif
    "Pulses/levels sequence configuration" },

  { "tty", cmd_tty, 1, "% \"tty X\" Use uart X for command line interface.\n\r",
    "IO redirect" },

  { "suspend", cmd_suspend, 0, "% \"suspend\" : Suspend main loop()\n\r", "Suspend sketch execution" },
  { "resume", cmd_resume, 0, "% \"resume\" : Resume main loop()\n\r", "Resume sketch execution" },

  { "tone", cmd_tone, 3,
#if WITH_HELP
    "% \"tone X FREQ DUTY\"\n\r"
    "%\n\r"
    "% Start PWM generator on pin X, frequency FREQ Hz and duty cycle of DUTY\n\r"
    "% Max frequency is " xstr(MAGIC_FREQ) " Hz\n\r"
                                           "% Value of DUTY is in range [0..1023] with 511 being a 50% duty cycle",
#else
    "",
#endif
    "PWM output" },
  { "tone", cmd_tone, 2,
#if WITH_HELP
    "% \"tone X FREQ\"\n\r"
    "% Start squarewave generator on pin X, frequency FREQ Hz\n\r"
    "% duty cycle is  set to 50%",
#else
    "",
#endif
    NULL },
  { "tone", cmd_tone, 1,
#if WITH_HELP
    "% \"tone X\" Stop generator on pin X",
#else
    "",
#endif
    NULL },
  { "count", cmd_count, 3,
#if WITH_HELP
    "% \"count X [neg|pos|both [DELAY_MS]]\"\n\r"
    "% Count pulses (negative/positive edge or both) on pin X within DELAY time\n\r"
    "% Pulse edge type and delay time are optional. Defaults are: \"pos\" and \"1000\" \n\r"
    "% NOTE: It is a 16-bit counter so consider using shorter delays on frequencies > 30Khz\n\r"
    "%\n\r"
    "% Ex.: \"count 4\"           - count positive edges on pin 4 for 1000ms\n\r"
    "% Ex.: \"count 4 neg 2000\"  - count pulses (falling edge) on pin 4 for 2 sec.",
#else
    "",
#endif
    "Pulse counter" },
  { "count", cmd_count, 2, NULL, NULL },  //HIDDEN COMMAND (help=brief=NULL)
  { "count", cmd_count, 1, NULL, NULL },  //HIDDEN COMMAND (help=brief=NULL)

  KEYWORDS_END
};


//TAG:globals
//current keywords list to use
static struct keywords_t *keywords = keywords_main;

//common Failed message
static const char *Failed = "% Failed\n\r";

// prompts
static const char *prompt = PROMPT;
static const char *prompt_old = NULL;

// sequences
static struct sequence sequences[SEQUENCES_NUM];

// interface unit number when entering a subderictory.
// also sequence number when entering "sequence" subdir
static int Context = 0;

//TAG:utility
//check if given ascii string is a decimal number. Ex.: "12345"
static const inline bool isnum(char *p) {
  while (*p >= '0' && *p <= '9')
    p++;
  return !(*p);  //if *p is 0 then all the chars were digits (end of line reached).
}

static const bool isfloat(char *p) {
  bool dot = false;
  while ((*p >= '0' && *p <= '9') || (*p == '.' && !dot)) {
    if (*p == '.')
      dot = true;
    p++;
  }
  return !(*p);  //if *p is 0 then all the chars were digits (end of line reached).
}



//check if given ascii string is a hex number. Only first two bytes are checked
//TODO: rewrite
static const inline bool ishex(char *p) {
  if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
    p++;
    if ((*p == 0) || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
      return true;
  }

  return false;
}

//convert hex ascii byte.
//input strings are 1 or 2 chars long:  Ex.:  "0A", "A","6E"
//TODO: rewrite
static unsigned char ascii2hex(char *p) {

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


// checks if pin (GPIO) number is in valid range.
// display a message if pin is out of range
static bool pin_exist(int pin) {

  if ((pin >= 0) && (pin < SOC_GPIO_PIN_COUNT) && (((uint64_t )1 << pin) & SOC_GPIO_VALID_GPIO_MASK))
    return true;

  log_printf("%% Available pin numbers are are 0..%d, except ",SOC_GPIO_PIN_COUNT-1);
  for (pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
    if (!(((uint64_t )1 << pin) & SOC_GPIO_VALID_GPIO_MASK))
      log_printf("%d,",pin);
#if 0      
  log_printf("\n\r%% Reserved pins (used internally): ");  
  
  int count;
  for (pin = count = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
    if (esp_gpio_is_reserved(BIT64(pin))) {
      count++;
      log_printf("%d, ",pin);
    }
  if (!count)
    log_printf("none");
#endif    
  log_printf(CRLF);
  return false;
}

// calculate frequency from tick length
// 0.1uS = 10Mhz
unsigned long seq_tick2freq(float tick_us) {

  return tick_us ? (unsigned long)((float)1000000 / tick_us) : 0;
}

static void seq_init() {
  int i;

  for (i = 0; i < SEQUENCES_NUM; i++) {
    sequences[i].tick = 0;
    sequences[i].seq = NULL;
    sequences[i].bits = NULL;

    sequences[i].alph[0].duration0 = 0;
    sequences[i].alph[0].duration1 = 0;
    sequences[i].alph[1].duration0 = 0;
    sequences[i].alph[1].duration1 = 0;
  }
}

static const char *Notset = "is not set yet\n\r";

// dump sequence content
static void seq_dump(int seq) {

  struct sequence *s;

  if (seq < 0 || seq >= SEQUENCES_NUM) {
    log_printf("%% Sequence %d does not exist\n\r");
    return;
  }

  s = &sequences[seq];

  log_printf("%%\n\r%% Sequence #%d:\n\r%% Resolution ", seq);

  if (s->tick)
    log_printf(": %.4fuS  (Frequency: %lu Hz)\n\r", seq, s->tick, seq_tick2freq(s->tick));
  else
    log_printf(Notset);

  log_printf("%% Levels ");
  if (s->seq) {

    int i;
    unsigned long total = 0;

    log_printf("are :");

    for (i = 0; i < s->seq_len; i++) {
      if (!(i & 3))
        log_printf("\n\r%% ");
      log_printf("%d/%d, %d/%d, ", s->seq[i].level0, s->seq[i].duration0, s->seq[i].level1, s->seq[i].duration1);
      total += s->seq[i].duration0 + s->seq[i].duration1;
    }
    log_printf("\n\r%% Total: %d levels, duration: %lu ticks, (~%lu uS)\n\r", s->seq_len * 2, total, (unsigned long)((float)total * s->tick));
  } else
    log_printf(Notset);

  log_printf("%% Modulation ");
  if (s->mod_freq)
    log_printf(" : yes, \"%s\" are modulated at %luHz, duty %.2f%%\n\r", s->mod_high ? "HIGH" : "LOW", s->mod_freq, s->mod_duty * 100);
  else
    log_printf("is not used\n\r");

  log_printf("%% Bit sequence ");

  if (s->bits)
    log_printf(": (%d bits) \"%s\"\n\r", strlen(s->bits), s->bits);
  else
    log_printf(Notset);

  log_printf("%% Zero ");

  if (s->alph[0].duration0) {
    if (s->alph[0].duration1)
      log_printf("%d/%d %d/%d\n\r", s->alph[0].level0, s->alph[0].duration0, s->alph[0].level1, s->alph[0].duration1);
    else
      log_printf("%d/%d\n\r", s->alph[0].level0, s->alph[0].duration0);
  } else
    log_printf(Notset);

  log_printf("%% One ");

  if (s->alph[1].duration0) {
    if (s->alph[1].duration1)
      log_printf("%d/%d %d/%d\n\r", s->alph[1].level0, s->alph[1].duration0, s->alph[1].level1, s->alph[1].duration1);
    else
      log_printf("%d/%d\n\r", s->alph[1].level0, s->alph[1].duration0);
  } else
    log_printf(Notset);
  log_printf("%% Pull pin %s after transmission is done\n\r", s->eot ? "high" : "low");
}

// convert a level string to numerical values:
// "1/500" gets converted to level=1 and duration=500
//
static int seq_atol(int *level, int *duration, char *p) {

  char *lp = p;
  char *dp = NULL;

  while (*p) {
    if (*p == '/' || *p == '\\') {
      if (dp)
        return -1;  // separator was found already
      if (level)
        *p = '\0';  // spli the string for atoi
      dp = p + 1;
    } else if (*p < '0' || *p > '9')
      return -1;
    p++;
  }
  if (!dp)  // no separator found
    return -2;
  if (!*lp)  // separator was the first symbol
    return -3;

  //FIXME: check that the level is either 1 or 0
  if (level) {
    *level = atoi(lp);
    *(dp - 1) = '\\';  // restore separator
  }

  //FIXME: check that the duration is < 32768
  if (duration)
    *duration = atoi(dp);

  return 0;
}


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
static int seq_isready(int seq) {

  if (seq < 0 || seq >= SEQUENCES_NUM)
    return 0;

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
        log_printf("%% \"One\" defined as a level, but \"Zero\" is a pulse\n\r");
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
        log_printf("%% \"One\" defined as a pulse, but \"Zero\" is a level\n\r");
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
        log_printf("%% Bitstring was padded with one extra \"0\"\n\r");
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
    prompt = prompt_old;
  }
  return 0;
}

//TAG:show
//show KEYWORD ARG1 ARG2 .. ARGN
static int cmd_show(int argc, char **argv) {

  if (argc < 2)
    return -1;
  if (!strcmp("seq", argv[1]))
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
    log_printf("%% Sequence numbers are 0..%d\n\r", SEQUENCES_NUM - 1);
    return 1;
  }

  Context = seq;
  keywords = keywords_sequence;
  prompt_old = prompt;
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

  if (!strcmp(argv[1], "high") || argv[1][0] == '1')
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

    if (duty <= 0.0f || duty > 1.0f) {
      log_printf("%% Duty cycle is a number in range (0 .. 1] : 0.5 means 50%% duty\n\r");
      return 2;
    }
  }


  //third argument: "high" or "1" means moduleate when line is HIGH (modulate 1's)
  // "low" or "0" - modulate when line is LOW (modulate zeros)
  if (argc > 3) {
    if (!strcmp(argv[3], "low") || argv[3][0] == '1')
      high = 0;
    else if (!strcmp(argv[3], "high") || argv[3][0] == '0')
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
  if (!strcmp(argv[0], "one"))
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


  if ((i = seq_compile(Context)) < 0)
    log_printf("Attempted compilation: %d\n\r", i);

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
    log_printf("%% Tick must be in range 0.0125..3.2 microseconds\n\r");
    return 1;
  }

  // either "0" or not number was entered
  if (!sequences[Context].tick)
    return 1;

  if ((argc = seq_compile(Context)) < 0)
    log_printf("Attempted compilation: %d\n\r", argc);

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

  // attempt to compile.
  if ((argc = seq_compile(Context)) < 0)
    log_printf("Attempted compilation: %d\n\r", argc);

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
  for (i = 1; i < argc; i++) {
    int e;
    if ((e = seq_atol(NULL, NULL, argv[i])) < 0) {
      //log_printf("seq_atol() : %d\n",e);
      return i;
    }
  }

  seq_freemem(Context);

  i = argc - 1;

  if (i & 1) {
    log_printf("%% Uneven number if levels. Please add 1 more\n\r");
    return 0;
  }


  s->seq_len = i / 2;
  s->seq = (rmt_data_t *)malloc(sizeof(rmt_data_t) * s->seq_len);

  if (!s->seq)
    return -1;


  memset(s->seq, 0, sizeof(rmt_data_t) * s->seq_len);

  // each RMT symbol (->seq entry) can hold 2 levels
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

  //log_printf("%d levels encoded to %d rmt_data_t\n\r",i,j);

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

  if (argc < 2) {

    seq_dump(Context);
    return 0;
  }

  if (argc != 3)
    return -1;

  if (strcmp(argv[1], "seq"))
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

#define PULSE_WAIT 1000

//TAG:count
//TODO: convert to new PCNT api as this one is deprecated
//"count PIN [neg|pos|both [DELAY_MS]]"
//
// To deal with counter overflows interrupts must be used.
// At the same time I want to keep it simple: just tell
// the user to use shorter delays on higher frequencies
static int cmd_count(int argc, char **argv) {

  pcnt_config_t cfg;
  int16_t count, wait = 1000;
  int pos = PCNT_COUNT_DIS;
  int neg = PCNT_COUNT_DIS;
  int pin;

  //pin number
  if (!isnum(argv[1]))
    return 1;


  memset(&cfg, 0, sizeof(cfg));

  cfg.pulse_gpio_num = pin = atoi(argv[1]);

  if (!pin_exist(pin))
    return 1;

  cfg.ctrl_gpio_num = -1;  // don't use "control pin" feature
  cfg.channel = PCNT_CHANNEL_0;
  cfg.unit = PCNT_UNIT_0;

  // user has provided second argument?
  if (argc > 2) {
    if (!strcmp(argv[2], "pos")) pos = PCNT_COUNT_INC;
    else if (!strcmp(argv[2], "neg")) neg = PCNT_COUNT_INC;
    else if (!strcmp(argv[2], "both")) {
      neg = pos = PCNT_COUNT_INC;
    } else
      return 2;
    //user has provided 3rd argument?
    if (argc > 3) {
      // delay must be a number
      if (!isnum(argv[3]))
        return 3;
      wait = atoi(argv[3]);
    }
  } else
    pos = PCNT_COUNT_INC;  // default is to count positive edges

  cfg.pos_mode = pos;
  cfg.neg_mode = neg;

  log_printf("%% Counting pulses on GPIO%d.. ", pin);

  pcnt_unit_config(&cfg);
  pcnt_counter_pause(PCNT_UNIT_0);
  pcnt_counter_clear(PCNT_UNIT_0);
  pcnt_counter_resume(PCNT_UNIT_0);
  delay(wait);
  pcnt_counter_pause(PCNT_UNIT_0);
  pcnt_get_counter_value(PCNT_UNIT_0, &count);
  log_printf("%lu pulses (%.3f sec)\n\r", (unsigned int)count, (float)wait / 1000.0f);
  return 0;
}



//TAG:tone
#include "soc/soc_caps.h"
#include "esp32-hal-ledc.h"

//tone PIN FREQ [DUTY]   - pwm on
//tone PIN               - pwm off
//FIXME: max frequency is 78277 and I don't know why

static int cmd_tone(int argc, char **argv) {

  int resolution = 10;

  if (argc < 2)
    return -1;  //missing arg

  if (!isnum(argv[1]))
    return 1;  // arg 1 is bad

  unsigned char pin = (unsigned char)(atoi(argv[1]));
  int freq = 0, duty = ((1 << resolution) - 1) / 2;  //default duty is 50%

  if (!pin_exist(pin))
    return 1;

  // frequency parameter?
  if (argc > 2) {
    if (!isnum(argv[2]))
      return 2;  // arg 2 is bad
    freq = atoi(argv[2]);
    if (freq < 0 || freq > MAGIC_FREQ) {
      log_printf("%% Valid frequency range is [1 .. %u] Hz\n\r", MAGIC_FREQ);
      return 2;
    }
  }

  // duty specified?
  if (argc > 3) {
    if (!isnum(argv[3]))
      return 3;
    duty = atoi(argv[3]);
  }

  if (duty < 0 || duty > 1023) {
    log_printf("%% Valid duty range is [1 .. %d] Hz\n\r", (1 << resolution) - 1);
    return 3;  // arg 3 is bad
  }


  pinMode(pin,OUTPUT);

  //disable ledc at pin.
  //if pin wasn't used before then it will be a error log message from
  //the IDF. Just ignore it
  ledcWriteTone(pin, 0);
  //ledcWriteTone(PIN,0) detaches pin?! TODO: check Arduino Core
  //ledcDetach(pin);

  if (freq) {
    if (ledcAttach(pin, freq, resolution) == 0) {
      log_printf(Failed);
      return 0;
    }
    // delay is required.
    // or ledcWriteTone may fail on a first call (ESP32-WROOM-32D)
    delay(100);
    ledcWriteTone(pin, freq);
    //if (duty != 0x1ff)
    ledcWrite(pin, duty);
  }

  return 0;
}

#include <soc/gpio_struct.h>
#include <hal/gpio_ll.h>
#include <driver/gpio.h>

extern gpio_dev_t GPIO;

static struct {
  uint8_t flags;
  uint16_t sig_out;
  uint16_t fun_sel;
} Pins[SOC_GPIO_PIN_COUNT];

static void pin_save(int pin) {

	bool pd,pu,ie,oe,od,slp_sel;
	uint32_t drv,fun_sel,sig_out;

	gpio_ll_get_io_config(&GPIO,pin,&pu, &pd, &ie, &oe, &od,&drv, &fun_sel, &sig_out, &slp_sel);

  Pins[pin].sig_out = sig_out;
  Pins[pin].fun_sel = fun_sel;

  Pins[pin].flags = 0;
  if (pu) Pins[pin].flags |= PULLUP;
  if (pd) Pins[pin].flags |= PULLDOWN;
  if (ie) Pins[pin].flags |= INPUT;
  if (oe) Pins[pin].flags |= OUTPUT;
  if (od) Pins[pin].flags |= OPEN_DRAIN;
  
  //TODO: save peripherial connections 
}

static void pin_load(int pin) {

  pinMode(pin,Pins[pin].flags);
  // TODO: restore perephireal connections (FUNC_SEL,)

}
//TAG:pin
// "pin NUM arg1 arg2 ... argN"
// "pin NUM"
static int cmd_pin(int argc, char **argv) {

  unsigned int flags = 0;
  int i = 2, level = -1, pin, read = 0, aread = 0;

  if (argc < 2)
    return -1;  //missing argument

  if (!isnum(argv[1]))  //first argument must be a decimal number
    return 1;

  pin = atoi(argv[1]);

  if (!pin_exist(pin))
    return 1;

  //two tokens: "pin NUM". Display pin configuration
  if (argc == 2) {

    level = digitalRead(pin);  //FIXME: check if pin is readable
    log_printf("%% Digital pin value = %d\n\r", level);

    gpio_dump_io_configuration(stdout, (uint64_t)1 << pin);  // works only on default UART
    return 0;
  }

  
  if (argc > 2) {

    // pin X seq Y
    if (!strcmp(argv[2], "seq")) {
      if (argc < 4)
        return -1;
      if (!isnum(argv[3]))
        return 3;
      int seq = atoi(argv[3]);
      if (seq < 0 || seq >= SEQUENCES_NUM)
        return 3;
      if (!seq_isready(seq)) {
        log_printf("%% Sequence %d is not fully configured\n\r", seq);
        return 0;
      }
      // enable RMT sequence 'seq' on pin 'pin'
      // TODO:
      log_printf("%% Sending sequence %d over GPIO %d\n\r", seq, pin);
      if (seq_send(pin, seq) < 0)
        log_printf(Failed);
      return 0;
    }
  }

  //more than 2 tokens: read all the options and set the parameters
  while (i < argc) {

    // execute parameters in sequence. this allows for reading and writing, saving
    // and restoring in one command:
    // Example: pin 2 save out high in read load
    if (!strcmp(argv[i], "save")) pin_save(pin);
    else if (!strcmp(argv[i], "load")) pin_load(pin);
    else if (!strcmp(argv[i], "up"))    { flags |= PULLUP; pinMode(pin, flags); }
    else if (!strcmp(argv[i], "down"))  { flags |= PULLDOWN; pinMode(pin, flags); }
    else if (!strcmp(argv[i], "open"))      { flags |= OPEN_DRAIN; pinMode(pin, flags); }
    else if (!strcmp(argv[i], "in"))        { flags |= INPUT; pinMode(pin, flags); }
    else if (!strcmp(argv[i], "out"))       { flags |= OUTPUT; pinMode(pin, flags); }
    else if (!strcmp(argv[i], "low"))       digitalWrite(pin, LOW);
    else if (!strcmp(argv[i], "high"))      digitalWrite(pin, HIGH);
    else if (!strcmp(argv[i], "read"))      log_printf("%% GPIO%d : logic %d\n\r", pin, digitalRead(pin) == HIGH ? 1 : 0);
    else if (!strcmp(argv[i], "aread"))     log_printf("%% GPIO%d : analog %d (%d mV)\n\r", pin, analogRead(pin), analogReadMilliVolts(pin));
    else
      return i;  // argument i was not recognized
    i++;
  }


  return 0;
}

//TAG:mem
//"mem"
static int cmd_mem(int argc, char **argv) {

  log_printf("%% Chip memory total: %lu, free: %lu\n\r"
             "%% External SPI RAM total:%luKB, free: %luKB\n\r",
             heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  return 0;
}


//TAG:nap
//"nap NUM"
//"nap"
static int cmd_nap(int argc, char **argv) {

  // plan "nap" command: sleep until we receive at least 3 positive edges on UART pin
  // (press any key for a wakeup)
  if (argc == 1) {
    esp_sleep_enable_uart_wakeup(rl_set_uart(-1));  //wakeup by uart
    uart_set_wakeup_threshold(rl_set_uart(-1), 3);  // 3 positive edges on RX pin to wake up ('spacebar' button two times)
  } else
    // "nap NUM" command: sleep NUM seconds. Wakeup by a timer
    if (argc == 2) {

      if (!isnum(argv[1]))  //arg1 must be a number
        return 1;

      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_UART);  //disable wakeup by uart
      esp_sleep_enable_timer_wakeup(1000000UL * (unsigned long)atoi(argv[1]));
    }

  log_printf("%% Light sleep..");
  esp_light_sleep_start();
  log_printf("Resuming\n\r");
  return 0;
}


//TAG:iic
//check if I2C has its driver installed
static inline bool i2c_isup(int iic) {

  extern bool i2cIsInit(uint8_t i2c_num);  //not a public API, no .h file
  return (iic < 0 || iic >= SOC_I2C_NUM) ? 0 : i2cIsInit(iic);
}



//"iic X"
// save context, switch command list, change the prompt
static int cmd_i2c_if(int argc, char **argv) {

  int iic;
  if (argc < 2)
    return -1;

  if (!isnum(argv[1]))
    return 1;

  iic = atoi(argv[1]);
  if (iic < 0 || iic >= SOC_I2C_NUM) {
    log_printf("%% Valid I2C interface numbers are 0..%d\n\r", SOC_I2C_NUM - 1);
    return 1;
  }

  Context = iic;
  keywords = keywords_i2c;
  prompt_old = prompt;
  prompt = PROMPT_I2C;

  return 0;
}

// same as the above but for uart commands
// "uart X"
static int cmd_uart_if(int argc, char **argv) {

  int u;
  if (argc < 2)
    return -1;

  if (!isnum(argv[1]))
    return 1;

  u = atoi(argv[1]);
  if (u < 0 || u >= SOC_UART_NUM) {
    log_printf("%% Valid UART interface numbers are 0..%d\n\r", SOC_UART_NUM - 1);
    return 1;
  }

  if (uart == u) {
    log_printf("%% You are configuring Serial interface shell is running on! BE CAREFUL :)\n\r");
  }
  Context = u;
  keywords = keywords_uart;
  prompt_old = prompt;
  prompt = PROMPT_UART;
  return 0;
}


//TAG:clock
static int cmd_i2c_clock(int argc, char **argv) {

  int iic = Context;

  if (argc < 2)
    return -1;

  if (!isnum(argv[1]))
    return 1;

  if (!i2c_isup(iic)) {
    log_printf("%% I2C %d is not initialized. use command \"up\" to initialize\n\r", iic);
    return 0;
  }

  if (ESP_OK != i2cSetClock(iic, atoi(argv[1])))
    log_printf(Failed);

  return 0;
}

//"iic NUM / up SDA SCL CLOCK"
//"iic NUM / down"
//"iic NUM / scan"
//"iic NUM / write ADDR A1 A2 A3 ... AN"
//"iic NUM / read ADDR NUM_BYTES"
#define IIC_MAX_TBUF 256  //max of 256 bytes as arguments to the "uart NUM write Arg1 [Arg2 .. Arg256]"

static int cmd_i2c(int argc, char **argv) {

  unsigned char iic, sda, scl;
  unsigned int clock = 0;
  int i;
  unsigned char addr;
  unsigned char data[IIC_MAX_TBUF];
  int size;

  iic = Context;

  //IIC UP
  if (!strcmp(argv[0], "up")) {

    if (argc < 4)
      return -1;

    if (i2c_isup(iic)) {
      log_printf("%% I2C%d is already initialized\n\r", iic);
      return 0;
    }


    if (!isnum(argv[1])) return 1;
    sda = atoi(argv[1]);
    if (!pin_exist(sda)) return 1;
    if (!isnum(argv[2])) return 2;
    scl = atoi(argv[2]);
    if (!pin_exist(scl)) return 2;
    if (!isnum(argv[3])) return 3;
    clock = atoi(argv[3]);

    if (ESP_OK != i2cInit(iic, sda, scl, clock))
      log_printf(Failed);
  } else if (!strcmp(argv[0], "down")) {
    if (!i2c_isup(iic))
      goto noinit;
    i2cDeinit(iic);
  } else if (!strcmp(argv[0], "write")) {  //write 4B 1 2 3 4

    // at least 1 but not more than 255 bytes
    if (argc < 3 || argc > sizeof(data))
      return -1;

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
    log_printf("%% Sending %d bytes over I2C%d\n\r", size, iic);
    if (ESP_OK != i2cWrite(iic, addr, data, size, 2000))
      log_printf(Failed);
  } else if (!strcmp(argv[0], "read")) {  //read 68 7

    size_t got;

    if (argc < 3)
      return -1;

    if (!ishex(argv[1]))
      return 1;

    addr = ascii2hex(argv[1]);

    if (addr < 1 || addr > 127)
      return 1;

    if (!isnum(argv[2]))
      return 2;

    size = atoi(argv[2]);

    if (size > sizeof(data)) {
      size = sizeof(data);
      log_printf("%% Max read size buffer is %d bytes\n\r", size);
    }

    if (i2cRead(iic, addr, data, size, 2000, &got) != ESP_OK)
      log_printf(Failed);
    else {
      log_printf("%% I2C%d received %d bytes:\n\r", iic, got);
      for (i = 0; i < got; i++)
        log_printf("%02X ", data[i]);
      log_printf("\n\r");
    }
  } else if (!strcmp(argv[0], "scan")) {
    if (!i2c_isup(iic)) {
      log_printf("%% I2C %d is not initialized\n\r", iic);
      return 0;
    }

    log_printf("%% Scanning I2C bus %d...\n\r", iic);

    for (addr = 1, i = 0; addr < 128; addr++) {
      char b;
      if (ESP_OK == i2cWrite(iic, addr, &b, 0, 500)) {
        i++;
        log_printf("%% Device found at address %02X\n\r", addr);
      }
    }

    if (!i)
      log_printf("%% Nothing found\n\r");
    else
      log_printf("%% %d devices found\n\r", i);
  } else return 2;

  return 0;
noinit:
  // love gotos
  log_printf("%% I2C %d is not initialized\n\r", iic);
  return 0;
}



#include <esp32-hal-uart.h>

//defined in HardwareSerial and must be kept in sync
//unfortunately HardwareSerial.h can not be included directly in a .c code
#define SERIAL_8N1 0x800001c

//check if UART has its driver installed
static inline bool uart_isup(int uart) {

  return (uart < 0 || uart >= SOC_UART_NUM) ? 0 : uart_is_driver_installed(uart);
}


//TAG:baud
static int cmd_uart_baud(int argc, char **argv) {

  int u = Context;

  if (argc < 2)
    return -1;

  if (!isnum(argv[1]))
    return 1;

  if (!uart_isup(u)) {
    log_printf("%% uart %d is not initialized. use command \"up\" to initialize\n\r", u);
    return 0;
  }

  if (ESP_OK != uart_set_baudrate(u, atoi(argv[1])))
    log_printf(Failed);

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

  size_t av;
  char buf[256];

  while (1) {

    // 1. read all user input and send it to remote
    while (1) {
      av = 0;
      // fails when interface is down. must not happen.
      if (ESP_OK != uart_get_buffered_data_len(uart, &av))
        break;

      // must not happen unless UART FIFO sizes were changed in ESP-IDF
      if (av > sizeof(buf)) {
        log_printf("%% RX buffer overflow\n\r");
        return;
      }

      //nothing to read?
      if (!av)
        break;

      uart_read_bytes(uart, buf, av, portMAX_DELAY);
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
        log_printf("%% UART%d is not initialized\n\r", remote);
        return;
      }
      if (av > sizeof(buf)) {
        log_printf("%% RX buffer overflow\n\r");
        return;
      }

      if (!av)
        break;

      uart_read_bytes(remote, buf, av, portMAX_DELAY);
      uart_write_bytes(uart, buf, av);
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

  int uart, sent = 0;


  uart = Context;

  // UART TAP
  if (!strcmp(argv[0], "tap")) {
    //do not tap to the same uart
    if (uart == rl_set_uart(-1)) {
      log_printf("%% Can not tap on itself\n\r");
      return 0;
    }

    if (!uart_isup(uart))
      goto noinit;

    log_printf("%% Tapping to UART%d, CTRL+C to exit\n\r", uart);
    uart_tap(uart);
    log_printf("\n\r%% Ctrl+C, exiting\n\r");
    return 0;
  } else if (!strcmp(argv[0], "up")) {  //up RX TX SPEED
    if (argc < 4)
      return -1;
    // uart number, rx/tx pins and speed must be numbers

    int rx, tx, speed;
    if (!isnum(argv[1])) return 1;
    else rx = atoi(argv[1]);
    if (!pin_exist(rx)) return 1;
    if (!isnum(argv[2])) return 2;
    else tx = atoi(argv[2]);
    if (!pin_exist(tx)) return 2;
    if (!isnum(argv[3])) return 3;
    else speed = atoi(argv[3]);  //TODO: check speed

    if (NULL == uartBegin(uart, speed, SERIAL_8N1, rx, tx, 256, 0, false, 112))
      log_printf(Failed);
  } else if (!strcmp(argv[0], "down")) {  // down
    if (!uart_isup(uart))
      goto noinit;
    else
      uartEnd(uart);
  } else if (!strcmp(argv[0], "write")) {  //write TEXT
    if (argc < 2)
      return -1;

    int i = 1, j;

    if (!uart_isup(uart))
      goto noinit;

    // go thru all the arguments and send them. the space is inserted between arguments
    do {
      char *p = argv[i];

      // char by char. parse escape sequences if any: \n \r \t \XY and \\ 
            // XY are hexadecimal characters
      // hexadecimals at the end of every token allowed to be in a short form as well: \X
      // valid: "uart 0 write Hello\20World!\9"
      // valid: "uart 0 write Hello\9 World"
      // invalid: "uart 0 write Hello\9World" (Must be \09)
      while (*p) {
        char c = *p;
        p++;
        if (c == '\\') {

          switch (*p) {
            case '\\':
              p++;
              c = '\\';
              break;
            case 'n':
              p++;
              c = '\n';
              break;
            case 'r':
              p++;
              c = '\r';
              break;
            case 't':
              p++;
              c = '\r';
              break;
            default:
              if (ishex(p))
                c = ascii2hex(p);
              else {
                log_printf("%% Unknown escape sequence: \"%s\"\n\r", p);
                return i;
              }
              p++;
              if (*p) p++;
          }
        }
        sent++;
        //TODO: check result, count succesfull writes, report if there were errors
        uart_write_bytes(uart, &c, 1);
      }

      i++;
      //if there are more arguments - insert a space
      if (i < argc) {
        char space = ' ';
        //TODO: check result
        uart_write_bytes(uart, &space, 1);
        sent++;
      }
    } while (i < argc);

    log_printf("%% %u bytes sent\n\r", sent);
  } else if (!strcmp(argv[0], "read")) {  // read
    size_t available = 0, tmp;
    if (ESP_OK != uart_get_buffered_data_len(uart, &available))
      goto noinit;
    tmp = available;
    while (available--) {
      unsigned char c;
      if (uart_read_bytes(uart, &c, 1, portMAX_DELAY /* TODO: make short delay! */) == 1) {
        if (c >= ' ' || c == '\r' || c == '\n' || c == '\t')
          log_printf("%c", c);
        else
          log_printf("\\x%02x", c);
      }
    }
    log_printf("\n\r%% %d bytes read\n\r", tmp);
  }

  // command executed or was not understood
  return 0;
noinit:
  log_printf("%% UART%d is not initialized\n\r", uart);
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
    log_printf("%% UART%d is not initialized\n\r", u);
  else {
#if WITH_HELP
    log_printf("%% See you on UART%d. Bye!\n\r", u);
#endif
    rl_set_uart(u);
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

//TAG:cpu
//"cpu"
//"cpu CLOCK"
//TODO: check the code on all ESP32 models, not just ESP32-WROOM-32D
//TODO: split to 2 functions: cmd_cpu & cmd_cpu_freq
static int cmd_cpu(int argc, char **argv) {

  esp_chip_info_t chip_info;

  uint32_t chip_ver;
  uint32_t pkg_ver;
  const char *chipid = "ESP32-(Unknown)>";

  //cpu FREQUENCY command
  if (argc > 1) {
    unsigned int freq = atoi(argv[1]);

    while (freq != 240 && freq != 160 && freq != 120 && freq != 80) {

      unsigned int xtal = getXtalFrequencyMhz();

      if ((freq == xtal) || (freq == xtal / 2)) break;
      if ((xtal >= 40) && (freq == xtal / 4)) break;
      log_printf("%% Supported frequencies are: 240, 160, 120, 80, ");
      if (xtal >= 40)
        log_printf("%u, %u and %u\n\r", xtal, xtal / 2, xtal / 4);
      else
        log_printf("%u and %u\n\r", xtal, xtal / 2);
      return 1;
    }

    if (!setCpuFrequencyMhz(freq))
      log_printf(Failed);

    return 0;
  }

  esp_chip_info(&chip_info);

#if CONFIG_IDF_TARGET_ESP32
  chip_ver = REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_PACKAGE);
  pkg_ver = chip_ver & 0x7;

  switch (pkg_ver) {

    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6: chipid = "ESP32-D0WD-Q6"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5: chipid = "ESP32-D0WD-Q5"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5: chipid = "ESP32-D2WD-Q5"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD2: chipid = "ESP32-PICO-D2"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4: chipid = "ESP32-PICO-D4"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302: chipid = "ESP32-PICO-V3-02"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3: chipid = "ESP32-D0WDR2-V3"; break;
    default: log_printf("%% Detected PKG_VER=%04x\n\r", pkg_ver);
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
#endif

  log_printf("%% CPU ID: %s, Rev.: %d.%d\n\r%% CPU %luMhz, Xtal %luMhz, Bus %luMhz, Temperature: %.1fC\n\r",
             chipid,
             (chip_info.revision >> 8) & 0xf,
             chip_info.revision & 0xff,
             getCpuFrequencyMhz(),
             getXtalFrequencyMhz(),
             getApbFrequency() / 1000000,
             temperatureRead());


  return 0;
}


//Time counter value (in seconds) right before entering
//main()/app_main()
static unsigned int uptime = 0;



// pre-main() hook
//
// the function gets called right before entering main()
// FreeRTOS is initialized but task scheduler is not started yet.
#if AUTOSTART
__attribute__((constructor)) static
#endif
  void
  espshell_start() {

  // save the counter value (seconds) on program start
  // must be 0 but once I saw strange glitch when the timer was not
  // reset on reboot resulting in a wrong "uptime" command values
  uptime = (uint32_t)(esp_timer_get_time() / 1000000);

  //initialize sequences
  seq_init();

  //start shell task.
  //task have to wait until Serial.begin() in order to start
  //reading and process user input
  espshell_task(PROMPT);
}

//TAG:uptime
// "uptime"
//
static int
cmd_uptime(int argc, char **argv) {

  uint32_t sec, min = 0, hr = 0, day = 0;
  sec = (uint32_t)(esp_timer_get_time() / 1000000) - uptime;

  log_printf("%% Last boot was ");
  if (sec > 60 * 60 * 24) {
    day = sec / (60 * 60 * 24);
    sec = sec % (60 * 60 * 24);
    log_printf("%u days ", day);
  }
  if (sec > 60 * 60) {
    hr = sec / (60 * 60);
    sec = sec % (60 * 60);
    log_printf("%u hours ", hr);
  }
  if (sec > 60) {
    min = sec / 60;
    sec = sec % 60;
    log_printf("%u minutes ", min);
  }

  log_printf("%u seconds ago\n\r", sec);

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

//TAG:resume
//"resume"
static int cmd_resume(int argc, char **argv) {

  vTaskResume(loopTaskHandle);

  return 0;
}


///////////////////////////////// PARSE AND EXECUTE
//
#define INDENT 8  //TODO: use a variable calculated at the shell task startup

//TAG:?
// "?"
// question mark command: display all commands available
// along with their brief description.
static int cmd_question(int argc, char **argv) {
#if WITH_HELP
  int i = 0;
  const char *prev = "";
  char indent[INDENT + 1];

  // user typed "? text" by mistake
  if (argc > 1) {
#if WITH_HELP
    log_printf("%% To get help try \"%s ?\" instead!\n\r", argv[1]);
    return 0;
#else
    // in no-help mode try to get a hint
    return 1;
#endif
  }

  // commands which are shorter than INDENT will be padded with extra
  // spaces to be INDENT bytes long
  memset(indent, ' ', INDENT);
  indent[INDENT] = 0;
  char *spaces;

  log_printf("%% Enter \"command ?\" to get details about the command.\n\r"
             "%% List of available commands:\n\r"
             "%% \n\r");

  //Run thru the keywords[] and print brief info for every command
  //For entries with the same base command only the first entry's description used
  //Entries with both help lines (help and brief) set to NULL are hidden commands
  while (keywords[i].cmd) {

    if (keywords[i].help || keywords[i].brief) {
      if (strcmp(prev, keywords[i].cmd)) {  // previous != current
        const char *brief;
        if (!(brief = keywords[i].brief))  //use "brief" or fallback to "help"
          if (!(brief = keywords[i].help))
            brief = "";

        // indent list: short commands are padded with spaces so
        // total length is always INDENT bytes. Commands which size
        // is bigger than INDENT will not be padded
        int clen;
        spaces = &indent[INDENT];  //points to \0
        if ((clen = strlen(keywords[i].cmd)) < INDENT)
          spaces = &indent[clen];
        log_printf("%% \"%s\"%s : %s\n\r", keywords[i].cmd, spaces, brief);
      }
    }

    prev = keywords[i].cmd;
    i++;
  }
#else
  argc = argc;
  argv = argv;
#endif
  return 0;
}


// Parse a string: split it to tokens, find an appropriate entry in keywords[] array
// and execute coresponding callback.
//
//TAG:shell
static void
espshell_command(char *p) {

  char **argv = NULL;
  int argc;
  int i = 0, found = 0;

  if (!p)
    return;

  //make a history entry
  rl_add_history(p);

  //tokenize. argv points inside p. original string is split by injecting '\0'
  // directly to the source string
  argc = argify((CHAR *)p, (CHAR ***)&argv);

  //argc must be at least 1 if tokenization was successful. the very first token is a command while
  //others are arguments. argc of zero means there was a memory allocation error, or input string was
  //empty/consisting completely of whitespace
  if (argc < 1)
    return;

  // process "?" argument to a  command: Ex.: "pin ?"
  if (argc > 1 && *(argv[1]) == '?') {
#if WITH_HELP
    // run thru keywords[] and print out "help" for every entriy.
    int cmd_len = strlen(argv[0]);
    while (keywords[i].cmd) {
      if (keywords[i].help || keywords[i].brief) {  //skip hidden commands
        if (strlen(keywords[i].cmd) >= cmd_len) {   // allow for partial name match
          if (!strncmp(keywords[i].cmd, argv[0], cmd_len)) {
            found = 1;
            if (keywords[i].help)
              log_printf("\n\r%s\n\r", keywords[i].help);  //display long help
            else
              log_printf("\n\r%s\n\r", keywords[i].brief);  //display brief (MUST not happen)
          }
        }
      }
      i++;
    }
    if (!found)
      goto notfound;
#endif
  } else {


    //process command
    //find a corresponding entry in a keywords[] : match the name and minimum number of arguments
    int cmd_len = strlen(argv[0]);

    while (keywords[i].cmd) {
      // command name match

      if (strlen(keywords[i].cmd) >= cmd_len) {
        if (!strncmp(keywords[i].cmd, argv[0], cmd_len)) {
          found = 1;
          // number of arguments match
          if (((argc - 1) == keywords[i].min_argc) || (keywords[i].min_argc < 0)) {

            // execute the command. if nonzero value is returned then it is an index of the "failed" argument
            // value of 3 means argv[3] was bad (for values > 0)
            // value of zero means successful execution
            int bad;
            if (keywords[i].cb) {
              //!!! handler MAY change keywords pointer! keywords[i] may be invalid pointer after
              // callback execute with return code 0
              bad = keywords[i].cb(argc, argv);
              if (bad > 0)
                log_printf("%% Invalid argument \"%s\" (\"%s ?\" for help)\n\r", argv[bad], argv[0]);
              else if (bad < 0)
                log_printf("%% Missing argument (\"%s ?\" for help)\n\r", argv[0]);
              else
                i = 0;  // make sure keywords[i] is valid pointer
              break;
            }  // if callback is provided
          }    // if argc matched
        }      // if name matched
      }        // if command length is ok
      i++;
    }

    // reached the end of the list and didn't find any exact match
    if (!keywords[i].cmd) {
      if (found)  //we had a name match but number of arguments was wrong
        log_printf("%% \"%s\": wrong number of arguments (\"%s ?\" for help)\n\r", argv[0], argv[0]);
      else
notfound:
        log_printf("%% \"%s\": command not found\n\r", argv[0]);
    }
#if WITH_HELP
    if (!found)
      log_printf("%% Type \"?\" to get the list of available commands\n\r");
#endif
  }

  // free the argv list
  if (argv)
    free(argv);

  free(p);
}

//TAG:task
// shell task
// only one shell task can be started
void espshell_task(const void *arg) {

  //start task and return
  if (arg) {
    TaskHandle_t h;
    if (pdPASS != xTaskCreate((TaskFunction_t)espshell_task, NULL, STACKSIZE, NULL, tskIDLE_PRIORITY, &h)) {
#if AUTOSTART
      // uart is still down when autostart==1
#else
      log_printf("%% espshell failed to start task\n\r");
#endif
    }
  } else {

    // wait until user code calls Serial.begin()
    // it is assumed that user console is using UART, not a native USB interface
    while (!uart_isup(uart))
      delay(1000);


#if WITH_HELP
    log_printf("%% ESP Shell. Type \"?\" and press enter for help\n\r");
#endif
    while (1)
      espshell_command(readline(prompt));
    delay(100);  //delay between commands to prevent flooding from
                  //failed readline() (if any)
  }
}
