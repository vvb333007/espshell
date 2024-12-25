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
static int memory_display_content(unsigned char *address, unsigned int length) {

  // dont print this header when using shortform of q_printhex.
  if (length > 15) 
    HELP(q_printf("%% Memory content (starting from %08x, %u bytes)\r\n", (unsigned int)address,length));

  q_printhex(address, length);

  return 0;
}


#endif //

