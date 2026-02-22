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

// Gets called by ESP-IDF if memory allocation fails. Registered as a callback
// in espshell_initonce()
//
static void out_of_memory_event(size_t size, uint32_t caps,const char * function_name) {

  if (taskid_arduino_sketch())
    task_suspend(taskid_arduino_sketch());

  q_printf("\r\n%% <w> Boom! Out of memory in \"%s\" (asked %u bytes, caps: %x)</>\r\n"
           "%% Sketch is suspended, you resume with \"resume\"\r\n",
           function_name,
           size,
           (unsigned int)caps);
}


// Display memory contens
//
static int memory_display_content(unsigned char *address,  // starting address
                                  unsigned int count,      // number of elements to display
                                  unsigned char length,    // element size in bytes
                                  bool isu,                // display as unsigned?
                                  bool isf,                // display as floating point numbers?
                                  bool isp) {              // display as pointers?

  // Some memory regions on ESP32-family chips are not byte-accessible: instead these regions can only be read
  // in chunks of 4 bytes; Check if requested memory region is byte-accessible and warn the user
  // if memory can not be read. These memory regions can be read as signed/unsigned int, void * and float
  // because all these types are 4 bytes long
  //
  if (length != sizeof(unsigned int))
    if (!esp_ptr_byte_accessible(address) || !esp_ptr_byte_accessible(address + length * count)) {
      //TODO: make safe function (buffered read) to read byte-by-byte . For now just warn the user
      q_printf("%% A memory region within [0x%p..0x%p] is not byte-accessible\r\n"
               "%% Try \"<i>show memory 0x%p %u void *</>\" instead, to see a hexdump\r\n", address, address+length*count,address, count / 4 + 1);
      return CMD_FAILED;
    }
  

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
// TODO:refactor to use read_ctype
// TODO:refactor with switch()
// TODO:refactor ifs

static int cmd_show_memory_address(int argc, char **argv) {

  unsigned char *address;
  unsigned int count = 256;
  unsigned char length = 1;

  // read the address. NULL will be returned if address is 0 or has incorrect syntax.
  address = (unsigned char *)hex2uintptr(argv[2]);

  // read the rest of arguments if specified
  int i = 3;
  bool count_is_specified = false,  // user has provided COUNT
        sign_is_specified = false,  // "signed" or "unsigned" keywords seen
        type_is_specified = false,  // "char", "void", "int", "short" ... etc seen
        isu = false,                // Display unsigned values
        isf = false,                // Display as floating point
        isp = false;                // Generic 32bit hex display

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

      type_is_specified = true;


      if (!q_strcmp(argv[i],"signed")) { isu = false; sign_is_specified = true; } else
      if (!q_strcmp(argv[i],"unsigned")) { isu = true; sign_is_specified = true; } else
      if (!q_strcmp(argv[i],"void*") || argv[i][0] == '*') isp = true; else
      if (!q_strcmp(argv[i],"float")) isf = true; else
      if (!q_strcmp(argv[i],"int") || !q_strcmp(argv[i],"long")) length = sizeof(int); else
      if (!q_strcmp(argv[i],"short")) length = sizeof(short); else
      if (!q_strcmp(argv[i],"char") || !q_strcmp(argv[i],"void")) {} else
        q_printf("%% Unrecognized keyword \"%s\" ignored\r\n",argv[i]);
      if (isp || isf)
        length = sizeof(void *);
    }
    i++;
  }

  // Make simple form "sh mem ADDRESS" to use isu=true, length=1, count=256
  // If signedness was not specified but type was specified, then we assume SIGNED argument: "int" == "signed int"
  if (!sign_is_specified) {
    if (!type_is_specified)
      isu = true;
    else
      isu = false;
  } else {
    if (isf /* || isp*/) // don't warn users on requests like "unsigned int *" : it is a pointer anyway
      q_print("% \"signed\" and \"unsigned\" keywords were ignored\r\n");
  }

  if (!is_valid_address(address, count * length)) {
    HELP(q_print("% Bad address range. Must be  a hex number > 0x2000000 (e.g. 0x3fff0000)\r\n"));
    return 2; //argv[2] is bad
  }

  return memory_display_content(address,count,length,isu,isf,isp);
}


// "show memory ARG1 ARG2 ... ARGn"
//
static int cmd_show_memory(int argc, char **argv) {

    if (argc < 3) { // "show memory"
      unsigned int total;
      q_printf( "%% <r>-- Heap information --                                 </>\r\n%%\r\n"
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

      q_print("%\r\n%<r> -- Low watermarks (minimum available memory) --</>\r\n%\r\n");

      q_printf("%% Internal SRAM  : <i>%u</> bytes, heap integrity check: %s</>\r\n"
               "%% External SPIRAM: <i>%u</> bytes, heap integrity check: %s</>\r\n",
               heap_caps_get_minimum_free_size( MALLOC_CAP_INTERNAL ), 
               heap_caps_check_integrity(MALLOC_CAP_INTERNAL, false) ? "<g>PASS" : "<w>FAIL",
               heap_caps_get_minimum_free_size( MALLOC_CAP_SPIRAM ), 
               heap_caps_check_integrity(MALLOC_CAP_SPIRAM, false) ? "<g>PASS" : "<w>FAIL");
                


      // this one gets printed only #if MEMTEST == 1
      q_memleaks(" -- Entries allocated by ESPShell --");
      return 0;
    }
    //"show memory ADDRESS ..."
    return cmd_show_memory_address(argc, argv);
}

#endif //


