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
  uint8_t has_core:1;      
  uint8_t has_prio:1;
  uint8_t reserved:5;
  uint8_t prio;          // task priority. only valid if has_prio is set
  uint8_t core;          // CPU core
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
    mutex_lock(argv_mux); // TODO: try to make lockless version
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
    mutex_unlock(argv_mux); // TODO: execute immediately after ref--
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
        a->has_core  = 0;
        a->core = 0;

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
static void userinput_show(argcargv_t *aa) {
  if (aa) {
    for (int i = 0; i < aa->argc; i++) {
      // TODO: quoted arguments will show up unqoted.
      q_print(aa->argv[i]);
      q_print(" ");
    }
    
    if (aa->has_amp) {
      q_print("&");
      if (aa->has_prio)
        q_printf("%u",aa->prio);
      if (aa->has_core)
        q_printf(".%u",aa->core);
    }
  }
}
#if 0
// Redisplay user input & prompt. 
// TODO: unused for now: causes glitches; have to dive deeper in editline lib
static void userinput_redraw() {
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
/////////////////////////////////////////////////////////////////////////////////////////
// Readers: read argumnents of a command in special formats like date, time, or read
// multiple arguments as a single continuos buffer
/////////////////////////////////////////////////////////////////////////////////////////


// Join Arguments: takes argc/argv and starting index to read arguments, starting
// from argv[start] till the end, joins them together and return a pointer to an allocated memory
// containing joined text. This function is used to join up argv's which are the same logical text.
// Alternatively one can use quotes
//
// "arg1" "arg2" "arg3" --> "arg1 arg2 arg3"
//
// Since all whitespace between arguments is lost due to argify(), this function can not recover
// multiple spaces:
//
// esp32#>write This        is     a TEXT
//
// will be read as "This is a TEXT". The only way to preserve repeating spaces is to use
// quotes, or encode spaces as \20
//
// /argc/ - number of elements available in argv[] array
// /argv/ - array of char * pointers to arguments
// /i/    - first argv to start collecting text from

// Returns number of bytes in buffer /*out/
// /out/  - allocated buffer, must be q_free()ed
//
static int userinput_join(int argc, char **argv, int i /* START */, char **out) {

  int size = 0, bsize = 0;
  char *b;

  if (i >= argc)
    return -1;

  // Rough estimate for the required buffer size
  // "buffer" --> 6 bytes required; "\01\02\03" --> 3 bytes required
  // Assume the worst case: no escapes, only characters. Multiple arguments are joined with speces inserted between them
  // thats one extra byte
  for (int j = i; j < argc; j++)
    bsize = bsize + strlen(argv[j]) + 1;

  // Extra bytes at the end to be able to insert \0
  bsize += 16;

  if ((*out = b = (char *)q_malloc(bsize, MEM_TEXT2BUF)) != NULL) {
    // go thru all the arguments and send them. the space is inserted between arguments
    do {
      char *p = argv[i];
      while (*p) {
        char c = *p;
        p++;
        // a backslash: escape sequence
        if (c == '\\') {
          switch (*p) {
            case '\\':             p++;              c = '\\';              break;
            case 'n':              p++;              c = '\n';              break;
            case 'r':              p++;              c = '\r';              break;
            case 't':              p++;              c = '\t';              break;
            case '"':              p++;              c = '"';               break;
            //case 'e':              p++;              c = '\e';            break;  //interferes with \HEX numbers
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
      // input line length limiting.
      // should not happen but just to be sure. bsize shoould be big enough
      if (size >= (bsize - 16))
        break;

    } while (i < argc);
  }
  return size;
}


// Convert argc/argv (e.g "10","seconds","20","days","48","hours" to microseconds by adding up all timespecs.
// Any negative value turn whole result negative: "-1 hour 45 min" is "-105 min",   "1 hour -45 min" is 
// the same. 
// Used to input a time interval: can be as simple as just a number: "21", which mean "25 seconds" or
// complex, like  "21 day 480 h 1 sec 25 millis"
//
// In:
// /start/ is the index in argv of the first element to process (that element must be numeric)
// /stop/  is the pointer to the index, where to stop (index /stop/ is not processed).
//         value of -1 means (process till the end). Pointer can be NULL which implies processing till the end
//
// Out: /stop/ is populated with index where processing stopped (i.e. element argv[stop] was not processed)
//      processing stops when next token can not be understood. E.g. for input "1 say 2 week 3 apples" processing stops 
//      on apples, stop==5
//      Returned value is in microseconds.
//      Returned value of 0 must be treated as error.
//
// TODO: refactor "if" and "every" commands to use userinput_read_timespec. Refactor show if/every also
//
static int64_t userinput_read_timespec(int argc, char **argv, int start, int *stop) {

  int32_t t;
  uint64_t val = 0;
  int stop0 = -1;
  bool minus = false;
  bool got_something = false;

  if (!stop)
    stop = &stop0;

  for (; (start < argc) && (start != *stop); start++) {

    if (!q_isnumeric(argv[start])) {
      if (!got_something)
        q_printf("%% Numeric value expected instead of \"%s\"\r\n",argv[start]);
      break;
    }

    t = q_atoi(argv[start++],0);
    if (t < 0) {
      minus = true;
      t = -t;
    }

    if (t != 0)
      got_something = true;

    // We have a number but no more input. Treat as seconds, so timespec like "3" will read as "3 seconds"
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
      break;
    }
  }
  *stop = start;
  return minus ? -((int64_t)val) : (int64_t)val;
}

#if WITH_TIME

// Convert string to hours/minutes/seconds. Omitted parameters are assumed to be zero.
// 24-hours format is assumed. Time string must have at least one ':' in it
//
// "12:3" "01:02:33" "1:1:1"
static bool userinput_read_hms(const char *p, int8_t *h, int8_t *m, int8_t *s) {

  bool colon_seen = false;
  int8_t hms[3] = { 0 }, i = 0;

  while (*p) {
    if (*p == ':') {
      colon_seen = true;
      if (++i > 2)
        return false;
    } else if (*p >= '0' && *p <= '9')
      hms[i] = hms[i] * 10 + (*p - '0');
    else
      return false;
    ++p;
  }

  if (!colon_seen || hms[0] > 23 || hms[1] > 59 || hms[2] > 59)
    return false;

  if (h) *h = hms[0];
  if (m) *m = hms[1];
  if (s) *s = hms[2];

  return true;
}

// Converts argc/argv, starting from index /start/, stopping at index /*stop/ to time_t
// Populates *stop with index where parsing stopped. /stop/ can be NULL
// Used to input time/date
// Examples of valid inputs:
// {"1978"},
// {"31"},
// {"1978","31","april"},
// {"11:31:31","am"},
// {"11:31","april","am","1978","25"},
//
// Returns (time_t)0 on error
//
static time_t userinput_read_datime(int argc, char **argv, int start, int *stop) {

  //uint32_t t;
  time_t val = 0;
  int stop0 = -1,v;
  bool pm = false, hour12 = false;
  int8_t month;
  int8_t h,m,s;
  bool hms_seen = false; // did we see HH:MM:SS already?

  struct tm t = { 0 };

  if (!stop)
    stop = &stop0;

  // Get local time and split it to /struct tm/
  time_t now = time(NULL);
  localtime_r(&now, &t);
  
  // Reset some fields
  t.tm_wday = -1;
  t.tm_yday = -1;
  t.tm_isdst = -1;

  // Scan through arguments, read values and populate /struct tm/
  for (; (start < argc) && (start != *stop); start++) {


    // 1. Numeric argument: either a day-of-the-month or a year
    if (isnum(argv[start])) {
      // Read the number. No number can be less than 1.
      // Day number is in range [1..31] while year number is >1970
      v = q_atoi(argv[start], 32);
      if (v > 31 && v < 1970) {
        q_printf("%% Days are [1..31], years are [1970..inf]. What is %s? \r\n",argv[start]);
        goto bad;
      }
      if (v < 32)
        t.tm_mday = v;
      else
        t.tm_year = v - 1900;
    } else 
    // 2. No month has 'm' as its second letter, must be "am" or "pm"!
    //    note that argv[1] is always references a valid allocated memory
    if (argv[start][1] == 'm') {
        hour12 = true;      
        if (argv[start][0] == 'a')
          pm = false;
        else if (argv[start][0] == 'p')
          pm = true;
        else {
          q_printf("%% Unknown token \"%s\", expected \"am\" or \"pm\"\r\n",argv[start]);
          goto bad;
        }
    } else
    // 3. A time? time can be in forms: hh:mm or hh:mm:ss, so first character is digit
    if (argv[start][0] >= '0' && argv[start][0] <= '9') {
      if (userinput_read_hms(argv[start],&h,&m,&s)) {
        hms_seen = true;
        t.tm_hour = h;
        t.tm_min = m;
        t.tm_sec = s;
      } else {
        q_printf("%% Can not recognize the input: \"%s\"\r\n",argv[start]);
        goto bad;
      }
    } else
    // 4. May be a month?
    if ((month = time_month_by_name(argv[start])) > 0) {
      t.tm_mon = month - 1;
    } else 
    // 5. Unrecognized keyword
    {
      q_printf("%% Unrecognized keyword \"%s\"\r\n",argv[start]);
      goto bad;
    }
  }

  // Convert to 24 if 12h format was used (i.e. "am" or "pm" keywords were seen)
  if (hms_seen && hour12) {
    
    if (t.tm_hour == 12)
      t.tm_hour = t.tm_hour + (pm ? 12 : 0);
    else if (pm)
      t.tm_hour = t.tm_hour + 12;
  }
  
  val = mktime(&t);
bad:
  *stop = start;
  return val;
}
#endif // WITH_TIME
#endif //#if COMPILING_ESPSHELL
