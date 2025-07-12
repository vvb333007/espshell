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

// -- Tasks --
// Everything about tasks: main shell task, async tasks (background commands), command handlers & utility functions
//
#if COMPILING_ESPSHELL

#if WITH_WRAP
//
// Intercept calls to FreeRTOS to get and maintain a list of started tasks. 
// The list is available as a convar "Tasks" and can be accessed via "var Tasks"
//
// It is a link-time interception and it is done by linker: calls to real functions gets replaced with calls to __wrap_ equivalents;
// Original function is still available through __real_vTaskDelete (as an example)
//
// In order to work, LD_FLAGS must be set to -Wl,--wrap=vTaskDelete -Wl,--wrap=xTaskCreatePinnedToCore -Wl,--wrap=xTaskCreateStaticPinnedToCore
// Unfortunately, as of Arduino IDE v2.3.4 there is no way to pass these linker flags: instead, one had to modify "ld_flags" file in the shipped
// (precompiled) ESP-IDF
//
#warning "Don't forget to add -Wl,--wrap=vTaskDelete -Wl,--wrap=xTaskCreatePinnedToCore -Wl,--wrap=xTaskCreateStaticPinnedToCore to your ld_flags file!!!"
static TaskHandle_t Tasks[20] = { 0 };

// Declare task creation / deletion functions as **possibly** unresolved external.
// Declare wrappers & "real" functions.
// Linker will replace all calls to vTaskDelete with __wrap_vTaskDelete(), which does statistics and pass control to the "real" vTaskDelete()
//
extern void vTaskDelete(TaskHandle_t h);
extern BaseType_t xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,const char * const pcName,const configSTACK_DEPTH_TYPE usStackDepth,void * const pvParameters,UBaseType_t uxPriority,TaskHandle_t * const pvCreatedTask,const BaseType_t xCoreID );
extern TaskHandle_t xTaskCreateStaticPinnedToCore( TaskFunction_t pxTaskCode,const char * const pcName,const uint32_t ulStackDepth,void * const pvParameters,UBaseType_t uxPriority,StackType_t * const pxStackBuffer,StaticTask_t * const pxTaskBuffer,const BaseType_t xCoreID );
extern void __real_vTaskDelete(TaskHandle_t h);
extern BaseType_t __real_xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,const char * const pcName,const configSTACK_DEPTH_TYPE usStackDepth,void * const pvParameters,UBaseType_t uxPriority,TaskHandle_t * const pvCreatedTask,const BaseType_t xCoreID );
extern TaskHandle_t __real_xTaskCreateStaticPinnedToCore( TaskFunction_t pxTaskCode,const char * const pcName,const uint32_t ulStackDepth,void * const pvParameters,UBaseType_t uxPriority,StackType_t * const pxStackBuffer,StaticTask_t * const pxTaskBuffer,const BaseType_t xCoreID );

// Remember unique task ID
static int taskid_store(TaskHandle_t h) {
  for (int i = 0; i < sizeof(Tasks)/sizeof(Tasks[0]); i++)
    if (Tasks[i] == h || !Tasks[i]) {
      Tasks[i] = h;
      return i;
    }
  return -1;
}

// Forget task ID
static void taskid_forget(TaskHandle_t h) {
  for (int i = 0; i < sizeof(Tasks)/sizeof(Tasks[0]); i++)
    if (Tasks[i] == h) {
      Tasks[i] = 0;
      break;
    }
}


// Sumple proxy wrappers to maintain currently running tasks list
// The list is accessible via "var Tasks"
//
void __wrap_vTaskDelete(TaskHandle_t h) {
  taskid_forget(h);
  __real_vTaskDelete(h);
}

BaseType_t __wrap_xTaskCreatePinnedToCore( TaskFunction_t pxTaskCode,const char * const pcName,const configSTACK_DEPTH_TYPE usStackDepth,void * const pvParameters,UBaseType_t uxPriority,TaskHandle_t * const pvCreatedTask,const BaseType_t xCoreID ) {

  TaskHandle_t tmp, *h;
  BaseType_t ret;

  if ((h = pvCreatedTask) == NULL)
    h = &tmp;

  ret = __real_xTaskCreatePinnedToCore(pxTaskCode,pcName,usStackDepth,pvParameters,uxPriority,h,xCoreID );
  taskid_store(*h);
  return ret;
}

TaskHandle_t __wrap_xTaskCreateStaticPinnedToCore( TaskFunction_t pxTaskCode,const char * const pcName,const uint32_t ulStackDepth,void * const pvParameters,UBaseType_t uxPriority,StackType_t * const pxStackBuffer,StaticTask_t * const pxTaskBuffer,const BaseType_t xCoreID ) {
  TaskHandle_t ret = __real_xTaskCreateStaticPinnedToCore(pxTaskCode,pcName,ulStackDepth,pvParameters,uxPriority,pxStackBuffer,pxTaskBuffer,xCoreID );
  if (ret)
    taskid_store(ret);
  return ret;
}
#endif //WITH_WRAP

#define CONSOLE_UP_POLL_DELAY 1000   // 1000ms. How often to check if Serial is up

extern TaskHandle_t loopTaskHandle;  // task handle of a task which calls Arduino's loop(). Defined somewhere in the ESP32 Arduino Core
static TaskHandle_t shell_task = 0;  // Main espshell task ID
static int          shell_core = 0;  // CPU core number ESPShell is running on. For single core systems it is always 0

// Signals for use in task_signal() and task_wait_for_signal() functions
//
#define SIGNAL_TERM 0  // Request to terminate. (Must be zero, DO NOT CHANGE its default value)
#define SIGNAL_GPIO 1  // "Pin interrupt" signal. Generated by GPIO ISR.
#define SIGNAL_KILL 2  // Force task deletion. This value can be sent but can't be received
#define SIGNAL_HUP  3  // "Reinitialize/Re-read configuration" (Unused, for future extensions)

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

// Yeld to another task
// TODO: test the shell with taskYIELD() as better alternative to q_delay(1)
#define task_yield() q_delay(1)


// Block until any signal is received but not longer than /timeout/ milliseconds. Value of 0xffffffff (DELAY_INFINITE) means "infinite timeout":
// function will block until it receives ANY signal; the signal value is returned in /*sig/, pointer can be NULL;
// 
// Returns /true/ if signal was received
// Returns /false/ if timeout has fired before any signal was received
//
static bool task_wait_for_signal(uint32_t *sig, uint32_t timeout_ms) {

  uint32_t sig0;
//  timeout_ms = timeout_ms ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY; 
  timeout_ms = (timeout_ms == DELAY_INFINITE) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms); 
 
  if (NULL == sig)
    sig = &sig0;

  // block until any notification or timeout
  return xTaskNotifyWait(0, 0xffffffff, sig, timeout_ms ) == pdPASS;
}


// Can we perform commands on this taskid?
// The task must be not the espshell's main task AND taskid must be in a valid address range
//
static bool taskid_good(unsigned int taskid) {

  // Use range of "1" for now. TODO: change it to the sizeof(TaskHandle)
  if (!is_valid_address((void *)taskid,1)) {
    HELP(q_print("% Task ID is a <i>hex number</>, something like \"3ffb0030\" or \"0x40005566\"\r\n"));
    return false;
  }
  
  // Ignore attempts to manipulate the main espshell task
  if (shell_task == (TaskHandle_t )taskid) {
    HELP(q_printf("%% Task <i>0x%x</> is the main espshell task, access denied :)\r\n",taskid));
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

// Moved to a function, because it is called both from asyn shell command and command processor
//
static void espshell_display_error(int ret, int argc, char **argv) {
    
  MUST_NOT_HAPPEN(argc < 1);
  MUST_NOT_HAPPEN(ret >= argc);

  if (ret > 0)
    q_printf("%% <e>Invalid %u%s argument (\"%s\")</>\r\n", NEE(ret), ret < argc ? argv[ret] : "Empty");
  else if (ret < 0) 
    if (ret == CMD_MISSING_ARG)
      q_printf("%% <e>Wrong number of arguments (%d). Help page: \"? %s\" </>\r\n",argc - 1, argv[0]);
    // Keep silent on other error codes which are <0 :
    // CMD_FAILED return code assumes that handler did display error message before returning CMD_FAILED
}  


// Helper task, which runs (cmd_...) handlers in a background.
//
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
    // TODO: this is a workaround: espshell_command() strips last "&" so here we restore it. it is solely for "exec" command
    // TODO: to be removed soon
    aa->argc = aa->argc0; 

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

  vTaskDelete(NULL);
}

// Executes commands in a background (commands which names end with &).
// This one used by espshell_command() processor to execute &-commands 
//
static int exec_in_background(argcargv_t *aa_current) {

  TaskHandle_t ignored;

  MUST_NOT_HAPPEN(aa_current == NULL);

  //increase refcount on argcargv because it will be used by async task and
  // we want this memory remain allocated after this command
  userinput_ref(aa_current);

  // Start async task. Pin to the same core where espshell is executed
  if (pdPASS != xTaskCreatePinnedToCore((TaskFunction_t)espshell_async_task, "Async", STACKSIZE, aa_current, tskIDLE_PRIORITY, &ignored, shell_core)) {
    q_print("% <e>Can not start a new task. Resources low? Adjust STACKSIZE macro in \"espshell.h\"</>\r\n");
    userinput_unref(aa_current);
  } else
    //Hint user on how to stop bg command
    q_printf("%% Background task started\r\n%% Copy/paste \"kill 0x%x\" to abort\r\n", (unsigned int)ignored);

  return 0;
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
    return CMD_MISSING_ARG;

  if (argv[i][0] == '-') { // an option, task id follows
    q_tolower(argv[i]);
    if (!q_strcmp(argv[i],"-term") || !q_strcmp(argv[i],"-15")) sig = SIGNAL_TERM; else
    if (!q_strcmp(argv[i],"-hup") ||  !q_strcmp(argv[i],"-1")) sig = SIGNAL_HUP; else
    if (!q_strcmp(argv[i],"-kill") || !q_strcmp(argv[i],"-9"))  sig = SIGNAL_KILL; else return 1;
    i++;
  }

  if (i >= argc)
    return CMD_MISSING_ARG;

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

