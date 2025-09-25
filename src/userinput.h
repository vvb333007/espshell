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

#if COMPILING_ESPSHELL

// -- User input helper routines --
//
// Every time user inputs a string and presses <Enter> all its input gets tokenized
// and placed in argcargv_t structure. These structures normally persist only during command execution
// (either background or foreground commands)


// Structure representing a tokenized user input:
// /argc/ and /argv/ are amount of tokens and pointers to tokens respectively. /argv/ array consist of pointers
//                   pointing somewhere inside /userinput/
// /userinput/ is the raw user input with zeros inserted at whitespace positions by tokenizer
// /ref/ is the reference counter (to support background commands)
// /gpp/ is the pointer to the command handler function which supposed to be called (unly for background commands)
//
// Structure deallocated by userinput_unref()

struct argcargv {
  struct argcargv *next; // **logical** link: used by alias code to chain commands together. 
  short ref;             // reference counter. normally 1 but async commands can increase it. alias commands also increase this
  short argc;            // number of tokens after stripping "&" or alike
  uint8_t has_amp:1;     // command has "&" at the end?
  uint8_t unused:1;      
  uint8_t has_prio:1;
  uint8_t prio:5;        // task priority
  char **argv;           // tokenized input string (array of pointers to various locations withn /userinput/)
  char *userinput;       // original input string with '\0's inserted by tokenizer
  int (*gpp)(int, char **); //callback that is associated with argv[0] command.
};
typedef struct argcargv argcargv_t;

// Mutex to protect reference counters of argcargv_t structure.
// TODO: refactor to get rid of mutexes; make accesses lockless
static mutex_t argv_mux = MUTEX_INIT;

// Increase refcounter on argcargv structure. a == NULL is ok
// TODO: refactor to lockless
static void userinput_ref(argcargv_t *a) {
  if (a) {
    mutex_lock(argv_mux);
    a->ref++;
    //VERBOSE(q_printf("userinput_ref() : argcargv_t %p refcnt=%d\r\n",a, a->ref));
    mutex_unlock(argv_mux);
  }
}

// Decrease refcounter
// When refcounter hits zero the whole argcargv gets freed
// a == NULL is ok
//
static void userinput_unref(argcargv_t *a) {
  if (a) {
    mutex_lock(argv_mux);
    MUST_NOT_HAPPEN(a->ref < 1);
    a->ref--;
    // ref dropped to zero: delete everything
    if (a->ref == 0) {

      // array of pointers
      if (a->argv)
        q_free(a->argv);

      // user input (allocated by readline())
      if (a->userinput)
        q_free(a->userinput);

      // AA itself.
      // TODO: do not q_free(), but instead return to the pool of "free" entries
      //       where userinput_tokenize() can reuse them

      q_free(a);
    }
    mutex_unlock(argv_mux);
  }
}

// strip leading and trailing whitespace.
// argify() does this internally as well
//
static char *userinput_strip(char *p) {
  char *p0 = p, *p1 = p;
  if (p && *p) {
    int plen = strlen(p);
    
    while (--plen >= 0 && isspace(((unsigned char )(p[plen])))) // strip trailing whitespace
      p[plen] = '\0';
    
    while (*p && isspace(((unsigned char )(*p))))  // find where non-ws input starts ..
      p++;
    
    while (*p) *p0++ = *p++; //  .. from there shift whole string left, killing all leading whitespace

    // finalize string
    *p0 = '\0';
  }
  return p1;
}


// Split user input string to tokens.
// Returns NULL if string is empty or there were memory allocation errors.
// Returns pointer to allocated argcargv_t structure on success, which contains tokenized user input.
// NOTE: ** Structure must be freed after use by calling userinput_unref() **;
//
static argcargv_t *userinput_tokenize(char *userinput) {
  argcargv_t *a = NULL;
  // is user input non-empty string?
  if (userinput && *userinput) {

    // allocate argcargv
    if ((a = (argcargv_t *)q_malloc(sizeof(argcargv_t), MEM_ARGCARGV)) != NULL) {

      // use editline's argify() to extract tokens
      a->gpp = NULL; // this pointer can be reused to skip command lookup phase at certain conditions:
                     // aliases, as lists of precompiled argcargv_t's can update ->gpp on a first alias execution
                     // and use this value on subsequent calls to "exec alias"
      a->argv = NULL;
      a->argc = argify((unsigned char *)userinput, (unsigned char ***)&(a->argv));
      if (a->argc > 0) {
        // successfully tokenized: we have at least 1 token (or more)
        // Keep /userinput/ : we have to free() it after command finishes its execution
        a->userinput = userinput;
        a->ref = 1;
        a->next = NULL;
        a->has_amp  = 0;
        a->has_prio = 0;
        a->prio = 0;
        a->unused  = 0;

      } else {
        // Tokenization failed: either empty string or OOM event
        if (a->argv)
          q_free(a->argv);
        q_free(a);
        //q_free(userinput); It gets released by espshell_command() processor
        a = NULL;
      }
    }
  }
  return a;
}

// Display aa as a string
//
void userinput_show(argcargv_t *aa) {
  if (aa) {
    for (int i = 0; i < aa->argc; i++) {
      q_print(aa->argv[i]);
      q_print(" ");
    }
    
    if (aa->has_amp) {
      if (aa->has_prio)
        q_printf("&%u",aa->prio);
      else
        q_print("&");
    }
  }
}
#if 0
// Redisplay user input & prompt. 
// TODO: unused for now: causes glitches; have to dive deeper in editline lib
void userinput_redraw() {
  redisplay(); 
  TTYflush();
}
#endif

// Find corresponding command handler (cmd_..) for given argv[0]
// and put it to /aa->gpp/
//
// Returns 0 on success
//         CMD_NOT_FOUND on "no such command"
//         CMD_MISSING_ARG on "wrong number of arguments"
//
static int userinput_find_handler(argcargv_t *aa) {

  int i;
  bool found = false; // a candidate found (name match)

  // /keywords/ is an __thread  pointer to one of /keywords_main/, /keywords_uart/ ... etc keyword tables.
  // It points at main tree at startup and then can be switched. 
  const struct keywords_t *key = keywords_get();

  MUST_NOT_HAPPEN(aa == NULL);

one_more_try: // we get here if we wasn't able to find any suitable handler in a command subdirectory

  i = 0;                  // start from keyword #0
  
  // Find a key[] entry for a given command (argv[0])
  //
  // 1. Go through the keywords array till the end
  while (key[i].cmd) {

    // 2. Next keyword matches user input?
    // NOTE: A keyword that starts from "*" matches any user input. 
    //       This one is used in alias.h, to implement alias editing
    if (!q_strcmp(aa->argv[0], key[i].cmd) || key[i].cmd[0] == '*') {

      // 3. We have found a candidate (name match)
      found = true;

      // 4. Match number of arguments. There are many commands whose names are identical but number of arguments is different.
      // One special case is keywords with their /.argc/ set to -1: these are "any number of argument" commands. These should be positioned
      // carefully in keywords array to not shadow other entries which differs in number of arguments only
      if (((aa->argc - 1) == key[i].argc) || (key[i].argc < 0)) {

        // 5. We found the callback. It is only used when not NULL!
        // There are entries with /cb/ set to NULL: those are for help text only, as they are processed 
        // by "?" command/keypress
        if (key[i].cb) {
          
          aa->gpp = key[i].cb;
          //q_printf("\r\nFound %s -> %p\r\n",aa->argv[0],aa->gpp);
          return 0;

        }  // if callback is provided
      }    // if argc matched
    }      // if name matched
    i++;   // process next entry in key[]
  }        // until all key[] are processed

  // Reached the end of the list and didn't find any exact match?
  // Lets try to search in /keywords_main/ (if we are currently in a subdirectory)
  if (key != KEYWORDS(main)) {
    key = KEYWORDS(main);
    goto one_more_try;
  }

  // If we get here, then we have a problem:
  // 1. we had a name match but number of arguments was wrong
  // 2. no name match let alone arguments number
  return found ? CMD_MISSING_ARG
               : CMD_NOT_FOUND;
}



// Convert argc/argv e.g {10,seconds,20,days,48,hours" to microseconds by adding up all timespecs
// Any negative value turn whole result negative: "-1 hour 45 min" is "-105 min",   "1 hour -45 min" is the same
// In:
// /start/ is the index in argv of the first element to process (must be numeric)
// /stop/  is the pointer to the index, where to stop (index /stop/ is not processed).
//         value of -1 means (process till the end). Pointer can be NULL which implies processing till the end
//
// Out: /stop/ is populated with new index (in case of error new index is set to failed element)
//      Returned value is in microseconds.
//      Returned value of 0 must be treated as error.
//
// TODO: refactor "if", "nap" and "every" commands to use read_timespec
// TODO: rename to userinput_timespec
//
static int64_t read_timespec(int argc, char **argv, int start, int *stop) {

  int32_t t;
  uint64_t val = 0;
  int stop0 = -1;
  bool minus = false;

  if (!stop)
    stop = &stop0;

  for (; (start < argc) && (start != *stop); start++) {

    if (!q_isnumeric(argv[start])) {
      q_printf("%% Numeric value expected instead of \"%s\"\r\n",argv[start]);
      val = 0; //TODO: autostop on unknown token may be?
      break;
    }

    t = q_atoi(argv[start++],0);
    if (t < 0) {
      minus = true;
      t = -t;
    }

    if ((start >= argc) || (start == *stop)) {
      val += 1000000ULL * (uint64_t)t;
      break;
    }

    if (!q_strcmp(argv[start],"milliseconds"))
      val += 1000ULL * (uint64_t)t;
    else if (!q_strcmp(argv[start],"seconds"))
      val += 1000000ULL * (uint64_t)t;
    else if (!q_strcmp(argv[start],"minutes"))
      val += 1000000ULL * 60ULL * (uint64_t)t;
    else if (!q_strcmp(argv[start],"hours"))
      val += 1000000ULL * 60ULL * 60ULL * (uint64_t)t;
    else if (!q_strcmp(argv[start],"days"))
      val += 1000000ULL * 24ULL * 60ULL * 60ULL * (uint64_t)t;
    else {
      q_printf("%% Time specifier expected (e.g. \"days\", \"minutes\", \"millis\" ...)\r\n");
      val = 0;
      break;
    }
  }
  *stop = start;
  return minus ? -((int64_t)val) : (int64_t)val;
}

#if WITH_TIME

// "12:3" "01:02:33" "1:1:1"
//
static bool read_hms(const char *p, int8_t *h, int8_t *m, int8_t *s) {

  int8_t hms[3] = { 0 }, i = 0;

  while (*p) {
    if (*p == ':') {
      if (++i > 2)
        return false;
    } else if (*p >= '0' && *p <= '9')
      hms[i] = hms[i] * 10 + (*p - '0');
    else
      return false;
    ++p;
  }

  if (h) *h = &hms[0];
  if (m) *m = &hms[1];
  if (s) *s = &hms[2];

  return true;
}

//
// "1978"
// "31"
// "1978 31 april"
// "11:31:31 am"
// "11:31 april am 1978 25"
//
static time_t read_datime(int argc, char **argv, int start, int *stop) {

  uint32_t t;
  time_t val = 0;
  int stop0 = -1,v;
  int8_t am = -1; //1 = am, 0 = pm, -1 = n/a
  int8_t month;
  bool time_seen = false; // did we see HH:MM:SS already?

  struct tm t = { 0 };

  if (!stop)
    stop = &stop0;

  // Get local time and split it to /struct tm/
  now = time_local();
  gmtime_r(&now, &t);

  // Reset some fields
  t.tm_wday = -1;
  t.tm_yday = -1;
  t.tm_isdst = -1;

  // Scan through arguments, read values and populate /struct tm/
  for (; (start < argc) && (start != *stop); start++) {
    // Numeric argument: either a day-of-the-month or a year
    if (isnum(argv[start])) {
      // Read the number. No number can be less than 1.
      // Day number is in range [1..31] while year number is >1970
      if ((v = q_atoi(argv[start], -1)) < 1) {
        q_printf("%% Bad number, must be either [1..31] or [1970..9999]. (argument #%d)\r\n",start);
        goto bad;
      }
      if (v > 31 && v < 1970) {
        q_printf("%% Unrecognized number (argument #%d)\r\n",start);
        goto bad;
      }
      if (v < 32)
        t.tm_mday = v;
      else
        t.tm_year = v - 1900;
    } else 
    // No month has 'm' as its second letter, must be "am" or "pm"
    if (argv[start][1] == 'm') {
      
        if (argv[start][0] == 'a')
          am = 1;
        else if (argv[start][0] == 'p')
          am = 0;
        else {
          q_printf("%% Unknown token at position %d, expected \"am\" or \"pm\"\r\n",start);
          goto bad;
        }
    } else
    // a time? time can be in forms: hh:mm or hh:mm:ss
    if (argv[start][0] >= '0' && argv[start][0] <= '9') {
      int8_t h,m,s;
      if (read_hms(argv[start],&h,&m,&s)) {
        t.tm_hour = h;
        t.tm_min = m;
        t.tm_sec = s;
      } else {
        q_print("% Time is bad. Must be hh:mm or hh:mm:ss\r\n");
        goto bad;
      }
    } else
    // may be a month?
    if ((month = time_month_by_name(argv[start])) > 0) {
      tm.tm_mon = month - 1;
    } else 
    // unrecognized keyword
    {
      q_printf("%% Unrecognized keyword at position %d\r\n",start);
      goto bad;
    }
  }
  
  val = mktime(&tm);
bad:
  *stop = start;
  return val;
}
#endif


#endif //#if COMPILING_ESPSHELL


