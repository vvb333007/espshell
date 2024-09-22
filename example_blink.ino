// Minimal sketch - example, blining LED
// to use together with espshell.

#include <Arduino.h>

// Small blue LED on many ESP32 Devkit clones, 
#define LED 2 // ESPCam has led on pins 33 and 4

// setup our serial port & led pin
void setup() {
  
  Serial.begin(115200);
  delay(1000);

  // prepare our blink led, pin2
  pinMode(LED,OUTPUT);
}


// blink led to indicate running loop() task
// type "suspend" in serial monitor to stop this task, type "resume" to resume
void loop() {

  while(1) {
    digitalWrite(LED,0);
    delay(250);
    digitalWrite(LED,1);
    delay(250);
  }
} 
