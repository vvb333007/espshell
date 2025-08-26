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

// TODO: support 10 bit address
//
#if COMPILING_ESPSHELL

#define NUM_I2C SOC_I2C_NUM

// unfortunately this one is not in header files
extern bool i2cIsInit(uint8_t i2c_num);


//check if I2C has its driver installed
// TODO: this is bad. need more low level call. esp32cams i2c is not detected as "up"
//
static inline bool i2c_isup(unsigned char iic) {
  return (iic >= NUM_I2C) ? false : i2cIsInit(iic);
}

//"iic NUM"
//"i2c NUM"
// Save context, switch command list, change the prompt
//
static int cmd_i2c_if(int argc, char **argv) {

  unsigned int i;
  static char prom[MAX_PROMPT_LEN];

  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((i = q_atol(argv[1], NUM_I2C)) >= NUM_I2C) {
    HELP(q_printf("%% <e>Valid I2C interface numbers are 0..%d</>\r\n", NUM_I2C - 1));
    return 1;
  }

  sprintf(prom, PROMPT_I2C, i);
  change_command_directory(i, KEYWORDS(iic), prom, "I2C configuration");
  return 0;
}

//"clock FREQ"
// Set I2C bus clock
//
static int cmd_i2c_clock(int argc, char **argv) {

  unsigned char iic = context_get_uint();

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!i2c_isup(iic)) {
    q_printf(i2cIsDown, iic);
    return 0;
  }

  // set clock to 100kHz if atol() fails
  if (ESP_OK != i2cSetClock(iic, q_atol(argv[1], 100000)))
    q_print(Failed);

  return 0;
}



//"up SDA SCL [CLOCK]"
// initialize i2c interface
//
static int cmd_i2c_up(int argc, char **argv) {

  unsigned char iic = context_get_uint(), sda, scl;
  unsigned int clock = argc < 4 ? I2C_DEF_FREQ : q_atol(argv[3], I2C_DEF_FREQ);

  if (argc < 3) return CMD_MISSING_ARG;
  if (i2c_isup(iic)) return 0;
  if (!pin_exist((sda = q_atol(argv[1], 255)))) return 1;
  if (!pin_exist((scl = q_atol(argv[2], 255)))) return 2;
  if (ESP_OK != i2cInit(iic, sda, scl, clock))
    q_print(Failed);
  else
    HELP(q_printf("%% i2c%u is initialized, SDA=pin%u, SCL=pin%u, CLOCK=%.1f kHz\r\n", iic, sda, scl, (float)clock / 1000.0f));

  return 0;
}

//"down"
// Shutdown i2c interface
//
static int cmd_i2c_down(int argc, char **argv) {

  unsigned char iic = context_get_uint();
  if (i2c_isup(iic))
    i2cDeinit(iic);
  return 0;
}

// "read
// Read the I2C device at address ADRESS, request COUNT bytes
//
static int cmd_i2c_read(int argc, char **argv) {

  unsigned char iic = context_get_uint(), addr;
  int size;
  size_t got = 0;

  if (argc < 3) return CMD_MISSING_ARG;
  if ((addr = q_atol(argv[1], 0)) == 0) return 1;
  if ((size = q_atol(argv[2], I2C_RXTX_BUF + 1)) > I2C_RXTX_BUF) {
    size = I2C_RXTX_BUF;
    q_printf("%% Size adjusted to the maxumum: %u bytes\r\n", size);
  }

  unsigned char data[size];

  if (i2c_isup(iic)) {
    if (i2cRead(iic, addr, data, size, 2000, &got) != ESP_OK) // TODO: no magic numbers
      q_print(Failed);
    else {
      if (got != size) {
        q_printf("%% <e>Requested %d bytes but read %d</>\r\n", size, got);
        got = size;
      }
      HELP(q_printf("%% I2C%d received %d bytes:\r\n", iic, got));
      q_printhex(data, got);
    }
  } else
    q_printf(i2cIsDown, iic);
  return 0;
}

// "write ADDRESS BYTE1 [BYTE2 BYTE3 ... BYTEn]"
// Write bytes to the i2c device at address ADDRESS
// BYTE - is hex number with or without 0x
//
static int cmd_i2c_write(int argc, char **argv) {

  unsigned char iic = context_get_uint(), addr;
  int i, size;

  // at least 1 byte but not too much
  if (argc < 3 || argc > I2C_RXTX_BUF)
    return CMD_MISSING_ARG;

  unsigned char data[argc];

  if (i2c_isup(iic)) {
    // get i2c slave address
    if ((addr = q_atol(argv[1], 0)) == 0)
      return 1;

    // read all bytes to the data buffer
    for (i = 2, size = 0; i < argc; i++) {
      if (!ishex2(argv[i]))
        return i;
      data[size++] = hex2uint8(argv[i]);
    }
    // send over
    HELP(q_printf("%% Sending %d bytes over I2C%d\r\n", size, iic));
    if (ESP_OK != i2cWrite(iic, addr, data, size, 2000))
      q_print(Failed);
  } else
    q_printf(i2cIsDown, iic);
  return 0;
}

// "scan"
// Scan i2c bus and print out devices found
//
static int cmd_i2c_scan(int argc, char **argv) {

  unsigned char iic = context_get_uint(), addr;
  int i;

  if (i2c_isup(iic)) {
    HELP(q_printf("%% Scanning I2C%d bus...\r\n", iic));

    for (addr = 1, i = 0; addr < 128; addr++) {
      unsigned char b;
      if (ESP_OK == i2cWrite(iic, addr, &b, 0, 500)) {
        i++;
        q_printf("%% Device found at <i>address 0x%02x</>\r\n", addr);
      }
    }
    if (!i)
      q_print("% Nothing found\r\n");
    else
      q_printf("%% <i>%d</> devices found\r\n", i);
  } else
    q_printf(i2cIsDown, iic);
  return 0;
}
#endif // #if COMPILING_ESPSHELL

