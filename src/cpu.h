/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Burdzhanadze <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#if COMPILING_ESPSHELL

// not in .h files of ArduinoCore.
EXTERN uint32_t getCpuFrequencyMhz();
EXTERN bool setCpuFrequencyMhz(uint32_t);
EXTERN uint32_t getXtalFrequencyMhz();
EXTERN uint32_t getApbFrequency();


//"cpu"
//
// Display CPU ID information, frequencies and
// chip temperature
//
// TODO: check on different CPUs
static int cmd_cpu(int argc, char **argv) {

  esp_chip_info_t chip_info;

  uint32_t chip_ver;
  uint32_t pkg_ver;
  const char *chipid = "ESP32-(Unknown)>";

  esp_chip_info(&chip_info);

#if CONFIG_IDF_TARGET_ESP32
  chip_ver = REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_PACKAGE);
  pkg_ver = chip_ver & 0x7;

  switch (pkg_ver) {
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6: if (chip_info.revision / 100 == 3) 
                                              chipid = "ESP32-D0WDQ6-V3"; else 
                                              chipid = "ESP32-D0WDQ6"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5: if (chip_info.revision / 100 == 3) 
                                              chipid = "ESP32-D0WD-V3"; else 
                                              chipid = "ESP32-D0WD"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5:   chipid = "ESP32-D2WD-Q5"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD2:   chipid = "ESP32-PICO-D2"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4:   chipid = "ESP32-PICO-D4"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302: chipid = "ESP32-PICO-V3-02"; break;
    case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3: chipid = "ESP32-D0WDR2-V3"; break;
    default: q_printf("%% Detected PKG_VER=%04x\r\n", (unsigned int)pkg_ver);
  }
#elif CONFIG_IDF_TARGET_ESP32S2
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
  }
#endif  //CONFIG_IDF_TARGET_XXX

  q_printf("%% CPU ID: %s, Rev.: %d.%d\r\n%% CPU frequency is %luMhz, Xtal %luMhz, APB bus %luMhz\r\n%% Chip temperature: %.1f\xe8 C\r\n",
           chipid,
           (chip_info.revision >> 8) & 0xf,
           chip_info.revision & 0xff,
           getCpuFrequencyMhz(),
           getXtalFrequencyMhz(),
           getApbFrequency() / 1000000,
           temperatureRead());

  q_printf("%%\r\n%% Sketch is running on " ARDUINO_BOARD "/(" ARDUINO_VARIANT "), uses Arduino Core v%s, based on\r\n%% Espressif ESP-IDF version \"%s\"\r\n", ESP_ARDUINO_VERSION_STR, esp_get_idf_version());
  cmd_uptime(argc, argv);
  return 0;
}

//"cpu CLOCK"
//
// Set cpu frequency.
//
static int cmd_cpu_freq(int argc, char **argv) {

  if (argc < 2)
    return -1;  // not enough arguments

  unsigned int freq;

  if ((freq = q_atol(argv[1], 0)) == 0) {
    HELP(q_print("% Numeric value is expected (e.g. 240): frequency in MHz\r\n"));
    return 1;
  }

  // ESP32 boards do support 240,160,120 and 80 Mhz. If XTAL is 40Mhz or more then we also support
  // XTAL,XTAL/2 and XTAL/4 frequencies. If XTAL frequency is lower than 40 Mhz then we only support XTAL and XTAL/2
  // additional frequencies
  while (freq != 240 && freq != 160 && freq != 120 && freq != 80) {

    unsigned int xtal = getXtalFrequencyMhz();

    if ((freq == xtal) || (freq == xtal / 2)) break;
    if ((xtal >= 40) && (freq == xtal / 4)) break;
    q_printf("%% <e>%u MHz is unsupported frequency</>\r\n", freq);
#if WITH_HELP
    q_printf("%% Supported frequencies are: 240, 160, 120, 80, %u, %u", xtal, xtal / 2);
    if (xtal >= 40)
      q_printf(" and %u", xtal / 4);
    q_print(" MHz\r\n");
#endif  //WITH_HELP
    return 1;
  }

  if (!setCpuFrequencyMhz(freq))
    q_print(Failed);

  return 0;
}

//TAG:reload
//"reload"
static int NORETURN cmd_reload(UNUSED int argc, UNUSED char **argv) {
  esp_restart();
  /* NOT REACHED */
  //return 0;
}


//"nap [SECONDS]"
// Put cpu into light sleep
//
static int cmd_nap(int argc, char **argv) {

  static bool isen = false;

  if (argc < 2) {
    // no args: light sleep until 3 positive edges received by uart (i.e. wake up is by pressing <Enter>)
    esp_sleep_enable_uart_wakeup(uart);
    isen = true;
    uart_set_wakeup_threshold(uart, 3);  // 3 positive edges on RX pin to wake up ('spacebar' key two times or 'enter' once)
  } else
    // "nap SECONDS" command: sleep specified number of seconds. Wakeup by timer only.
    if (argc < 3) {
      unsigned long sleep;
      if (isen) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_UART);  //disable wakeup by uart
        isen = false;
      }
      if ((sleep = q_atol(argv[1], DEF_BAD)) == DEF_BAD) {
        HELP(q_printf("%% <e>Sleep time in seconds expected, instead of \"%s\"</>\r\n", argv[1]));
        return 1;
      }
      esp_sleep_enable_timer_wakeup(1000000UL * (unsigned long)sleep);
    }
  HELP(q_print("% Entering light sleep\r\n"));
  delay(100);  // give a chance to printf above do its job
  esp_light_sleep_start();
  HELP(q_print("%% Resuming\r\n"));
  return 0;
}
#endif // #if COMPILING_ESPSHELL
