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

#define PERMANENT_ALIASES 1 // Never delete aliases, as we use direct pointers to /struct alias/. Keep it 1.
#define MAX_ALIAS_LEN 31 // Maximum strlen()+1 of an alias name

// Helper macro for handlers (cmd_... ) : get a pointer to currently edited alias
#define ALIAS(_X) \
  struct alias *_X = ((struct alias *)Context); \
  MUST_NOT_HAPPEN(Context == 0)

static struct alias {
  struct alias *next;       // must be first field to be compatible with generic lists routines
  char name[MAX_ALIAS_LEN]; // asciiz
  unsigned char ref;        // reference counter
  argcargv_t *lines;    // actual alias content (a list of argcargv_t *)
} *Aliases = NULL;          // aliases db

#if 1
 // static MUTEX(Alias_mux);
# define alias_lock() mutex_lock(Alias_mux)
# define alias_unlock() mutex_unlock(Alias_mux)
#else
  static BARRIER(Alias_mux);
# define alias_lock() barrier_lock(Alias_mux)
# define alias_unlock() barrier_unlock(Alias_mux)
#endif


// Add new string to the alias
// String is already parsed by espshell_command() so we just store
// pointer to argcargv_t and increase its reference counter
//
static bool alias_add_line(argcargv_t **s,  argcargv_t *aa) {
  if (s && aa) {
      userinput_ref(aa);
      aa->next = NULL;
      // add to the end
      if (*s == NULL)
        *s = aa;
      else for (argcargv_t *p = *s; p; p = p->next) {
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

// barrier_lock() must be called prior to calling this
static int alias_show_lines(argcargv_t *s) {
  int i = 0,j;
  for ( ; s; s = s->next) {
    q_printf("%% %u: %s", ++i, s->argv[0]);
    for (j = 1; j < s->argc; j++) {
      q_print(" ");
      q_print(s->argv[j]);
    }
    q_print("\r\n");
  }
  if (!i)
    q_print("% Empty\r\n");
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


static void alias_unlink_and_free(struct alias *al) {
#if PERMANENT_ALIASES
  al->ref = 1;
  //alias_delete_line(&al->lines,-1);
#else  
  struct alias *prev;
  if (al) {
    for (prev = Aliases; prev; prev = prev->next)
      if (prev->next == al)
        break;

      if (prev)
        prev->next = al->next;
      else if (Aliases == al)
        Aliases = al->next;

      alias_delete_line(&al->lines,-1);
      q_free(al);
  }
#endif // PERMANENT_ALIASES  
}

// Reference counter--
// When refcounter drops to zero, the alias gets removed
//
static void alias_unref(struct alias *al) {
  if (al) {
    MUST_NOT_HAPPEN(al->ref < 1);
    if (--al->ref < 1) {
      
      alias_unlink_and_free(al);
    }
  }
}

// Reference counter++
//
static void alias_ref(struct alias *al) {
  if (al) {
    al->ref++;
    MUST_NOT_HAPPEN(al->ref == 0); // unsigned char overflow
  }
}

// Create new, empty alias OR find existing one
//
struct alias *alias_create_or_find(const char *name) {

  struct alias *al;

  // addref on existing alias
  if ((al = alias_by_name(name)) == NULL)
    if ((al = (struct alias *)q_malloc(sizeof(struct alias), MEM_ALIAS)) != NULL) {
      strlcpy(al->name, name, sizeof(al->name));
      al->lines = NULL;
      al->next = Aliases;
      al->ref = 1;
      Aliases = al;
    }

  if (al)
    alias_ref(al);

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
      if (strlen(argv[1]) < MAX_ALIAS_LEN) {
        if ((al = alias_create_or_find(argv[1])) != NULL) {
          // Use NULL as /text/ to supress standart banner: it is incorrect for /alias/ command directory
          change_command_directory((typeof(Context))al, keywords_alias, PROMPT_ALIAS, NULL);
          HELP(q_print("% Entering alias editing mode. \"quit\" to return\r\n"));
          return 0;
        } else q_print("% Failed to create / find alias\r\n");
      } else q_print("% Alias name must be short (max. " xstr(MAX_ALIAS_LEN) " characters)\r\n");
    } else q_print("% Either remove spaces from the name or use quotes\r\n");
  } else return CMD_MISSING_ARG;

  return CMD_FAILED;
}

// "quit" : replacement for "exit". there are at least 2 reasons to use "quit" instead of "exit":
// 1. command "exit" can belong to alias
// 2. we have to execute extra code (i.e. alias_unref()) before calling cmd_exit()
//
static int cmd_alias_end(int argc, char **argv) {
//  int n;
  ALIAS(al); // "al" points to  "struct alias"
  
  // refcounter at this point is usually 2 but can be 1 if some background async process has
  // executed alias_unref() on its own.
//  n = al->ref;
  alias_unref(al);

#if 0
  // if alias is still alive, check if it is empty. If it is -> unref it one more time, to trigger
  // alias removal. 
  if (n > 1)
    if (al->lines == NULL)
      alias_unref(al);
#endif  
  return cmd_exit(argc,argv);
}

// "list"
// "show alias [NAME]"
static int cmd_alias_list(int argc, char **argv) {
  ALIAS(al);
  q_printf("%% Alias \"%s\":\r\n",al->name);
  alias_show_lines(al->lines);
  return 0;
}

static int cmd_show_alias(int argc, char **argv) {

  struct alias *al;

  if (argc < 3) {
    if (Aliases) {
      q_print("% List of defined aliases:\r\n");
      for (al = Aliases; al ; al = al->next)
        q_printf("%% \"%s\"%s\r\n",al->name,al->lines ? "" : ", empty");
    } else
      q_print("% No aliases defined\r\n");
    return 0;
  } else {
    al = alias_by_name(argv[2]);
    if (al)
      alias_show_lines(al->lines);
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
  alias_delete_line(&al->lines, argc > 1 ? q_atoi(argv[1],-1)
                                         : 0);
  return 0;
}

// This one gets called whenever user issues commands in alias mode
//
static int cmd_alias_asterisk(int argc, char **argv) {
  ALIAS(al);
  int i;

  MUST_NOT_HAPPEN(argc < 1);
  MUST_NOT_HAPPEN(AA == NULL);

  // NOTE: command "alias" itself is unavailable when in alias mode; allowing may result in 
  // completely undefined behaviour because lack of locking mechanism
  if (!q_strcmp(argv[0],"alias")) {
    q_print("% Command \"alias\" can not be part of an alias, sorry.\r\n");
    return CMD_FAILED;
  }
  
  if (alias_add_line(&al->lines,AA))
    return 0;

  q_print("% Failed to add (out of memory)\r\n");
  return CMD_FAILED;
}

#endif // #if COMPILING_ESPSHELL

