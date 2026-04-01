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

// Displays ESPShell version.
// This string is guaranteed to have the same format so it can be used for atomatic queries
//
static int cmd_show_version(UNUSED int argc, UNUSED char **argv) {
  q_print("% ESPShell version " ESPSHELL_VERSION "\r\n");
  return 0;
}


//"show KEYWORD ARG1 ARG2 .. ARGn"
// 
// Find corresponding handler based on argv[1] and execute it.
//
static int cmd_show(int argc, char **argv) {

  cmd_handler_t cb;

  if (argc < 2)
    return CMD_MISSING_ARG;


    // TODO: for now we ignore argc_max
  if (NULL != (cb = userinput_find_handler_by_name(KEYWORDS(show), argv[1])))
    return cb(argc, argv);

  HELP(q_print("% Show what? Enter \"show ?\" to see what is available\r\n"));

  return 1; //  keyword argv[1] is bad
}
#endif //#if COMPILING_ESPSHELL

