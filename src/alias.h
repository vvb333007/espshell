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

#define MAX_ALIAS_LEN 31 // Maximum strlen() of an alias name

#define ALIAS(_X) \
  struct alias *_X = ((struct alias *)Context); \
  MUST_NOT_HAPPEN(Context == 0)

// Commands (asciiz strings) which are attached to an alias
// are stored in the /strings/ structure 
struct strings {
  struct strings *next;  // must be first field to be compatible with generic lists routines
  unsigned short len;    // strlen(/line/)
  char line[0];          // asciiz. size == strlen(line) + 1 (trailing zero)
};

static struct alias {
  struct alias *next;       // must be first field to be compatible with generic lists routines
  char name[MAX_ALIAS_LEN]; // asciiz
  unsigned char ref;        // reference counter
  struct strings *lines;    // actual alias content
} *Aliases = NULL;          // aliases db

#if NOT_YET
// TODO: consider background commands manipulating aliases
static MUTEX(Alias_mux); // One big global lock for everything about aliases.
#endif


// Add new string to the alias
//
static bool alias_add_line(struct strings **s, const char *line) {
  if (s && line && *line) {

    struct strings *n;
    size_t siz = strlen(line);

    if (siz >= ESPSHELL_MAX_INPUT_LENGTH)
      return false;

    if ((n = (struct strings *)q_malloc(sizeof(struct strings) + siz + 1, MEM_ALIAS)) != NULL) {
      strcpy(n->line, line);
      n->len = siz;
      n->next = NULL;
      // add to the end
      if (*s == NULL)
        *s = n;
      else for (struct strings *p = *s; p; p = p->next) {
        if (p->next == NULL) {
          p->next = n;
          break;
        }
      }
      return true;
    }
  }
  return false;
}

// lines in an alias are numbered from 1.
// line number 0 means "last line"
// line number -1 means "all lines"
static int alias_delete_line(struct strings **s, int nline) {

  int del = 0; // number of strings deleted
  if (s) {
    int i = 1;
    struct strings *p = NULL,  // pointer to "prev"
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

          q_free(curr);
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
static int alias_show_lines(struct strings *s) {
  int i = 0;
  for ( ; s; s = s->next)
    q_printf("%% %u: %s\r\n", ++i, s->line);
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
}

// Reference counter--
// When refcounter drops to zero, the alias gets removed
//
static void alias_unref(struct alias *al) {
  if (al) {
    MUST_NOT_HAPPEN(al->ref < 1);
    if (--al->ref < 1)
      alias_unlink_and_free(al);
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
          HELP(q_print("% Entering alias editing mode. \"end\" to return\r\n"));
          return 0;
        } else q_print("% Failed to create / find alias\r\n");
      } else q_print("% Alias name must be short (max. " xstr(MAX_ALIAS_LEN) " characters)\r\n");
    } else q_print("% Either remove spaces from the name or use quotes\r\n");
  } else return CMD_MISSING_ARG;

  return CMD_FAILED;
}

// "end" : replacement for "exit". Command "exit" can be used inside aliases, 
// so instead we introduce "end" command
//
static int cmd_alias_end(int argc, char **argv) {
  ALIAS(al);
  alias_unref(al);
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


static int cmd_alias_asterisk(int argc, char **argv) {
  ALIAS(al);
  int i;
  // TODO: what if there were quoted arguments? quotes will be removed by tokenizer. 
  //       Must restore quotes around arguments having spaces in it
  size_t siz = strlen(argv[0]);
  for (i = 1; i < argc; i++)
    siz += (1 + strlen(argv[i]));
  if (siz >= ESPSHELL_MAX_INPUT_LENGTH) {
    q_print("% Command line too long\r\n");
    return CMD_FAILED;
  }
  // TODO: Code below must be refactored to get rid of strcats. It is **too straightforward** and is very slow
  char tmp[siz + 1];
  strcpy(tmp,argv[0]);
  for (i = 1; i < argc; i++) {
    strcat(tmp, " ");
    strcat(tmp, argv[i]);
  }
  if (alias_add_line(&al->lines,tmp))
    return 0;

  q_print("% Failed to add\r\n");
  return CMD_FAILED;
}

#endif // #if COMPILING_ESPSHELL

