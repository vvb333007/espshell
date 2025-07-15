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

//
// Shell command entry. There are arrays of these entries defined. Each array represents
// a command **subtree** (or subdirectory). Root (main) command tree is called keywords_main[]
struct keywords_t {
  const char *cmd;               // Command keyword or "*"

#define HELP_ONLY NULL,0             // Used for entries whose sole purpose is to carry help lines: so-called /full/  and /brief/
  int (*cb)(int argc, char **argv);  // Callback to call (one of cmd_xxx functions)

#define MANY_ARGS -1                 
#define NO_ARGS 0
  signed char argc;              // Number of arguments required (a NUMBER or /MANY_ARGS/ or /NO_ARGS/)

#define HIDDEN_KEYWORD NULL, NULL// /help/ and /brief/ initializer which is used to hide command from the commands list
  const char *help;              // Help text displayed on "? command"
  const char *brief;             // Brief text displayed on "?". NULL means "use help text, not brief"
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
          "% Exit from uart, i2c, spi, files etc configuration modes.\r\n" \
          "% Has no effect when executed in main command mode unless typed twice\r\n" \
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

// A macro to declare a keywords array:
// KEYWORDS_DECL(keywords_main) {
//  {} , {} ...
// };
//
static void keywords_register(const struct keywords_t *key, const char *name);

#define KEYWORDS_DECL(_Key) \
  static const struct keywords_t keywords_ ## _Key [] =

#define KEYWORDS_REG(_Key) \
 /* Each keywords array has its own constructor which registers this particular array */ \
  void __attribute__((constructor)) __init_kwd_ ## _Key () { \
    keywords_register(keywords_ ## _Key, # _Key); \
  } \


#endif // #if COMPILING_ESPSHELL

