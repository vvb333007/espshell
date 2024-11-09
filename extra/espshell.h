#ifndef espshell_h
#define espshell_h


// -- ESPShell compile-time settings & their default values --
//
// If you choose to uncomment and change any of these please add #undef before it 
// or GCC will produce "redefinition" warnings
//
// Example: disable filesystem support in ESPShell
//
// #undef WITH_FS
// #define WITH_FS 0


//#define ESPCAM                     // Add support for AiThinker ESP32-CAM (read extra/README.md).
//#define AUTOSTART       1          // Set to 0 for manual shell start via espshell_start().
//#define WITH_COLOR      1          // Add support for terminal colors
//#define WITH_HELP       1          // Set to 0 to save some program space by excluding help strings/functions
//#define WITH_HISTORY    1          // Keep it 1. Disable/enable history in runtime with hidden command "history off" or "history on"
//#define WITH_FS         1          // Filesystems support
//#define WITH_SPIFFS     1          //   add support for SPIF filesystem
//#define WITH_LITTLEFS   1          //          --       LittleFS
//#define WITH_FAT        1          //          --       FAT
//#define HIST_SIZE       20         // History buffer size (number of commands to remember)
//#define STARTUP_PORT    UART_NUM_0 // Uart number (or 99 for USBCDC) where shell will be deployed at startup.
//#define SEQUENCES_NUM   10         // Max number of sequences available for command "sequence"
//#define MOUNTPOINTS_NUM 5          // Max number of simultaneously mounted filesystems
//#define STACKSIZE       (5*1024)   // Shell task stack size
//#define DIR_RECURSION_DEPTH 127    // Max subdirectory depth: max path of 256 symbols can include 127 directories: "/a/a/a/a/a/a/a.../a"
//#define MEMTEST         0          // Hunt for espshell's memory leaks. For ESPShell developers
//#define DO_ECHO         1          // Echo mode at espshell startup: 1:normal, 0:don't do echo, -1:don't do any screen output (see "echo" command)


// -- Access sketch variables from ESPShell --
//
// Yes it is possible, you just need to /register/ your variable by using
// convar_add() macro. Once regitered, variables are available for read/write 
// access via "var" command
// 
// Example: register two sketch variables in ESPShell
// ...
// int some_variable;
// static float volatile another_variable;
// ...
// convar_add(some_variable);
// convar_add(another_variable);
//
#if 1
   extern float dummy_float;
#  define convar_add( VAR ) espshell_varadd( #VAR, &VAR, sizeof(VAR), (__builtin_classify_type(VAR) == __builtin_classify_type(dummy_float)))
#else
#  define convar_add( VAR ) do {} while( 0 )
#endif



//
//  -- ESPShell API & utility functions --
// These are for sketch-to-ESPShell interaction: changing the UART espshell is currently uses,
// manually start espshell, executing arbitrary commands and so on

#ifdef __cplusplus
extern "C" {
#endif


// Start ESPShell
// By default espshell shell autostarts. If AUTOSTART is set to 0 in espshell.h then
// user sketch must call espshell_start() to manually start the shell.
//
#if !AUTOSTART
void espshell_start();
#endif

// Execute an arbitrary shell command (\n are allowed for multiline).
// it is an async call: it returns immediately. one can use espshell_exec_finished()
// to check if actual shell command has finished its execution. Data pointed by "p"
// must remain allocated until command execution finishes
//
// Example: espshell_exec("uptime \n cpu \n");
//
void espshell_exec(const char *p);


// check if last espshell_exec() call has completed its
// execution. Does NOT mean command itself finished its execution tho. This function
// only tells if it is possible to enqueue more commands with espshell_exec() or not
//
bool espshell_exec_finished();


// Register sketch variable
// DONT USE THIS! use convar_add() instead
// /name/ - variable name
// /ptr/  - pointer to variable
// /size/ - 1,2 or 4: variable size in bytes
// /isf/  - must be /true/ if registering a floating point variable
//
void espshell_varadd(const char *name, void *ptr, int size, bool isf);

// By default ESPShell occupies UART0. It could be changed
// at compile time by setting #define USE_UART in espshell.h
// to required value OR at runtime by calling console_attach2port()
// 
// Special value 99 means USB console port for boards with USB-OTG
// support
//
// /port/ - 0, 1 or 2 for UART interfaces or 99 for native USB console 
//          interface. If /port/ is negative number then this function 
//          returns port number which currently in use by ESPShell.
// 
// Number of port which is now used
//
int console_attach2port(int port);


// classic digitalRead() undergoes PeriMan checks (see Arduino Core, esp32-periman.c) 
// which do not allow to read data for pins which are NOT configured as GPIO:  
// UART pins or I2C pins are examples of pins that can not be read by digitalRead().
//
// digitalForceRead() is able to read ANY pin no matter what. 
// for OUTPUT pins it enables INPUT automatically.
//
// This function is much faster than digitalRead()
//
int digitalForceRead(int pin);

// Again, periferial manager bypassed: faster version
// of digitalWrite():
//
// 1. pin not need to be set for OUTPUT
// 2. it is faster
//
void digitalForceWrite(int pin, unsigned char level);

// Discussion: https://github.com/espressif/arduino-esp32/issues/10370
// 
// pinMode() is a heavy machinery: setting a pin INPUT or OUTPUT dors not just
// change pin modes: it also calls init/deinit functions of a driver associated 
// with pins. As a result, pinMode(3, OUTPU) (pin 3 is an UART0 TX pin on most ESP32's)
// breaks UART0 completely: it call deinit code for the pin, reconfigures pin to be "a GPIO"
// and only then sets pin to OUTPUT.
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
// /flags/  - flags as per pinMode()
//
void pinMode2(unsigned int pin, unsigned int flags);


#ifdef __cplusplus
};
#endif
                      
#endif //espshell_h
