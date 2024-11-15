#ifndef espshell_h
#define espshell_h


// -- ESPShell compile-time settings begin --
// 

// ===> Disable or enable certain features. I bet you don't need all 3 filesystems support
//
//#define ESPCAM          // Add support for AiThinker ESP32-CAM (read extra/README.md).
//undef AUTOSTART         // Disable autostart. User sketch can start the shell later by calling espshell_start()
//#undef WITH_COLOR       // Disable ANSI terminal colors
//#undef WITH_HELP        // Disable help system & hint messages to save program space
//#undef WITH_FS          // Disable file manager completely, or ..
//#undef WITH_SPIFFS      //     .. disable support for SPIFFS
//#undef  WITH_LITTLEFS   //     .. disable support for LittleFS
//#undef WITH_FAT         //     .. disable support for FAT/exFAT
//#define MEMTEST 1       // Include code for memory-leaks analisys: espshell tracks its own memory allocations

// ===> Runtime settings. Uncomment 2 lines at once to avoid GCC 'macro redefinition' warnings.
// These settings are about various limits and startup

// 1) UART port number (or 99 for USBCDC) where shell will be deployed at startup. Valid values are: UART_NUM_0, UART_NUM_1, UART_NUM_2 and 99
//#undef STARTUP_PORT
//#define STARTUP_PORT UART_NUM_0 

// 2) Echo mode at espshell startup: 1:normal, 0:don't do echo, -1:don't do any screen output (see "echo" command)
//#undef STARTUP_ECHO
//#define STARTUP_ECHO 1          

// 3) Max number of sequences that can be configured by command "sequence". Decrease it to save some RAM (min value is 1)
//#undef SEQUENCES_NUM
//#define SEQUENCES_NUM   10      

// 4) Max number of simultaneously mounted filesystems
//#undef MOUNTPOINTS_NUM
//#define MOUNTPOINTS_NUM 5       

// 5) Shell task stack size. 
//#undef  STACKSIZE
//#define STACKSIZE (5*1024)      

// 6) Max subdirectory depth: path of maximum length of 256 symbols can hold 127 directories: "/a/a/a/a/a/a/a.../a"
//    Bigger the number larger STACKSIZE is required
//#undef DIR_RECURSION_DEPTH
//#define DIR_RECURSION_DEPTH 127 

// -- ESPShell compile-time settings end --

// -- ESPShell public API --
// 1) Access sketch variables from ESPShell while sketch is running
//    Yes it is possible, you just need to /register/ your variable by using
//    "convar_add(VAR)"" macro. Once registered, variables are available for 
//    read/write access (via "var" command). Variable types supported: pointers,
//    integers and floating point types both signed and unsigned
// 
//    Example: register two sketch variables in ESPShell
//    ...
//    int some_variable;
//    const char *ptr;
//    static float volatile another_variable;
//    ...
//    convar_add(ptr);
//    convar_add(some_variable);
//    convar_add(another_variable);
#if 1
   extern float dummy_float;
   extern void *dummy_pointer;

#  define convar_add( VAR ) \
          espshell_varadd( #VAR, &VAR, sizeof(VAR), \
          (__builtin_classify_type(VAR) == __builtin_classify_type(dummy_float)), \
          (__builtin_classify_type(VAR) == __builtin_classify_type(dummy_pointer)))
#else
#  define convar_add( VAR ) do {} while( 0 )
#endif


#ifdef __cplusplus
extern "C" {
#endif

// 2) Start ESPShell manually.
// By default espshell shell autostarts. If AUTOSTART is set to 0 in espshell.h then
// user sketch must call espshell_start() to manually start the shell. Regardless of AUTOSTART:
// a shell which was closed by "exit ex" command it is ok to call this function to restart the shell
//
#if !AUTOSTART
void espshell_start();
#endif

// 3) Execute an arbitrary shell command (\n are allowed for multiline).
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
void espshell_varadd(const char *name, void *ptr, int size, bool isf, bool isp);

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
