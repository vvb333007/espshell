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
  uint8_t bg_exec:1;     // enforce background execution despite of "&" symbol
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
// TODO: do not q_free(), but instead return to the pool of "free" entries
//       where userinput_tokenize() can reuse them
static void userinput_unref(argcargv_t *a) {
  if (a) {
    mutex_lock(argv_mux);
    MUST_NOT_HAPPEN(a->ref < 1);
    a->ref--;
    //VERBOSE(q_printf("userinput_unref() : argcargv_t %p refcnt=%d\r\n",a, a->ref));
      // ref dropped to zero: delete everything
    if (a->ref == 0) {
      //VERBOSE(q_printf("userinput_unref() : killing %p\r\n",a));
      if (a->argv)
        q_free(a->argv);
      if (a->userinput)
        q_free(a->userinput);
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
        a->bg_exec  = 0;

      } else {
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

// Redisplay user input & prompt. 
// TODO: unused for now: causes glitches; have to dive deeper in editline lib
void userinput_redraw() {
  redisplay(); 
  TTYflush();
}

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

  // /keywords/ is an _Atomic  pointer to one of /keywords_main/, /keywords_uart/ ... etc keyword tables.
  // It points at main tree at startup and then can be switched. 
  const struct keywords_t *key = keywords;   

  MUST_NOT_HAPPEN(aa == NULL);

one_more_try: // we get here if we wasn't able to find any suitable handler in a command subdirectory

  i = 0;                  // start from keyword #0
  
  // Find a keywords[] entry for a given command (argv[0])
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
    i++;   // process next entry in keywords[]
  }        // until all keywords[] are processed

  // Reached the end of the list and didn't find any exact match?
  // Lets try to search in /keywords_main/ (if we are currently in a subdirectory)
  if (key != keywords_main) {
    key = keywords_main;
    goto one_more_try;
  }

  // If we get here, then we have a problem:
  // 1. we had a name match but number of arguments was wrong
  // 2. no name match let alone arguments number
  return found ? CMD_MISSING_ARG
               : CMD_NOT_FOUND;
}

#endif //#if COMPILING_ESPSHELL


