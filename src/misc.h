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

  const char *rr2[] = { 
    "",
    "Power on reset",
    "",
    "Software resets the digital core",
    "",
    "Deep sleep resets the digital core",
    "SDIO module resets the digital core",
    "Main watch dog 0 resets digital core",
    "Main watch dog 1 resets digital core",
    "RTC watch dog resets digital core",
    "",
    "Main watch dog resets CPU",
    "Software resets CPU",
    "RTC watch dog resets CPU",
    "CPU0 resets CPU1 by DPORT_APPCPU_RESETTING",
    "Reset when the VDD voltage is not stable",
    "RTC watch dog resets digital core and RTC module"
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

  q_printf( "%u second%s ago\r\n",PPA(sec));


  // Reset Reason.
  unsigned char i, core;

  // "Classic" ESP-IDF reset reason:
  if ((i = esp_reset_reason()) > ESP_RST_CPU_LOCKUP)
    i = 0;
  q_printf("%% Reset reason: \"%s</>\"\r\n", rr[i]);

  // "Bootloader-style" reset reason (for each core):
  for (core = 0; core < portNUM_PROCESSORS; core++)
    if ((i = esp_rom_get_reset_reason(core)) < sizeof(rr2) / sizeof(rr2[0]))
      q_printf("%%    CPU%u: %s\r\n",core, rr2[i]);

  return 0;
}


//"tty NUM"
//
// Set UART (or USBCDC) to use by this shell.
// Use this command to "pass the shell" to another UART. This allows for various "chain" configurations
// of multiple ESP32 together: UART1 is IN, UART2 is OUT. By using UART's "tap" command along with "tty"
// one can "login" to every device in the chain
//
static int cmd_tty(int argc, char **argv) {

  unsigned char tty;
  // No arguments? Print currently used UART number
  if (argc < 2) {
    tty = console_here(-1);
    q_printf("%% TTY device is %s%u\r\n", 
              tty < 99 ? "UART" : "USB", 
              tty < 99 ? tty : 0);
      
    return 0;
  }

  // Arguments were provided: read UART number and switch espshell input accordingly
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

//"echo [[-n] on|off|silent|TEXT]"
//
// Enable/disable local echo. Normally enabled, it lets software
// like TeraTerm and PuTTY to be used. Turning echo off supresses
// all shell output (except for command handlers output)
//
// Setting "echo silent" has effect of "echo off" + all command output
// is supressed as well. commands are executed but do not output anything
//
// Displays TEXT : "echo <i>Hello</>, <u>World!</>", tags are allowed
// Unless "-n" is used, adds CRLF automatically
//
// TODO: Recognize $VarName sequences
//
static int cmd_echo(int argc, char **argv) {
  
  
  if (argc < 2)
    q_printf("%% Echo is \"%s\"\r\n", Echo ? "on" : "off");  //if echo is silent we can't see it anyway so no bother printing
  else {
    int i = 1;
    bool add_nl = true;
    if (!q_strcmp(argv[1], "-n")) { 
      add_nl = false;
      i++;
    }
    if (!q_strcmp(argv[1], "on"))     Echo = 1;  else 
    if (!q_strcmp(argv[1], "off"))    Echo = 0;  else 
    if (!q_strcmp(argv[1], "silent")) Echo = -1; else {
      // Display TEXT, go through argvs, add separators (" ")
      // Mimic Linux "echo -n" behaviour
     
      while(i < argc) {
        q_print(argv[i]);
        if (++i < argc)
          q_print(" ");
      }
      if (add_nl)
        q_print(CRLF);
    }
  }
  return 0;
}


// enable/disable history saving. mostly for memory leaks detection:
// disabling history fixes "free memory" value ("show memory") so one can 
// execute commands and recheck remaining memory amount. With history enabled, every "show memory" will change the
// remaining memory value
//
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

  if (argc < 2)                       // no arguments? display history status
    q_printf("%% History is %sabled\r\n", History ? "en" : "dis");
  else if (!q_strcmp(argv[1], "off")) // history off: disable history and free all memory associated with history
    history_enable(false);
  else if (!q_strcmp(argv[1], "on"))  // history on: enable history
    history_enable(true);
  else
    return 1; // arg1 is bad

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
  if (!q_strcmp(argv[1], "test"))
    for (int i = 0; i < 108; i++)
      q_printf("%d: \e[%dmLorem Ipsum Dolor 1234567890 @#\e[0m\r\n", i, i);
  else
    return 1;
  return 0;
}
#endif  //WITH_COLOR



// Used in MUST_NOT_HAPPEN() macro. Declared here because of conflicts in include files
// (can not be declared in qlib.h so let it be here)
//
// NOTE: this function creates a memory leak: Cwd thread-local variable may be allocated and thus will be lost.
//
static NORETURN void must_not_happen(const char *message, const char *file, int line) {

  // Print location & cause of this MUST NOT HAPPEN event
  q_printf("%% ESPShell internal error: \"<i>%s</>\"\r\n"
           "%% in %s:%u, ESPShell is stopped, sketch is resumed\r\n",
           message,
           file,  
           line);
  // resume sketch (it may be paused)
  if (loopTaskHandle != NULL)
    task_resume(loopTaskHandle);
  // forcefully kill our parent task (the shell command processor) if we are running in a background
  if (is_background_task()) {
    task_suspend((task_t)shell_task);
    q_delay(100);
    task_kill((task_t)shell_task);
  }
  // foreground: kill ESPShell task
  // background: kill background command task, shell was killed before
  task_finished();
  
  // UNREACHABLE CODE:
  while(1)
    q_delay(1);
}




#include <nvs_flash.h>
#include <nvs.h>

//extern const char *PromptID, defined in editline.h

static bool nv_init = false;

static void nv_init_once() {

  if (nv_init)
    return ;

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    q_print("% NV flash init failed\r\n");
    return ;
  }

  nv_init = true;
}


// Save some vital configuration parameters to the NV storage.
// Right now it is just one parameter: a host id.
//
static bool nv_save_config(const char *nspace) {

    nvs_handle_t handle;
    esp_err_t err;

    if (!nspace)
      nspace = "espshell";

    nv_init_once();

    // Open NVS storage
    if ((err = nvs_open(nspace, NVS_READWRITE, &handle)) != ESP_OK) {
        q_printf("%% Error opening NVS, namespace \"%s\": code %s",nspace, esp_err_to_name(err));
        return false;
    }

    // Write values
    if ((err = nvs_set_str(handle, "hostid", PromptID)) == ESP_OK)
      if ((err = nvs_commit(handle)) != ESP_OK)
        q_printf("%% NVS commit failed: %s", esp_err_to_name(err));

    nvs_close(handle);
    return err == ESP_OK;
}

// Load some espshell parameters from the NV storage
//
static bool nv_load_config(const char *nspace) {

    nvs_handle_t handle;
    esp_err_t err;
    size_t length = sizeof(PromptID);

    if (!nspace)
      nspace = "espshell";

    nv_init_once(); 
    
    if ((err = nvs_open(nspace, NVS_READONLY, &handle)) != ESP_OK) {
        q_printf("%% Error opening NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Read hostname
    err = nvs_get_str(handle, "hostid", PromptID, &length);
    nvs_close(handle);
    return err == ESP_OK;
}

//"hostid [NAME]"
//
// Hidden command, to add a hostid to the prompt. hostid is saved in NVS and retained between power cycles
// This one may be useful when dealing with big number of devices it allows you to give a name to a particular board
// which will be displayed as part of the prompt.
//
static int cmd_hostid(int argc, char **argv) {

  if (argc < 2) {
    if (PromptID[0])
      q_printf("%% Host ID is \"%s\"\r\n", PromptID);
    else
      q_print("% Host ID is not set. (\"<i>hostid</> Name\" to set)\r\n");
  } else if (is_foreground_task()) {
    // only alphanumerics are allowed in prompt id: bad symbols (e.g. ANSI escape sequences) can screw 
    // the terminal up making shell IO not possible 
    for (int i = 0; argv[1][i]; i++)
      if (!isalnum((int)(argv[1][i]))) {
        HELP(q_print("%% Only alpha-numeric symbols are allowed\r\n"));
        return 1;
      }
    // Copy user input.
    strlcpy(PromptID,argv[1],sizeof(PromptID)); // TODO: unprotected
    nv_save_config("espshell");
  }
  return 0;
}


#endif // #if COMPILING_ESPSHELL
