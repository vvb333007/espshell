#include <Arduino.h>
//#include "espshell.h"

#define LED 2 // Small blue LED on many ESP32 Devkit clones, 

void setup() {
  
  Serial.begin(115200);
  delay(1000);

  

  // prepare our blink led, pin2
  pinMode(LED,OUTPUT);
    
  // start shell task
  //espshell_task("esp32#>");
}





void loop() {

  // blink led to indicate running loop() task
  // type "suspend" in serial monitor to stop this task, type "resume" to resume
  while(1) {
    digitalWrite(LED,0);
    delay(250);
    digitalWrite(LED,1);
    delay(250);
  }
} 
