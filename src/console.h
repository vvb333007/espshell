/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Stable releases: https://github.com/vvb333007/espshell/tags
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#if COMPILING_ESPSHELL

// --   "SHELL TO CONSOLE HARDWARE" GLUE --
//
// ESPShell uses abstract console_read../console_write.. and some other functions to print data or read user input.
// Currently this abstraction layer is implemented for UARTs 
// In order to implement support for another type of hardware (say USBCDC) one have to implement functions
// below

// espshell runs on this port:
static uart_port_t uart = STARTUP_PORT; // either UART number OR 99 for USB-CDC

#ifdef SERIAL_IS_USB
#  error "console_write_bytes() is not implemented"
#  error "console_read_bytes() is not implemented"
#  error "console_available() is not implemented"
static INLINE bool console_isup() {
  return uart == 99 ? true : uart_isup(uart);
}
#else
// Send characters to user terminal
static INLINE int console_write_bytes(const void *buf, size_t len) {
  return uart_write_bytes(uart, buf, len);
}
// How many characters are available for read without blocking?
static INLINE int console_available() {
  size_t av;
  return ESP_OK == uart_get_buffered_data_len(uart, &av) ? (int)av : -1;
}

// Read user input, with a timeout.
// Returns number of bytes read on success or <0 on error
//
static INLINE int console_read_bytes(void *buf, uint32_t len, TickType_t wait) {
  return uart_read_bytes(uart, buf, len, wait);
}
// is UART (or USBCDC) driver installed and can be used?
static INLINE bool console_isup() {
  return uart_isup(uart);
}
#endif  //SERIAL_IS_USB

// Make ESPShell to use specified UART (or USB) for its IO. Default is uart0.
// Code below reads: return current uart number if i < 0. If i is valid uart number or i is 99
// then set current uart to that number and return the same number as well. If i is not a valid uart number then -1 is returned
//
static INLINE int console_here(int i) {
  return i < 0 ? uart 
               : (i > UART_NUM_MAX ? (i == 99 ? (uart = i) 
                                              : -1) 
                                   : (uart = i));
}

// Detects if ANY key is pressed in serial terminal, or any character was sent in Arduino IDE Serial Monitor.
//
// TODO: Change polling logic to a TaskNotify. This will significally increases delay_interruptible() accuracy
//       Probably there should be a separate task doing this
static bool anykey_pressed() {

  unsigned char c;
  return console_available() > 0 ? console_read_bytes(&c, 1, 0) >= 0
                                 : false;
}
#endif // #if COMPILING_ESPSHELL

