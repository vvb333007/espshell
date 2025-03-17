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

// -- UART interface command handlers & utility functions --
// baud, read, write, up, down and tap commands down below
//

#if COMPILING_ESPSHELL


// Create a 32-bit number which stores UART configuration. Arduino Core passes it directly to the ESP-IDF UART driver.
//  bits   - 5,6,7 or 8
//  parity - 0,2 or 3  (no-parity, even or odd)
//  sbits  - 1,2,3 (1 bit, 1.5 bits and 2 bits respectively)
//
static uint32_t __attribute__((const)) make_config(char bits, char parity, char sbits) {
    return 0x80000000 | (((bits - 5) << 2) | parity | (sbits << 4));
}

// uart_tap() : create uart-to-uart bridge between user's serial and "remote"
// everything that comes from the user goes to "remote" and  vice versa
//
// returns  when BREAK_KEY is pressed
// TODO: refactor, make BREAK_KEY a convar
static void
uart_tap(int remote) {
  /* Infinite loop. Interrupted with Ctrl+C code on the UART*/
  do {
    // 1. read all user input and send it to remote
    while (1) {
      int av;
      // fails when interface is down. must not happen.
      if ((av = console_available()) <= 0)
        break;
      // must not happen unless UART FIFO sizes were changed in ESP-IDF
      if (av > UART_RXTX_BUF)
        av = UART_RXTX_BUF;
        
      char buf[av];
      console_read_bytes(buf, av, portMAX_DELAY); // TODO: use shortest possible delay here
      // CTRL+C. most likely sent as a single byte (av == 1), so get away with
      // just checking if buf[0] == CTRL+C
      if (buf[0] == BREAK_KEY)
        return;
      uart_write_bytes(remote, buf, av);
      task_yield();
    }

    // 2. read all the data from remote uart and echo it to the user
    while (1) {
      size_t av = 0;
      if (ESP_OK != uart_get_buffered_data_len(remote, &av)) {
        HELP(q_printf(uartIsDown, remote));
        return;
      }
      if (av > UART_RXTX_BUF)
        av = UART_RXTX_BUF;
      else if (!av)
        break;

      char *buf[av];

      uart_read_bytes(remote, buf, av, portMAX_DELAY);
      console_write_bytes(buf, av);
      task_yield();
    }
  } while ( true );
}

//check if UART has its driver installed
static inline bool uart_isup(unsigned char u) {
  return u >= SOC_UART_NUM ? false : uart_is_driver_installed(u);
}

// "uart X"
// Change to uart command tree
//
static int cmd_uart_if(int argc, char **argv) {

  unsigned int u;
  static char prom[MAX_PROMPT_LEN];

  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((u = q_atol(argv[1], SOC_UART_NUM)) >= SOC_UART_NUM) {
    HELP(q_printf("%% <e>Valid UART interface numbers are 0..%d</>\r\n", SOC_UART_NUM - 1));
    return 1;
  }

  if (uart == u)
    HELP(q_print("% <i>You are about to configure the Serial espshell is running on. Be careful</>\r\n"));

  sprintf(prom, PROMPT_UART, u);
  change_command_directory(u, keywords_uart, prom, "UART configuration");
  return 0;
}


//"baud SPEED"
// Set UART speed
//
static int cmd_uart_baud(int argc, char **argv) {

  unsigned char u = Context;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (uart_isup(u)) {
    if (ESP_OK != uart_set_baudrate(u, q_atol(argv[1], UART_DEF_BAUDRATE)))
      q_print(Failed);
  } else
    q_printf(uartIsDown, u);

  return 0;
}


//"up RX TX BAUD [BITS] [no|even|odd] [1|1.5|2]"
// Initialize UART interface (RX/TX, no hw flowcontrol) baudrate BAUD
// Optional parameters are data width (5,6,7 or 8), parity and stopbits
//
static int cmd_uart_up(int argc, char **argv) {

  unsigned char u = Context, bits = 8, parity = 0, sbits = 1;
  unsigned int rx, tx, speed;

  if (argc < 4)
    return CMD_MISSING_ARG;

  if (!pin_exist((rx = q_atol(argv[1], BAD_PIN))))
    return 1;

  if (!pin_exist((tx = q_atol(argv[2], BAD_PIN))))
    return 2;

  if ((speed = q_atol(argv[3], 0)) == 0)
    return 3;

  // Optional 'data bits' parameter. Non-numeric values have no effect.
  // Default value is 8
  if (argc > 4) {
    bits = q_atol(argv[4],bits);
    if (bits < 5 || bits > 8) {
      q_print("% <e>Data bits can be 5,6,7 or 8</>\r\n");
      return 4;
    }
  }

  // Optional Parity parameter
  // Default value is "No Parity"
  if (argc > 5) { // expected: "no" "odd" "even"
    if (argv[5][0] == 'e')
      parity = 3;
    else if (argv[5][0] == 'o')
      parity = 2;
  }

  // Optional stopbits count
  // Default value is 1
  if (argc > 6) { // expected: "1" "1.5" "2"
    if (argv[6][0] == '1' && argv[6][1] == '.')
      sbits = 2;
    else if (argv[6][0] == '2')
      sbits = 3;
  }
  
  //VERBOSE(q_printf("%% uart%u up: Config word is %08x\r\n",u,make_config(bits, parity, sbits)));

  if (NULL != uartBegin(u, speed, make_config(bits, parity, sbits), rx, tx, 256, 0, false, 112))
    HELP(q_printf("%% UART%u is initialized (RX=pin%u, TX=pin%u, speed=%u)\r\n", u, rx, tx, speed));
  else
    q_print(Failed);

  return 0;
}

// "down"
// shutdown uart interface
//
static int cmd_uart_down(UNUSED int argc, UNUSED char **argv) {
  if (uart_isup(Context)) {
    HELP(q_printf("%% Shutting down UART%u\r\n", Context));
    uartEnd(Context);
  }
  return 0;
}

// "read"
// Read bytes which are currently received (are in FIFO)
//
static int cmd_uart_read(UNUSED int argc, UNUSED char **argv) {

  unsigned char u = Context;
  size_t available = 0, tmp = 0;

  if (uart_isup(u)) {
    if (ESP_OK == uart_get_buffered_data_len(u, &available)) {
      tmp = available;
      while (available--) {
        unsigned char c;
        // We use small number as a delay because we don't want read() to block
        if (uart_read_bytes(u, &c, 1, /*portMAX_DELAY*/ pdMS_TO_TICKS(500) ) == 1) {
          if (c >= ' ' || c == '\r' || c == '\n' || c == '\t')
            q_printf("%c", c);
          else
            q_printf("\\x%02x", c);
        }
      }
    }
  } else
    q_printf(uartIsDown, u);

  q_printf("\r\n%% EOF (%d bytes)\r\n", tmp);
  return 0;
}

// "write TEXT"
// Send arbitrary text and/or bytes
//
static int cmd_uart_write(int argc, char **argv) {

  int sent = 0, size;
  unsigned char u = Context;
  char *out = NULL;

  if (argc < 2)
    return CMD_MISSING_ARG;
  if (uart_isup(u)) {
    if ((size = text2buf(argc, argv, 1, &out)) > 0)
      if ((size = uart_write_bytes(u, out, size)) > 0)
        sent += size;
    if (out)
      q_free(out);
  } else
    q_printf(uartIsDown, u);
  HELP(q_printf("%% %u bytes sent\r\n", sent));
  return 0;
}

//"tap"
// uart-to-console bridge
//
static int cmd_uart_tap(int argc, char **argv) {
  unsigned char u = Context;
  if (uart != u) {
    if (uart_isup(u)) {
      q_printf("%% Tapping to UART%d, CTRL+C to exit\r\n", u);
      uart_tap(u);  //TODO: inline it here
      q_print("\r\n% Ctrl+C, exiting\r\n");
    } else
      q_printf(uartIsDown, u);
  } else
    q_printf("%% <e>Can not bridge uart%u to uart%u</>\r\n", u, u);
  return 0;
}

#endif //#if COMPILING_ESPSHELL
