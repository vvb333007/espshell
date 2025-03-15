/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#define COMPILING_ESPSHELL 1  // dont touch this!

// Limits 
#define PWM_MAX_FREQUENCY 10000000     // Max frequency for PWM, 10Mhz. Must be below XTAL clock and well below APB frequency. 
#define MAX_PROMPT_LEN 16              // Prompt length ( except for PROMPT_FILES), max length of a prompt
#define MAX_PATH 256                   // max filesystem path len
#define MAX_FILENAME MAX_PATH          // max filename len (equal to MAX_PATH for now)
#define UART_DEF_BAUDRATE 115200
#define UART_RXTX_BUF 512
#define I2C_RXTX_BUF 1024
#define I2C_DEF_FREQ 100000            
#define ESPSHELL_MAX_INPUT_LENGTH 500  // Maximum input length (strlen()). User input greater than 500 characters will be silently discarded
#define ESPSHELL_MAX_CNLEN 10          // Maximum length (strlen()) of a command name. 
                                       // NOTE!! If you change this, make sure initializer string is changed too in question.h:help_command_list()

// Prompts used by command subdirectories
#define PROMPT "esp32#>"                // Main prompt
#define PROMPT_I2C "esp32-i2c%u>"       // I2C prompt
#define PROMPT_SPI "esp32-spi%u>"       // SPI prompt
#define PROMPT_UART "esp32-uart%u>"     // UART prompt
#define PROMPT_SEQ "esp32-seq%u>"       // Sequence (RMT) subtree prompt
#define PROMPT_FILES "esp32#(%s%s%s)>"  // File manager prompt (format string is /color tag/, /current working directory/, /color tag/)
#define PROMPT_SEARCH "Search: "        // History search prompt
#define PROMPT_ESPCAM "esp32-cam>"      // ESPCam settings directory

// Includes. Lots of them.
// classic C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
// Arduino
#include <Arduino.h>
// ESP-IDF
#include "sdkconfig.h"
// FreeRTOS
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
// ESP-IDF again
#include <soc/soc_caps.h>
#include <soc/gpio_struct.h>
#include <soc/pcnt_struct.h>
#include <soc/efuse_reg.h>
#include <hal/gpio_ll.h>
#include <driver/gpio.h>
#include <driver/pcnt.h>
#include <driver/uart.h>
#include <rom/gpio.h>
#include <esp_timer.h>
#include <esp_chip_info.h>
// Arduino Core
#include <esp32-hal-periman.h>
#include <esp32-hal-ledc.h>
#include <esp32-hal-rmt.h>
#include <esp32-hal-uart.h>
// Filesystems
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <esp_vfs.h>
#include <esp_partition.h>
#include <esp_littlefs.h>
#include <esp_spiffs.h>
#include <esp_vfs_fat.h>
#include <diskio.h>
#include <diskio_wl.h>
#include <vfs_fat_internal.h>
#include <wear_levelling.h>
#include <sdmmc_cmd.h>
// Camera
#include <esp_camera.h>

// Espressif devteam has changed their core API once again
#if __has_include("esp_private/esp_gpio_reserve.h")
#  include "esp_private/esp_gpio_reserve.h"
#elif __has_include("esp_gpio_reserve.h")
#  include "esp_gpio_reserve.h"
#else
#  warning "esp_gpio_reserve.h is not found, lets see if it will compile at all"
#endif

// Compile-time settings
#include "espshell.h"

// Common macros used throughout the code, GCC-specific stuff, etc
#define UNUSED __attribute__((unused))
#define INLINE inline __attribute__((always_inline))
#define NORETURN __attribute__((noreturn))
#define PRINTF_LIKE __attribute__((format(printf, 1, 2)))
#define EXTERN extern //TODO: get rid of it, we don't need another macro

#if AUTOSTART
#  define STARTUP_HOOK __attribute__((constructor))
#else
#  define STARTUP_HOOK
#endif

#if WITH_VERBOSE
#  define VERBOSE( ... ) __VA_ARGS__
#else
#  define VERBOSE( ... ) { /* Nothing here */ }
#endif

// gcc stringify which accepts macro names
#define xstr(s) ystr(s)   
#define ystr(s) #s


#define SERIAL_8N1 0x800001c
#define BREAK_KEY 3

// Special pin names.
#define BAD_PIN    255 // Don't change! Non-existing pin number. 
#define UNUSED_PIN  -1 // Don't change! A constant which is used to initialize ESP-IDF structures field, a pin number, when 
                       // we want to tell ESP-IDF that we don't need / don't use this structure field. (see count.h)

// enable -Wformat warnings. Turned off by Arduino IDE by default.
#pragma GCC diagnostic warning "-Wformat"  

// -- Miscellaneous forwards. These can not be resolved by rearranging of "#include"'s :-/

// block current task, wait for signal from another task or from an ISR.
// Timeout value less than 1 means infinite timeout. Declared in task.h
static bool task_wait_for_signal(uint32_t *sig, uint32_t timeout_ms); 

// are we running in espshell task context? (task.h)
static INLINE bool is_foreground_task();             

// check if UART u is up and operationg (is driver installed?). Declared in uart.h
static inline bool uart_isup(unsigned char u);       

// check if address is in range 0x20000000 .. 0x80000000. Declared in qlib.h
bool __attribute__((const)) is_valid_address(const void *addr, unsigned int count); 

static int q_strcmp(const char *, const char *);     // loose strcmp
static int PRINTF_LIKE q_printf(const char *, ...);  // printf()
static int q_print(const char *);                    // puts()

static bool pin_is_input_only_pin(int pin);
static bool pin_exist(unsigned char pin);
static bool pin_exist_silent(unsigned char pin);

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 0)
// TODO: rename
EXTERN bool esp_gpio_is_pin_reserved(unsigned int gpio);
#else
static INLINE bool esp_gpio_is_pin_reserved(unsigned int gpio) {
  return esp_gpio_is_reserved(1ULL << gpio);
}
#endif

static NORETURN void must_not_happen(const char *message, const char *file, int line);


void STARTUP_HOOK espshell_start();
static int espshell_command(char *p);


// Globals & string constants
//
// "Context": an user-defined value (a number) which is set by change_command_directory() when switching to new command subtree. 
// This is how command "uart 1" passes its argument  (the number "1") to the subtree commands like "write" or "read". 
// Used to store: sequence number, uart,i2c interface number, probably something else
static unsigned int Context = 0;

// Currently used prompt
static const char * prompt = PROMPT; 

// Common messages. 
static const char *Failed = "% <e>Failed</>\r\n";

static const char *Notset = "<i>not set</>\r\n";

static const char *i2cIsDown = "%% I2C%u bus is not initialized. Use command \"up\" to initialize\r\n";

static const char *uartIsDown = "%% UART%u is down. Use command \"up\" to initialize it\r\n";

static const char *WelcomeBanner = "\033[H\033[2J%\r\n"
                                   "% ESPShell " ESPSHELL_VERSION "\r\n"
                                   "% Type \"?\" and press <Enter> for help\r\n"
                                   "% Press <Ctrl+L> to clear the screen, enable colors and show \"tip of the day\"\r\n";

#if WITH_HELP
static const char *Bye = "% Sayonara!\r\n";

static const char *SpacesInPath = "<e>% Too many arguments.\r\n"
                                  "% If your path contains spaces, please enter spaces as \"*\":\r\n"
                                  "% Examples: \"cd Path*With*Spaces\",  \"rm /ffat/Program*Files\"</>\r\n";

static const char *MultipleEntries = "% Processing multiple paths.\r\n"
                                     "% Not what you want? Use asteriks (*) instead of spaces in the path\r\n";

static const char *VarOops = "<e>% Oops :-(\r\n"
                             "% No registered variables to play with</>\r\n"
                             "% Try this:\r\n"
                             "%  <i>1. Add include \"espshell.h\" to your sketch</>\r\n"
                             "%  <i>2. Use \"convar_add()\" macro to register your variables</>\r\n"
                             "%\r\n"
                             "% Once registered, variables can be manipulated by the \"var\" command\r\n"
                             "% while your sketch is running\r\n";
#endif //WITH_HELP

// -- Actual ESPShell code #included here --

// 1. Common macros used by keywords trees
#include "keywords_defs.h"      

// 2. Console abstraction layer: provides generic read/write operation on UART or USBCDC device. Can be overriden with
// any other implementation to support specific devices
#include "console.h"

// 3. qLib : utility functions like q_printf(), string to number conversions, mutexes, memory management etc core functions
// Must be included before anything else
#include "qlib.h"

// 4. Really old (but refactored) version of editline. Probably from 80's. Works well, rock solid :)
#include "editline.h"           // editline library
#include "userinput.h"          // userinput tokenizer and reference counter

// 5. ESPShell core
// .h files contain actual code, not just declarations: this way Arduino IDE will not attempt to compile them
#include "convar.h"             // code for registering/accessing sketch variables
#include "task.h"               // main shell task, async task helper, misc. task-related functions
#include "keywords.h"           // all command trees
#include "sequence.h"           // RMT component (sequencer)   
#include "pwm.h"                // PWM component
#include "pin.h"                // GPIO manipulation
#include "count.h"              // Pulse counter / frequency meter
#include "i2c.h"                // i2c generic interface
#include "spi.h"                // spi generic interface
#include "uart.h"               // uart generic interface
#include "misc.h"               // misc command handlers
#include "cpu.h"                // cpu-related command handlers  
#include "filesystem.h"         // file manager
#include "memory.h"             // memory component
#include "espcam.h"             // Camera support

// 6. These two must be included last as they are supposed to call functions from every other module
#include "show.h"               // "show KEYWORD [ARG1 ARG2 ... ARGn]" command
#include "question.h"           // cmd_question(), context help handler and help pages


// Parse & execute: main ESPShell user input processor. User input, an asciiz string is passed to this processor as is.
// The main task, which reads user input and calls this function is in task.h
//
// 1. Split user input /p/ into tokens. Token #0 is a command, other tokens are command arguments.
// 2. Find an appropriate entry in keywords[] array (command name and number of arguments must match)
// 3. Execute coresponding callback, may be in a newly created task cotext (for commands ending with "&", like "count&")
//
// asciiz /p/ - is the user input as returned by readline(). Must be writable memory!
//
// returns 0 on success, -1 if number of arguments doesn't match (missing argument) or >0 - the index in argv[] array
//         pointing to failed/problematic argument. espshell_command() relies on code returned by underlying command handler (callback function)
//
static int
espshell_command(char *p) {

  char **argv = NULL;
  int argc, i, bad;
  bool found, fg;

  // argc/argv container. Normally free()-ed before this function returns except for the cases, when background commands are executed:
  // background command is then resposible for container deletion.
  argcargv_t *aa = NULL;

  // got something to process?
  if (p && *p) {

    //make a history entry, if history is enabled (default)
    if (History)
      history_add_entry(userinput_strip(p));

    // tokenize user input
    if ((aa = userinput_tokenize(p)) == NULL) {
      q_free(p);
      return -1;
    }

    // /fg/ is /true/ for foreground commands. it is /false/ for background commands.
    // TODO: char comparision is enough, q_strcmp is overkill
    if ((fg = q_strcmp(aa->argv[aa->argc - 1],"&")) == false) {
      //q_print("% A background exec has been requested\r\n");
      // strip last "&" argument
      aa->argc--;
    }

    // from now on /p/ is can be freed by userinput_unref() only, as part of /aa/
    // Handy shortcuts. GCC is smart enough to eliminate these.
    argc = aa->argc;
    argv = aa->argv;


    // /keywords/ is a pointer to one of /keywords_main/, /keywords_uart/ ... etc keyword tables.
    // It points at main tree at startup and then can be switched. 
    barrier_lock(keywords_mux);
    const struct keywords_t *key = keywords;   
    barrier_unlock(keywords_mux);
    found = false;

one_more_try:
    i = 0;
    bad = -1;

    // Go through the keywords array till the end
    while (key[i].cmd) {

      // command name matches user input?
      if (!q_strcmp(argv[0], key[i].cmd)) {

        // found a candidate
        found = true;

        // Match number of arguments. There are many commands whose names are identical but number of arguments is different.
        // One special case is keywords with their /.argc/ set to -1: these are "any number of argument" commands. These should be positioned
        // carefully in keywords array to not shadow other entries which differs in number of arguments only
        if (((argc - 1) == key[i].argc) || (key[i].argc < 0)) {

          // Execute the command by calling a callback. It is only executed when not NULL. There are entries with /cb/ set to NULL: those
          // are for help text only, as they are processed by "?" command/keypress
          //
          // if nonzero value is returned then it is an index of the "failed" argument (value of 3 means argv[3] was bad (for values > 0))
          // value of zero means successful execution, return code <0 means argument number was wrong (missing argument, extra arguments)
          if (key[i].cb) {

            int l;
            MUST_NOT_HAPPEN((l = strlen(argv[0]) - 1) < 0);

            // save command handler (exec_in_background() will need it)
            aa->gpp = key[i].cb;

            bad = fg ? key[i].cb(argc, argv) : exec_in_background(aa);

            if (bad > 0)
              q_printf("%% <e>Invalid %u%s argument \"%s\" (\"? %s\" for help)</>\r\n",NEE(bad), bad < argc ? argv[bad] : "FIXME", argv[0]);
            else if (bad < 0) {
              if (bad == CMD_MISSING_ARG)
                q_printf("%% <e>One or more arguments are missing(\"? %s\" for help)</>\r\n", argv[0]);
              // Keep silent on other error codes which are <0
            }
            else
              // make sure keywords[i] is a valid pointer (change_command_directory() changes keywords list so keywords[i] might be invalid pointer)
              i = 0;
            break;
          }  // if callback is provided
        }    // if argc matched
      }      // if name matched
      i++;   // process next entry in keywords[]
    }        // until all keywords[] are processed

    // Reached the end of the list and didn't find any exact match?
    if (!key[i].cmd) {

      // Lets try to search in /keywords_main/ (if we are currently in a subdirectory)
      if (key != keywords_main) {
        key = keywords_main;
        goto one_more_try;
      }

      // If we get here, then we have problem:
      if (found)  // we had a name match but number of arguments was wrong
        q_printf("%% <e>\"%s\": wrong number of arguments</> (\"? %s\" for help)\r\n", argv[0], argv[0]);
      else        // no name match let alone arguments number
        q_printf("%% <e>\"%s\": command not found</>\r\n", argv[0]);
    }

    if (!found)
      // Be helpful :)
      HELP(q_print("% <e>Type \"?\" to show the list of commands available</>\r\n"));
  }

  // free memory associated wih user input
  userinput_unref(aa);
  return bad;
}


// Execute an arbitrary shell command (\n are allowed for multiline).
// Call returns immediately, but /p/ must remain valid memory until actual command finishes its execution.
//
// One can use espshell_exec_finished() to check when it is a time for another espshell_exec()
// Currently used by editline hotkey processing to inject various shell commands in user input.

void espshell_exec(const char *p) {
  TTYqueue(p);
}

// check if last espshell_exec() call has completed its
// execution
bool espshell_exec_finished() {
  bool ret;
  barrier_lock(Input_mux);
  ret = (*Input == '\0');
  barrier_unlock(Input_mux);
  return ret;
}


// This function may be called multiple times despite its name:
// Call functions which are intended to be called once, things like convars & memory allocations.
//
static bool call_once = false;

#if CMD_STATS
// Command tree stats, available as convars ("var NHa"...)
static unsigned short NHandlers = 0;  // Number of command handlers
static unsigned short NCmds = 2;     // "?" and "exit" only counted once, and it is done here
static unsigned short NTrees = 0;    // Number of command directories
#endif

static  void espshell_initonce() {

  if (!call_once) {
    call_once = true;

    // These asserts are mostly for var.h module since it *assumes* that sizeof(int) is 4 and so on
    // (as it should be on a 32bit cpu)
    // There is a code which assumes sizeof(unsigned int) >= sizeof(void *) here and there (mostly in task.h)
    // We do these asserts in the beginning so we can safely convert between 32-bit types and make safe our, generally speaking unsafe code
    
    static_assert(sizeof(int) == 4,"Unexpected int size");
    static_assert(sizeof(short) == 2,"Unexpected short size");
    static_assert(sizeof(char) == 1,"Unexpected char size");
    static_assert(sizeof(float) == 4,"Unexpected float size");
    static_assert(sizeof(void *) == 4,"Unexpected pointer size");
    static_assert(sizeof(unsigned long long) == 8,"Unexpected long long size");

    // Do some stats
#if CMD_STATS
    const struct keywords_t *k[] = { 
      keywords_uart, 
      keywords_i2c, 
#if WITH_SPI      
      keywords_spi, 
#endif      
      keywords_sequence, 
#if WITH_FS      
      keywords_files, 
#endif      
      keywords_main,
#if WITH_ESPCAM      
      keywords_espcam,
#endif      
      NULL 
    }, *tree;

    unsigned int j;
    for (j = 0;(tree = k[NTrees]) != NULL ;NTrees++) {
      void *prev = NULL;
      while (tree[j].cmd != NULL) {
        MUST_NOT_HAPPEN(ESPSHELL_MAX_CNLEN < strlen(tree[j].cmd));
        // Try to count only unique command handlers. This count will be < than total amount of commands
        // The counting procedure relies on that fact that the same command handler can appear at number of
        // keywords but these keywords must be grouped together (e.g. all "show" keywords are grouped together)
        if (tree[j].cb != prev) {
          NHandlers++;
          prev = (void *)tree[j].cb;
        }
        NCmds++;
        j++;
        
      }
    }
    convar_add(NCmds);
    convar_add(NTrees);
    convar_add(NHandlers);
#endif // CMD_STATS
    // add internal variables ()
    convar_add(ls_show_dir_size);  // enable/disable dir size counting for "ls" command
    convar_add(pcnt_unit);         // PCNT unit which is used by "count" command
    convar_add(bypass_qm);         // enable/disable "?" as a context help hotkey
    convar_add(tbl_min_len);       // buffers whose length is > printhex_tbl (def: 16) are printed as fancy tables
    convar_add(ledc_res);          // Override PWM duty cycle resolution bitwidth: Duty range is from 0 to (2**ledc_res-1)
#if WITH_WRAP
    convar_addap(Tasks);
#endif    
    // init subsystems
    seq_init();
  }
}

// Start ESPShell
// Normally (AUTOSTART==1) starts automatically. With autostart disabled EPShell can
// be started by calling espshell_start().
//
// This function can be used to restart the shell which was terminated by command "exit ex"
//
void STARTUP_HOOK espshell_start() {

  espshell_initonce();
  if (espshell_started()) {
    HELP(q_print("% ESPShell is started already, exiting\r\n"));
  }
  else
    espshell_task((const void *)1); // defined in task.h
}
