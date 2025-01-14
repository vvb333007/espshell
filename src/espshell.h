/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

  // !! READ ME FIRST !!
 //
 // 1) Inclusion of this file will enable CLI automatically
 // 2) No functions need to be called in order to use the shell
 // 3) There are Compile Time Settings below to tweak ESP32Shell behaviour
 // 4) Console on USB (Serial is of USBCDC class) is not supported yet
 // 5) Using CompileTimeSettings section below disable code which is not needed to decrease the memory footprint

#ifndef espshell_h
#define espshell_h

// Code version, dont change. 
#define ESPSHELL_VERSION "0.99.1"

// -- Compile-time ESPShell settings --
//
//#define SERIAL_IS_USB          // Not yet

#define AUTOSTART 1              // Set to 0 for manual shell start via espshell_start().
#define STACKSIZE (5 * 1024)     // Shell task stack size

#define WITH_HELP 1              // Set to 0 to save some program space by excluding help strings/functions
#define WITH_HISTORY 1           // Set to 0 to when looking for memory leaks
#define HIST_SIZE 20             // History buffer size (number of commands to remember)

#define WITH_ESPCAM 0            // Include AiThinker ESP32CAM commands

#define WITH_VAR 1               // enable support for sketch variables

#define STARTUP_PORT UART_NUM_0  // Uart number (or 99 for USBCDC) where shell will be deployed at startup
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

#define SEQUENCES_NUM 10         // Max number of sequences available for command "sequence"

#define MEMTEST 0                // hunt for espshell's memory leaks

// -- ESPShell public API --
// 1) Access sketch variables from ESPShell while sketch is running.   Yes it is possible, you just need to /register/ your variable by using
//    "convar_add(VAR)" macro. Once registered, variables are available for read/write access (via "var" command). Variable types supported: 
//    pointers, integers and floating point types both signed and unsigned
// 
//    Example: register 3 sketch variables in ESPShell
//    ...
//    int some_variable;
//    const char *ptr;
//    static float volatile another_variable;
//    ...
//    convar_addp(ptr);
//    convar_add(some_variable);
//    convar_add(another_variable);
#if WITH_VAR


// TODO:
// The code below could be written simplier if we had acces to __builtin_types_compatible() or _Generic.
// For some reason GCC v13+ (Xtensa arch) does not support those. My old Cygwin installation supports both
// on GCC v11.
//
// Array registration and pointer registration are two different macro/functions because &VAR returns &VAR[0] for array
// but not for pointer
//
// All convar_addX() macros are reduced to single function call during compilation
//
extern float dummy_float;
extern void *dummy_pointer;

#  define convar_add( VAR ) do { \
          bool is_signed; \
          __typeof__(VAR) __x = ( __typeof__(VAR) )(-1); \
          is_signed = (__x < 0); /* HELLO! If you see this warning during compilation - just ignore it :) */ \
          espshell_varadd( #VAR, &VAR, sizeof(VAR), \
          (__builtin_classify_type(VAR) == __builtin_classify_type(dummy_float)), \
          (__builtin_classify_type(VAR) == __builtin_classify_type(dummy_pointer)), \
          !is_signed); \
} while( 0 )

#  define convar_addp( VAR ) do { \
          bool is_signed; \
          __typeof__(VAR[0]) __x = ( __typeof__(VAR[0]) )(-1); \
          is_signed = (__x < 0);   /* HELLO! If you see this warning during compilation - just ignore it :) */ \
          espshell_varaddp( #VAR, &VAR, sizeof(VAR[0]), \
          (__builtin_classify_type(VAR[0]) == __builtin_classify_type(dummy_float)), \
          (__builtin_classify_type(VAR[0]) == __builtin_classify_type(dummy_pointer)), \
          !is_signed); \
} while ( 0 )

#  define convar_adda( VAR ) do { \
          bool is_signed; \
          __typeof__(VAR[0]) __x = ( __typeof__(VAR[0]) )(-1); \
          is_signed = (__x < 0);   /* HELLO! If you see this warning during compilation - just ignore it :) */ \
          espshell_varadda( #VAR, &VAR, sizeof(VAR[0]), sizeof(VAR) / sizeof(VAR[0]), \
          (__builtin_classify_type(VAR[0]) == __builtin_classify_type(dummy_float)), \
          (__builtin_classify_type(VAR[0]) == __builtin_classify_type(dummy_pointer)), \
          !is_signed); \
} while ( 0 )

#else
#  define convar_add( ... )  do {} while( 0 )
#  define convar_addp( ... ) do {} while( 0 )
#  define convar_adda( ... ) do {} while( 0 )
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 2) Start ESPShell manually (case when AUTOSTART is 0).
// By default espshell shell autostarts. If AUTOSTART is set to 0 in espshell.h then
// user sketch must call espshell_start() to manually start the shell. Regardless of AUTOSTART:
// a shell which was closed by "exit ex" command it is ok to call this function to restart the shell
//
#if !AUTOSTART
void espshell_start();
#endif

// 3) Execute an arbitrary shell command (\n are allowed for multiline, i.e. multiple commands at once).
// This function injects its argument to espshell's input stream as if it was typed by user. 
// It is an asyn call, returns immediately. Next call can be done only after espshell_exec_finished() 
// returns /true/
//
// Example: espshell_exec("uptime \n cpu \n");
//
void espshell_exec(const char *p);


// 4) Check if ESPShell has finished processing of last espshell_exec() call and is ready for new espshell_exec()
// This function does NOT tell you that command execution is finished. It is about readiness of the shell to accept new
// commands
//
bool espshell_exec_finished();


// DONT USE THIS! use convar_add() instead
void espshell_varadd(const char *name, void *ptr, int size, bool isf, bool isp, bool isu);
void espshell_varaddp(const char *name, void *ptr, int size, bool isf, bool isp, bool isu);
void espshell_varadda(const char *name, void *ptr, int size,int count, bool isf, bool isp, bool isu);

// 5) By default ESPShell occupies UART0. Default port could be changed
// at compile time by setting #define STARTUP_PORT in "extra/espshell.h"
// to required value OR at runtime by calling console_attach2port()
// 
// Special value 99 means USB console port for boards with USB-OTG
// support
//
// /port/ - 0, 1 or 2 for UART interfaces or 99 for native USB console 
//          interface. If /port/ is negative number then this function 
//          returns port number which currently in use by ESPShell.
// 
// returns number of port which is now used
//
int console_attach2port(int port);


// 6) classic digitalRead() undergoes PeriMan checks (see Arduino Core, esp32-periman.c) 
// which do not allow to read data for pins which are NOT configured as GPIO:  
// UART pins or I2C pins are examples of pins that can not be read by digitalRead().
//
// digitalForceRead() is able to read ANY pin no matter what. 
// for OUTPUT pins it enables INPUT automatically.
//
// This function is much faster than digitalRead()
//
int digitalForceRead(int pin);

// 7) Again, periferial manager bypassed: faster version
// of digitalWrite():
//
// 1. pin not need to be set for OUTPUT
// 2. it is faster
//
void digitalForceWrite(int pin, unsigned char level);

// 8) Discussion: https://github.com/espressif/arduino-esp32/issues/10370
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
// Function pinMode2() bypasses periman, does not reconfigure pin and even can be applied
// to **reserved** ESP32 pins (SPI FLASH CLK) for example providing that new flags are compatible
// with pin function.
//
// calling pinMode(6,...) will likely crash your ESP32, pinMode2() - not
//
// /pin/    - pin (GPIO) number
// /flags/  - flags as per pinMode(): INPUT, OUTPUT, OPEN_DRAIN,PULL_UP, PULL_DOWN and OUTPUT_ONLY
//          - NOTE: On ESP32 OUTPUT is defined as INPUT and OUTPUT. You can use flag OUTPUT_ONLY if you don't want
//            INPUT to be automatically set.
//
#define OUTPUT_ONLY ((OUTPUT) & ~(INPUT))

void pinMode2(unsigned int pin, unsigned int flags);


#ifdef __cplusplus
};
#endif
                      
#endif //espshell_h

