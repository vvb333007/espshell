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

#if COMPILING_ESPSHELL

// --   "SHELL TO CONSOLE HARDWARE" GLUE --
//
// ESPShell uses abstract console_read../console_write.. and some other functions to print data or read user input.
// Currently this abstraction layer is implemented for UARTs (natively) and USB (see hwcdc.cpp)

// espshell runs on this port:
//
static uart_port_t uart = STARTUP_PORT; // either UART number OR 99 for USB-CDC

#if SERIAL_IS_USB
// Arduino Nano ESP32 and many others use USB as their primary serial port, in hardware CDC mode.
// Here are console functions for such cases (implemented in hwcdc.cpp)
//
extern bool console_isup();
extern int  console_write_bytes(const void *buf, size_t len);
extern int  console_available();
extern int  console_read_bytes(void *buf, uint32_t len, TickType_t wait);
#else
// Generic ESP32 boards usually use UART0 as their default console device.
// Below is the console interface for this
//


// Send characters to user terminal
// Returns number of bytes written
//
static INLINE int console_write_bytes(const void *buf, size_t len) {
  return uart_write_bytes(uart, buf, len);
}

// How many characters are available for read right now?
// Returns number of characters in the fifo (can be zero) or <0 on failure (uart shutdown)
//
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

// Is console device ( UART ) is up and running (can be used) ?
static inline bool console_isup() {
  return uart_isup(uart);
}
#endif  //SERIAL_IS_USB

// Make ESPShell to use specified UART ( or USB-CDC ) for its IO.
// Code below reads: return current uart number if i < 0. If i is valid uart number or i is 99
// then set current uart to that number and return the same number as well. If i is not a valid uart number then -1 is returned
//
static int console_here(int i) {
  return i < 0 ? uart 
               : (i > UART_NUM_MAX ? (i == 99 ? (uart = i) 
                                              : -1) 
                                   : (uart = i));
}

// This variable gets updated by enter_pressed_cr() : once we see "\r" from user SeenCR is set to /true/
// This variable is used to detect extra <LF> symbol and ignore it: consider command "pin 0 delay 9999". If <Enter> sends
// <CR>+<LF> then <CR> will start command execution in a background while <LF> immediately trigger anykey_pressed() causing
// command "pin ..." to abort.
//
// There are 3 possibilities for user terminal:
//
// 1. Send <CR> :  this is what most terminals do
// 2. Send <LF> : SeenCR is always /false/, <LF> is NOT ignored
// 3. Send <CR> + <LF> : SeenCR is /true/, trailing <LF> is ignored
//
static bool SeenCR = false;     

// Detects if ANY key is pressed in serial terminal, or any character was sent in Arduino IDE Serial Monitor.
//
// TODO: Change polling logic to a TaskNotify. This will significally increases delay_interruptible() accuracy
//       Probably there should be a separate task doing this

static bool anykey_pressed() {

  unsigned char c = 0;
  if (console_available() > 0)
    if (console_read_bytes(&c, 1, 0) >= 0)
    // If user terminal is configured to send <CR>+<LF> then we silently discard <LF>
      if (c == '\n')
        return !SeenCR;
  return false;

    
}
#endif // #if COMPILING_ESPSHELL