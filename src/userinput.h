/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
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
// /gpp/ is the pointer to the command handler function. it is used by cmd_async() to execute commands in background
//
// Structure deallocated by userinput_unref()
typedef struct {
  int ref;          // reference counter. normally 1 but async commands can increase it
  int argc;         // number of tokens
  char **argv;      // tokenized input string (array of pointers to various locations withn userinput)
  char *userinput;  // original input string with '\0's inserted by tokenizer
  int (*gpp)(int, char **);
} argcargv_t;

// Mutex to protect reference counters of argcargv_t structure.
static MUTEX(argv_mux);

// Increase refcounter on argcargv structure. a == NULL is ok
static void userinput_ref(argcargv_t *a) {
  if (a) {
    mutex_lock(argv_mux);
    a->ref++;
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
      // ref dropped to zero: delete everything
    if (a->ref == 0) {
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
      a->argv = NULL;
      a->argc = argify((unsigned char *)userinput, (unsigned char ***)&(a->argv));
      if (a->argc > 0) {
        // successfully tokenized: we have at least 1 token (or more)
        // Keep /userinput/ : we have to free() it after command finishes its execution
        a->userinput = userinput;
        a->ref = 1;
        // Convert argv[0] (a command name) to lowercase to workaround some dumb terminals 
        q_tolower(a->argv[0],0);
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

// Redisplay user input & prompt. 
//
void userinput_redraw() {
  redisplay(); 
  TTYflush();
}
#endif //#if COMPILING_ESPSHELL


