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

 // TODO: make all calls to be cmd_show_...(int, char**) and use a calltable

#if COMPILING_ESPSHELL

typedef int (*show_cb_t)(int, char **); 

static int cmd_show_version(UNUSED int argc, UNUSED char **argv) {
  q_print("% ESPShell version " ESPSHELL_VERSION "\r\n");
  return 0;
}

// calltable for "show KEYWORD ..." 
static const struct {
  const char     *key;
  const show_cb_t cb;
} show_keywords[] = {
  { "pwm", cmd_show_pwm },
  { "counters", cmd_show_counters },
  { "memory", cmd_show_memory },
  { "iomux", cmd_show_iomux },
  { "pin", cmd_show_pin },
#if WITH_FS  
  { "mount", cmd_show_mount },
#endif    
  { "sequence", cmd_seq_show },
#if WITH_ESPCAM
  { "camera", cmd_show_camera },
#endif
  { "cpuid", cmd_show_cpuid },
  { "version", cmd_show_version },
  {NULL,NULL}
};

//"show KEYWORD ARG1 ARG2 .. ARGn"
// it is in a separate file because it is going to be big
//
static int cmd_show(int argc, char **argv) {

  if (argc < 2)
    return CMD_MISSING_ARG;
    
  for (int i = 0; show_keywords[i].key; i++)
    if (!q_strcmp(argv[1],show_keywords[i].key))
      return show_keywords[i].cb(argc,argv);

  HELP(q_print("% Show what?\r\n"));

  return 1; //  keyword argv[1] is bad
}
#endif //#if COMPILING_ESPSHELL

