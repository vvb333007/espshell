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

// TODO: implement wakeup by a touch pad

#if COMPILING_ESPSHELL

#include <esp_rom_spiflash.h>
#include <soc/rtc.h>

// Really old ESP-IDF / ArduinoCore may be missing these frequency values on particular targets.
#if !defined(APB_CLK_FREQ) || !defined(MODEM_REQUIRED_MIN_APB_CLK_FREQ)
#  error "Please update ESP-IDF and/or Arduino Core libraries to a newer version"
#endif

// not in .h files of ArduinoCore.
extern bool setCpuFrequencyMhz(uint32_t);

// TODO: make cpu_ticks() to accept an address where result is stored
//       if *address == 0, then CCOUNT is stored otherwise, if *address != 0
//       then *address = CCOUNT - *address
//
static inline __attribute__((always_inline)) uint32_t cpu_ticks() {
  uint32_t ccount;
#if __XTENSA__
  asm ( "rsr.ccount %0;" : "=a"(ccount) /*Out*/ : /*In*/ : /* Clobber */);
#else // RISCV
  asm ( "csrr %0, mcycle" : "=r"(ccount) /*Out*/ : /*In*/ : /* Clobber */);
#endif
  return ccount;
}

// Check if APB frequency is optimal or can be raised
// ESP32 and ESP32-S2 lower their APB frequency if CPU frequency goes below 80 MHz
// RISCV ESP CPUs have APB freq of 40 or 32 MHz, while modem min required frequency is 80MHz
//
#define apb_freq_max() (MODEM_REQUIRED_MIN_APB_CLK_FREQ / 1000000)
#define apb_freq_is_optimal() (APBFreq >= (APB_CLK_FREQ / 1000000))
#define apb_freq_can_be_raised() (APBFreq < apb_freq_max())



// Read and save CPU,APB and XTAL frequency values. These are used by espshell to calculate some intervals
// and PWM duty cycle resolution.
//
// It is very unlikely that user app is changing CPU frequency at runtime.
// If , however, it does, then "cpu NEW_FREQ" command will recache new values
//
// Changing the CPU frequency via shell command "cpu FREQ" will save new values automatically
//

// Globals
static unsigned short CPUFreq  = 240,  // Default values (or "expected values")
                      APBFreq  = 80, 
                      XTALFreq = 40;

// Read vital information (XTAL, CPU and APB frequencies) and store it in global variables
// Called at startup, before main()
// Called every time the cpu frequency gets changed via the "cpu" command
// Called every time user issues "show cpu" command
//
static void __attribute__((constructor)) cpu_read_frequencies() {

  rtc_cpu_freq_config_t conf;
  rtc_clk_cpu_freq_get_config(&conf);

  XTALFreq = rtc_clk_xtal_freq_get();
  CPUFreq = conf.freq_mhz;
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
  // ESP32 and ESP32-S2 lower their APB frequency if CPU frequency goes below 80MHz
  APBFreq = (conf.freq_mhz >= 80) ? 80 : conf.source_freq_mhz/conf.div;
#else
  // Other Espressif's SoCs have fixed APB frequency
  APBFreq = APB_CLK_FREQ / 1000000;
#endif
}

//"show cpuid"
//
// Display CPU ID information, frequencies, chip temperature, flash chip information
// and a couple of other things.
// Code below is based on Arduino Core 3.x.x and must be kept in sync with latest ArduinoCore
//
static int cmd_show_cpuid(int argc, char **argv) {

  esp_chip_info_t chip_info;
  const char *chipid = "ESP32-(Unknown)";

  esp_chip_info(&chip_info);

#if CONFIG_IDF_TARGET_ESP32
  uint32_t chip_ver;
  uint32_t pkg_ver;

  chip_ver = REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_PACKAGE);
  pkg_ver = chip_ver & 0x7;

  switch (pkg_ver) {
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6: if (chip_info.revision / 100 == 3) 
                                              chipid = "ESP32-D0WD-Q6-V3"; else 
                                              chipid = "ESP32-D0WD-Q6"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5: if (chip_info.revision / 100 == 3) 
                                              chipid = "ESP32-D0WD-Q5-V3"; else 
                                              chipid = "ESP32-D0WD-Q5"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5:   chipid = "ESP32-D2WD-Q5"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD2:   chipid = "ESP32-PICO-D2 / ESP32-U4WDH"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4:   chipid = "ESP32-PICO-D4"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302: chipid = "ESP32-PICO-V3-02"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3: chipid = "ESP32-D0WDR2-V3"; break;
    default: q_printf("%% Detected PKG_VER=%04x\r\n", (unsigned int)pkg_ver);
  }
#elif CONFIG_IDF_TARGET_ESP32S2
  uint32_t pkg_ver;

  pkg_ver = REG_GET_FIELD(EFUSE_RD_MAC_SPI_SYS_3_REG, EFUSE_PKG_VERSION);
  switch (pkg_ver) {
    case 0: chipid = "ESP32-S2"; break;
    case 1: chipid = "ESP32-S2FH16"; break;
    case 2: chipid = "ESP32-S2FH32"; break;
  }
#else
  switch (chip_info.model) {
    case CHIP_ESP32S3: chipid = "ESP32-S3"; break;
    case CHIP_ESP32C3: chipid = "ESP32-C3"; break;
    case CHIP_ESP32C2: chipid = "ESP32-C2"; break;
    case CHIP_ESP32C6: chipid = "ESP32-C6"; break;
    case CHIP_ESP32H2: chipid = "ESP32-H2"; break;
    case CHIP_ESP32P4: chipid = "ESP32-P4"; break;
    case CHIP_ESP32C61: chipid = "ESP32-C61"; break;
    default:
  }
#endif  //CONFIG_IDF_TARGET_XXX

  // Just in case
  cpu_read_frequencies();

  q_print("% <u>Hardware:</>\r\n");
  q_printf("%% CPU ID: %s, (%u core%s), Chip revision: %d.%d\r\n"
           "%% CPU frequency is %uMhz, Crystal: %uMhz, APB bus %uMhz\r\n"
           "%% Chip temperature: %.1f deg. Celsius\r\n",
           chipid,
           PPA(chip_info.cores),
           (chip_info.revision >> 8) & 0xf,
           chip_info.revision & 0xff,
           CPUFreq,
           XTALFreq,
           APBFreq,
           temperatureRead());

  if (!apb_freq_is_optimal()) {
    q_printf("%% <i>APB frequency is not optimal</i>");
    if (apb_freq_can_be_raised())
      q_printf(" : it can be raised up to %u MHz", apb_freq_max() / 1000000);
    q_print(CRLF);
  }

  q_print("% SoC features: ");

  if (chip_info.features & CHIP_FEATURE_EMB_FLASH)
    q_print("Embedded flash, ");
  if (chip_info.features & CHIP_FEATURE_WIFI_BGN)
    q_print("WiFi 2.4GHz, ");
  if (chip_info.features & CHIP_FEATURE_BLE)
    q_print("Bluetooth LE, ");
  if (chip_info.features & CHIP_FEATURE_BT)
    q_print("Bluetooth, ");
  if (chip_info.features & CHIP_FEATURE_IEEE802154)
    q_print("IEEE 802.15.4, ");
  if (chip_info.features & CHIP_FEATURE_EMB_PSRAM)
    q_print("embedded PSRAM, ");

  unsigned long psram;
  // "external SPIRAM\r\n" below belongs to the "SoC features"
  if ((psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM)) > 0)
    q_printf("external PSRAM\r\n%% PSRAM (SPIRAM) size: %lu (%lu MB)",psram,  psram / (1024 * 1024));
  
  //
  // global variable g_rom_flashchip is defined in linker file.
  // may be use bootloader_read_flash_id() ?
  const char *mfg, *type; 

  uint8_t  manufacturer = (g_rom_flashchip.device_id >> 16) & 0xff;
  uint16_t id = g_rom_flashchip.device_id & 0xffff;
  uint32_t capacity = 1UL << (g_rom_flashchip.device_id & 0xFF);

  switch ( manufacturer ) {
    case 0x85: mfg =  "Puya"; break;           // May be Puya.
    case 0x5e: mfg =  "XTX Technology"; break; // May be XTX Technology.
    case 0x84:
    case 0xc8: mfg =  "Giga Device"; break;
    case 0x68: mfg =  "Boya"; break;
    case 0x9d: mfg =  "ISSI"; break;
    case 0xc2: mfg =  "MACRONIX"; break;
    case 0xcd: mfg =  "TH"; break;
    case 0xef: mfg =  "Winbond"; break;
    default:   mfg =  "see JEDEC JPL106 list";
  };
  
  // TODO: check carefully what these bits really mean. Sometimes it is just "SPI RAM", some manufacturers also
  //       encode SPI bus type (Normal, Dual, Quad) but this field is not standartized
  //
  if (id & 0x2000) type = "Quad SPI"; else
  if (id & 0x4000) type = "Dual SPI"; else
                   type = "Unknown";

  q_printf( "\r\n%%\r\n%% <u>Flash chip (SPI Flash):</>\r\n"
            "%% Chip ID: 0x%04X (%s), manufacturer ID: %02X (%s)\r\n"
            "%% Size <i>%lu</> bytes (%lu MB)\r\n"
            "%% Block size is <i>%lu</>, sector size is %lu and page size is %lu",
            id,
            type,
            manufacturer,
            mfg,
            capacity,
            capacity >> 20, // divide by 1024*1024
            g_rom_flashchip.block_size,
            g_rom_flashchip.sector_size,
            g_rom_flashchip.page_size);

  q_print("\r\n%\r\n% <u>Firmware:</>\r\n");
  q_printf( "%% Sketch is running on <b>" ARDUINO_BOARD "</>, (an <b>" ARDUINO_VARIANT "</> variant), uses:\r\n"
            "%% Arduino Core version <i>%s</>, which uses\r\n"
            "%% Espressif ESP-IDF version \"<i>%u.%u.%u</>\"\r\n"
            "%% ESPShell library <i>" ESPSHELL_VERSION "</>\r\n", 
            ESP_ARDUINO_VERSION_STR, 
            ESP_IDF_VERSION_MAJOR,ESP_IDF_VERSION_MINOR,ESP_IDF_VERSION_PATCH);

  q_print("%\r\n% <u>Last boot:</>\r\n");            
  cmd_uptime(argc, argv);
  return 0;
}

//"cpu CLOCK"
//
// Set cpu frequency.
//
static int cmd_cpu(int argc, char **argv) {

  unsigned int xtal = XTALFreq;

  if (argc < 2)
    goto show_hint_and_exit;

  unsigned int freq;

  if ((freq = q_atol(argv[1], DEF_BAD)) == DEF_BAD) {
    HELP(q_print("% Numeric value is expected (e.g. 240): frequency in MHz\r\n"));
    return 1;
  }

  // Do nothing if requested frequency is the same
  if (freq == CPUFreq)
    return 0;

  // ESP32 boards do support 240,160,120 and 80 Mhz. If XTAL is 40Mhz or more then we also support
  // XTAL,XTAL/2 and XTAL/4 frequencies. If XTAL frequency is lower than 40 Mhz then we only support XTAL and XTAL/2
  // additional frequencies
  while (freq != 240 && freq != 160 && freq != 120 && freq != 80) {

    if ((freq == xtal) || 
        (freq == xtal / 2) ||
        ((xtal >= 40) && (freq == xtal / 4))) break;
    
    q_printf("%% <e>%u MHz is unsupported frequency</>\r\n", freq);
show_hint_and_exit:
    q_printf("%% Supported frequencies are: 240, 160, 120, 80, %u, %u", xtal, xtal / 2);
    if (xtal >= 40)
      q_printf(" and %u", xtal / 4);
    q_print(" MHz\r\n");

    return argc < 2 ? 0 : 1; // no args == success, 1 arg == invalid 1st arg
  }

  // Set new frequency. Don't check the return code but re-read frequencies instead
  // TODO: use on_frequency_change callbacks (there are some in Arduino Core or/and IDF)
  setCpuFrequencyMhz(freq);
  cpu_read_frequencies();

  if (CPUFreq == freq)
    HELP(q_printf("%% CPU frequency set to %u MHz, APB is %u MHz\r\n",freq, APBFreq)); // informational messages are wrapped in HELP
  else {
    q_printf("%% CPU frequency was not updated (still %u MHz)\r\n", CPUFreq); // error messages are persistent
    return CMD_FAILED;
  }

  return 0;
}

//"reload"
// Performs software reload
//
static int NORETURN cmd_reload(UNUSED int argc, UNUSED char **argv) {
  esp_restart();
  /* NOT REACHED */
  //return 0;
}


// The main purpose of this global variable is to attract user attention to "nap alarm" command.
// Thats why there is no default wakeup source nor default wakeup interval
static int Nap_alarm_set = 0;

// Located in RTC SLOW_MEM so after deep sleep one can query sleep duration
static RTC_DATA_ATTR uint64_t Nap_alarm_time = 0;

static bool is_alarm_set(bool deep) {

  if ((Nap_alarm_set & (1<<ESP_SLEEP_WAKEUP_EXT0)) ||
      (Nap_alarm_set & (1<<ESP_SLEEP_WAKEUP_EXT1)) ||
      (Nap_alarm_set & (1<<ESP_SLEEP_WAKEUP_TIMER)) ||
      (Nap_alarm_set & (1<<ESP_SLEEP_WAKEUP_TOUCHPAD)))
      return true;
  
  if ((Nap_alarm_set & (1 << ESP_SLEEP_WAKEUP_UART))) {
    if (deep) {
      q_print("% Your current wakeup source is \"UART\" which works for light sleep only\r\n");
      return false;
    }
    return true;
  }
  q_print("% Wakeup source is not properly set, use \"<i>nap alarm</>\" to set one\r\n");
  return false;
}

//
//
static int cmd_show_nap(int argc, char **argv) {
  if (Nap_alarm_set == 0)
    q_print("% There are no sleep alarms set\r\n");
  else {
    if (Nap_alarm_set & (1<<ESP_SLEEP_WAKEUP_TIMER))
      q_printf("%% Enabled wakeup source: TIMER, duration: %llu sec\r\n", Nap_alarm_time / 1000000ULL);

    if (Nap_alarm_set & (1<<ESP_SLEEP_WAKEUP_EXT0))
      q_printf("%% Enabled wakeup source: EXT0 (single GPIO)\r\n");

    if (Nap_alarm_set & (1<<ESP_SLEEP_WAKEUP_EXT1))
      q_printf("%% Enabled wakeup source: EXT1 (multiple GPIOs)\r\n");

    if (Nap_alarm_set & (1<<ESP_SLEEP_WAKEUP_TOUCHPAD))
      q_printf("%% Enabled wakeup source: Touch sensor\r\n");

    if (Nap_alarm_set & (1<<ESP_SLEEP_WAKEUP_UART))
      q_printf("%% Enabled wakeup source: UART RX\r\n");
  }
  return 0;
}

// Set sleep wakeup source and wakeup parameters.
//
// nap alarm uart NUM [THRESHOLD]
// nap alarm low|high|touch NUM1 [NUM2 NUM3 ... NUMn]
// nap alarm <TIME> [<TIME> <TIME> .. <TIME>]
// nap alarm disable-all
//
static int cmd_nap_alarm(int argc, char **argv) {

  if (argc < 3)
    return CMD_MISSING_ARG;
  
  //"nap alarm disable-all"
  if (!q_strcmp(argv[2],"disable-all")) {
    esp_err_t err = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    q_printf("%% All sleep wakeup sources %s disabled\r\n", err == ESP_OK ? "were" : "are already");
    Nap_alarm_set = 0;
    return 0;
  }
  
  if (argc < 4)
    return CMD_MISSING_ARG;

  // "nap alarm low 1 2 3 4"
  if (!q_strcmp(argv[2],"low") || 
      !q_strcmp(argv[2],"high")) {

    uint64_t pins = 0;
    uint8_t pin;
    int level;

    if (argc < 4) {
      HELP(q_print("% Pin number expected\r\n"));
      return CMD_MISSING_ARG;
    }

    if (argv[2][0] == 'h')
      level = 1;
    else
      level = 0;

    for (int i = 3; i < argc; i++) {
      if ((pin = q_atol(argv[i],BAD_PIN)) == BAD_PIN)
        return i;
      if (!pin_can_wakeup(pin))
        return CMD_FAILED;
      pins |= pin;
    }

#if SOC_PM_SUPPORT_EXT1_WAKEUP && SOC_PM_SUPPORT_EXT0_WAKEUP && SOC_RTCIO_PIN_COUNT > 0
    if (argc < 5) {
      // only one pin. Its value is in the /pin/ variable
      if (ESP_OK == esp_sleep_enable_ext0_wakeup(pin, level))
        Nap_alarm_set |= 1 << ESP_SLEEP_WAKEUP_EXT0;
      else {
        q_print("% Can not set EXT0 wakeup source\r\n");
        return CMD_FAILED;
      }
    } else {
      // Multiple pins. 
      if (ESP_OK == esp_sleep_enable_ext1_wakeup(pins, level))
        Nap_alarm_set |= 1 << ESP_SLEEP_WAKEUP_EXT1;
      else {
        q_print("% Can not set EXT1 wakeup source\r\n");
        return CMD_FAILED;
      }
    }
    VERBOSE(q_printf("%% Sleep wakeup source: EXT%d\r\n",(argc > 4)));
#else
    q_print("% Target is not supported yet\r\n");
    return CMD_FAILED;
#endif
  } else if (!q_strcmp(argv[2],"touch")) {

    q_print("% Not implemented yet");
    //Nap_alarm_set |= 1 << ESP_SLEEP_WAKEUP_TOUCHPAD;
    return CMD_FAILED;

  } else if (!q_strcmp(argv[2],"uart")) {

    int threshold = 3;   // 3 positive edges on UART_RX pin to wake up ('spacebar' key two times or 'enter' once)
    int u;

    if (argc < 4) {
      HELP(q_print("% UART number expected\r\n"));
      return CMD_MISSING_ARG;
    }

    u = q_atoi(argv[3], -1);
    if (u < 0 || u >= NUM_UARTS) {
      HELP(q_printf("%% UART number is out of range. Valid numbers are 0..%u\r\n",NUM_UARTS - 1));
      return 3;
    }

    if (ESP_OK != esp_sleep_enable_uart_wakeup(u)) {
      HELP(q_print("% Failed to set UART%u as a wakeup source\r\n"));
      return CMD_FAILED;
    }

    VERBOSE(q_printf("%% Sleep wakeup source: uart%d\r\n",u));

    Nap_alarm_set |= 1 << ESP_SLEEP_WAKEUP_UART;
    
    if (argc > 4) {
      if ((threshold = q_atoi(argv[4], -1)) < 0) {
        HELP(q_print("% Number of rising edges is expected (default is 3)\r\n"));
        return 4;
      }
    }
      
    if (ESP_OK != uart_set_wakeup_threshold(u, threshold))
      HELP(q_print("% UART threshold value was not changed\r\n"));

  } else if (q_isnumeric(argv[2])) {

    int64_t tim = read_timespec(argc, argv, 2, NULL);
    if (tim <= 0)
      return CMD_FAILED;

    if (ESP_OK == esp_sleep_enable_timer_wakeup(tim)) {
      Nap_alarm_set |= 1 << ESP_SLEEP_WAKEUP_TIMER;
      Nap_alarm_time = tim;
    }
    else {
      HELP(q_print("% Failed to set wakeup alarm timer\r\n"));
      return CMD_FAILED;
    }

    VERBOSE(q_printf("%% Sleep wakeup timer: %llu usec\r\n",tim));
  }

  return 0;
}

//"nap [deep] [NUM seconds|minutes|hours|days]"
// Put cpu into light or deep sleep
//
static int cmd_nap(int argc, char **argv) {

  bool deep = false;

  if (argc > 1) {
    if (!q_strcmp(argv[1],"deep"))
      deep = true;
  }

  if (!is_alarm_set(deep)) {
    HELP(q_printf("%% When should we wakeup? (set \"<i>nap alarm</>\" first)\r\n"));
    return CMD_FAILED;
  }

  HELP(q_printf("%% Entering %s sleep\r\n", deep ? "deep" : "light"));
  HELP(q_delay(100));  // give a chance to the q_print above to do its job

  if (deep)
    esp_deep_sleep_start();
  else 
    esp_light_sleep_start();

  HELP(q_print("% Returning from the sleep\r\n"));

  return 0;
}
#endif // #if COMPILING_ESPSHELL

