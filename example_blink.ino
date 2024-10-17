// Minimal sketch - example, blinking LED.
// Also registers few variables to play with shell command "var"

#include <Arduino.h>


#if __has_include("espshell.h")
#  include "espshell.h"
#elif __has_include("extra/espshell.h")
#  include "extra/espshell.h"
#else
#  define convar_add(X)
#endif

#define LED 2       // Generic ESP32 Dev Board
//#define LED 4     // ESP32Cam
//#define LED 33    // ESP32Cam


static unsigned int test = 0x1234;  // test variables for "var" shell command
static unsigned char Char = 12;     // test variables for "var" shell command 
       signed short Short = -1;     // test variables for "var" shell command
static  int Blink = 1;              // "var Blink 0" will set Blink to 0 causing loop() to stop



// setup our serial port & led pin
void setup() {
  
  Serial.begin(115200);

  // prepare our blinking led
  pinMode(LED,OUTPUT);

  //Make 4 variables accessible from the shell
  convar_add(Blink);
  convar_add(Char);
  convar_add(Short);
  convar_add(test);
  
}


void loop() {

  Serial.println("Start blinking");
  
  while(Blink) {
    digitalWrite(LED,0);
    delay(250);
    digitalWrite(LED,1);
    delay(250);
  }

  Serial.println("Stop blinking");

  while (1) delay(9999);
} 
