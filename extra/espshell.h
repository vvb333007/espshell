#ifndef espshell_h
#define espshell_h

// -- ESPShell compile-time setting default values. --
//
// Uncomment whatever you need and change default value to values you want to
// override default settings.
//
// IMPORTANT: also please #undef the setting BEFORE #define'ing it or it will
//            be bunch of GCC warnings on macro redefinition.
//            i.e. #undef POMPT before uncommenting #define PROMPT
//


//#define AUTOSTART 1                 // Autostart ESPShell with sketch.
                                      // if disabled, then sketch can call "extern "C" void espshell_start();" function to start

//#define PROMPT      "esp32#>"       // Normal prompt
//#define PROMPT_I2C  "esp32-i2c#>"   // i2c prompt
//#define PROMPT_UART "esp32-uart#>"  // uart prompt
//#define PROMPT_SEQ  "esp32-seq#>"   // sequence prompt
//#define PROMPT_VFAT "esp32-fat#>"   // FAT filesystem prompt

//#define SERIAL_IS_USB               // class Serial is USBCDC (Arduino Nano ESP32 and othe USB-OTG enabled boards)
//#define STARTUP_PORT UART_NUM_0     // Uart number (or 99 for USBCDC) where shell will be deployed at startup
//#define DO_ECHO 1                   // -1 - espshell is completely silent, commands are executed but all screen output is disabled
                                      //  0 - espshell does not do echo for user input (as modem ATE0 command). commands are executed and their output is displayed
                                      //  1 - espshell default behaviour (do echo, enable all output)

//#define ESPCAM                      // Include ESP32CAM commands (read extra/README.md) or not.
//#define WITH_COLOR 1                // Enable(1) / Disable(0) terminal colors
//#define WITH_HELP 1                 // Set to 0 to save some program space by excluding help strings/functions
//#define UNIQUE_HISTORY 1            // Wheither to discard repeating commands from the history or not
//#define HIST_SIZE 25                // History buffer size (number of commands to remember)
//#define STACKSIZE 5000              // Shell task stack size
//#define BREAK_KEY 3                 // Keycode of an "Exit" key: CTRL+C to exit uart "tap" mode
//#define SEQUENCES_NUM 10            // Max number of sequences available for command "sequence"




// Register global or static variable to be accessible from espshell:
// ----------------- Example Sketch Code -------------------
// int my_var;
//
// void setup() {
//   ...
//   espshell_var(my_var);
// }
// ---------------------------------------------------------
//
// Registered variables can be accessed by "var" command:
//
// esp32#>var my_var 666       <--- set variable
// esp32#>var my_var           <--- display variable
// esp32#>var                  <--- display list of registered variables
#if 1
#  define convar_add( VAR ) espshell_varadd( #VAR, &VAR, sizeof(VAR))
#else
#  define convar_add( VAR )
#endif




//
// ESPShell API & utility functions
//
#ifdef __cplusplus
extern "C" {
#endif


//



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
bool espshell_exec_finished();


// DONT USE THIS! use convar_add() instead
void espshell_varadd(const char *name, void *ptr, int size);

// By default ESPShell occupies UART0. It could be changed
// at compile time by setting #define USE_UART in espshell.h
// to required value OR at runtime by calling console_attach2port()
// 
// Special value 99 means USB console port for boards with USB-OTG
// support
int console_attach2port(int i);


// classic digitalRead() undergoes PeriMan checks which do not allow to
// read data for pins which are NOT configured as GPIO: for example uart pins
// or i2c pins.
//
// digitalForceRead() is able to read any pin no matter what. 
// for OUTPUT pins it enables INPUT automatically.
int digitalForceRead(int pin);

// same as digitalWrite() but bypasses periman so no init/deinit
// callbacks are called. pin bus type remain unchanged.
// GPIO configured as GPIO via IO MUX remain on IO_MUX and dont get
// reconfigured as "simple GPIO" via GPIO matrix
void digitalForceWrite(int pin, unsigned char level);


// same as pinMode() but calls IDF directly bypassing
// PeriMan's pin deinit/init. As a result it allows flags manipulation on
// reserved pins without crashing & rebooting. And it is much faster than 
// classic pinMode()
void pinMode2(unsigned int pin, unsigned int flags);



#ifdef __cplusplus
};
#endif
                      
#endif //espshell_h
