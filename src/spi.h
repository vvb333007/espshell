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

// -- SPI bus --
// TODO: not functional right now. Need an use-case
#if COMPILING_ESPSHELL
#if WITH_SPI
#if SOC_GPSPI_SUPPORTED

#include <esp32-hal-spi.h>

// Some Espressif SoCs do not have VSPI. ArduinoCore 3.0.7 for ESP32S2 choose SPI3 as the name for 3rd SPI
#ifndef VSPI
#  ifdef SPI3
#    define VSPI SPI3
#  else
#    define VSPI 255 // TODO: no magic numbers!
#  endif
#endif

#define NUM_SPI 3 // TODO: no magic numbers!



static int cmd_spi_if(int argc, char **argv) { 

  unsigned int spi;
  static char prom[MAX_PROMPT_LEN];

  if (argc < 2)
    return CMD_MISSING_ARG;

  // ESP-IDF style : "spi 3"
  if (q_isnumeric(argv[1])) {

    if ((spi = q_atol(argv[1], NUM_SPI)) >= NUM_SPI) {
      HELP(q_printf("%% <e>Valid SPI interface numbers are 0..%d</>\r\n", NUM_SPI));
      return 1;
    }

  } else
  // Arduino style: "spi vspi"
  if (!q_strcmp(argv[1],"fspi")) spi = FSPI; else 
  if (!q_strcmp(argv[1],"vspi")) spi = VSPI; else 
  if (!q_strcmp(argv[1],"hspi")) spi = HSPI; else {

    HELP(q_printf("%% <e>Expected SPI bus number or name (e.g. 1, 2, hspi or vspi)</>\r\n"));
    return 1;
  }

  // every SoC has FSPI and HSPI
  if (spi == 255) {

    q_printf("%% This SoC does not have VSPI (spi3) bus.\r\n"
             "%% Only FSPI (spi1) and HSPI(spi2) are available\r\n");

    return 0;
  }
  
  if (spi == 0)
    HELP(q_print("% <i>You are about to configure SPI0 (flash access bus). Be careful.</>\r\n"));


  sprintf(prom, PROMPT_SPI, spi);
  change_command_directory(spi, KEYWORDS(spi), prom, "SPI bus");



  return 0;
}

static int cmd_spi_clock(int argc, char **argv) { return 0; }
static int cmd_spi_mode(int argc, char **argv) { return 0; }
static int cmd_spi_order(int argc, char **argv) { return 0; }
static int cmd_spi_chip_select(int argc, char **argv) { return 0; }
static int cmd_spi_up(int argc, char **argv) { return 0; }
static int cmd_spi_down(int argc, char **argv) { return 0; }
static int cmd_spi_xfer(int argc, char **argv) { return 0; }
#endif // SPI supported? 
#endif
#endif

