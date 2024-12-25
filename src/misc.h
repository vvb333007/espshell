/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


// -- misc command handlers --

#if COMPILING_ESPSHELL


// "uptime"
//
// Displays system uptime as returned by esp_timer_get_time() counter
// Displays last reboot cause
static int
cmd_uptime(UNUSED int argc, UNUSED char **argv) {

  unsigned int sec, min = 0, hr = 0, day = 0;
  sec = (unsigned int)(esp_timer_get_time() / 1000000ULL);

  const char *rr;

  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: rr = "<i>Board power-on"; break;
    case ESP_RST_SW: rr = "<i>reload command"; break;
    case ESP_RST_DEEPSLEEP: rr = "<i>Returned from a deep sleep"; break;
    case ESP_RST_SDIO: rr = "<i>SDIO event"; break;
    case ESP_RST_USB: rr = "<i>USB event"; break;
    case ESP_RST_JTAG: rr = "<i>JTAG event"; break;

    case ESP_RST_PANIC: rr = "<w>panic() called :-("; break;
    case ESP_RST_INT_WDT: rr = "<w>an interrupt watchdog"; break;
    case ESP_RST_TASK_WDT: rr = "<w>a task watchdog"; break;
    case ESP_RST_WDT: rr = "<w>an unspecified watchdog"; break;
    case ESP_RST_CPU_LOCKUP: rr = "<w>lockup (double exception)"; break;

    case ESP_RST_BROWNOUT: rr = "<e>Brownout"; break;
    case ESP_RST_PWR_GLITCH: rr = "<e>Power glitch"; break;
    case ESP_RST_EFUSE: rr = "<e>eFuse errors"; break;
    default: rr = "<e>no idea";
  };

  q_print("% Last boot was ");
  if (sec >= 60 * 60 * 24) {
    day = sec / (60 * 60 * 24);
    sec = sec % (60 * 60 * 24);
    q_printf("%u day%s ", day, day == 1 ? "" : "s");
  }
  if (sec >= 60 * 60) {
    hr = sec / (60 * 60);
    sec = sec % (60 * 60);
    q_printf("%u hour%s ", hr, hr == 1 ? "" : "s");
  }
  if (sec >= 60) {
    min = sec / 60;
    sec = sec % 60;
    q_printf("%u minute%s ", min, min == 1 ? "" : "s");
  }

  q_printf("%u second%s ago\r\n%% Restart reason was \"%s</>\"\r\n", sec, sec == 1 ? "" : "s", rr);


  return 0;
}


//TAG:tty
//"tty NUM"
//
// Set UART (or USBCDC) to use by this shell.
// this command is hidden to not confuse users
static int cmd_tty(int argc, char **argv) {

  unsigned char tty;

  if (argc < 2)
    return -1;

  if ((tty = q_atol(argv[1], 100)) < 100) {
    // if not USB then check if requested UART is up & running
    if ((tty == 99) || ((tty < 99) && uart_isup(tty))) {
      HELP(q_print("% See you there\r\n"));
      console_here(tty);
      return 0;
    }
  } else
    HELP(q_print("%% <e>UART number is expected. (use 99 for USB CDC)</>\r\n"));

  if (tty < 99)
    q_printf(uartIsDown, tty);

  return 0;
}

//TAG:echo
//"echo on|off|silent"
//
// Enable/disable local echo. Normally enabled, it lets software
// like TeraTerm and PuTTY to be used. Turning echo off supresses
// all shell output (except for command handlers output)
//
// Setting "echo silent" has effect of "echo off" + all command output
// is supressed as well. commands executed do not output anything
//
static int cmd_echo(int argc, char **argv) {

  if (argc < 2)
    q_printf("%% Echo is \"%s\"\r\n", Echo ? "on" : "off");  //if echo is silent we can't see it anyway so no bother printing
  else {
    if (!q_strcmp(argv[1], "on")) Echo = 1;
    else if (!q_strcmp(argv[1], "off")) Echo = 0;
    else if (!q_strcmp(argv[1], "silent")) Echo = -1;
    else return 1;
  }
  return 0;
}


// enable/disable history saving. mostly for memory leaks detection
static void
history_enable(bool enable) {
  if (!enable) {
    if (History) {
      int i;
      for (i = 0; i < H.Size; i++) {
        if (H.Lines[i]) {
          q_free(H.Lines[i]);
          H.Lines[i] = NULL;
        }
      }
      H.Size = H.Pos = 0;
      History = false;
      HELP(q_printf("%% Command history purged, history is disabled\r\n"));
    }
  } else {
    if (!History) {
      History = true;
      HELP(q_printf("%% Command history is enabled\r\n"));
    }
  }
}


// "history [on|off]"
// disable/enable/show status for command history
//
static int cmd_history(int argc, char **argv) {
  if (argc < 2)
    // no arguments? display history status
    q_printf("%% History is %s\r\n", History ? "enabled" : "disabled");
  else
    // history off: disable history and free all memory associated with history
    if (!q_strcmp(argv[1], "off"))
      history_enable(false);
    else
      // history on: enable history
      if (!q_strcmp(argv[1], "on"))
        history_enable(true);
      else return 1;
  return 0;
}

#if WITH_COLOR
// "colors [on|off|auto]"
//
// disable/enable terminal colors (or show colorer status)
// Automated output processing or broken terminals need this
//
static int cmd_colors(int argc, char **argv) {
  if (argc < 2)
    q_printf("%% Color is \"%s\"\r\n", ColorAuto ? "auto" : (Color ? "on" : "off"));
  else
    // auto-colors: colors are enabled by ESPShell if it detects proper terminal software on user side
    if (!q_strcmp(argv[1], "auto")) {
      Color = false;
      ColorAuto = true;
    } else
      // colors off: don't send any ANSI color escape sequences. Use with broken terminals
      if (!q_strcmp(argv[1], "off"))
        ColorAuto = Color = false;
      else
        // colors on : enable color sequences
        if (!q_strcmp(argv[1], "on")) {
          ColorAuto = false;
          Color = true;
        } else
          // hidden command
          if (!q_strcmp(argv[1], "test")) {
            int i;

            for (i = 0; i < 8; i++)
              q_printf("3%d: \e[3%dmLorem Ipsum Dolor 1234567890 @#\e[0m 9%d\e[9%dmLorem Ipsum Dolor 1234567890 @#\e[0m\r\n", i, i, i, i);

            for (i = 0; i < 108; i++)
              q_printf("%d: \e[%dmLorem Ipsum Dolor 1234567890 @#\e[0m\r\n", i, i);

          } else
            return 1;

  return 0;
}
#endif  //WITH_COLOR
#endif // #if COMPILING_ESPSHELL
