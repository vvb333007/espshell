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

// Callback function type. This is the type of every command handler in the espshell. Command handlers are functions
// which implement espshell commands. Functions are scattered throughout the code but have distinct names: handler function
// name always starts with "cmd_" and continues with command name: for example, handler function for command "count" is cmd_count(),
// handler function for the command "up" in a subdirectory "wifi" is cmd_wifi_up()
//
typedef int (*cmd_handler_t)(int argc, char **argv);

// Shell command entry. There are arrays of these entries defined. Each array represents
// a command **subtree** (or subdirectory). Root (main) command tree is called keywords_main[]
// See keywords.h for the full tree of commands
//
struct keywords_t {
  const char *cmd;               // Command keyword or "*"

#define HELP_ONLY NULL,0             // keywords.h: used for entries whose sole purpose is to carry help lines: so-called /full/  and /brief/
  cmd_handler_t cb;  // Callback to call (one of cmd_xxx functions) TODO: make typedef, because it is used in userinput.c

#define MANY_ARGS -1                 // command accepts any number of arguments including zero
#define NO_ARGS 0                    // command accepts no arguments
  signed char argc;                  // Number of arguments required (a number, MANY_ARGS or NO_ARGS)

#define HIDDEN_KEYWORD NULL, NULL    // keywords.h: /help/ and /brief/ initializer which is used to hide command from the commands list

  const char *help;              // A help page for the command (multiline)
  const char *brief;             // Brief (1 line) description. If NULL, then ->help is used instead of ->brief
};

// HELP(...) and HELPK(...) macros: arguments are evaluated only when compiled WITH_HELP, otherwise 
// args evaluate to an empty code block (HELP) or to an empty string (HELPK)
//
#if WITH_HELP
#  define HELP(...) __VA_ARGS__
#  define HELPK(...) __VA_ARGS__
// Common commands inserted in every command tree at the beginning and HELP macro:
#  define KEYWORDS_BEGIN { "?", cmd_question, MANY_ARGS, \
                         "% \"? [<o>KEYWORD</>|<o>keys</>]\"\r\n" \
                         "%\r\n" \
                         "% Displays a <u>list of commands</> or shows <u>help page for a command</>:\r\n" \
                         "%\r\n" \
                         "% \"?\"         - Display a list of available commands\r\n" \
                         "% \"? <i>KEYWORD</>\" - Show the help page for the specified command\r\n" \
                         "% \"<i>KEYWORD</> ?\" - The question mark is used as a hot-key\r\n" \
                         "% \"? <i>keys</>\"    - Show information about terminal keys supported by ESPShell", \
                         "Commands list & help" }, \
                         { "help", cmd_question, MANY_ARGS, HIDDEN_KEYWORD }, //an alias for the "?" command.
#else // Help system disabled
#  define HELP(...) do { } while (0)
#  define HELPK(...) ""
#  define KEYWORDS_BEGIN
#endif

// Common commands that are inserted at the end of every command tree
#define KEYWORDS_END \
  { "exit", cmd_exit, MANY_ARGS, \
    HELPK("% \"<b>exit</> [<o>exit</>]\"  (Hotkey: Ctrl+Z)\r\n" \
          "% Exit from uart, i2c, wifi, filesystem etc configuration modes.\r\n" \
          "% Has no effect when executed in the main command mode unless typed twice\r\n" \
          "% (i.e. \"exit exit\"): in this case ESPShell closes and stops its task"), \
    HELPK("Exit") }, \
  /* Last entry must be all-zeros */\
  { \
    NULL, NULL, 0, NULL, NULL \
  }

// Return values from a command handler (functions whose name starts from "cmd_": cmd_pin, cmd_count, cmd_mount etc):
// 0  : successful operation
// >0 : command has failed. returned value is an index of a failed argument (0 < INDEX < argc). espshell_command() displays error text
// -1 : "not enough arguments". espshell_command() displays error text
// -2 : "other failure". command handler displays error text, ESPShell keeps silent.

#define CMD_SUCCESS      0    // unused. code uses "0" instead.
#define CMD_MISSING_ARG -1
#define CMD_FAILED      -2    // explanation is printed by handler, espshell_command() keeps silent
#define CMD_NOT_FOUND   -3    // no such command at all

// A macro to declare a keywords array:
// KEYWORDS_DECL(keywords_main) {
//  { ... },
//  { ... },
//  ...
// };
//
#define KEYWORDS_DECL(_Key) \
  static const struct keywords_t keywords_ ## _Key [] =

// Register keywords array (command directory) upon startup. This enables extra color marks for commands
// which are command directories. Registration is not mandatory.
//
static void keywords_register(const struct keywords_t *key, const char *name, int const count);

// Create template constructor function. These are called on startup
#define KEYWORDS_REG(_Key) \
 /* Each keywords array has its own constructor which registers this particular array */ \
  static void __attribute__((constructor)) __init_kwd_ ## _Key () { \
    keywords_register(keywords_ ## _Key, # _Key, sizeof(keywords_ ## _Key) / sizeof(keywords_ ## _Key[0])); \
  }

// Get pointer to a keywords array by its name: KEYWORDS(main), KEYWORDS(files) ...
#define KEYWORDS(_Key) \
  keywords_ ## _Key

// Set currently used keywords list by its name: keywords_set(main), keywords_set(files) ...
#define keywords_set(_Key) \
  keywords = KEYWORDS(_Key)

// Set currently used keywords list by ptr: keywords_set_ptr(KEYWORDS(main))
#define keywords_set_ptr(_Ptr) \
  keywords = _Ptr

// Get pointer to a currently used keywords array
#define keywords_get() \
  keywords

// Helper macro to make forward declarations for command handlers
//
#define has_handler(_Name) static int _Name(int argc, char **argv)



// Some stats on commands and command directories
// This array is populated by KEYWORDS_REG().
//
#define MAX_CMD_SUBDIRS 16       // Max number of command directories which can be registered. (including "main")

static struct {
  const struct keywords_t *key;  // pointer to keywords
  const char *name;              // directory name (e.g. "wifi", "uart" or "main")
  uint8_t count;                 // number of commands
} Subdirs[MAX_CMD_SUBDIRS] = { 0 };


// Current keywords list in use. It is initialized upon startup in espshell_initonce() / espshell.c
static _Thread_local const struct keywords_t *keywords;

// Two messages which must be defined here and can not be moved to the language definition files:
// thats why we inline translations here; TODO: refactor
//
#if WITH_LANG
static const char *Exit_message =   "% Не в каталоге команд; (чтобы закрыть шелл, введите \"exit ex\")\r\n";

static const char *Subdir_message = "%% Вход в режим %s. Ctrl+Z или \"exit\", чтобы выйти из режима\r\n"
                                    "%% Основные команды доступны, хоть и не отображаются по \"?\"\r\n";
#else
// Displayed when user tries to "exit" from the main commands directory.
static const char *Exit_message   = "% Not in a subdirectory; (to close the shell type \"exit ex\")\r\n";

// Displayed when user enters some command subdirectory
static const char *Subdir_message = "%% Entering %s mode. Ctrl+Z or \"exit\" to return\r\n"
                                    "%% Main commands are still available (but not visible in \"?\" command list)\r\n";
#endif


// Register a command tree. This one called by a C startup code as part of KEYWORDS_REG() macro
// well before app_main(), setup() or loop(). It could be called after startup to register additional
// command directories if required
//
static void keywords_register(const struct keywords_t *key, const char *name, int const count) {

  static unsigned char idx = 0;

#if WITH_DEVEL
  if (idx >= MAX_CMD_SUBDIRS) {
    esp_rom_printf("%% BOOM !!! Increase MAX_CMD_SUBDIRS value\r\n");
    abort();
  }
#endif  

  Subdirs[idx].key = key;
  Subdirs[idx].name = name;
  Subdirs[idx].count = count;
  idx++;
}

// Check, if given name is a command directory name. This check relies on subdirs registration:
// if a command directory chose not to register via KEYWORDS_REG macro then this directory becomes
// invisible for this function
//
static bool is_command_directory(const char *p) {

  if (p && *p) {
    int idx = 0;

    while(Subdirs[idx].name) {
      if (!q_strcmp(p, Subdirs[idx].name))
        return true;
      idx++;
    }
  }
  return false;
}
#endif // #if COMPILING_ESPSHELL

