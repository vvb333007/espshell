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

// -- Code which doesn't fit other files --
// Misc command handlers, misc code, misc macros..


#if COMPILING_ESPSHELL

// "uptime"
//
// Displays system uptime as returned by esp_timer_get_time() counter
// Displays last reboot cause
//
static int cmd_uptime(UNUSED int argc, UNUSED char **argv) {

  // Restart Reason (or Reset Reason)
  const char *rr[] = {
    "<w>reason can not be determined",   "<g>board power-on",                   "<g>external (pin) reset",   "<g>reload command",
    "<e>exception and/or kernel panic",  "<e>interrupt watchdog",               "<e>task watchdog",          "<e>other watchdog",
    "<g>returning from a deep sleep",    "<w>brownout (software or hardware)",  "<i>reset over SDIO",        "<i>reset by USB peripheral",
    "<i>reset by JTAG",                  "<e>reset due to eFuse error",         "<w>power glitch detected",  "<e>CPU lock up (double exception)"
  };

  unsigned int val, sec = q_millis() / 1000, div = 60 * 60 * 24;

  // lets check if esp_reset_reason_t is still what we think it is: RST_CPU_LOCKUP must be the last entry (entry #15)
  static_assert(ESP_RST_CPU_LOCKUP == 15, "Code review is required");

#define XX(_Text, _Divider) do {\
  if (sec >= div) { \
    val = sec / div; \
    sec = sec % div; \
    q_printf("%u " #_Text "%s ", PPA(val)); \
  } \
  div /= _Divider; \
} while (0)

  q_print("% Last boot was ");

  XX(day,24);
  XX(hour,60);
  XX(minute,60);

  q_printf( "%u second%s ago\r\n"
            "%% Reset reason: \"%s</>\"\r\n", 
            PPA(sec), 
            rr[esp_reset_reason()]);
  return 0;
}


//"tty NUM"
//
// Set UART (or USBCDC) to use by this shell.
// this command is hidden to not confuse users
//
static int cmd_tty(int argc, char **argv) {

  unsigned char tty;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((tty = q_atol(argv[1], 100)) < 100) {
    // if not USB then check if requested UART is up & running
    if ((tty == 99) || ((tty < 99) && uart_isup(tty))) {
      HELP(q_print("% See you there\r\n"));
      console_here(tty);
      return 0;
    }
  } else {
    HELP(q_print("%% <e>UART number is expected. (use 99 for USB CDC)</>\r\n"));
    return 1;
  }

  if (tty < 99)
    q_printf(uartIsDown, tty);

  return 0;
}

//"echo on|off|silent"
//
// Enable/disable local echo. Normally enabled, it lets software
// like TeraTerm and PuTTY to be used. Turning echo off supresses
// all shell output (except for command handlers output)
//
// Setting "echo silent" has effect of "echo off" + all command output
// is supressed as well. commands are executed but do not output anything
//
static int cmd_echo(int argc, char **argv) {

  if (argc < 2)
    q_printf("%% Echo is \"%s\"\r\n", Echo ? "on" : "off");  //if echo is silent we can't see it anyway so no bother printing
  else {
#if 0    
    if (!q_strcmp(argv[1], "on"))     Echo = 1;  else 
    if (!q_strcmp(argv[1], "off"))    Echo = 0;  else 
    if (!q_strcmp(argv[1], "silent")) Echo = -1; else return 1;
#else
    Echo = (argv[1][0] == 'o') ? (argv[1][1] == 'n' ? 1
                                                    : 0) 
                               : (argv[1][0] == 's' ? -1 
                                                    : Echo);
#endif    
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

  // "colors"
  if (argc < 2) q_printf("%% Color is \"%s\"\r\n", ColorAuto ? "auto" : (Color ? "on" : "off")); else
  // "color auto": colors are enabled by ESPShell if it detects proper terminal software on user side
  if (!q_strcmp(argv[1], "auto")) { Color = false; ColorAuto = true; } else
  // "color off": don't send any ANSI color escape sequences. Use with broken terminals
  if (!q_strcmp(argv[1], "off"))  ColorAuto = Color = false; else
  // "colors on" : enable color sequences
  if (!q_strcmp(argv[1], "on")) { ColorAuto = false; Color = true; } else
  // "color test" : hidden developers command
  if (!q_strcmp(argv[1], "test")) {
    int i;
    for (i = 0; i < 8; i++)
      q_printf("3%d: \e[3%dmLorem Ipsum Dolor 1234567890 @#\e[0m 9%d\e[9%dmLorem Ipsum Dolor 1234567890 @#\e[0m\r\n", i, i, i, i);
    for (i = 0; i < 108; i++)
      q_printf("%d: \e[%dmLorem Ipsum Dolor 1234567890 @#\e[0m\r\n", i, i);
  } else
  // "color ????"
    return 1;

  return 0;
}
#endif  //WITH_COLOR

// Used in MUST_NOT_HAPPEN() macro. Declared here because of conflicts in include files
// (can not be declared in qlib.h so let it be here)
//
static NORETURN void must_not_happen(const char *message, const char *file, int line) {

  // Print location & cause of this MUST NOT HAPPEN event
  q_printf("%% ESPShell internal error: \"<i>%s</>\" in file\r\n%% %s, line: %u\r\n%% ESPShell is stopped\r\n",message,file,line);

  // forcefully kill our own background task
  if (is_background_task()) {
    // Signal ESPShell to exit its main loop
    Exit = true; 
    task_signal(taskid_self(), SIGNAL_KILL); // executes vTaskDelete
    // UNREACHABLE (TODO: test it) 
  }
  
  // Sleep for ever
  while(1)
    q_delay(999999);
}

#endif // #if COMPILING_ESPSHELL
