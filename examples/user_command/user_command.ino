// Override hidden command "misc" to call your own code 
//

#include <Arduino.h>
#include "espshell.h"

// Espshell provides one hidden, user-definable command: "misc", which does nothing by default.
//
// esp32#> misc
// esp32#> % No user-defined command
//
// This command can be hooked by user code simply by defining a corresponding handler function.
//
// If the user code defines the SHELL_USER_HANDLER() function, for example:
//
//   SHELL_USER_HANDLER();  // important line, do not remove
//   SHELL_USER_HANDLER() {
//     printf("argc=%d, argv=%p\r\n", argc, argv);
//     // 'argc' and 'argv' are available inside this function
//     return 0;
//   }
//
// then this user-defined function will be used instead of the default handler.
//
// The example implementation below overrides the default handler and currently just
// prints "Hello, World!" when the "misc" command is executed.
//
// The code below also demonstrates basic argument processing:
// argv[0] is the command name itself (i.e. "misc"),
// while argv[1], argv[2], ..., argv[X] are the command arguments.
//
SHELL_USER_HANDLER();

// This function is called every time the user executes the "misc" command.
// The command line, split into argc/argv, is passed to the function by the shell.
// argv[0] is always the command name itself (i.e. "misc").
//
SHELL_USER_HANDLER() {

    if (argc > 1)
      Serial.printf("Hello, %s!\r\n", argv[1]);
    else
      Serial.printf("Hello, World!\r\n");
    return 0;
}

// Setup terminal
//
void setup() {
  Serial.begin(115200);

  // Let espshell fully start before printing banners
  delay(2000); 

  Serial.println(" ------ misc command demo -----");

  Serial.println(" type \"misc\" and press Enter or");
  Serial.println(" type \"misc SomeText\" and press Enter");
}

// Do nothing, wait for the command "misc" (or any other command) to be entered in Serial Monitor.
//
void loop() {
  delay(9999);
}
