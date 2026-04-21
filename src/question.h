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

// -- Help subsystem --
// Question mark command and question mark context help, help pages and everything about this
// Help system can be completely disabled by setting WITH_HELP macro to 0. It will save lots of
// program space

#if COMPILING_ESPSHELL
#if WITH_HELP
#include <esp_random.h>

#if WITH_LANG
#  include "lang/question_messages_ru.inc"
#else
static const char *Hints[] = {
  "% Press <TAB> repeatedly to cycle the cursor through command arguments.\r\n"
  "% This is faster than using the arrow keys (<-- and -->).",

  "% <HOME> and <END> keys not working? Use Ctrl+A instead of <HOME> and\r\n"
  "% Ctrl+E instead of <END>. Read the help page on keys used in ESPShell: \"? keys\"",

  "% Press <ESC>, then type a number, and press <ESC> again to enter a symbol\r\n"
  "% by its code: <ESC>, 32, <ESC> sends a \"space\" (code 32).",

  "% Pressing <ESC> and then <BACKSPACE> deletes one word instead of a single character.",

  "% Use the command \"colors off\" if your terminal does not support ANSI colors.",

  "% Use the command \"history off\" to disable command history and clear existing entries.",

  "% The \"uptime\" command also shows the last reboot (crash) cause.",

  "% The \"suspend\" command (or Ctrl+C) pauses sketch execution. Resume with \"resume\".",

  "% You can use Ctrl+Z as a shortcut for the \"exit\" command.",

  "% You can shorten command names and their arguments: \"suspend\" can be \"su\" or\r\n"
  "% even \"p 2 i o op\" instead of \"pin 2 in out open\".",

  "% To mount a filesystem on the \"FancyName\" partition, type \"mount F\".\r\n"
  "% Shortening also works for \"unmount\" arguments.",

  "% The \"unmount\" command has an alias: \"umount\".",

  "% The \"mkdir\" command creates all missing directories in the given path.",

  "% The \"touch\" command creates all missing directories in the given path before file creation.",

  "% Use \"var ls_show_dir_size 0\" to disable directory size calculation in the \"ls\" command:\r\n"
  "% filesystems with a large number of files and directories may slow down.",

  "% To use spaces in filenames, replace them with an asterisk (*): \"mkdir A*Path\"\r\n"
  "% or just use double quotes(\"\"): mkdir \"A Path\"\r\n",

  "% Main commands are available in every command subdirectory: you can run\r\n"
  "% the \"pin\" command while in UART configuration mode without having to \"exit\".",

  "% You can send files over UART using the filesystem's \"cat\" command.",

  "% Press Ctrl+R to search through the command history: start typing and press\r\n"
  "% <Enter> to find a previously entered matching command.",

  "% Use the \"^\" symbol when searching history (Ctrl+R) to match from the start\r\n"
  "% of the string (similar to regexp \"^\").",

  "% Press Ctrl+L to clear the screen and enable terminal colors.",

  "% Adding an \"&\" at the end of any command runs that command in the background,\r\n"
  "% just like in Bash/Linux: \"count 4 &\".",

  "% The \"if\" command can be used to set up GPIO conditions and corresponding\r\n"
  "% actions, for example: \"if rising 2 exec my_alias\".",

  "% The \"every\" command can be used to schedule periodic tasks (delayed or immediate),\r\n"
  "% for example: \"every 2 hours exec my_alias\".",

  "% The \"if\" command can also be used to poll GPIO values:\r\n"
  "% \"if low 4 high 5 poll 1000 exec my_alias\".",

  "% Press @ at the beginning of the input prompt to hide your input.\r\n"
  "% The shell will return to normal operation after <Enter> is pressed.",

  "% You can view/edit NVS keys and values with NVS editor: (command \"nvs\")",

  "% You can synchronize system time with \"ntp enable\" WiFi command",

  "% Use \"nat enable\" command to enable NAT router on the AP WiFi interface"
};


// 25 lines maximum to fit in default terminal window without scrolling
static const char *Keys_Manual =
  "%             -- ESPShell Keys -- \r\n\r\n"
  "% <ENTER>         : Execute command.\r\n"
  "% <- ->           : Arrows: move cursor left or right. Up and down to scroll\r\n"
  "%                   through command history\r\n"
  "% <DEL>           : As in Notepad\r\n"
  "% <BACKSPACE>     : As in Notepad\r\n"
  "% <HOME>, <END>   : Use Ctrl+A instead of <HOME> and Ctrl+E as <END>\r\n"
  "% <TAB>           : Move cursor to the next word/argument: press <TAB> multiple\r\n"
  "%                   times to cycle through words in the line\r\n"
  "% Ctrl+R          : Command history search\r\n"
  "% Ctrl+K          : [K]ill line: clear input line from cursor to the end\r\n"
  "% Ctrl+L          : Clear screen\r\n"
  "% Ctrl+Z          : Same as entering \"exit\" command\r\n"
  "% Ctrl+C          : Suspend sketch execution\r\n"
  "% <ESC>,NUM,<ESC> : Same as entering letter with decimal ASCII code NUM\r\n%\r\n"
  "% -- Terminal compatibility workarounds (alternative key sequences) --\r\n%\r\n"
  "% Ctrl+B and Ctrl+F work as \"<-\" and \"->\" ([B]ack & [F]orward arrows)>\r\n"
  "% Ctrl+O or P   : Go through the command history: O=backward, P=forward\r\n"
  "% Ctrl+D works as <[D]elete> key\r\n"
  "% Ctrl+H works as <BACKSPACE> key\r\n";


static const char *Error_No_Manual = 
  "\r\n"
  "%% Sorry, no manual entry for \"%s\"\r\n"
  "%% Type \"<i>?</>\" and press <Enter> to see what is available\r\n";


static const char *List_Banner =
  "% Enter \"<b>?</> <i>COMMAND</>\" to view details about a specific command.\r\n"
  "% Enter \"<b>? <i>keys</>\" to display the ESPShell keyboard help page.\r\n"
  "%\r\n";

#endif // LANG

// Display useful hints. 
// Only first one is choosed randomly, so pressing Ctrl+L enough times will display all the hints

static const char *random_hint() {
  static unsigned tick = 0;
  if (tick == 0)
    tick = esp_random();
  return Hints[(tick++) % (sizeof(Hints)/sizeof(Hints[0]))];
}



// Display command directory content
//
//
static bool help_list_dir(const struct keywords_t *key, const char *banner) {

  int i = 0;
  const char *prev = "";

  HELP(q_print( banner == NULL ? List_Banner : banner));

  //run through the key[] and print brief info for every entry
  // 1. for repeating entries (same command name) only the first entry's description
  //    used. Such entries in key[] must be grouped to avoid undefined bahaviour
  //    Ex.: see "count" command entries
  // 2. entries with both help lines (help and brief) set to NULL are hidden commands
  //    and are not displayed (but are executed if requested).
  while (key[i].cmd) {

    if (key[i].help || key[i].brief) {
      if (strcmp(prev, key[i].cmd)) {  // skip entry if its command name is the same as previous
        const char *brief;
        if (!(brief = key[i].brief))  //use "brief" or fallback to "help"
          if (!(brief = key[i].help))
            brief = "No description";
        // command : description
        q_printf("%% <%c>%-11.11s</> : %s\r\n", is_command_directory(key[i].cmd) ? 'b' : 'i', key[i].cmd, brief);
      }
    }

    prev = key[i].cmd;
    i++;
  }
  return true;
}


// Display full help for a command /cmd/ which is searched in a command directory /key/
//
//
static bool help_for_dir_command(const struct keywords_t *key, const char *cmd) {

  int i = 0, found = 0;
  const char *brief = ""; 

  MUST_NOT_HAPPEN(key == NULL || cmd == NULL);

  // go through all matched commands (only name is matched) and print their
  // help lines. hidden commands are ignored
  while (key[i].cmd) {
    if (key[i].help || key[i].brief) {
      if (!q_strcmp(cmd, key[i].cmd)) {

        // Print header
        if (key[i].brief)
          brief = key[i].brief;

        q_printf("\r\n%%<r> -- 📚👇 %40.40s --</>\r\n", brief);

        // Print help page
        q_printf("%s\r\n\r\n",key[i].help ? key[i].help                        // use /.help/ if it is exists
                                          : (key[i].brief ? key[i].brief  // otherwise use /.brief/
        /* This one can not happen --------------> */     : "Help page is missing"));

        found++;
      }
    }
    i++;
  }

  return found > 0;
}


// pressing "?" during command line editing will display help
// page for a command if there is a command name typed (may be partially)
//
// However, the "show ?" and "show ARG ?" are special cases:
//
//  a) When user types "show ?", then command list of a hidden subdirectory "show" is displayed:
//     we call help_command_list() for the "show" subdir;
//
//  b) When user types "show ARG ?", then we display a help page from the "show" subdir as if ARG was our
//     argv[0]
//
static bool help_page_for_inputline(unsigned char *raw0) {

  bool ret = false;

  if (raw0) {

    char *raw = q_strdup((const char *)raw0, MEM_TMP);

    if (raw == NULL)
      return false;

    int argc;
    char **argv = NULL;

    argc = argify((unsigned char *)raw, (unsigned char ***)&argv);

    if (argc < 1) {

      q_free(raw);
      return false;
    }

    // if /command/ is NOT "show" then we just display a help page for the command
    // if /command/ IS "show" then we either display "show" directory content ("show ?")
    // or we display help pages on the last token ("show pin ?")
    // We use argv[argc-1] instead of argv[1] here because of future "pin" help system refactoring
    if (q_strcmp(argv[0], "show"))
      ret = help_for_dir_command(keywords_get(), argv[0]);
    else {
    // "show ?"
      if (argc < 2)
        ret = help_list_dir(KEYWORDS(show),
                              "\r\n"
                              "% Below is the <i>full list of keywords</> for command \"show\"\r\n"
                              "% Type \"<i>show KEYWORD</>\" and press \"?\" to get help on KEYWORD\r\n"
                              "%\r\n");
      else
      // "show keyword ?"
        ret = help_for_dir_command(KEYWORDS(show), argv[argc - 1]);
    }

    q_free((void *)argv);
    q_free(raw);
    
  }
  return ret;
}

// "?"
//
// A "question mark" command: displays general help pages ("? keys", "? pinout"),
// displays a command usage details ("? some_command") or displays the list
// of available commands ("?" with no arguments)
//
// Note that if question mark is not the first token in a command
// then it is handled by editline's callback qm_pressed()

//
static int cmd_question(int argc, char **argv) {

  //"?"
  if (argc < 2) {
    if (false == help_list_dir(keywords_get(), NULL))
      return CMD_FAILED;
    return 0;
  }

  //"? keys"
  // strcmp, not q_strcmp: we don't want false matches
  if (!strcmp(argv[1],"keys")) {
    q_print( Keys_Manual );
    return 0;
  }

  //"? COMMAND". Look in the current command directory
  if (help_for_dir_command(keywords_get(), argv[1]))
    return 0;

  // Nothing was found. Look in the main command directory
  if (help_for_dir_command(KEYWORDS(main), argv[1]))
    return 0;

  // Still nothing?
  q_printf(Error_No_Manual ,argv[1]);

  return CMD_FAILED;
}
#endif  // WITH_HELP

#endif //#if COMPILING_ESPSHELL

