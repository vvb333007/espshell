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

// -- Command Aliases --
//
// A "command alias" is a named sequence of shell commands. 
// It acts as a shortcut that lets you run multiple commands by entering a single new command.
//
// Aliases are created but never destroyed: an user can clear the contents of an alias, but the alias 
// descriptor itself will remain permanently. 

// This ensures that pointers to aliases always stay valid. 
// Alternative approaches (such as using synchronization objects or mutexes) are not suitable here, 
// because we need FAST access to aliases. We want direct pointers to the alias structures, 
// not just their names, and we want to avoid the overhead of locking. 
// Since aliases are primarily used within "if" and "every" commands (interrupt-driven execution), 
// their handling must be as lightweight as possible.
//
// Once created, aliases can be executed either with the "exec" command 
// or as part of an event (see "if" and "every" in ifcond.h).
//
// THREAD SAFETY:
// 1. Pointers to aliases are persistent, and whole list of aliases is an _Atomic pointer
// 2. Pointer to lines (alias->lines) is not persistent and must be checked for NULL value. Access to lines
//    is protected by alias' RWlock (see e.g. alias_is_empty() )
//
// TODO: "goto [+|-]NUM|end" command  (only available within an alias) - go to specified line,+-lines, last line

#if COMPILING_ESPSHELL
#if  WITH_ALIAS

// Helper macro for handlers (cmd_... ) : get a pointer to currently edited alias
// Pointer resides in the /Context/
#define THIS_ALIAS(_X) \
  struct alias *_X = context_get_ptr(struct alias); \
  MUST_NOT_HAPPEN(_X == NULL)

// Aliases database (a list):
// Elements of the list are never deleted: one can not delete an alias, only its content can be deleted
// As a result - we don't need any locking here. Insertion to the list are made "to the head", thats why
// /Aliases/ is an _Atomic pointer
//
struct alias {
  struct alias *next;    // must be first field to be compatible with generic lists routines
  rwlock_t      rw;      // RW lock to protect /lines/ list
  argcargv_t   *lines;   // actual alias content (a list of argcargv_t *) TODO: make it _Atomic to simplify alias_is_empty()
  char          name[0]; // asciiz alias name
};

static _Atomic(struct alias *) Aliases = NULL;

// Check if alias is empty.
// NOTE: calls rw_lock/unlock
//
static bool alias_is_empty(struct alias *al) {
  bool is_empty = true;
  if (likely(al != NULL)) {
    rw_lockr(&al->rw);
    is_empty = al->lines == NULL; // TODO: replace with atomic_load() once al->lines become _Atomic
    rw_unlockr(&al->rw);
  }
  return is_empty;
}
// Add a new "line" (aa) to the alias. 
// Here, a "line" refers to user input that has already been processed into an argcargv_t structure. 
// We simply store a pointer to the argcargv_t and increment its reference counter. 
// The internal argcargv->next field is used to link multiple argcargv_t structures together within the alias.
//
static bool alias_add_line(argcargv_t **s,  argcargv_t *aa) {
  if (s && aa) {
    userinput_ref(aa);
    aa->next = NULL;
    // add to the end, because we need all commands to be in the same
    // order user entered them
    if (*s == NULL)
      *s = aa;
    else 
      for (argcargv_t *p = *s; p; p = p->next) {
        if (p->next == NULL) {
          p->next = aa;
          break;
        }
      }

    return true;
  }
  return false;
}

// Lines in an alias are numbered from 1 and up.
// When deleting, 
// line number 0 means "last line in the alias" 
// line number -1 means "all lines in the alias"
// TODO:3 change 0 and -1, because -1 looks more natural when deleting the last line
//
static int alias_delete_line(argcargv_t **s, int nline) {

  int del = 0; // number of strings deleted
  if (s) {
    int i = 1;
    argcargv_t  *p = NULL,  // pointer to "prev"
                *curr = *s,   // currently proccessed line
                *tmp;
    while (curr) {

      if ( nline == i ||                    // exact line match, or
          (!nline && curr->next == NULL) || // last line, or
          nline < 0) {                      // every line

          // Unlink /curr/
          if (p)
            tmp = p->next = curr->next;
          else
            tmp = *s = curr->next;

          userinput_unref(curr);
          del++;
          // Unless nline is negative we must exit here 
          curr = nline < 0 ? tmp : NULL;
      } else {
        p = curr;
        curr = curr->next;
        i++;
      }
    }
  }

  return del;
}

// Displays alias content
// /s/ is either pointer to AA or NULL
static int alias_show_lines(argcargv_t *s) {
  int i = 0;
  const char *pre = "";
  for ( ; s; s = s->next) {

    q_printf("%% %3u: %s", ++i, pre);
    userinput_show(s);
    q_print(CRLF);

    // Indent commands inside subdirectories. Only one level. Indent is restored on "exit" command
    if (!q_strcmp(s->argv[0],"exit"))
      pre = "";
    else if (is_command_directory(s->argv[0]))
        pre = "  ";

  }
  q_printf("%% %s\r\n", i ? "--- END ---" : "Empty.");
  
  return i;
}

// Find an alias descriptor by alias name
// Lockless version
//
struct alias *alias_by_name(const char *name) {
  struct alias *al = atomic_load_explicit(&Aliases, memory_order_acquire);
  // once al is loaded it is safe to walk through the list even if it is being modified: modification happens
  // only to the /Aliases/ variable itself, no existing ->next pointers are modified
  if (likely(name && *name))
    for (; al; al = al->next)
      //if (!q_strcmp(name, al->name))
      if (!strcmp(name, al->name)) // can't use loose strcmp here: test and test2 would match
        break;
  return al;
}

// Create new, empty alias OR find existing one
//
struct alias *alias_create_or_find(const char *name) {

  struct alias *al;

  if (!name)
    return NULL;

  if ((al = alias_by_name(name)) == NULL) {
    size_t siz = strlen(name);
    // allocate alias and its name buffer
    if ((al = (struct alias *)q_malloc(sizeof(struct alias) + siz + 1, MEM_ALIAS)) != NULL) {
      strlcpy(al->name, name, siz + 1);
      al->lines = NULL;
      rwlock_t tmp = RWLOCK_INITIALIZER_UNLOCKED;
      al->rw = tmp;
      // insert into the list head, lockless version
      do {
        al->next = atomic_load_explicit(&Aliases, memory_order_relaxed);
      } while(!atomic_compare_exchange_strong_explicit( &Aliases, &al->next, al, memory_order_release, memory_order_relaxed));
    }
  }

  return al;
}


//"alias NAME"
// Create/find an alias, set pointer to that alias as a Context, 
// switch command list, change the prompt
//
static int cmd_alias_if(int argc, char **argv) {
  struct alias *al;

  if (argc > 1) {
    if (argc < 3) {
      // "exec" uses '/' to distinguish between alias names and file names, so '/' is forbidden
      // in alias name (as a first symbol only. "/name" is not ok, "n/ame" is ok)
      if (argv[1][0] == '/') {
        HELP(q_print("% \"/\" is not allowed as a first symbol of the alias name\r\n"));
        return CMD_FAILED;
      }

      if ((al = alias_create_or_find(argv[1])) != NULL) {
        // Use NULL as /text/ to supress standart banner: it is incorrect for /alias/ command directory.
        // Set /Context/ to be an alias pointer: alias pointers are peristent
        change_command_directory((typeof(Context))al, KEYWORDS(alias), PROMPT_ALIAS, NULL);
        HELP(q_print("% Entering alias editing mode. \"quit\" to return\r\n"));
        return 0;
      } else 
        q_print("% Failed to create / find alias\r\n");
    } else
      q_print("% Either remove spaces from the name or use quotes\r\n");
  } else
    return CMD_MISSING_ARG;

  return CMD_FAILED;
}

// "quit" : replacement for "exit": command "exit" can belong to alias
//
static int cmd_alias_quit(int argc, char **argv) {
  THIS_ALIAS(al); // Fetch pointer to the alias we are editing
  if (!al->lines && (al->rw.sem != SEM_INIT)) {
    sem_destroy(al->rw.sem);
    al->rw.sem = SEM_INIT;
    VERBOSE(q_printf("%% Alias \"%s\" is empty, destroying semaphore\r\n",al->name));
}
  return cmd_exit(argc,argv);
}

// "list"
//
static int cmd_alias_list(int argc, char **argv) {
  THIS_ALIAS(al); // Fetch pointer to the alias we are editing
  q_printf("%% Alias \"%s\":\r\n",al->name);
  rw_lockr(&al->rw);
  alias_show_lines(al->lines);
  rw_unlockr(&al->rw);
  return 0;
}

// "show alias [NAME]"
//
static int cmd_show_alias(int argc, char **argv) {

  struct alias *al;
  int i;

  if (argc < 3) {
    if ((al = atomic_load_explicit(&Aliases, memory_order_acquire)) != NULL) {
      q_print("% List of defined aliases:\r\n");
      for (i = 1; al != NULL ; ++i, al = al->next)
        q_printf("%% %d. \"%s\"%s\r\n",i,al->name,al->lines ? "" : ", empty");
      HELP(q_print("% Use command \"<i>show alias NAME</>\" to display alias content\r\n"));
    } else
      HELP(q_print("% No aliases defined. (\"<i>alias NAME</>\" to create one)\r\n"));
    return 0;
  } else {
    al = alias_by_name(argv[2]);
    if (al) {
      rw_lockr(&al->rw);
      alias_show_lines(al->lines);
      rw_unlockr(&al->rw);
    }
    else
      q_printf("%% Unknown alias \"%s\" (\"<i>show alias</>\" to list names)\r\n",argv[2]);
    return CMD_FAILED;
  }

  return 0;
}

// Delete lines (commands) from alias:
// Last line, specific line or all lines
// "delete [all|NUMBER]"
//
static int cmd_alias_delete(int argc, char **argv) {
  THIS_ALIAS(al); // Fetch pointer to the alias we are editing
  rw_lockw(&al->rw);
  alias_delete_line(&al->lines, argc > 1 ? q_atoi(argv[1],-1)
                                         : 0);
  rw_unlockw(&al->rw);                                         
  return 0;
}

// This one gets called whenever user issues commands in alias mode:
// each of such commands is stored into alias /lines/ list
// The only commands which are not stored, but processed instead are "list", "quit", "delete"
//
static int cmd_alias_asterisk(int argc, char **argv) {
  
  THIS_ALIAS(al); // Fetch pointer to the alias we are editing

  MUST_NOT_HAPPEN(argc < 1);
  MUST_NOT_HAPPEN(AA == NULL); // Set by espshell_command()

  // NOTE: The "alias" command is unavailable while in alias mode. 
  // Allowing it could result in completely undefined behavior due to the lack of a locking mechanism.

  if (!q_strcmp(argv[0],"alias")) {
    q_print("% Command \"alias\" can not be part of an alias, sorry.\r\n");
    return CMD_FAILED;
  }

  // Reset GPP: Right now it points to cmd_alias_asterisk() (i.e. this function)
  // AA global variable is created and maintained by espshell_command(); its only use is to temporary hold currently
  // processing argcargv_t.
  // This pointer *must not* be used outside of this (alias editing) scope
  AA->gpp = NULL;

  // Precache the command handler. Precaching will save time on first time alias is executed. Once alias was 
  // executed it remembers associated command handler to skip handler search process for subsequent execs (aliases usually
  // executed more than once)
  //
  // If handler can't be found - that means command has typos in it OR it was a command from a 
  // subdirectory: we don't track directories. In such cases command handler is not precached and will be found on
  // a first alias use
  //
  // TODO: KNOWN BUG: if command is a subdir command (say wifi's "up") it will be wrongly precached as the "uptime".
  // This happens because precacher does not keep track of the currently used keywords array: keywords are always searched from the keywords_main.
  // If we have "wifi sta" and "up" in our alias, the precacher does not know that command "up" must be searched within "keywords_sta".
  //
  //                  solution1: add "!" flag to the command: "don't precache"
  //
  // TODO: Add a parameter to userinput_find_handler() to specify search directories
  //
  const struct keywords_t *tmp = keywords_get();
  keywords_set(main); // sets thread-specific copy, thread-safe
  userinput_find_handler(AA);
  keywords_set_ptr(tmp);

  //q_printf("\r\nPrecached handler %p\r\n",AA->gpp);

  rw_lockw(&al->rw);
  bool res = alias_add_line(&al->lines,AA);
  rw_unlockw(&al->rw);

  if (res)
    return 0; // Success

  q_print("% Failed to save command (out of memory?)\r\n");
  return CMD_FAILED;
}

// TODO:"esp32-alias>save /FILENAME"
// TODO: "esp32>alias NAME|* save /FILENAME"

// Execute an alias: lock it for reading, go  through stored argcargv_t lists
// and send them to the command processor. We do increment line's refcount before calling espshell_command()
// because espshell_command() decrements it
//
static int alias_exec(struct alias *al) {

  int ret = 0;
  argcargv_t *p;
    
  rw_lockr(&al->rw);
    
  for (p = al->lines; p; p = p->next) {
    userinput_ref(p);                   // espshell_command() does unref()
    if (espshell_command(NULL, p) != 0) {// execute
      ret = CMD_FAILED; // if there were errors during execution, signal it as generic error:
                        // no point in returning real code as it makes sence only for a particular argcargv, not for command "exec"
      HELP(q_printf("%% Alias \"%s\" execution was interrupted because of errors\r\n",al->name));
      break;
    }
  }
  rw_unlockr(&al->rw);
  return ret;
}

// The task which executes aliases in a background. It is called from alias_exec_in_background()
// usually in response to ifcond/every events. Ordinary bg commands are executed via exec_in_background()
//
static void alias_helper_task(void *arg) {
  struct helper_arg *ha = (struct helper_arg *)arg;

  // TODO: copy ha, do ha_put() asap
  if (likely(ha)) {

    // set our "global" variables (inherited from the spawning task)
    context_set(ha->context);
    keywords_set_ptr(ha->keywords);
    files_set_cwd(ha->cwd);
    if (ha->cwd)
      q_free(ha->cwd); // it was strdup()ed in ha_get()

    // delay, if required
    if (ha->delay_ms)
      q_delay(ha->delay_ms);
    alias_exec(ha->al);
    ha_put(ha);
  }
  
  task_finished();
}

// Execute alias as if it was with "&" symbol at the end 
#define alias_exec_in_background(_Alias) \
          alias_exec_in_background_delayed(_Alias, 0)

static int alias_exec_in_background_delayed(struct alias *al, uint32_t delay_ms) {
  struct helper_arg *ha;

  if (likely(al != NULL))
    if ((ha = ha_get()) != NULL) {
      // ha->aa = NULL; maybe?
      ha->al = al;
      ha->delay_ms = delay_ms;
      return task_new(alias_helper_task,
                      ha,
                      al->name,
                      shell_core) == NULL ? CMD_FAILED : 0;  // TODO: let the user choose the core or tskNO_AFFINITY
    }
  return CMD_FAILED;
}
#endif // WITH_ALIAS

// "exec NAME [ NAME2 NAME3 ... NAMEn]"
// "import NAME [ NAME2 NAME3 ... NAMEn]"
// Execute files or/and aliases.
// To specify a filename, this function must be called by the "import" command or NAME must start with "/"
// (otherwise there is no way to determine if NAME is an alias name or a file name)
//
static int cmd_exec(int argc, char **argv) {

  int errors = 0;
  bool import = false;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[0],"import"))
    import = true;

  for (int i = 1; i < argc; i++) {
    // exec: file starts with /, aliases - not.
    // import: only files, no aliases
    if (argv[i][0] == '/' || import) {
#if WITH_FS
//      char *p = files_full_path(argv[i], PROCESS_ASTERISK);
//      if (files_path_exist_file(p)) {
        // files_exec() call full_path internally
        if (files_exec(argv[i]) != 0)
          errors++;
//      } else {
//        q_printf("%% \"Can't read <i>%s</>\". Is filesystem mounted?\r\n",p);
//        errors++;
//      }
#else
      HELP(q_print("% No support for filesystems was compiled in\r\n"
                   "% Edit the espshell.h and set WITH_FS to \"1\"\r\n");
#endif      
    } else {
#if WITH_ALIAS      
      struct alias *al;
      if ((al = alias_by_name(argv[i])) != NULL) {
        if (alias_exec(al) != 0)
          errors++;
      } else {
        q_printf("%% \"%s\" : no such alias\r\n",argv[i]);
        errors++;
      }
#else
      HELP(q_print("% No support for aliases was compiled in\r\n"
                   "% Edit the espshell.h and set WITH_ALIAS to \"1\"\r\n");
#endif      
    }
  }
  
  return errors ? CMD_FAILED : 0;
}
#endif // #if COMPILING_ESPSHELL
