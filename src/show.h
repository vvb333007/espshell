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

//"show KEYWORD ARG1 ARG2 .. ARGn"
// it is in a separate file because it is going to be big
//
static int cmd_show(int argc, char **argv) {

  if (argc < 2)
    return CMD_MISSING_ARG;
    
  // show version
  if (!q_strcmp(argv[1],"version"))
    return q_print("% ESPShell version " ESPSHELL_VERSION "\r\n"),0; // always return 0

  // show pwm
  if (!q_strcmp(argv[1],"pwm"))
    return cmd_show_pwm(argc,argv);

  // show iomux
  if (!q_strcmp(argv[1],"iomux"))
    return pin_show_mux_functions();

  // "show sequence NUMBER"
  if (!q_strcmp(argv[1], "sequence"))
    return cmd_seq_show(argc, argv);

  // "show cpuid"
  if (!q_strcmp(argv[1], "cpuid"))
    return cmd_cpu(1, argv);

#if WITH_FS
  // "show mount"
  // "show mount /PATH"
  if (!q_strcmp(argv[1], "mount")) {
    if ( argc < 3) // "show mount"
      return cmd_files_mount0(1, argv);
    return files_show_mountpoint(argv[2]) == 0 ? 0 : 2;
  }
#endif
  
  // "show memory"
  // "show memory ADDRESS [COUNT | unsigned | int | signed | short | char | float | long | void * ]"
  if (!q_strcmp(argv[1], "memory")) {
    if (argc < 3)
      return memory_show_information();
    return cmd_show_address(argc, argv); // memory.h
  }

  if (!q_strcmp(argv[1],"counters"))
    return count_show_counters();

#if WITH_ESPCAM
  if (!q_strcmp(argv[1],"camera"))
    return cmd_show_camera(argc, argv);
#endif

  HELP(q_print("% Show what?\r\n"));

  return 1; //  keyword argv[1] is bad
}
#endif //#if COMPILING_ESPSHELL

