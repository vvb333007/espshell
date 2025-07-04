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

// Written to add support for a hardware CDC, this code actually enables ANY hardware as long as Serial object
// supports it. It can be HWCDC or USBCDC class or just HardwareSerial. It can be SoftwareSerial as well.
//
// Unfortunately I was not able to find something like hwcdc_read_bytes() to implement console_read_bytes().
// So instead of duplicating Arduino Core HWCDC code here I decided to wrap up calls to Serial object; 
//
// Pros:
// 1. It is more efficient in terms of code size. 
// 2. Hardware part is hidden within Serial object so we don't have to think about it
//
// Cons:
// Huge drawback is that HWCDC::read(...) is non-blocking call so we have to simulate this behaviour 
// via loop. Yes I know there is a blocking Stream::readBytes() but it does not yield()
//
// So this is a tradeoff between "doing it right" and "keeping it small & simple". 
//
#include <Arduino.h>        // Types
#include <HardwareSerial.h> // Serial macro
#include "espshell.h"       // SERIAL_IS_USB macro

// SERIAL_IS_USB is autodetected from Arduino IDE settings: selecting "Hardware CDC On Boot" will 
// set SERIAL_IS_USB to 1 (see espshell.h)
#if SERIAL_IS_USB

// Check if Serial is up and running.
//
extern "C" bool console_isup() {
  return Serial; //Serial:: bool operator
}

// Send characters to user terminal
//
extern "C" int console_write_bytes(const void *buf, size_t len) {
  return Serial.write((const uint8_t *)buf, len);
}

// How many characters are available for read?
//
extern "C" int console_available() {
  return Serial.available();
}

// Read user input, with a timeout.
// Returns number of bytes read on success or <0 on error
//
extern "C" int console_read_bytes(void *buf, uint32_t len, TickType_t wait) {

  int av;

// TODO: Make proper blocking read within timeout. Not just wait for the first byte and read what was in the FIFO.
// TODO: 
// TODO: Should read() in a loop, until either timeout OR full /len/ bytes read. Every successful read() resets /wait/ to its
//       initial value

  while((av = Serial.available()) <= 0 && (wait-- > 0))
    taskYIELD();

  
  return (wait == 0) ? -1 : Serial.read((uint8_t *)buf, len);
}
#endif //COMPILING_ESPSHELL

