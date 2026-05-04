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
// TODO: hardware flow control support

#if COMPILING_ESPSHELL

// Create a 32-bit number which stores UART configuration. Arduino Core passes it directly to the ESP-IDF UART driver.
//  bits   - 5,6,7 or 8
//  parity - 0,2 or 3  (no-parity, even or odd)
//  sbits  - 1,2,3 (1 bit, 1.5 bits and 2 bits respectively)
//
static uint32_t __attribute__((const)) make_config(char bits, char parity, char sbits) {
    return 0x80000000 | (((bits - 5) << 2) | parity | (sbits << 4));
}


// uart_tap() : create console-to-uart bridge between user's serial and "remote"
// everything that comes from the user goes to "remote" and  vice versa
//
// returns  
// true  (recoverable) when Break_key is pressed (a convar, Ctrl+C by default) or
// false (non-recoverable) when remote interface is down
//
static bool
uart_tap(int remote) {

  size_t av;
  char rx;
  char buf[UART_RXTX_BUF];

  /* Infinite loop. Interrupted with Break_key code on the UART*/
  while ( true ) {
  
    // Process user side, char by char. If FIFO is empty - let other tasks do their job
    if (console_read_bytes(&rx, 1, 3) == 1) {
      if (rx == Break_key)
        return true;
      uart_write_bytes(remote, &rx, 1);
    } else
      q_yield();
    
    // Process remote side, bulk
    if (ESP_OK == uart_get_buffered_data_len(remote, &av)) {
      if (av > 0) {
        if (av > UART_RXTX_BUF)
          av = UART_RXTX_BUF;
        // send to the user what was read from the remote
        if ((av = uart_read_bytes(remote, buf, av, 3)) > 0)
          console_write_bytes(buf, av);
      }
    } else {
      // Hardware problems
      HELP(q_printf(Error_UART_Down, remote));
      break;
    }

  } // while( true )

  return false;
}

//check if UART has its driver installed
static inline bool uart_isup(unsigned char u) {
  return u >= NUM_UARTS ? false : uart_is_driver_installed(u);
}


// TODO: display pins allocated for the UART
// TODO: cmd_uart_save()
// esp32#>show uart NUM
// esp32-uart1#>show
//
static int cmd_show_uart(int argc, char **argv) {

  uint8_t u;

  uart_word_length_t data_bit;
  uart_stop_bits_t   stop_bits;
  uart_parity_t      parity_mode;
  uint32_t           baudrate;
  uart_hw_flowcontrol_t flow_ctrl;
  int                wakeup_threshold;

  // called as "show" within the uart subdirectory
  if (argc < 2)
    u = context_get_uint();
  else 
  // called as "show uart NUM" as a global command
  if (argc > 2) {
    if ((u = q_atol(argv[2], NUM_UARTS)) >= NUM_UARTS) {
      HELP(q_print("% UART number is out of range\r\n"));
      return CMD_FAILED;
    }
  } else
    return CMD_MISSING_ARG;

  if (!uart_isup(u)) {
    q_printf("%% UART%d is down, nothing to see\r\n",u);
    return 0;
  }

  uart_get_word_length(u,&data_bit);
  uart_get_stop_bits(u, &stop_bits);
  uart_get_parity(u, &parity_mode);
  
  uart_get_baudrate(u, &baudrate);
  uart_get_hw_flow_ctrl(u, &flow_ctrl);
  uart_get_wakeup_threshold(u, &wakeup_threshold);

  q_printf( "%% -- UART#%u is up --\r\n"
            "%% Data bits: <i>%u</>, Stop bits: <i>%s</>, Parity: <i>%s</>\r\n"
            "%% Baud rate: <i>%lu</> (real)\r\n"
            "%% Hardware flow control: <i>%s</>\r\n"
            "%% Sleep wakeup threshold: <i>%d</> positive edges\r\n",
            u,
            data_bit + 5,
            stop_bits == UART_STOP_BITS_1 ? "1"
                                          : (stop_bits == UART_STOP_BITS_2 ? "2"
                                                                           : "1.5"),
            parity_mode ? (parity_mode & 1 ? "odd"
                                           : "even")
                        : "none",
            baudrate,
            flow_ctrl ? "enabled" : "disabled",
            wakeup_threshold);

  return 0;
}

// "uart X"
// Change to uart command tree
//
static int cmd_uart_if(int argc, char **argv) {

  unsigned int u;
  static char prom[MAX_PROMPT_LEN];

  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((u = q_atol(argv[1], NUM_UARTS)) >= NUM_UARTS) {
    HELP(q_printf("%% <e>Valid UART interface numbers are 0..%d</>\r\n", NUM_UARTS - 1));
    return 1;
  }

  if (uart == u)
    HELP(q_print("% <i>You are about to configure the Serial, espshell is running on. Be careful</>\r\n"));

  // create esp32-uartX> prompt and change command directory to "uart"
  // save /u/ in Context
  sprintf(prom, PROMPT_UART, u);
  change_command_directory(u, KEYWORDS(uart), prom, "UART configuration");
  return 0;
}

//"invert rx|tx|cts|dsr|nothing"
//
static int cmd_uart_invert(int argc, char **argv) {

  uart_signal_inv_t inv_sig = (uart_signal_inv_t )0;
  unsigned char u = context_get_uint();

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!uart_isup(u)) {
      q_printf(Error_UART_Down, u);
      return CMD_FAILED;
  }

  for (int i = 1; i < argc; i++) {
    if (!q_strcmp(argv[i],"rx")) inv_sig |= UART_SIGNAL_RXD_INV;  else
    if (!q_strcmp(argv[i],"tx")) inv_sig |= UART_SIGNAL_TXD_INV;  else
    // Not currently used
    if (!q_strcmp(argv[i],"cts")) inv_sig |= UART_SIGNAL_CTS_INV; else
    if (!q_strcmp(argv[i],"dsr")) inv_sig |= UART_SIGNAL_DSR_INV; else
    if (!q_strcmp(argv[i],"none") ||
        !q_strcmp(argv[i],"nothing") ||
        !q_strcmp(argv[i],"disable")) inv_sig = 0;                else return i;
  }

  if (ESP_OK != uart_set_line_inverse(u, inv_sig))
    q_print("% Can not set line inverse\r\n");

  return 0;
}

//"mode normal | half | coldet | app | irda"
//
static int cmd_uart_mode(int argc, char **argv) {

  uart_mode_t mode = UART_MODE_UART;
  unsigned char u = context_get_uint();

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!uart_isup(u)) {
      q_printf(Error_UART_Down, u);
      return CMD_FAILED;
  }

  for (int i = 1; i < argc; i++) {
    if (!q_strcmp(argv[i],"half"))   mode = UART_MODE_RS485_HALF_DUPLEX;   else
    if (!q_strcmp(argv[i],"coldet")) mode = UART_MODE_RS485_COLLISION_DETECT;  else
    if (!q_strcmp(argv[i],"app"))    mode = UART_MODE_RS485_APP_CTRL;      else
    if (!q_strcmp(argv[i],"irda"))   mode = UART_MODE_IRDA;                else
    if (!q_strcmp(argv[i],"normal") ||
        !q_strcmp(argv[i],"uart"))   mode = UART_MODE_UART;                else return i;
  }

  if (ESP_OK != uart_set_mode(u, mode))
    q_print("% Can not switch UART to the requested mode\r\n");

  return 0;
}

//"pin rx|tx|rts|cts NUM"
//
static int cmd_uart_pin(int argc, char **argv) {

  unsigned int pin, 
               rx  = UART_PIN_NO_CHANGE,
               tx  = UART_PIN_NO_CHANGE,
               rts = UART_PIN_NO_CHANGE,
               cts = UART_PIN_NO_CHANGE;  

  unsigned char u = context_get_uint();

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!uart_isup(u)) {
      q_printf(Error_UART_Down, u);
      return CMD_FAILED;
  }

  for (int i = 1; i < argc; i += 2) {

    if (i + 1 >= argc)
      return CMD_MISSING_ARG;

    if (!q_isnumeric(argv[i + 1])) {
      q_printf("%% Pin (GPIO) number is expected after \"%s\"\r\n",argv[i]);
      return i + 1;
    }

    if (!pin_exist((pin = q_atol(argv[i + 1], DEF_BAD)))) 
      return i + 1;

    if (!q_strcmp(argv[i],"rx")) rx = pin;  else
    if (!q_strcmp(argv[i],"tx")) tx = pin;  else
    if (!q_strcmp(argv[i],"rts")) rts = pin; else
    if (!q_strcmp(argv[i],"cts")) cts = pin; else return i;
  }

  // Bye-bye Periman :(. We can just call pinMode() here because it calls Periman's deinit
  // effectively shutting down the UART. set_pin(), however reassign signals without changing Periman's saved values
  // so Periman and ESP-IDF here go out of sync.
  if (ESP_OK != uart_set_pin(u, tx, rx, cts, rts)) {
    q_print("% Failed to set new UART pins\r\n");
    return CMD_FAILED;
  }

  return 0;
}


//"baud SPEED"
// Set UART speed
//
static int cmd_uart_baud(int argc, char **argv) {

  unsigned char u = context_get_uint();

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (uart_isup(u)) {
    if (ESP_OK != uart_set_baudrate(u, q_atol(argv[1], UART_DEF_BAUDRATE)))
      q_print(Failed);
  } else
    q_printf(Error_UART_Down, u);

  return 0;
}


//"up RX TX BAUD [BITS] [no|even|odd] [1|1.5|2]"
// Initialize UART interface (RX/TX, no hw flowcontrol) baudrate BAUD
// Optional parameters are data width (5,6,7 or 8), parity and stopbits
//
static int cmd_uart_up(int argc, char **argv) {

  unsigned char u = context_get_uint(), bits = 8, parity = 0, sbits = 1;
  unsigned int rx, tx, speed;

  if (argc < 4)
    return CMD_MISSING_ARG;

  if (!pin_exist((rx = q_atol(argv[1], BAD_PIN))))
    return 1;

  if (!pin_exist((tx = q_atol(argv[2], BAD_PIN))))
    return 2;

  if (pin_isvirtual(rx) || pin_isvirtual(tx))
    return CMD_FAILED;

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
  
//  VERBOSE(q_printf("%% uart%u up: Config word is %08x, bits=%u, parity=%u, sbits=%u\r\n",u,make_config(bits, parity, sbits), bits, parity, sbits));

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

  if (uart_isup(context_get_uint())) {
    HELP(q_printf("%% Shutting down UART%u\r\n", context_get_uint()));
    uartEnd(context_get_uint());
  }

  return 0;
}

// "read"
// Read bytes which are currently received (are in FIFO)
//
static int cmd_uart_read(UNUSED int argc, UNUSED char **argv) {

  unsigned char u = context_get_uint();
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
    q_printf(Error_UART_Down, u);

  q_printf("\r\n%% EOF (%d bytes)\r\n", tmp);
  return 0;
}

// "write TEXT"
// Send arbitrary text and/or bytes
//
static int cmd_uart_write(int argc, char **argv) {

  int sent = 0, size;
  unsigned char u = context_get_uint();
  char *out = NULL;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (uart_isup(u)) {
    if ((size = userinput_join(argc, argv, 1, &out)) > 0)
      if ((size = uart_write_bytes(u, out, size)) > 0)
        sent += size;
    if (out)
      q_free(out);
  } else
    q_printf(Error_UART_Down, u);
  HELP(q_printf("%% %u bytes sent\r\n", sent));
  return 0;
}


// uart-to-console bridge
// Bridges all user input to the uart and vice versa.
//
static int cmd_uart_tap(int argc, char **argv) {

  unsigned char u = context_get_uint();

  if (uart != u) {
    if (uart_isup(u)) {
      q_printf("%% Tapping to UART%d, CTRL+C to exit\r\n", u);
      if (uart_tap(u))
        q_print("\r\n% Ctrl+C, exiting\r\n");
      else
        q_print("\r\n% Remote UART is down, exiting\r\n");
    } else
      q_printf(Error_UART_Down, u);
  } else
    q_printf("%% <e>Can not bridge uart%u to uart%u</>\r\n", u, u);
  return 0;
}

#endif //#if COMPILING_ESPSHELL
