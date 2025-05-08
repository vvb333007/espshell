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

  // !! READ ME FIRST !!
 //
 // 1) Inclusion of this file will enable CLI automatically
 // 2) No functions needs to be called in order to use the shell
 // 3) There are Compile-time Settings below to tweak ESP32Shell behaviour
 //    Using Compile-time Settings section below disable code which is not needed to 
 //    decrease the memory footprint

#ifndef espshell_h
#define espshell_h

// Code version, dont change. 
#define ESPSHELL_VERSION "0.99.8"

// -- Compile-time ESPShell settings --
//
#define AUTOSTART 1              // Set to 0 for manual shell start via espshell_start().
#define STACKSIZE (5 * 1024)     // Shell task stack size

#define WITH_HELP 1              // Set to 0 to save some program space by excluding help strings/functions
#define WITH_HISTORY 1           // Enable command history (!! set to 0 when using MEMTEST !!)
#define HIST_SIZE 20             // History buffer size (number of commands to remember)

#define WITH_ESPCAM 1            // Include camera commands

#define WITH_VAR 1               // enable support for sketch variables

#define STARTUP_ECHO 1           // echo mode at espshell startup (-1=blackhole, 0=no echo or 1=echo)
#define WITH_COLOR 1             // Enable terminal colors support 
#define AUTO_COLOR 1             // Let ESPShell decide wheither to enable coloring or not. Command "color on|off|auto" is about that

#define WITH_FS 1                // Filesystems (fat/spiffs/littlefs) support
#define MOUNTPOINTS_NUM 5        // Max number of simultaneously mounted filesystems
#define WITH_SPIFFS 1            // support SPIF filesystem
#define WITH_LITTLEFS 1          //   --    LittleFS
#define WITH_FAT 1               //   --    FAT (enable it if you want to enable WITH_SD below )
#define WITH_SD 1                // Support FAT filesystem on SD/TF card over SPI
#define DIR_RECURSION_DEPTH 127  // Max directory depth TODO: make a test with long "/a/a/a/.../a" path

#define SEQUENCES_NUM 10         // Max number of sequences available for the command "sequence"

#if ARDUINO_USB_CDC_ON_BOOT      // USB mode?
#  define SERIAL_IS_USB 1        
#  define STARTUP_PORT 99        // don't change this
#else                             
#  define SERIAL_IS_USB 0
#  define STARTUP_PORT UART_NUM_0  // UART number, where shell will be deployed at startup. can be changed.
#endif                            

// -- ESPShell public API --

#ifdef __cplusplus
extern "C" {
#endif

// 1) Start ESPShell manually
// By default espshell autostarts. If AUTOSTART is set to 0 in espshell.h then
// user sketch must call espshell_start() to manually start the shell. 
// A shell which was closed by "exit ex" command can be restarted by this function 
//
#if !AUTOSTART
void espshell_start();
#endif

// 2) Execute an arbitrary shell command (\n are allowed for multiline, i.e. multiple commands at once).
// This function injects its argument to espshell's input stream as if it was typed by user. 
// It is an asyn call, returns immediately. Next call can be done only after espshell_exec_finished()
//
// @param p - A pointer to a valid asciiz string. String must remain a valid memory until espshell finishes its processing!
//
void espshell_exec(const char *p);


// 3) Check if ESPShell has finished processing of last espshell_exec() call and is ready for new espshell_exec()
// This function does NOT tell you that command execution is finished. It is about readiness of the shell to accept new
// commands
//
bool espshell_exec_finished();


// used internally by convar_add macro (see below [8] )
void espshell_varadd(const char *name, void *ptr, int size, bool isf, bool isp, bool isu);
void espshell_varaddp(const char *name, void *ptr, int size, bool isf, bool isp, bool isu);
void espshell_varadda(const char *name, void *ptr, int size,int count, bool isf, bool isp, bool isu);

// 4) By default ESPShell occupies UART0  (or USB). Default port could be changed
// at compile time by setting #define STARTUP_PORT in "extra/espshell.h"
// to required value OR at runtime by calling console_attach2port()
// 
// Special value 99 means USB console port for boards with USB-CDC
// support
//
// /port/ - 0, 1 or 2 for UART interfaces or 99 for native USB console 
//          interface. If /port/ is negative number then this function 
//          returns port number which currently in use by ESPShell.
// 
// returns number of port which is now used
//
int console_attach2port(int port);


// 5) classic digitalRead() undergoes PeriMan checks (see Arduino Core, esp32-periman.c) 
// which do not allow to read data for pins which are NOT configured as GPIO:  
// UART pins or I2C pins are examples of pins that can not be read by digitalRead().
//
// digitalForceRead() is able to read ANY pin no matter what. 
// for OUTPUT pins it enables INPUT automatically.
//
// This function is much faster than digitalRead()
//
int digitalForceRead(int pin);

// 6) Again, periferial manager bypassed: faster version
// of digitalWrite():
//
// 1. pin not need to be set for OUTPUT
// 2. it is faster
//
void digitalForceWrite(int pin, unsigned char level);

// 7) Discussion: https://github.com/espressif/arduino-esp32/issues/10370
// 
// pinMode() is a heavy machinery: setting a pin INPUT or OUTPUT does not just
// change pin modes: it also calls init/deinit functions of a driver associated 
// with pins. As a result, pinMode(3, OUTPUT) (pin 3 is an UART0 TX pin on most ESP32's)
// breaks UART0 completely.
//
// Another big drawback is that setting any pin to OUTPUT or INPUT automatically turns it into 
// GPIO Matrix pin, even if it was at correct IO_MUX function. As a result we can not have
// "working" pin which is managed by IO MUX.
//
// Function pinForceMode() bypasses periman, does not reconfigure pin and even can be applied
// to a **reserved** ESP32 pins (SPI FLASH CLK) for example providing that new flags are compatible
// with pin function.
//
// calling pinMode(6,...) will likely crash your ESP32, pinForceMode() - not
//
// /pin/    - pin (GPIO) number
// /flags/  - flags as per pinMode(): INPUT, OUTPUT, OPEN_DRAIN,PULL_UP, PULL_DOWN and OUTPUT_ONLY
//          - NOTE: On ESP32 OUTPUT is defined as INPUT and OUTPUT. You can use flag OUTPUT_ONLY if you don't want
//            INPUT to be automatically set.
//
#define OUTPUT_ONLY ((OUTPUT) & ~(INPUT))

void pinForceMode(unsigned int pin, unsigned int flags);

// 8)
// Access sketch variables from ESPShell while sketch is running: in order to do so variables 
// must be **registered** (using one of convar_addX() macros). Once registered, variables are 
// available for read/write access (via "var" command). 
//
//    Variable types supported: 
//
//      1. Simple types: unsigned/signed char, short, int and long; float; bool;
//      2. Pointers: pointers to Simple Types, pointer to a pointer
//      3. Arrays of Simple Types, arrays of pointers
//
//    To register a non-pointer type variable (i.e. "int", "unsigned char" and so on) use "convar_add()"
//    To register a pointer to a simple scalar type use convar_addp()
//    To register a pointer to a pointer use convar_addpp()
//    Arrays of scalar types is registered with convar_adda()
//    Arrays of pointers are registered with convar_addap()
// 
//    Example: register sketch variables in ESPShell
//    ...
//    int some_variable;
//    const int *ptr = &some_variable;
//    static float volatile another_variable;
//    int arr[] = {1,2,3};
//    void **bb = &ptr;
//    ...
//    convar_addp(ptr);
//    convar_adda(arr);
//    convar_add(some_variable);
//    convar_add(another_variable);
//    convar_addpp(bb);
//
//
#if WITH_VAR
extern float dummy_float;
// TODO: use GCC's _Generic to make one single macro for all cases
// Any non-pointer variable of simple (builtin) type (e.g. float, unsigned int, signed char, bool and so on)
#  define convar_add( VAR ) do { \
          __typeof__(VAR) __x = ( __typeof__(VAR) )(-1); \
          bool is_signed = (__x < 0); /* HELLO! If you see this warning during compilation - just ignore it :) */ \
          espshell_varadd( #VAR, &VAR, sizeof(VAR),(__builtin_classify_type(VAR) == __builtin_classify_type(dummy_float)),0,!is_signed); \
} while( 0 )

// Pointer to a simple type
#  define convar_addp( VAR ) do { \
          __typeof__(VAR[0]) __x = ( __typeof__(VAR[0]) )(-1); \
          bool is_signed = (__x < 0);   /* HELLO! If you see this warning during compilation - just ignore it :) */ \
          espshell_varaddp( #VAR, &VAR, sizeof(VAR[0]), (__builtin_classify_type(VAR[0]) == __builtin_classify_type(dummy_float)),0,!is_signed); \
} while ( 0 )

// Array of elements of a simple type
#  define convar_adda( VAR ) do { \
          __typeof__(VAR[0]) __x = ( __typeof__(VAR[0]) )(-1); \
          bool is_signed = (__x < 0);   /* HELLO! If you see this warning during compilation - just ignore it :) */ \
          espshell_varadda( #VAR, &VAR, sizeof(VAR[0]), sizeof(VAR) / sizeof(VAR[0]), (__builtin_classify_type(VAR[0]) == __builtin_classify_type(dummy_float)), 0,!is_signed); \
} while ( 0 )

// Pointer to a pointer
#  define convar_addpp( VAR ) do { \
          espshell_varaddp( #VAR, &VAR, sizeof(void *), 0, 1, 1); \
} while ( 0 )

// Array of pointers
#  define convar_addap( VAR ) do { \
          espshell_varadda( #VAR, &VAR, sizeof(VAR[0]), sizeof(VAR) / sizeof(VAR[0]), 0, 1, 1); \
} while ( 0 )

#else
// convar_addX API disabled
#  define convar_add( ... )  do {} while( 0 )
#  define convar_addp( ... ) do {} while( 0 )
#  define convar_adda( ... ) do {} while( 0 )
#  define convar_addpp( ... ) do {} while( 0 )
#  define convar_addap( ... ) do {} while( 0 )
#endif

#ifdef __cplusplus
};
#endif
                      
#endif //espshell_h

