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

# error "alias has only stub code and is not functional"

#define MAX_ALIAS_LEN 32

struct alias {
  struct alias *next;
  char name[MAX_ALIAS_LEN];
  char script[0]; 
};

//"alias NAME"
// save context, switch command list, change the prompt
//
static int cmd_alias_if(int argc, char **argv) {

  unsigned int iic;
  static char prom[MAX_PROMPT_LEN] = { 0 };
  static char alias[MAX_ALIAS_LEN];

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (argc > 2) {
    q_print("%% Alias name must have no spaces in it\r\n");
    return CMD_FAILED;
  }

  // TODO: how to store a pointer to strdup()ed argv[1] so later exit frees it?
  //       workaround now to use static buffer. ESPShell is designed as solely single-user library with only 1 espshell running
  //       so static buffer should not be a problem
  if (strlen(argv[1]) >= sizeof(alias)) {
    q_printf("%% Alias name must not be too long: maximum %u characters\r\n", sizeof(alias) - 1);
    return CMD_FAILED;
  }

  strcpy(alias, argv[1]);
  strcpy(prom, PROMPT_ALIAS);

  change_command_directory((unsigned int )(&alias[0]), keywords_alias, prom, "alias editing");
  return 0;
}


#endif // #if COMPILING_ESPSHELL

