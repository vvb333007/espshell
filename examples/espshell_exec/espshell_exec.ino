// Example use of espshell_exec() to execute arbitrary shell commands
// from the sketch

#include <Arduino.h>
#include "espshell.h"

// Choose your LED pin:
//
#define LED    "2"  // Generic ESP32 Dev Board
//#define LED  "4"  // ESP32Cam high intensity lamp
//#define LED "33"  // ESP32Cam low power red LED


// Example command:
static const char *command = "pin " LED " high delay 250 low delay 250 loop 100";


// Setup our serial port
void setup() {
  Serial.begin(115200);
}


// Main loop
void loop() {

  printf("Sending command \"%s\"to the shell...\r\n",command);

  espshell_exec(command);

  while (!espshell_exec_finished())
    delay(1);

  Serial.println("Blinking LED on GPIO" LED "\r\n");

  while (1)
    delay(1000);
} 
