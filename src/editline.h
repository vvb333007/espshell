/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#if COMPILING_ESPSHELL


#define MEM_INC 64   // generic  buffer realloc increments
#define MEM_INC2 16  // dont touch this

#define SCREEN_INC 256  // "Screen" buffer realloc increments

#define DISPOSE(p) q_free((char *)(p))
#define NEW(T, c, Typ) ((T *)q_malloc((unsigned int)(sizeof(T) * (c)), Typ))
#define RENEW(p, T, c) (p = (T *)q_realloc((char *)(p), (unsigned int)(sizeof(T) * (c)), MEM_EDITLINE))
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
typedef enum { CSdone,       // Line is ready to be processed, user pressed <Enter>
               CSeof,        // Must not happen. 
               CSmove,       // Move cursor
               CSdispatch,   //
               CSstay,       // Don't move cursor
               CSsignal      // Not used
              } EL_STATUS;

//  Key to command mapping.
typedef struct {
  unsigned char Key;
  EL_STATUS(*Function)();
} KEYMAP;


// Command history storage for HIST_SIZE unique commands
// TODO: rewrite "scrolling" routine: we don't have to scroll whole history to add a new entry
static struct {
  signed short Size;
  signed short Pos;
  unsigned char *Lines[HIST_SIZE];
} H;

static portMUX_TYPE Input_mux = portMUX_INITIALIZER_UNLOCKED;
static const char *CRLF = "\r\n";
static unsigned char *Line = NULL;  // Raw user input
static const char *Prompt = NULL;   // Current prompt to use
static char *Screen = NULL;

static int Repeat;
static int End;
static int Mark;
static int OldPoint;
static int Point;
static int PushBack;
static int Pushed;
static const char *Input = "";  // "Artificial input queue". if non empty then symbols are
                                // fed to TTYget as if it was user input. used by espshell_exec()
static unsigned int Length;
static unsigned int ScreenCount;
static unsigned int ScreenSize;
static bool History = true;

static unsigned char *editinput();

// brother of help_command() but accepts plaintext buffer (non-tokenized)
static bool help_page_for_inputline(unsigned char *raw);

static EL_STATUS ring_bell();
static EL_STATUS ctrlz_pressed();
static EL_STATUS ctrlc_pressed();
static EL_STATUS tab_pressed();
static EL_STATUS home_pressed();
static EL_STATUS end_pressed();
static EL_STATUS kill_line();
static EL_STATUS enter_pressed();
static EL_STATUS left_pressed();
static EL_STATUS del_pressed();

static EL_STATUS right_pressed();
static EL_STATUS backspace_pressed();
static EL_STATUS bk_kill_word();
static EL_STATUS bk_word();

static EL_STATUS h_next();
static EL_STATUS h_prev();
static EL_STATUS h_search();
static EL_STATUS redisplay();
static EL_STATUS clear_screen();
static EL_STATUS meta();

static const KEYMAP Map[] = {
  //Key       Callback                 Action
  { CTL('C'), ctrlc_pressed },      // "suspend" commands
  { CTL('Z'), ctrlz_pressed },      // "exit" command
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
  if (ScreenCount && (Echo > 0)) {
    console_write_bytes(Screen, ScreenCount);
    ScreenCount = 0;
  }
}

// queue===============================================================================================================to be printed
static void
TTYput(unsigned char c) {

  Screen[ScreenCount] = c;
  if (++ScreenCount >= ScreenSize - 1) {
    ScreenSize += SCREEN_INC;
    RENEW(Screen, char, ScreenSize);
  }
}

//queue a string to be printed
static inline void
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
static inline void
TTYstring(unsigned char *p) {
  while (*p)
    TTYshow(*p++);
}

//read a character from user.
//
static unsigned int
TTYget() {
  unsigned char c = 0;

  // print all queued symbols (if any) before we block in console_read_bytes()
  TTYflush();

  if (Pushed) {
    Pushed = 0;
    return PushBack;
  }

  do {
    // read byte from a priority queue if any.
    portENTER_CRITICAL(&Input_mux);
    if (*Input) c = *Input++;
    portEXIT_CRITICAL(&Input_mux);
    if (c) return c;

    // read 1 byte from user. we use timeout to enable Input processing if it was set
    // mid console_read_bytes() call: i.e. Input polling happens every 500ms
    // TODO: figure out something better instead of polling. May be use task notifications
  } while (console_read_bytes(&c, 1, pdMS_TO_TICKS(500)) < 1);

#if WITH_COLOR
  // Trying to be smart when coloring mode is set to "auto" (default behaviour):
  // If we receive lower keycodes (arrow keys, ESC secuences, Ctrl+KEY codes) from user that means his/her terminal 
  // is not an Arduino IDE Serial Monitor (or alike, primitive terminal program) so we can enable ESPShell colors
  if (!Color && ColorAuto && c < ' ' && c != '\n' && c != '\r' && c != '\t')
      Color = true;
#endif
  return c;
}

// print a backspace symbol, to move cursor 1 char left
#define TTYback() TTYput('\b')

// n backspaces
static inline void
TTYbackn(int n) {
  while (--n >= 0)
    TTYback();
}

// redraw current input line
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
left(EL_STATUS Change) {
  TTYback();
  if (Point) {
    if (ISCTL(Line[Point - 1]))
      TTYback();
  }
  if (Change == CSmove)
    Point--;
}

// move cursor right, draw corresponding symbol
static void
right(EL_STATUS Change) {
  TTYshow(Line[Point]);
  if (Change == CSmove)
    Point++;
}

// stub function which rings a bell (if your terminal software permits it. TeraTerm doesn't)
static EL_STATUS
ring_bell() {
  TTYput('\07');
  TTYflush();
  return CSstay;
}

// Ctrl+Z hanlder: send "exit" commnd
static EL_STATUS
ctrlz_pressed() {
  TTYqueue("exit\n");
  return CSstay;
}

// Ctrl+C handler: sends "suspend"
static EL_STATUS
ctrlc_pressed() {
  TTYqueue("suspend\n");
  return CSstay;
}


static EL_STATUS
do_forward(EL_STATUS move) {
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

static EL_STATUS
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

static EL_STATUS
redisplay() {
  const unsigned char *nl = (const unsigned char *)"\r\n";
  TTYputs(nl);
  TTYputs((const unsigned char *)Prompt);

  TTYstring(Line);
  return CSmove;
}


static EL_STATUS
do_insert_hist(unsigned char *p) {
  if (p == NULL)
    return ring_bell();
  Point = 0;
  reposition();
  ceol();
  End = 0;
  return insert_string(p);
}

static EL_STATUS
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

static unsigned char *next_hist() {
  return H.Pos >= H.Size - 1 ? NULL : H.Lines[++H.Pos];
}
static unsigned char *prev_hist() {
  return H.Pos == 0 ? NULL : H.Lines[--H.Pos];
}

static EL_STATUS h_next() {
  return do_hist(next_hist);
}
static EL_STATUS h_prev() {
  return do_hist(prev_hist);
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
    old_search = (unsigned char *)q_strdup((char *)search, MEM_EDITLINE);
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
static EL_STATUS h_search() {

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

static EL_STATUS
right_pressed() {
  int i = 0;
  do {
    if (Point >= End)
      break;
    right(CSmove);
  } while (++i < Repeat);
  return CSstay;
}


static EL_STATUS
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

static EL_STATUS
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

// called by Ctrl+L
// clears terminal window by ansi sequence, displays TipOfTheDay
//
static const char *random_hint();

static EL_STATUS
clear_screen() {
  q_print("\033[H\033[2J");
#if WITH_HELP
  q_printf("%% Tip of the day:\r\n%s\r\n", random_hint());
#endif
  return redisplay();
}

static EL_STATUS
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

static EL_STATUS
insert_char(int c) {
  EL_STATUS s;
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
static EL_STATUS
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

static EL_STATUS
emacs(unsigned int c) {
  EL_STATUS s;
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

static int bypass_qm = 0;  //TODO: register variable

static EL_STATUS
TTYspecial(unsigned int c) {
  if (ISMETA(c))
    return CSdispatch;

  if (c == DEL)
    return del_pressed();

  if ((c == '?') && !bypass_qm) {
    if (help_page_for_inputline(Line) == true)
      return redisplay();
  }


  if (c == 0 && Point == 0 && End == 0)  //TODO: investigate CSeof and CSsignal
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
      case CSdone: return Line;

      case CSeof: return NULL;

      case CSsignal: return (unsigned char *)"";

      case CSmove: reposition(); break;

      case CSdispatch:
            switch (emacs(c)) {
              case CSdone: return Line;
              case CSeof: return NULL;
              case CSsignal: return (unsigned char *)"";
              case CSmove: reposition(); break;
              case CSdispatch:
              case CSstay: break;
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
  // TODO: investigate
  q_free(Line);
  q_print("Wow\r\n");
  return (Line = NULL);
}

static void
hist_add(unsigned char *p) {
  int i;

  if ((p = (unsigned char *)q_strdup((char *)p, MEM_HISTORY)) == NULL)
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

  hist_add(nil);  //TODO: how does it work?!

  ScreenSize = SCREEN_INC;
  Screen = NEW(char, ScreenSize, MEM_TMP);

  TTYputs((const unsigned char *)(Prompt = prompt));
  TTYflush();

  if ((line = editinput()) != NULL) {
    const unsigned char *nl = (const unsigned char *)"\r\n";
    line = (unsigned char *)q_strdup((char *)line, MEM_EDITLINE);
    TTYputs(nl);
    TTYflush();
  }

  DISPOSE(Screen);
  DISPOSE(H.Lines[--H.Size]);

  return (char *)line;
}


// Add an arbitrary string p to the command history.
// Repeating strings are discarded
//
static void history_add_entry(char *p) {
  if (p && *p)
    if (!H.Size || (H.Size && strcmp(p, (char *)H.Lines[H.Size - 1])))
      hist_add((unsigned char *)p);
}



static EL_STATUS
del_pressed() {
  return delete_string(Repeat == NO_ARG ? 1 : Repeat);
}

static EL_STATUS
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

static EL_STATUS
home_pressed() {
  if (Point) {
    Point = 0;
    return CSmove;
  }
  return CSstay;
}

static EL_STATUS
end_pressed() {
  if (Point != End) {
    Point = End;
    return CSmove;
  }
  return CSstay;
}

static EL_STATUS
enter_pressed() {
  Line[End] = '\0';

#if WITH_COLOR
  // user has pressed <Enter>: set colors to default
  if (Color) TTYputs((const unsigned char *)"\033[0m");
#endif
  return CSdone;
}

static EL_STATUS
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

static EL_STATUS
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
  if ((*avp = p = NEW(unsigned char *, i, MEM_ARGIFY)) == NULL)
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

          _new = NEW(unsigned char *, i + MEM_INC2, MEM_ARGIFY);

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
#endif // #ifdef COMPILING_ESPSHELL
