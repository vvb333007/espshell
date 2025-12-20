
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
// It is a simple C++ class Serial ---> C console...() wrapper, nothing more
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
#define COMPILING_ESPSHELL 1
#include "espshell.h"       // SERIAL_IS_USB macro


// SERIAL_IS_USB is autodetected from Arduino IDE settings: selecting "Hardware CDC On Boot" will 
// set SERIAL_IS_USB to 1 (see espshell.h)
//
#if SERIAL_IS_USB

// Flush console IO
//
extern "C" void console_flush() {
  Serial.flush();
}


// Check if Serial is up and running.
//
extern "C" bool console_isup() {
  return Serial; //Serial:: bool operator
}


// Send characters to user terminal
extern "C" int console_write_bytes(const void *buf, size_t len) {
  return Serial.write((const uint8_t *)buf, len);
}

// How many characters are available for read?
//
extern "C" int console_available() {
  return Serial.available();
}

// Read user input, with a timeout.
// Returns number of bytes read on success or 0 on error
//
extern "C" int console_read_bytes(void *buf, uint32_t len, TickType_t wait) {
  int av;
  uint32_t len0 = len, min;
  uint8_t *buf0 = (uint8_t *)buf;
  size_t r;

  while (len) {
    av = Serial.available();
    if (av < 1) {
      if (wait-- > 0)
        taskYIELD();
      else
        return len0 - len;
    } else {
      min = (av <= len) ? av : len;
      r = Serial.read(buf0, min);
      if (r) {
        buf0 += r;
        len -= r;
      }
    }
  }

  return len0 - len;
}
#endif //SERIAL_IS_USB
