/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#if COMPILING_ESPSHELL

//"show KEYWORD ARG1 ARG2 .. ARGn"
// it is in a separate file because it is going to be big

static int cmd_show(int argc, char **argv) {

  if (argc < 2)
    return -1;
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
  // "show memory ADDRESS [COUNT]"
  if (!q_strcmp(argv[1], "memory")) {
    if (argc < 3)
      return memory_show_information();

    unsigned char *address;
    unsigned int length = 256;

    // read the address. NULL will be returned if address is 0 or has incorrect syntax.
    if ((address = (unsigned char *)(hex2uint32(argv[2]))) == NULL) {
      HELP(q_print("% Bad address. Don't go below 0x3ffb0000, there are chances for system crash\r\n"));
      return 2;
    }

    // read /count/ argument if specified
    if (argc > 3)
      length = q_atol(argv[3], length);

    return memory_display_content(address,length);
  }

  if (!q_strcmp(argv[1],"counters"))
    return count_show_counters();


  return 1; //  keyword argv[1] is bad
}
#endif //#if COMPILING_ESPSHELL
