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
  // "show memory ADDRESS [COUNT | uint | int | ushort | short | uchar | char | float | void * ]"
  if (!q_strcmp(argv[1], "memory")) {
    if (argc < 3)
      return memory_show_information();

    unsigned char *address;
    unsigned int count = 256;
    unsigned char length = 1;

    // read the address. NULL will be returned if address is 0 or has incorrect syntax.
    if ((address = (unsigned char *)(hex2uint32(argv[2]))) == NULL) {
      HELP(q_print("% Bad address. Don't go below 0x3ffb0000, there are chances for system crash\r\n"));
      return 2;
    }

    // read the rest of arguments if specified
    int i = 3;
    bool count_is_specified = false;
    bool isu = false, isf = false, isp = false;
    while (i < argc) {
      if (isnum(argv[i])) {
        count = q_atol(argv[3], count);
        count_is_specified = true;
      }
      else {
        // Type was provided. 
        // Most likely user wants to see *(var), not just 256 bytes of memory content, so if /count/ was not explicitly set, make count = 1
        // or we risk LoadProhibited exception here
        if (!count_is_specified)
          count = 1;

        if (!q_strcmp(argv[i],"unsigned"))
          isu = true;
        else
        if (!q_strcmp(argv[i],"void*") || argv[i][0] == '*')
          isp = true;
        else
        if (!q_strcmp(argv[i],"float"))
          isf = true;
        else
        if (!q_strcmp(argv[i],"int"))
          length = 4;
        else
        if (!q_strcmp(argv[i],"short"))
          length = 2;
        else
        if (!q_strcmp(argv[i],"void")) {} // probably "void *", so just skip it and wait for an *
        else
          q_printf("%% Keyword \"%s\" was ignored\r\n",argv[i]);

        if (isp || isf)
          length = 4;
      }
      i++;
    }

    return memory_display_content(address,count,length,isu,isf,isp);
  }

  if (!q_strcmp(argv[1],"counters"))
    return count_show_counters();


  return 1; //  keyword argv[1] is bad
}
#endif //#if COMPILING_ESPSHELL

