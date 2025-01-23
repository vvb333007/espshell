/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

// -- Help --
// Question mark command and question mark context help, help pages and everything about this
// Help system can be completely disabled by setting WITH_HELP macro to 0. It will save lots of
// program space

#if COMPILING_ESPSHELL

#if WITH_HELP
static const char *Hints[] = {
  "% Press <TAB> repeatedly to cycle cursor through command arguments.\r\n"
  "% This is faster than using arrows <-- -->",

  "% <HOME> and <END> keys are not working? Use Ctrl+A instead of <HOME> and\r\n"
  "% Ctrl+E instead of <END>. Read help page on keys used in ESPShell: \"? keys\"",

  "% Press <ESC> then type a number and press <ESC> again to enter symbol by\r\n"
  "% its code: <ESC>, 32, <ESC> sends \"space\" (code 32)",

  "% Pressing <ESC> and then <BACKSPACE> removes one word instead of single symbol",

  "% Use command \"colors off\" if your terminal does not support ANSI colors",

  "% Use command \"history off\" to disable history and remove history entries",

  "% Command \"uptime\" also shows the last reboot (crash) cause",

  "% Command \"suspend\" (or Ctrl+C) pauses sketch execution. Resume with \"resume\"",

  "% You can use Ctrl+Z as a hotkey for \"exit\" command",

  "% You can shorten command names and its arguments: \"suspend\" can be \"su\" or\r\n"
  "% even \"p 2 i o op\" instead of \"pin 2 in out open\"",

  "% To mount a filesystem on partition \"FancyName\" one can type \"mount F\".\r\n"
  "% Shortening also works for \"unmout\" arguments",

  "% Command \"unmount\" has alias \"umount\"",

  "% \"mkdir\" creates all missing directories in given path",

  "% \"touch\" creates all missing directories in given path before file creation",

  "% Use \"var ls_show_dir_size 0\" to disable dirs size counting by \"ls\" command",

  "% To use spaces in filenames, replace spasces with asteriks (*): \"mkdir A*Path\"",

  "% Main commands are available in every command subdirectory: one can execute\r\n"
  "% command \"pin\" while in UART configuration mode, without need to \"exit\")",

  "% You can send files over UARTs with filesystem's \"cat\" command",

  "% Press Ctrl+R to search through the commands history: start typing and press\r\n"
  "% <Enter> to find a matched command entered previously",

  "% Use \"^\" symbol when history searching (Ctrl+R) to emphasize that search\r\n"
  "% pattern is matched from the beginning of the string (i.e. regexp-like \"^\")",

  "% Press Ctrl+L to clear the screen and enable terminal colors"
};

// Display useful hints. 
// Only first one is choosed randomly, so pressing Ctrl+L enough times will display all the hints
#include <esp_random.h>
static const char *random_hint() {
  static unsigned tick = 0;
  if (tick == 0)
    tick = esp_random();
  return Hints[(tick++) % (sizeof(Hints)/sizeof(Hints[0]))];
}

// "? keys"
// display keyboard usage help page
static int help_keys(UNUSED int argc, UNUSED char **argv) {

  // 25 lines maximum to fit in default terminal window without scrolling
  q_print("%             -- ESPShell Keys -- \r\n\r\n"
          "% <ENTER>         : Execute command.\r\n"
          "% <- -> /\\ \\/     : Arrows: move cursor left or right. Up and down to scroll\r\n"
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
          "% Ctrl+H works as <BACKSPACE> key\r\n");

  return 0;
}

// "? pinout"
// display some known interfaces pin numbers
static int help_pinout(UNUSED int argc, UNUSED char **argv) {
  q_print("% Sorry brother, not yet implemented\r\n");
  return 0;
}

// "? COMMAND_NAME"
//
// display a command usage details ("? some_command")
//
static int help_command(int argc, char **argv) {

  int i = 0;
  int found = 0;

  // go through all matched commands (only name is matched) and print their
  // help lines. hidden commands are ignored
  while (keywords[i].cmd) {
    if (keywords[i].help || keywords[i].brief) {  //skip hidden commands
      if (!q_strcmp(argv[1], keywords[i].cmd)) {
        // print common header for the first entry
        if (!found && keywords[i].brief)
          q_printf("\r\n%% -- %s --\r\n", keywords[i].brief);

        q_printf("%s\r\n\r\n", keywords[i].help ? keywords[i].help : (keywords[i].brief ? keywords[i].brief : "FIXME:"));
        found++;
      }
    }
    i++;
  }
  // if we didnt find anything, return 1 (index of failed argument)
  return found ? 0 : 1;
}

//"?"
// display command list
//
#define INDENT 10
static int help_command_list(int argc, char **argv) {
  int i = 0;
  const char *prev = "";
  char indent[INDENT + 1];

  // commands which are shorter than INDENT will be padded with extra spaces to be INDENT bytes long
  memset(indent, ' ', INDENT);
  indent[INDENT] = 0;
  char *spaces;

  q_print("% Enter \"? command\" to get details about specific command.\r\n"
          "% Enter \"? keys\" to display the espshell keyboard help page\r\n"
          "%\r\n");

  //run through the keywords[] and print brief info for every entry
  // 1. for repeating entries (same command name) only the first entry's description
  //    used. Such entries in keywords[] must be grouped to avoid undefined bahaviour
  //    Ex.: see "count" command entries
  // 2. entries with both help lines (help and brief) set to NULL are hidden commands
  //    and are not displayed (but are executed if requested).
  while (keywords[i].cmd) {

    if (keywords[i].help || keywords[i].brief) {
      if (strcmp(prev, keywords[i].cmd)) {  // skip entry if its command name is the same as previous
        const char *brief;
        if (!(brief = keywords[i].brief))  //use "brief" or fallback to "help"
          if (!(brief = keywords[i].help))
            brief = "% FIXME: No description";

        // indent: commands with short names are padded with spaces so
        // total length is always INDENT bytes. Longer commands are not padded
        int clen;
        spaces = &indent[INDENT];  //points to \0
        if ((clen = strlen(keywords[i].cmd)) < INDENT)
          spaces = &indent[clen];
        // "COMMAND" PADDING_SPACES : DESCRIPTION
        q_printf("%% \"<i>%s</>\"%s : %s\r\n", keywords[i].cmd, spaces, brief);
      }
    }

    prev = keywords[i].cmd;
    i++;
  }

  return 0;
}

// pressing "?" during command line editing will display help
// page for a command if there is a command name typed (may be partially)
//
static bool help_page_for_inputline(unsigned char *raw) {

  if (raw) {

    // skip leading whitespace
    while (*raw && isspace(*raw))
      raw++;

    // have characters?
    if (*raw) {
      unsigned char *end = raw + 1;
      char qm[] = {'?', '\0'};

      //find next whitespace (or string end)
      while (*end && !isspace(*end))
        end++;

      // save whitespace code (it is most likely 0x20 but who knows)
      // and replace it with \0; create fake argc/argv to call "? KEYWORD" handler
      unsigned char tmp = *end;
      *end = '\0';
      char *argv[2] = { qm, (char *)raw };
      help_command(2, argv);

      // restore whitespace symbol
      *end = tmp;
      return true;
    }
  }
  return false;
}


// "?"
//
// question mark command: display general help pages ("? keys", "? pinout")
// display a command usage details ("? some_command") or display the list
// of available commands ("?" with no arguments)
//
static int cmd_question(int argc, char **argv) {

  return argc <= 1                                 /* Have arguments? */
      ? help_command_list(argc, argv)              /*   No!  Display command list */
      : (!strcmp(argv[1], "keys")                  /*   Yes! Is it "keys" ? */
          ? help_keys(argc, argv)                  /*      Yes! Display appropriate helppage */
          : (!strcmp("pinout", argv[1])            /*      No!. Is it "pinout" ? */
              ? help_pinout(argc, argv)            /*         Yes, display pinout helppage */ 
              : help_command(argc, argv)));        /*         No. Asking for help on given command */
}
#endif  // WITH_HELP
#endif //#if COMPILING_ESPSHELL

