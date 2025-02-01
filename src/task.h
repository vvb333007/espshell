/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


// -- Tasks --
// Everything about tasks: main shell task, async tasks (background commands), command handlers & utility functions
//
#if COMPILING_ESPSHELL

#define CONSOLE_UP_POLL_DELAY 1000   // 1000ms. 

EXTERN TaskHandle_t loopTaskHandle;  // task handle of a task which calls Arduino's loop(). Defined somewhere in the ESP32 Arduino Core
static TaskHandle_t shell_task = 0;  // Main espshell task ID
static int          shell_core = 0;  // CPU core number ESPShell is running on. For single core systems it is always 0

// Signals for use in task_signal()
#define SIGNAL_TERM 0  // Request to terminate. (Must be zero, DO NOT CHANGE its default value)
#define SIGNAL_GPIO 1  // "Pin interrupt" signal
#define SIGNAL_KILL 2  // Force task deletion
#define SIGNAL_HUP  3  // "Reinitialize/Re-read configuration"

// current task id
#define taskid_self() xTaskGetCurrentTaskHandle()

// Task signalling wrappers. qlib is used in a few different projects so I try to keep it easily convertible to other API
// (POSIX for example). 

// Send a signal (uin32_t arbitrary value) to the task.If task was blocking on task_signal_wait() the task will unblock
// and receive signal value
#define task_signal(_Handle, _Signal) xTaskNotify((TaskHandle_t)(_Handle), _Signal, eSetValueWithOverwrite)

// Same as above but ISR-safe
#define task_signal_from_isr(_Handle, _Signal) \
  do { \
    BaseType_t ignored; \
    xTaskNotifyFromISR((TaskHandle_t)(_Handle), _Signal, eSetValueWithOverwrite, &ignored); \
  } while (0)

// Yeld to another task. We don't use portyield or taskyield here as they can't switch to lower priority task.
// Our task_yield is implemented via delay(1) which forces task switch
#define task_yield() q_delay(1)



// Block until any signal is received but not longer than /timeout/ milliseconds. Value of 0 means "no timeout".
// When current task receives a signal it unblocks, stores signal value in /*sig/ (if /sig/ is not NULL) and returns
// 
// Returns /true/ if signal was received
// Returns /false/ if timeout has fired before any signal was received
// Writes /*sig/ with signal received if /sig/ is a valid pointer
//
static bool task_wait_for_signal(uint32_t *sig, uint32_t timeout_ms) {

  uint32_t sig0;
  timeout_ms = timeout_ms ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY; 
 
  if (NULL == sig)
    sig = &sig0;

  // block until any notification or timeout
  return xTaskNotifyWait(0, 0xffffffff, sig, timeout_ms ) == pdPASS;
}


// Can we perform commands on this taskid?
//
static bool taskid_good(unsigned int taskid) {

  // Suspicously low address. Probably misstyped.
  // TODO: This value is approximate. Need to go deeper on memory mapping in different ESP32 models
  if (taskid <= 0x30000000) {
    HELP(q_print("% Task id is a hex number, something like \"3ffb0030\" or \"0x40005566\"\r\n"));
    return false;
  }
  
  // Ignore attempts to manipulate the main espshell task
  if (shell_task == (TaskHandle_t )taskid) {
    HELP(q_printf("%% Task <i>0x%x</> is the main ESPShell task\r\n%% To exit ESPShell use command \"exit ex\" instead\r\n",taskid));
    return false;
  }

  return true;
}

// check if *this* task (a caller) is executed as separate (background) task
// or it is executed in context of ESPShell
//
static INLINE bool is_foreground_task() {
  return shell_task == taskid_self();
}

#define is_background_task() (!is_foreground_task())


// check if ESPShell's task is already created
static INLINE bool espshell_started() {
  return shell_task != NULL;
}

// Helper task, which runs (cmd_...) handlers in a background.
//
// Normally, when user inputs "pin" command, the corresponding handler (i.e. cmd_pin()) is called directly by espshell_command() parser
// However, for commands which ends with "&" (e.g. "pin 8 up high &", "count 4 &") cmd_async() handler is called. This handler starts a task 
// (espshell_async_task()) which executes "real" command handler stored in aa->gpp.
//
// The "&" syntax is inspired by Linux bash's "&" keyword
//
static void espshell_async_task(void *arg) {

  argcargv_t *aa = (argcargv_t *)arg;
  int ret = -1;

  // aa->gpp points to actual command handler (e.g. cmd_pin for commands "pin" and "pin&"); aa->gpp is set up
  // by command processor (espshell_command()) according to first keyword (argv[0])
  if (aa && aa->gpp) {
    ret = (*(aa->gpp))(aa->argc, aa->argv);
    q_printf("\r\n%% Background command \"%s\" has %s", aa->argv[0], ret == 0 ? "finished its job" : "<e>failed</>");
    // do the same job espshell_command() does: interpret error codes returned by the handler. Keep in sync with espshell_command() code
    if (ret < 0)
      q_print("\r\n% <e>Wrong number of arguments</>");
    else if (ret > 0)
      q_printf("\r\n%% <e>Invalid %u%s argument \"%s\"</>", NEE(ret), ret < aa->argc ? aa->argv[ret] : "FIXME:");
  }
  // its ok to unref null pointer
  userinput_unref(aa);

  // Redraw ESPShell command line prompt (yes we need it, because ESPShell has already printed its prompt out once) 
  userinput_redraw();

  vTaskDelete(NULL);
}

// "KEYWORD& ARG1 ARG2 .. ARGn"
// Executes commands in a background (commands which names end with "&").
// This one used by espshell_command() processor to execute &-commands 
//
static int exec_in_background(argcargv_t *aa_current) {

  TaskHandle_t ignored;

  MUST_NOT_HAPPEN(aa_current == NULL);

  //increase refcount on argcargv (tokenized user input) because it will be used by async task and
  // we dont want this memory to be freed immediately after this command finishes
  userinput_ref(aa_current);

  // Start async task. Pin to the same core where espshell is executed
  if (pdPASS != xTaskCreatePinnedToCore((TaskFunction_t)espshell_async_task, "Async", STACKSIZE, aa_current, tskIDLE_PRIORITY, &ignored, shell_core)) {
    q_print("% <e>Can not start a new task. Resources low? Adjust STACKSIZE macro</>\r\n");
    userinput_unref(aa_current);
  } else
    //Hint user on how to stop bg command
    q_printf("%% Background task started\r\n%% Copy/paste \"kill 0x%x\" command to stop execution\r\n", (unsigned int)ignored);

  return 0;
}


// shell task. reads and processes user input.
//
// only one shell task can be started!
// this task started from espshell_start()
//
static void espshell_task(const void *arg) {

  // arg is not NULL - first time call: start the task and return immediately
  if (arg) {
    MUST_NOT_HAPPEN (shell_task != NULL);

    // on multicore processors use another core: if Arduino uses Core1 then
    // espshell will be on core 0 and vice versa. 
    shell_core = xPortGetCoreID();
    if (portNUM_PROCESSORS > 1)
      shell_core = shell_core ? 0 : 1;
    if (pdPASS != xTaskCreatePinnedToCore((TaskFunction_t)espshell_task, NULL, STACKSIZE, NULL, tskIDLE_PRIORITY, &shell_task, shell_core))
      q_print("% ESPShell failed to start its task\r\n");
  } else {

    // wait until user code calls Serial.begin()
    while (!console_isup())
      q_delay(CONSOLE_UP_POLL_DELAY);

    HELP(q_print(WelcomeBanner));

    // read & execute commands until "exit ex" is entered
    while (!Exit) {
      espshell_command(readline(prompt));
      task_yield();
    }
    HELP(q_print(Bye));

    // Make espshell restart possible
    Exit = false;
    vTaskDelete((shell_task = NULL));
  }
}


//"suspend"
// suspends main Arduino task (i.e loop())
static int cmd_suspend(int argc, char **argv) {

  unsigned int taskid;
  TaskHandle_t sus = loopTaskHandle;
  if (argc > 1) {
    taskid = hex2uint32(argv[1]);
    if (taskid_good(taskid)) 
      sus = (TaskHandle_t )taskid; 
    else 
      return 1;
  }
  vTaskSuspend(sus);
  return 0;
}

//"resume"
// Resume previously suspended task
//
static int cmd_resume(int argc, char **argv) {
  unsigned int taskid;
  TaskHandle_t sus = loopTaskHandle;
  if (argc > 1) {
    taskid = hex2uint32(argv[1]);
    if (taskid_good(taskid)) sus = (TaskHandle_t )taskid; else return 1;
  }
  vTaskResume(sus);
  return 0;
}

//"kill [-term|-kill|-9|-15] TASK_ID"
// 1. Stop a background command
// 2. Terminate arbitrary FreeRTOS task
//
static int cmd_kill(int argc, char **argv) {

  unsigned int sig = SIGNAL_TERM, i = 1, taskid;
  if (argc < 2)
    return -1;

  if (argv[i][0] == '-') { // an option, task id follows
    q_tolower(argv[i],0);
    if (!q_strcmp(argv[i],"-term") || !q_strcmp(argv[i],"-15")) sig = SIGNAL_TERM; else
    if (!q_strcmp(argv[i],"-hup") ||  !q_strcmp(argv[i],"-1")) sig = SIGNAL_HUP; else
    if (!q_strcmp(argv[i],"-kill") || !q_strcmp(argv[i],"-9"))  sig = SIGNAL_KILL; else return 1;
    i++;
  }

  if (i >= argc)
    return -1;

  if (taskid_good((taskid = hex2uint32(argv[i])))) {
    // SIGNAL_KILL is never sent to a task. Instead, task is deleted.
    if (sig == SIGNAL_KILL) {
      vTaskSuspend((TaskHandle_t)taskid);
      task_yield();
      vTaskDelete((TaskHandle_t)taskid);
      HELP(q_printf("%% Killed: \"0x%x\". Resources are not freed!\r\n", taskid));
    } else
      // -term, -hup and other signals are sent directly to the task
      task_signal(taskid, sig);
  } else
    return i;
  return 0;
}

#endif // COMPILING_ESPSHELL

