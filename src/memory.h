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

// Display memory contens
// Only use with readable memory otherwise it will crash (LoadProhibited exception)
//
static int memory_display_content(unsigned char *address, unsigned int count, unsigned char length, bool isu, bool isf, bool isp) {

  // dont print this header when using short form of q_printhex.
  if (length >= tbl_min_len)
    HELP(q_printf("%% Memory content (starting from %08x, %u bytes)\r\n", (unsigned int)address,length * count));

  // If length == 1 then it is "char". We display unsigned char as ordinary hexdump, and signed as a table
  if (length > 1 || !isu) 
    q_printtable(address, count, length, isu, isf, isp);
  else
    q_printhex(address, count * length);

  return 0;
}

// Implementation of "show memory address ARG1 ARG2 ... ARGn"
// This one is called from cmd_show()
// TODO: support int64_t and uint64_t

static int cmd_show_memory_address(int argc, char **argv) {

  unsigned char *address;
  unsigned int count = 256;
  unsigned char length = 1;

  // read the address. NULL will be returned if address is 0 or has incorrect syntax.
  address = (unsigned char *)hex2uint32(argv[2]);

  // read the rest of arguments if specified
  int i = 3;
  bool count_is_specified = false;
  bool isu = false, isf = false, isp = false;
  while (i < argc) {
    if (isnum(argv[i])) {
      count = q_atol(argv[3], count);
      count_is_specified = true;
    } else {
      // Type was provided. 
      // Most likely user wants to see *(var), not just 256 bytes of memory content, so if /count/ was not explicitly set, make count = 1
      // or we risk LoadProhibited exception here
      if (!count_is_specified)
        count = 1;

      if (!q_strcmp(argv[i],"unsigned")) isu = true; else
      if (!q_strcmp(argv[i],"void*") || argv[i][0] == '*') isp = true; else
      if (!q_strcmp(argv[i],"float")) isf = true; else
      if (!q_strcmp(argv[i],"int") || !q_strcmp(argv[i],"long")) length = sizeof(int); else
      if (!q_strcmp(argv[i],"short")) length = sizeof(short); else
      if (!q_strcmp(argv[i],"char") || !q_strcmp(argv[i],"void") || !q_strcmp(argv[i],"signed")) {} else // TODO: signed char!
        q_printf("%% Unrecognized keyword \"%s\" ignored\r\n",argv[i]);
      if (isp || isf)
        length = sizeof(void *);
    }
    i++;
  }

  if (!is_valid_address(address, count * length)) {
    HELP(q_print("% Bad address range. Must be  a hex number > 0x2000000 (e.g. 3fff0000)\r\n"));
    return 2;
  }

  return memory_display_content(address,count,length,isu,isf,isp);
}


// "show memory ARG1 ARG2 ... ARGn"
//
static int cmd_show_memory(int argc, char **argv) {

    if (argc < 3) { // "show memory"
      unsigned int total;
      q_printf( "%% -- Heap information --\r\n%%\r\n"
                "%% If using \"malloc()\" (default allocator))\":\r\n"
                "%% <i>%u</> bytes total, <i>%u</> available, %u max per allocation\r\n%%\r\n"
                "%% If using \"heap_caps_malloc(MALLOC_CAP_INTERNAL)\", internal SRAM:\r\n"
                "%% <i>%u</> bytes total,  <i>%u</> available, %u max per allocation\r\n%%\r\n",
                heap_caps_get_total_size(MALLOC_CAP_DEFAULT), 
                heap_caps_get_free_size(MALLOC_CAP_DEFAULT), 
                heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
                heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
      
      if ((total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) > 0)
        q_printf("%% External SPIRAM detected (available to \"malloc()\"):\r\n"
                "%% Total <i>%u</>Mb, of them <i>%u</> bytes are allocated\r\n",
                total/(1024*1024), total - heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
      else
        q_print("% No accessible SPIRAM/PSRAM found. If your board has one then try\r\n"
                "% to change build target in Arduino IDE (<i>Tools->Board</>) or enable\r\n"
                "% PSRAM (<i>Tools->PSRAM:->Enabled</>)\r\n");

      q_memleaks(" -- Entries allocated by ESPShell --");
      return 0;
    }
    //"show memory ADDRESS ..."
    return cmd_show_memory_address(argc, argv);
}

#endif //

