// Minimal sketch - example, blinking LED.
//
// 1. Use command "suspend" or press Ctrl+Z to suspend sketch execution
// 2. Use command "var" to display/change sketch variables (there are few)
//    Note that setting variable "Blink" to "0" will cause blink sketch to
//    end
// 3. 

#include <Arduino.h>
#include "espshell.h"

// Choose your LED pin:
#define LED 2       // Generic ESP32 Dev Board
//#define LED 4     // ESP32Cam high intensity lamp
//#define LED 33    // ESP32Cam low power red LED


// Test variables for "var" shell command. They do nothing
// but you can change or display them via "var" commands
static unsigned int test  = 0x1234;  
static unsigned char Char = 12;
       signed short Short = -1;
              float Float = -11.11;
              float fp[]  = { 0 };
const char         *piu   = "";

// One more variable. Setting it to 0 via "var Blink 0" stops
// this sketch
static  int Blink = 1;

///////////////////////////////////
// Setup our serial port & LED pin
void setup() {
  
  Serial.begin(115200);

  // prepare our blinking led
  pinMode(LED,OUTPUT);

  //Make these 6 variables be accessible from the shell
  convar_add(Blink);
  convar_add(Char);
  convar_add(Short);
  convar_add(test);
  convar_add(Float);
  convar_add(fp);
  convar_add(piu);
  
}

// Main loop
void loop() {

  Serial.println("\r\nStart blinking. Use \"var Blink 0\" to stop\r\n");

  while(Blink) {
    digitalWrite(LED, LOW);  delay(250);
    digitalWrite(LED, HIGH); delay(250);
  }

  Serial.println("\r\nStop blinking\r\n");
  delay(9999);
  Blink = 1;
  Serial.println("Restarting..\r\n");
} 
