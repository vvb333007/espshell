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
  short ref;          // reference counter. normally 1 but async commands can increase it. alias commands also increase this
  short argc;         // number of tokens after stripping "&" or alike
  short argc0;        // raw number of tokens
  char **argv;      // tokenized input string (array of pointers to various locations withn userinput)
  char *userinput;  // original input string with '\0's inserted by tokenizer
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
      a->argc = a->argc0 = argify((unsigned char *)userinput, (unsigned char ***)&(a->argv));
      if (a->argc > 0) {
        // successfully tokenized: we have at least 1 token (or more)
        // Keep /userinput/ : we have to free() it after command finishes its execution
        a->userinput = userinput;
        a->ref = 1;
        a->next = NULL;
        //VERBOSE(q_printf("userinput_tokenize() : created argcargv_t %p\r\n",a));
        // Convert argv[0] (a command name) to lowercase to workaround some dumb terminals 
//        q_tolower(a->argv[0]);

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
  }
}

// Redisplay user input & prompt. 
// TODO: unused for now: causes glitches; have to dive deeper in editline lib
void userinput_redraw() {
  redisplay(); 
  TTYflush();
}
#endif //#if COMPILING_ESPSHELL


