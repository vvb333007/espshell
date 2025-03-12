/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Stable releases: https://github.com/vvb333007/espshell/tags
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

// Declare them extern "C" so names will not be mangled

#include <Arduino.h>
#include <HardwareSerial.h>
#include "espshell.h"

#if SERIAL_IS_USB

extern "C" {
  bool console_isup();
  int  console_write_bytes(const void *buf, size_t len);
  int  console_available();
  int  console_read_bytes(void *buf, uint32_t len, TickType_t wait);
};

bool console_isup() {
  return Serial;
}

// Send characters to user terminal
int console_write_bytes(const void *buf, size_t len) {
  return Serial.write((const uint8_t *)buf, len);
}
// How many characters are available for read without blocking?
int console_available() {
  return Serial.available();
}

// Read user input, with a timeout.
// Returns number of bytes read on success or <0 on error
//
int console_read_bytes(void *buf, uint32_t len, TickType_t wait) {
  int av;

  while((av = Serial.available()) <= 0 && (wait-- > 0))
    delay(1);

  if (wait == 0)
    return -1;

  return Serial.read((uint8_t *)buf, len);
}

#endif
