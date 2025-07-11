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
// A sequence of shell commands with assigned name to it called a "command alias", It is a shortcut to execute 
// multiple commands by entering one new command.
//

#if COMPILING_ESPSHELL

// Helper macro for handlers (cmd_... ) : get a pointer to currently edited alias
#define ALIAS(_X) \
  struct alias *_X = ((struct alias *)Context); \
  MUST_NOT_HAPPEN(Context == 0)

// Aliases DB: SL list
static struct alias {
  struct alias *next;   // must be first field to be compatible with generic lists routines
  rwlock_t      rw;     // RW lock to protect /lines/ list
  argcargv_t *lines;    // actual alias content (a list of argcargv_t *)
  char name[0];         // asciiz alias name
} *Aliases = NULL;


// Add new "line" to the alias. By line we mean user input that was already processed to the form of argcargv_t
// We just store pointer to argcargv_t and increase its reference counter. We use internal argcargv's /->next/ field
// to link AA together
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

// lines in an alias are numbered from 1.
// line number 0 means "last line"
// line number -1 means "all lines"
static int alias_delete_line(argcargv_t **s, int nline) {

  int del = 0; // number of strings deleted
  if (s) {
    int i = 1;
    argcargv_t  *p = NULL,  // pointer to "prev"
                *curr = *s,   // currently proccessed line
                *tmp;
    while (curr) {

      if ( nline == i ||                    // exact line match, or
          (!nline && curr->next == NULL) ||   // last line, or
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

// "show alias NAME"
// "list"
//
static int alias_show_lines(argcargv_t *s) {
  int i = 0,j;
  const char *pre = "";
  for ( ; s; s = s->next) {
    q_printf("%% %u: %s%s", ++i, pre, s->argv[0]);
    for (j = 1; j < s->argc; j++) {
      q_print(" ");
      q_print(s->argv[j]);
    }
    q_print("\r\n");

    // Indent commands inside subdirectories. Only one level. Indent restored on "exit" command
    if (!q_strcmp(s->argv[0],"exit"))
      pre = "";
    else {
      const char *subd[] = {"alias", "iic", "uart", "sequence", "files", "spi", NULL };
      j = 0;
      while(subd[j]) {
        if (!q_strcmp(s->argv[0],subd[j]))
          break;
        j++;
      }
      if (subd[j] || // Any of command directories
          (s->argc > 1 && !q_strcmp(s->argv[0],"camera") && (!q_strcmp(s->argv[1],"settings")))) // "camera settings" directory
        pre = "  ";
    }
  }
  q_printf("%% %s\r\n", i ? "--- END ---" : "Empty.");
  
  return i;
}

struct alias *alias_by_name(const char *name) {
  struct alias *al = Aliases;
  if (name && *name)
    for (; al; al = al->next)
      if (!q_strcmp(name, al->name))
        break;
  return al;
}

// Create new, empty alias OR find existing one
//
struct alias *alias_create_or_find(const char *name) {

  struct alias *al;

  if (!name)
    return NULL;

  // addref on existing alias
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
  alias_show_lines(al->lines);
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
    } else
      q_print("% No aliases defined. Use \"alias NAME\" to create one\r\n");
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

// This one gets called whenever user issues commands in alias mode
//
static int cmd_alias_asterisk(int argc, char **argv) {
  ALIAS(al);
  

  MUST_NOT_HAPPEN(argc < 1);
  MUST_NOT_HAPPEN(AA == NULL);

  // NOTE: command "alias" itself is unavailable when in alias mode; allowing so may result in 
  // completely undefined behaviour because lack of locking mechanism
  if (!q_strcmp(argv[0],"alias")) {
    q_print("% Command \"alias\" can not be part of an alias, sorry.\r\n");
    return CMD_FAILED;
  }

  // Reset GPP. Right now it points to cmd_alias_asterisk()
  AA->gpp = NULL;
  
  rw_lockw(&al->rw);
  bool res = alias_add_line(&al->lines,AA);
  rw_unlockw(&al->rw);

  if (res)
    return 0;

  q_print("% Failed to add (out of memory)\r\n");
  return CMD_FAILED;
}

static int alias_exec(struct alias *al) {

    argcargv_t *p;
    
    rw_lockr(&al->rw);
    
    for (p = al->lines; p; p = p->next) {
      userinput_ref(p);
      // TODO:
      // Due to design limitations, espshell_command() manipulates argc values on live argcargv_t
      // and this can lead to weird behaviour:
      // 1. create an alias (lets name it "c" ) with a background command, e.g. "pin 2 toggle loop 80000 &"
      // 2. type "exec c c c c"
      // You will see that sometimes alias gets executed in foreground. This is a known bug and happens
      // because of shared argcargv_t, where argc changes forth and back. Deeper problem is that espshell_command()
      // alters arcargv_t while under ReadLock
      // Solutions is to NOT MODIFY argc and do not strip "&": it will also require small changes to "pin loop" logic
      espshell_command(NULL, p);
    }
    rw_unlockr(&al->rw);
    return 0;
}

// "exec ALIAS_NAME [ NAME2 NAME3 ... NAMEn]"
// Main command directory
//
static int cmd_exec(int argc, char **argv) {

  struct alias *al;

  if (argc < 2)
    return CMD_MISSING_ARG;

  for (int i = 1; i < argc; i++)
    if ((al = alias_by_name(argv[i])) != NULL)
      alias_exec(al);
    else
      q_printf("%% No alias named \"%s\"\r\n",argv[i]);
  
  return 0;
}
#endif // #if COMPILING_ESPSHELL

