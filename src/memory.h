/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is available at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#if COMPILING_ESPSHELL


// Display memory amount (total/available) for different
// API functions: malloc() and heap_caps() allocator with different flags
//
static int memory_show_information() {

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

// Display memory contens
// Only use with readable memory otherwise it will crash (LoadProhibited exception)
//
static int memory_display_content(unsigned char *address, unsigned int count, unsigned char length, bool isu, bool isf, bool isp) {

  // dont print this header when using shortform of q_printhex.
  if (length > 15) //TODO: use tbl_min_len
    HELP(q_printf("%% Memory content (starting from %08x, %u bytes)\r\n", (unsigned int)address,length * count));

  if ((length > 1))
    q_printtable(address, count, length, isu, isf, isp);
  else
    q_printhex(address, count * length);

  return 0;
}

// Implementation of "show memory address ARG1 ARG2 ... ARGn"
// This one is called from cmd_show()
//
static int memory_show_address(int argc, char **argv) {
{
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
        if (!q_strcmp(argv[i],"int") || !q_strcmp(argv[i],"long"))
          length = sizeof(int);
        else
        if (!q_strcmp(argv[i],"short"))
          length = sizeof(short);
        else
        if (!q_strcmp(argv[i],"char")) {
          //skip
        } else
        if (!q_strcmp(argv[i],"void")) { // probably "void *", so just skip it and wait for an *
          //skip
        } 
        else
          q_printf("%% Keyword \"%s\" was ignored\r\n",argv[i]);

        if (isp || isf)
          length = sizeof(void *);
      }
      i++;
    }
    return memory_display_content(address,count,length,isu,isf,isp);
  }
}

#endif //

