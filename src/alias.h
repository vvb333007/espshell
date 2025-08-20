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
// A sequence of shell commands with assigned name to it called a "command alias", It is a shortcut to execute 
// multiple commands by entering one new command.
//
// Aliases are created but never destroyed: user can remove alias content, but alias descriptor itself will live
// forever. Reason for this is to keep pointers to aliases valid all the time. Other options (use sync objects, 
// mutexes) is not viable - we want FAST access to aliases, we want a pointer to the alias, not its name, nor we want mess
// with locking. Since alias' primary use is to be executed as part of "if" and "every" commands (interrupts!), its 
// processing must have minimal overhead
//
// Once created, aliases can be executed either with command "exec" or, as a part of an event 
// (see commands "if" and "every", ifcond.h)

#if COMPILING_ESPSHELL

// Helper macro for handlers (cmd_... ) : get a pointer to currently edited alias
// Pointer resides in the /Context/
#define ALIAS(_X) \
  struct alias *_X = context_get_ptr(struct alias); \
  MUST_NOT_HAPPEN(_X == NULL)

// Aliases database (a list):
//
static struct alias {
  struct alias *next;    // must be first field to be compatible with generic lists routines
  rwlock_t      rw;      // RW lock to protect /lines/ list
  argcargv_t   *lines;   // actual alias content (a list of argcargv_t *)
  char          name[0]; // asciiz alias name
} *Aliases = NULL;


// Add a new "line" to the alias. By line we mean user input that was already processed to the form of argcargv_t
// We just store pointer to argcargv_t and increase its reference counter. We use internal argcargv's /->next/ field
// to link argcargv_t structures together. 
//
static bool alias_add_line(argcargv_t **s,  argcargv_t *aa) {
  if (s && aa) {
    userinput_ref(aa);
    aa->next = NULL;
    // add to the end
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
    // TODO:2
    // "camera settings" is not indented because is_command_directory() only pays attention to the first element (i.e. "camera")
    // Plus to that, keywords are named "keywords_espcam", not "keywords_camera"
    // SOLUTION: rename keywords_espcam to keywords_camera and move all camera commands under the "camera" directory
    if (!q_strcmp(s->argv[0],"exit"))
      pre = "";
    else if (is_command_directory(s->argv[0]))
        pre = "  ";

  }
  q_printf("%% %s\r\n", i ? "--- END ---" : "Empty.");
  
  return i;
}

// Find an alias descriptor by alias name
//
//
struct alias *alias_by_name(const char *name) {
  struct alias *al = Aliases;
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
    if ((al = (struct alias *)q_malloc(sizeof(struct alias) + siz + 1, MEM_ALIAS)) != NULL) {
      strlcpy(al->name, name, siz + 1);
      al->lines = NULL;
      al->next = Aliases;
      rwlock_t tmp = RWLOCK_INIT;
      al->rw = tmp;
      Aliases = al;
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
      
        if ((al = alias_create_or_find(argv[1])) != NULL) {
          // Use NULL as /text/ to supress standart banner: it is incorrect for /alias/ command directory
          change_command_directory((typeof(Context))al, keywords_alias, PROMPT_ALIAS, NULL);
          HELP(q_print("% Entering alias editing mode. \"quit\" to return\r\n"));
          return 0;
        } else q_print("% Failed to create / find alias\r\n");
      
    } else q_print("% Either remove spaces from the name or use quotes\r\n");
  } else return CMD_MISSING_ARG;

  return CMD_FAILED;
}

// "quit" : replacement for "exit": command "exit" can belong to alias
//
static int cmd_alias_quit(int argc, char **argv) {
  ALIAS(al);
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
  ALIAS(al);
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
    if (Aliases) {
      q_print("% List of defined aliases:\r\n");
      for (i = 1, al = Aliases; al ; ++i, al = al->next)
        q_printf("%% %d. \"%s\"%s\r\n",i,al->name,al->lines ? "" : ", empty");
      HELP(q_print("% Use command \"<i>show alias NAME</>\" to display alias content\r\n"));
    } else
      HELP(q_print("% No aliases defined. Use \"<i>alias NAME</>\" to create one\r\n"));
    return 0;
  } else {
    al = alias_by_name(argv[2]);
    if (al) {
      rw_lockr(&al->rw);
      alias_show_lines(al->lines);
      rw_unlockr(&al->rw);
    }
    else
      q_printf("%% Unknown alias \"%s\" (\"show alias\" to list names)\r\n",argv[2]);
    return CMD_FAILED;
  }

  return 0;
}

// Delete lines (commands) from alias:
// Last line, specific line or all lines
// "delete [all|NUMBER]"
//
static int cmd_alias_delete(int argc, char **argv) {
  ALIAS(al);
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

  // Fetch pointer to the alias we are editing
  ALIAS(al);

  MUST_NOT_HAPPEN(argc < 1);
  MUST_NOT_HAPPEN(AA == NULL); // Set by espshell_command()

  // NOTE: The "alias" command is unavailable while in alias mode. 
  // Allowing it could result in completely undefined behavior due to the lack of a locking mechanism.

  if (!q_strcmp(argv[0],"alias")) {
    q_print("% Command \"alias\" can not be part of an alias, sorry.\r\n");
    return CMD_FAILED;
  }

  // Reset GPP: Right now it points to cmd_alias_asterisk()
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
  // TODO:1 refactor, probably a racecond. Add a parameter to userinput_find_handler() 
  //       to specify search directories, don't modify global /keywords/ variable!
  const struct keywords_t *tmp = keywords;
  keywords = keywords_main;
  userinput_find_handler(AA);
  keywords = tmp;

  //q_printf("\r\nPrecached handler %p\r\n",AA->gpp);

  rw_lockw(&al->rw);
  bool res = alias_add_line(&al->lines,AA);
  rw_unlockw(&al->rw);

  if (res)
    return 0;

  q_print("% Failed to save command (out of memory?)\r\n");
  return CMD_FAILED;
}

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

    // delay, if required (see alias_exec_in_background_delayed())
    if (ha->delay_ms)
      q_delay(ha->delay_ms);
    alias_exec(ha->al);
    ha_put(ha);
  }
  // prompt must be unchanged at this point, because change_command_directory() changes prompt
  // for foreground tasks only. Background commands can not change prompt.

  task_finished();
}

// Execute alias as if it was with "&" symbol at the end 
#define alias_exec_in_background(_Alias) \
          alias_exec_in_background_delayed(_Alias, 0)

static int alias_exec_in_background_delayed(struct alias *al, uint32_t delay_ms) {
  struct helper_arg *ha;

  if ((ha = ha_get()) != NULL) {

    ha->al = al;
    ha->delay_ms = delay_ms;
    // Context & Keywords are per-thread variables!
    // Make local copy of the context & keywords values. Spawned task will initialize its
    // own Context and keywords variables from these values.
    ha->context = context_get_uint();
    ha->keywords = keywords_get();
    //ha->prompt = prompt;

    return task_new(alias_helper_task,
                    ha,
                    al->name) == NULL ? CMD_FAILED : 0;
  }
  return CMD_FAILED;
}

// "exec ALIAS_NAME [ NAME2 NAME3 ... NAMEn]"
// Main command directory
// TODO: "exec /file_name"
static int cmd_exec(int argc, char **argv) {

  struct alias *al;

  if (argc < 2)
    return CMD_MISSING_ARG;

  for (int i = 1; i < argc; i++)
    if ((al = alias_by_name(argv[i])) != NULL)
      alias_exec(al);
    else
      q_printf("%% \"%s\" : no such alias\r\n",argv[i]);
  
  return 0;
}
#endif // #if COMPILING_ESPSHELL

