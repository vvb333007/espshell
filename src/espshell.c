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

 // HELLO DEAR RANDOM PROGRAMMER!
 // ACTUAL SOURCES IS IN .H FILES
 // 
 // STARTUP FLOW: espshell_start()->starts espshell_task()->which implements REPL (via readline()/espshell_command())

#define COMPILING_ESPSHELL 1  // dont touch this!

// Limits
#define CONSOLE_UP_POLL_DELAY 1000     // 1000ms. How often to check if Serial is up
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

// Prompts used by command subdirectories. Must be not longer than MAX_PROMPT_LEN
#define PROMPT "esp32#>"                // Main prompt
#define PROMPT_I2C "esp32-i2c%u>"       // I2C prompt
#define PROMPT_SPI "esp32-spi%u>"       // SPI prompt
#define PROMPT_UART "esp32-uart%u>"     // UART prompt
#define PROMPT_SEQ "esp32-seq%u>"       // Sequence (RMT) subtree prompt
#define PROMPT_FILES "esp32#(%s%s%s)>"  // File manager prompt (format string is /color tag/, /current working directory/, /color tag/)
#define PROMPT_SEARCH "Search: "        // History search prompt
#define PROMPT_ESPCAM "esp32-cam>"      // ESPCam settings directory
#define PROMPT_ALIAS "esp32-alias>"     // Alias editing directory.

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
#include <stdatomic.h>

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

#if AUTOSTART
#  define STARTUP_HOOK __attribute__((constructor))
#else
#  define STARTUP_HOOK
#endif

// Enable VERBOSE(...) macro only when "Tools->Core Debug Level" is set to "Verbose"
#if ARDUHAL_LOG_LEVEL == ARDUHAL_LOG_LEVEL_VERBOSE
#   warning "VERBOSE macro is enabled"
#  define VERBOSE( ... ) __VA_ARGS__
#else
#  define VERBOSE( ... ) { /* Nothing here */ }
#endif

// gcc stringify which accepts macro names
#define xstr(s) ystr(s)   
#define ystr(s) #s

#define xPRAGMA(string) _Pragma(#string)
#define PRAGMA(string) xPRAGMA(string)

#define BREAK_KEY 3    // Ctrl+C code

// Special pin names.
#define BAD_PIN    255 // Don't change! Non-existing pin number. 
#define UNUSED_PIN  -1 // Don't change! A constant which is used to initialize ESP-IDF structures field, a pin number, when 
                       // we want to tell ESP-IDF that we don't need / don't use this structure field. (see count.h)
// Number of pins available: 0..NUM_PINS-1
#define NUM_PINS SOC_GPIO_PIN_COUNT

// enable -Wformat warnings. Turned off by Arduino IDE by default.
#pragma GCC diagnostic warning "-Wformat"  

#define WE() PRAGMA(GCC diagnostic warning "-Wformat")
#define WD() PRAGMA(GCC diagnostic ignored "-Wformat")



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
static bool pin_is_reserved(unsigned char pin);

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 0)
extern bool esp_gpio_is_pin_reserved(unsigned int gpio);
#else
static INLINE bool esp_gpio_is_pin_reserved(unsigned int gpio) {
  return esp_gpio_is_reserved(1ULL << gpio);
}
#endif



static NORETURN void must_not_happen(const char *message, const char *file, int line);

void STARTUP_HOOK espshell_start();

// Globals & string constants
//
// "Context": an user-defined value (a number) which is set by change_command_directory() when switching to new command subtree. 
// This is how command "uart 1" passes its argument  (the number "1") to the subtree commands like "write" or "read". 
// Used to store: sequence number, uart,i2c interface number, probably something else
// TODO: make it to be an union of basic C-types
static unsigned int Context = 0;

// Currently used prompt
// TODO: get rid of all these /static char prompt[]/ (defined in functions) by replacing them with one
//       single buffer for everyone
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
                                     "% Not what you want? Use asterisk (*) instead of spaces in the path\r\n";

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
#include "keywords.h"           // all command trees
#include "userinput.h"          // userinput tokenizer and reference counter

static argcargv_t *AA = NULL;   // only valid for foreground commands; used to access to raw user input, mainly by alias code
static int espshell_command(char *p, argcargv_t *aa);


// 5. ESPShell core
// Why? Historical reasons + prevent Arduino from compiling them.
// Rearranging it in classic module.c/module.h way will add alot of code (interfaces, declarations, functions can't be static and so on)

#include "convar.h"             // code for registering/accessing sketch variables
#include "task.h"               // main shell task, async task helper, misc. task-related functions

#include "sequence.h"           // RMT component (sequencer)   
#include "cpu.h"                // cpu-related command handlers  
#include "pwm.h"                // PWM component
#include "pin.h"                // GPIO manipulation
#include "count.h"              // Pulse counter / frequency meter
#include "i2c.h"                // i2c generic interface
#include "spi.h"                // spi generic interface. Not functional, unused
#include "uart.h"               // uart generic interface
#include "misc.h"               // misc command handlers
#if WITH_ALIAS
#include "alias.h"
#endif

#include "filesystem.h"         // file manager
#include "memory.h"             // memory component
#include "espcam.h"             // Camera support

// 6. These two must be included last as they are supposed to call functions from every other module
#include "show.h"               // "show KEYWORD [ARG1 ARG2 ... ARGn]" command
#include "question.h"           // cmd_question(), context help handler and help pages

// Moved to a function, because it is called both from asyn shell command and command processor
//
static void espshell_display_error(int ret, int argc, char **argv) {
    
  MUST_NOT_HAPPEN(argc < 1);
  MUST_NOT_HAPPEN(ret >= argc);

  if (ret > 0)
    q_printf("%% <e>Invalid %u%s argument (\"%s\")</>\r\n", NEE(ret), ret < argc ? argv[ret] : "Empty");
  else if (ret < 0) {
    if (ret == CMD_MISSING_ARG)
      q_printf("%% <e>Wrong number of arguments (%d). Help page: \"? %s\" </>\r\n",argc - 1, argv[0]);
    else if (ret == CMD_NOT_FOUND)
      q_printf("%% <e>\"%s\": command not found</>\r\n"
               "%% Type \"?\" to show the list of commands available\r\n", argv[0]);

    // Keep silent on other error codes which are <0 :
    // CMD_FAILED return code assumes that handler did display error message before returning CMD_FAILED
  }
}  


// Helper task, which runs (cmd_...) handlers in a background.
// When user inputs, say, "pin 8 up high" command, the corresponding handler: cmd_pin()) is called directly 
// by espshell_command() parser,  all execution happens in the loop() task context.
//
// Long story short:
//
// When user asks for a background execution of the command (by adding an "&" as the very argument to any command)
// (e.g. "pin 8 up high &") then exec_in_background() is called instead. It starts a task 
// (espshell_async_task()) which executes "real" command handler stored in aa->gpp.
//
static void espshell_async_task(void *arg) {

  argcargv_t *aa = (argcargv_t *)arg;
  int ret = -1;

  // aa->gpp points to actual command handler (e.g. cmd_pin for command "pin"); aa->gpp is set up
  // by command processor (espshell_command()) according to first keyword (argv[0])
  if (aa) {

    MUST_NOT_HAPPEN(aa->gpp == NULL);

    ret = (*(aa->gpp))(aa->argc, aa->argv);

    q_print("\r\n% Finished: \"<i>");
    userinput_show(aa); // display command name and arguments
    q_print("\"</>, ");
    
    if (ret != 0) {
      espshell_display_error(ret, aa->argc, aa->argv);
      q_print("failed\r\n");
    } else
      q_print("Ok!\r\n");
  }
  
  // its ok to unref null pointer
  userinput_unref(aa);

  // Redraw ESPShell command line prompt (yes we need it, because ESPShell has already printed its prompt out once)
  // TODO: causes output glitches, need to dig it deeper
  //userinput_redraw();

  task_finished();
}

// Executes commands in a background (commands which names end with &).
//
static int exec_in_background(argcargv_t *aa_current) {

  task_t ignored;

  MUST_NOT_HAPPEN(aa_current == NULL);

  //increase refcount on argcargv because it will be used by async task and
  // we want this memory remain allocated after this command
  userinput_ref(aa_current);

  // Start async task. Pin to the same core where espshell is executed
  
// TODO: make_task_name_from_aa()
  if ((ignored = task_new(espshell_async_task, aa_current, aa_current->argv[0])) == NULL) {
    q_print("% <e>Can not start a new task. Resources low? Adjust STACKSIZE macro in \"espshell.h\"</>\r\n");
    userinput_unref(aa_current);
  } else

    // Update task priority if requested    
    if (aa_current->has_prio)
      task_set_priority(ignored, aa_current->prio);

    //Hint user on how to stop bg command
    q_printf("%% Background task started\r\n%% Copy/paste \"<i>kill %p</>\" to abort\r\n", ignored);

  return 0;
}


// Parse & execute: main ESPShell user input processor. 
// The main task, which reads user input and calls this function is in task.h
// User input, an asciiz string is passed to this processor as is; pre-parsed argcargv_t can be passed as the second argument
// but then first argument must be NULL
//
// 1. Split user input /p/ into tokens. Token #0 is a command, other tokens are command arguments.
// 2. Find an appropriate entry in keywords[] array (command name and number of arguments must match)
// 3. Execute coresponding callback, may be in a newly created task cotext (for commands ending with "&")
//
// /p/        - is the user input as returned by readline(). Must be writable memory!
// /aa/       - must be NULL if /p/ is not NULL. Must be a valid pointer if /p/ is NULL
//              This one is used to execute user input which was parsed already (see alias.h)
//
// returns 0 on success, -1 if number of arguments doesn't match (missing argument) or >0 - the index in argv[] array
//         pointing to failed/problematic argument. espshell_command() relies on code returned by underlying command handler (callback function)
//
static int
espshell_command(char *p, argcargv_t *aa) {
  
  int bad = CMD_FAILED;
  bool fg = true;

  MUST_NOT_HAPPEN(((aa != NULL) && (p != NULL)) || ((aa == NULL) && (p == NULL)));
  
  // got ascii string to process?
  // if we got only /aa/ but /p/ is NULL then we execute /aa/ and don't update history:
  // this behaviour is needed for "exec ALIAS_NAME"
  //
  // if we got only /p/ and /aa/ is NULL then /aa/ is created from /p/:
  if (p) {
    //make a history entry, if history is enabled (default)
    if (History) {
      userinput_strip(p);
      if (*p)
        history_add_entry(p);
    }

    // tokenize user input
    if ((aa = userinput_tokenize(p)) == NULL) {
      q_free(p);
      return -1;
    }
  }

  // Find a handler if not found yet
  if (!aa->gpp) 
    if ((bad = userinput_find_handler(aa)) != 0)
      goto unref_and_exit; // command not found?
  
  // Foreground or background?
  // /fg/ is /false/ if we have "&" keyword at the end of a command
  // This code is executed once per each AA. Subsequent executions of the same AA (via alias exec)
  // will skip this
  if (aa->argv[aa->argc - 1][0] == '&') {
    fg = false;
#if WITH_ALIAS      
    // An "&" symbol within alias editing mode should not be stripped or be translated for background exec
    // TODO: stop the event system when user is in alias editing mode or it will cause assortment of hard-to-find bugs
    if (keywords == keywords_alias)
      fg = true;
    else 
#endif      
    {
      // Strip last "&" argument and store it in the AA flag.
      // Accept priority value if extended &-syntax was used: ("&10" - set priority to 10)
      //
      if (aa->argv[aa->argc - 1][1]) {
        aa->has_prio = 1;
        aa->prio = q_atoi(&(aa->argv[aa->argc - 1][1]), TASK_MAX_PRIO + 1);
        if (aa->prio >= TASK_MAX_PRIO + 1) {
          q_print("% Unrecognized priority value, priority will be inherited\r\n");
          aa->has_prio = 0;
        }
      }
      aa->has_amp = 1;
      aa->argc--;
    }
  } // if "&"


  if (aa->has_amp)
    fg = false;

  if (fg) {
    AA = aa; // Temporary store pointer to the current aa: it is used exclusively when in alias editing mode
             // NOTE: don't use this pointer for anything except alias editing, it is volatile!

    // call command handler directly
    bad = aa->gpp(aa->argc, aa->argv);
  } else {
    AA = NULL;
    // create a task and call the handler from that task context
    bad = exec_in_background(aa);
  }

unref_and_exit:
  // Decrease aa's reference counter, display error code if any and exit
  // Normally, refcounter is 1, so aa is removed as part of unref(). However, aliases have their refcounter > 1 so
  // aa's of the alias are kept intact
  //
    if (bad != 0)
      espshell_display_error(bad,aa->argc,aa->argv);

  // decrease reference counter
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
  return (*Input == '\0');
}


// This function may be called multiple times despite its name:
// Call functions which are intended to be called once, things like convars & memory allocations.
//
static bool call_once = false;

static  void espshell_initonce() {

  if (!call_once) {
    call_once = true;

    // These asserts are mostly for var.h module since it *assumes* that sizeof(int) is 4 and so on
    // (as it should be on a 32bit cpu)
    // There is a code which assumes sizeof(unsigned int) >= sizeof(void *) here and there (mostly in task.h)
    // We do these asserts in the beginning so we can safely convert between 32-bit types and make safe our, 
    // generally speaking unsafe code
    //
    // Shell often converts between pointers and integer types
    
    static_assert(sizeof(unsigned long long) == 8,"Unexpected basic type size");
    static_assert(sizeof(int) == 4,"Unexpected basic type size");
    static_assert(sizeof(float) == 4,"Unexpected basic type size");
    static_assert(sizeof(void *) == 4,"Unexpected basic type size");
    static_assert(sizeof(short) == 2,"Unexpected basic type size");
    static_assert(sizeof(char) == 1,"Unexpected basic type size");
    static_assert(sizeof(task_t) == sizeof(uint32_t),"Unexpected basic type size");

    // Add internal shell variables
    convar_add(ls_show_dir_size);  // enable/disable dir size counting for "ls" command
    convar_add(pcnt_unit);         // PCNT unit which is used by "count" command
    convar_add(bypass_qm);         // enable/disable "?" as a context help hotkey
    convar_add(tbl_min_len);       // buffers whose length is > printhex_tbl (def: 16) are printed as fancy tables
    convar_add(ledc_res);          // Override PWM duty cycle resolution bitwidth: Duty range is from 0 to (2**ledc_res-1)
    convar_add(pwm_ch_inc);        // 1 or 2: hop over odd or even channel numbers.
#if WITH_ESPCAM
    convar_add(cam_ledc_chan);  // Avoiding interference: LEDC channels used by ESPCAM for generating XCLK
    convar_add(cam_ledc_timer);    // Avoiding interference: ESP32 TIMER used by ESPCAM
#endif
    // init subsystems
    seq_init();
  }
}



// ESPShell main task. Reads and processes user input by calling espshell_command(), the command processor
// Only one shell task can be started at the time!
// This task is visible in "show tasks" list as "ESPShell".
//
static void espshell_task(const void *arg) {

  // arg is not NULL - first time call: start the task and return immediately
  if (arg) {
    MUST_NOT_HAPPEN (shell_task != NULL);

    // on multicore processors use another core: if Arduino uses Core1 then
    // espshell will be on core 0 and vice versa. 
    // Code below assumes that there either 1 or 2 cores available
    shell_core = xPortGetCoreID();
    if (portNUM_PROCESSORS > 1)
      shell_core = shell_core ? 0 : 1;
    
    if ((shell_task = task_new(espshell_task, NULL, "ESPShell")) == NULL)
      q_print("% ESPShell failed to start its task\r\n");
  } else {

    // wait until user code calls Serial.begin()
    while (!console_isup())
      q_delay(CONSOLE_UP_POLL_DELAY);

    // Now we can be sure that "loopTaskHandle" (see Arduino Core) is not NULL
    // Add loop() task to our list of tasks. This code could be called more than once but
    // it is ok to taskid_remember() the same number - it gets overwritten
    taskid_remember(loopTaskHandle);

    HELP(q_print(WelcomeBanner));

    // The main REPL : read & execute commands until "exit ex" is entered
    // TODO: if readline() return NULL or an empty string it may be caused
    //       by UART/USB link going down. In such case we are risking to be in a spinning loop
    //       blocking other tasks.
    while (!Exit) {
      espshell_command(readline(prompt), NULL);
      q_yield(); 
    }
    HELP(q_print(Bye));

    // Make espshell restart possible
    Exit = false;
    shell_task = NULL;
    task_finished();
  }
}

// Check if ESPShell's task is already started
//
static INLINE bool espshell_started() {
  return shell_task != NULL;
}


// Start ESPShell
// Normally (AUTOSTART==1) starts automatically. With autostart disabled EPShell can
// be started by calling espshell_start().
//
// This function can be used to restart the shell which was terminated by command "exit ex"
//
void STARTUP_HOOK espshell_start() {

  espshell_initonce();
  if (espshell_started())
    HELP(q_print("% ESPShell is started already, exiting\r\n"));
  else
    espshell_task((const void *)1);
}
