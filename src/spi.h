/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Burdzhanadze <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

// -- SPI bus --
// Not yet done
//
#if COMPILING_ESPSHELL
#if WITH_SPI
#include <esp32-hal-spi.h>
#if SOC_GPSPI_SUPPORTED

// Some low-end Espressif SoCs do not have VSPI. ArduinoCore 3.0.7 for ESP32S2 choose SPI3 as the name for 3rd SPI
#ifndef VSPI
#  ifdef SPI3
#    define VSPI SPI3
#  else
#    define VSPI 255
#  endif
#endif

static int cmd_spi_if(int argc, char **argv) { 

  unsigned int spi;
  static char prom[MAX_PROMPT_LEN];

  if (argc < 2)
    return -1;

  #if 0
  if ((spi = q_atol(argv[1], SOC_SPI_NUM)) >= SOC_SPI_NUM) {
    HELP(q_printf("%% <e>Valid SPI interface numbers are 0..%d</>\r\n", SOC_SPI_NUM - 1));
    return 1;
  }
  #endif
  if (!q_strcmp(argv[1],"fspi")) spi = FSPI; else 
  if (!q_strcmp(argv[1],"vspi")) spi = VSPI; else 
  if (!q_strcmp(argv[1],"hspi")) spi = HSPI; else  {
    HELP(q_printf("%% Expected hspi, vspi or fspi instead of \"%s\"\r\n",argv[1]));
    return 1;
  }

  // every SoC has FSPI and HSPI
  if (spi == 255) {
    q_printf("%% This SoC doesn't have VSPI bus. Only FSPI and HSPI are available\r\n");
    return 0;
  }
  

  sprintf(prom, PROMPT_SPI, spi);
  change_command_directory(spi, keywords_spi, prom, "SPI bus");
  return 0;
}

static int cmd_spi_clock(int argc, char **argv) { return 0; }
static int cmd_spi_up(int argc, char **argv) { return 0; }
static int cmd_spi_down(int argc, char **argv) { return 0; }
static int cmd_spi_write(int argc, char **argv) { return 0; }
#endif // is SPI supported by SoC?
#endif
#endif
