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

// TODO: Add "as hex" keyword: "show memory 0x40000000 10 int as hex"
// TODO: This will require q_printtable() etc to be refactored. Right now the output format is hard linked
// TODO: to the type: "int" is displayed as "%d", "void *" as "%p" but there is no way to dump array of "int" as hex

#if COMPILING_ESPSHELL

// Gets called by ESP-IDF if memory allocation fails. Registered as a callback
// in espshell_initonce()
//
static void out_of_memory_event(size_t size, uint32_t caps,const char * function_name) {

  if (taskid_arduino_sketch())
    task_suspend(taskid_arduino_sketch());

  // in OOM event we can not call q_printf as it can try to malloc() its output buffer.
  // Instead we call ROM function
  esp_rom_printf("\r\n%% <w> Boom! Out of memory in \"%s\" (asked %u bytes, caps: %x)</>\r\n"
           "%% Sketch is suspended, you can resume it with \"resume\" command\r\n",
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
                                  bool isp,                // display as %p?
                                  bool force_hex) {              // force display as %x

  // Some memory regions on ESP32-family chips are not byte-accessible: instead these regions can only be read
  // in chunks of 4 bytes; Check if requested memory region is byte-accessible and warn the user
  // if memory can not be read. These memory regions can be read as signed/unsigned int, void * and float
  // because all these types are 4 bytes long
  //
  if (length != sizeof(unsigned int))
    if (!esp_ptr_byte_accessible(address) || !esp_ptr_byte_accessible(address + length * count)) {
      //TODO: make safe function (buffered read) to read byte-by-byte . For now just warn the user
      q_printf("%% A memory region within [0x%p..0x%p] is not byte-accessible\r\n"
               "%% Try \"<i>show memory 0x%p %u *</>\" instead, to see a hexdump\r\n", address, address+length*count,address, count / 4 + 1);
      return CMD_FAILED;
    }
  

  // dont print this header when using short form of q  _printhex.
  if (length >= tbl_min_len)
    HELP(q_printf("%% Memory content (starting from %08x, %u bytes)\r\n", (unsigned int)address,length * count));

  // If length == 1 then it is "char". We display unsigned char as ordinary hexdump, and signed as a table
  if (length > 1 || !isu) 
    q_printtable(address, count, length, isu, isf, isp, force_hex);
  else
    q_printhex(address, count * length);

  return 0;
}


// Check pointer and display some if its caps.
//
static void memory_display_ptr_info(const void *a) {

  if (esp_ptr_external_ram(a))
    q_printf("%% Address is in external SPI RAM, %sDMA-capable\r\n", esp_ptr_dma_ext_capable(a) ? "" : "NOT ");
  else {
    const char *where =
         esp_ptr_in_drom(a) ? "DROM"  : 
        (esp_ptr_in_rom(a)  ? "ROM"   : 
        (esp_ptr_in_iram(a) ? "I-RAM" :
        (esp_ptr_in_dram(a) ? "D-RAM" : NULL)));

    if (where) {
      q_printf("%% The address is in SoC internal %s", where);

      where = esp_ptr_in_diram_dram(a) ? "DIRAM D" : 
             (esp_ptr_in_diram_iram(a) ? "DIRAM I" : NULL);

      if (where)
        q_printf(" (region: %s)", where);

      q_printf(", %sDMA-capable\r\n", esp_ptr_dma_capable(a) ? "" : "NOT ");      

    } else {

      where = esp_ptr_in_rtc_iram_fast(a) ? "RTC IRAM (fast)" :
             (esp_ptr_in_rtc_dram_fast(a) ? "RTC DRAM (fast)" :
             (esp_ptr_in_rtc_slow(a) ? "RTC SLOW" : NULL));

      if (where)
        q_printf("%% The address is in RTC peri: %s\r\n", where);
      else {
//        if ( a >= 0x60000000) //TODO:
          //q_print("%% The address belongs to a peripheral\r\n");
        //else
          q_print("%% Unknown region\r\n");
      }
    }
  } 
}

// Implementation of "show memory address ARG1 ARG2 ... ARGn"
// This one is called from cmd_show()

static int cmd_show_memory_address(int argc, char **argv) {

  unsigned char *address;
  unsigned int count = 256;
  unsigned int length = 1;

  // read the address. NULL will be returned if address is 0 or has incorrect syntax.
  address = (unsigned char *)hex2uintptr(argv[2]);

  // read the rest of arguments if specified
  // If we have numeric arg next to the address - it is the "count", otherwise it is a CTYPE
  int end;
  bool count_is_specified = false,  // user has provided COUNT
        type_is_specified = false,  // "char", "void", "int", "short" ... etc seen
        is_float = false, is_str = false, is_blob = false, is_signed = true,
        isu = false,                // Display unsigned values
        is_hex = false;             // Force hex display

  if (argc > 3) {
    end = 3;
    if (q_isnumeric(argv[3])) {
      count = q_atol(argv[3], count);
      count_is_specified = true;
      end++;
    }

    if (end < argc) {

      end = userinput_read_ctype(argc, argv, end, &length, &is_str, &is_blob, &is_signed, &is_float);

      //q_printf("end=%d,length=%d,is_str=%d,is_blob=%d,is_signed=%d,is_float=%d)\r\n", end, length, is_str, is_blob, is_signed, is_float);

      if (length > 8 || (length == 0 && !is_str && !is_blob)) {
        q_print("% Sorry, can not parse your type definition\r\n"
                "% Use C syntax: \"<i>char</>\", \"<i>unsigned long long int</>\", \"<i>char *</>\" and so on\r\n");
        return CMD_FAILED;
      }
      type_is_specified = true;
      isu = !is_signed;

      // Type was provided. 
      // Most likely user wants to see *(var), not just 256 bytes of memory content, so if /count/ was not explicitly set, make count = 1
      // or we risk LoadProhibited exception here
      if (!count_is_specified)
        count = 1;

      if (is_blob || is_str)
        length = sizeof(void *);
    }

    if (end < argc) {
      if (!q_strcmp(argv[end],"hex"))
        is_hex = true;
    }
  } else {
      isu = true;
  }

  if (type_is_specified == false)
    isu = true;

  if (!is_valid_address(address, count * length)) {
    HELP(q_print("% Bad address range. Must be  a hex number > 0x2000000 (e.g. 0x3fff0000)\r\n"));
    return 2; //argv[2] is bad
  }

  memory_display_ptr_info(address);

  return memory_display_content(address,count,length,isu,is_float,is_blob || is_str, is_hex);
}

// "show memory map"
//
static int cmd_show_memory_map(int argc, char **argv) {

  const char *text;

  q_print("% <r>SoC memory map: (region name and address range)  </>\r\n");

#if CONFIG_IDF_TARGET_ESP32
  text = "% \r\n"
          "% <d>DATA ROM</>   [0x3F400000 .. 0x3f7fffff]\r\n"
          "% <b>PSRAM</>      [0x3F800000 .. 0x3fbfffff]\r\n"
          "% <i>PERIFERALS</> [0x3ff70000 .. 0x3ff7ffff]\r\n"
          "% <b>RTC DRAM</>   [0x3FF80000 .. 0x3ff81fff]\r\n"
          "% <b>DRAM</>       [0x3FFAE000 .. 0x3fffffff]\r\n"
          "% <d>CACHE</>      [0x40070000 .. 0x4007ffff]\r\n"
          "% <b>IRAM</>       [0x40080000 .. 0x400A9fff]\r\n"
          "% <b>RTC IRAM</>   [0x400C0000 .. 0x400C1fff]\r\n"
          "% <d>IROM</>       [0x400D0000 .. 0x403fffff]\r\n"
          "% <b>RTC DATA</>   [0x50000000 .. 0x50001fff]\r\n";

#elif CONFIG_IDF_TARGET_ESP32S3
  text = "% \r\n"
          "% <d>DROM</>        [0x3C000000 .. 0x3Dffffff]\r\n"
          "% <b>PSRAM</>       [0x3C000000 .. 0x3Dffffff] (overlaps DATA ROM!)\r\n"
          "% <b>DRAM</>        [0x3FC88000 .. 0x3FCfffff]\r\n"
          "% <b>IRAM</>        [0x40370000 .. 0x403dffff]\r\n"
          "% <d>IROM</>        [0x42000000 .. 0x43ffffff]\r\n"
          "% <b>RTC DATA</>    [0x50000000 .. 0x50001fff]\r\n"
          "% <i>PERIPHERALS</> [0x60000000 .. 0x600FDfff]\r\n"
          "% <b>RTC IRAM</>    [0x600FE000 .. 0x600fffff]\r\n"
          "% <b>RTC DRAM</>    [0x600FE000 .. 0x600fffff]\r\n";
#elif CONFIG_IDF_TARGET_ESP32S2

  text = "% \r\n"
          "% <d>DATA ROM</>   [0x3F000000 .. 0x3ff7ffff]\r\n"
          "% <d>CACHE</>      [0x3F000000 .. 0x3f4fffff] (overlaps data ROM)\r\n"
          "% <b>PSRAM</>      [0x3F500000 .. 0x3ff7ffff] (overlaps data ROM)\r\n"
          "% <i>PERIFERALS</> [0x3ff80000 .. 0x3ff8dfff]\r\n"
          "% <b>RTC DRAM</>   [0x3FF9e000 .. 0x3ff9ffff]\r\n"
          "% <b>DRAM</>       [0x3FFB0000 .. 0x3fffffff]\r\n"
          "% <b>IRAM</>       [0x40020000 .. 0x4006ffff]\r\n"
          "% <b>RTC IRAM</>   [0x40070000 .. 0x40071fff]\r\n"
          "% <d>IROM</>       [0x40080000 .. 0x407fffff]\r\n"
          "% <b>RTC DATA</>   [0x50000000 .. 0x50001fff]\r\n";
// TODO: add support for the models below
//#elif CONFIG_IDF_TARGET_ESP32C3
//#elif CONFIG_IDF_TARGET_ESP32C6
//#elif CONFIG_IDF_TARGET_ESP32H2
//#elif CONFIG_IDF_TARGET_ESP32H4
//#elif CONFIG_IDF_TARGET_ESP32P4
#else
  text = "% <e>Uhm, looks like I don't have a memory map for this CPU</>\r\n"
         "% Go to Github and create a feature request\r\n";
#endif

  q_print( text );

  return 0;
}


// "show memory [ARG1 ARG2 ... ARGn]"
// All show memory commands are handled here. The "show memory" logic is implemented in the function while
// "show memory ADDRESS" and "show memory map" are implemented as separate functions
//
static int cmd_show_memory(int argc, char **argv) {

    if (argc < 3) { // "show memory"
      unsigned int total;

      q_printf("%% <r>-- Memory caps --                                  </>\r\n"
               "%%\r\n"
               "%% Note1: DRAM and IRAM are %ssharing the same memory space.\r\n"
               "%% Note2: RTC_DRAM and RTC_IRAM are %ssharing the same memory space\r\n",
                esp_dram_match_iram() ? "not " : "",
                esp_rtc_dram_match_rtc_iram() ? "not " : "");

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

      q_print("%\r\n%<r> -- Low watermarks / Heap integrity --</>\r\n%\r\n");

      q_printf("%% Internal SRAM  : dropped as low as <i>%u</> bytes, heap integrity check: %s</>\r\n"
               "%% External SPIRAM: dropped as low as <i>%u</> bytes, heap integrity check: %s</>\r\n",
               heap_caps_get_minimum_free_size( MALLOC_CAP_INTERNAL ), 
               heap_caps_check_integrity(MALLOC_CAP_INTERNAL, false) ? "<g>PASS" : "<w>FAIL",
               heap_caps_get_minimum_free_size( MALLOC_CAP_SPIRAM ), 
               heap_caps_check_integrity(MALLOC_CAP_SPIRAM, false) ? "<g>PASS" : "<w>FAIL");

      // Devel: this one gets printed only #if MEMTEST == 1
      q_memleaks(" -- Entries allocated by ESPShell --");
      return 0;
    }
    
    // "show memory ADDRESS ..."
    if (q_isnumeric(argv[2])) 
      return cmd_show_memory_address(argc, argv);

    // "show memory map"
    if (!q_strcmp(argv[2], "map"))
      return cmd_show_memory_map(argc, argv);

    return 2; // unrecognized argv[2]
}

#endif //


