/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Burdzhanadze <vvb333007@gmail.com>, 
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

// check if *this* task (a caller) is executed as separate (background) task
// or it is executed in context of ESPShell
//
static INLINE int is_foreground_task() {
  return shell_task == xTaskGetCurrentTaskHandle();
}

#define current_task_id() (unsigned int)(xTaskGetCurrentTaskHandle())

// check if ESPShell's task is already created
static INLINE bool espshell_started() {
  return shell_task != NULL;
}

// Helper task, which runs (cmd_...) handlers in a background.
//
// Normally, when user inputs "pin" command, the corresponding handler (i.e. cmd_pin()) is called directly by espshell_command() parser
// However, for commands which name ends with "&" (e.g. "pin&", "count&") cmd_sync() handler is called. This handler starts a task 
// (espshell_async_task()) which executes "real" command handler.
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
    q_printf("%% Background task %p (\"%s\") %s\r\n", xTaskGetCurrentTaskHandle(), aa->argv[0], ret == 0 ? "is finished" : "<e>has failed</>");
    // do the same job espshell_command() does: interpret error codes returned by the handler. Keep in sync with espshell_command() code
    if (ret < 0)
      q_print("% <e>Wrong number of arguments</>\r\n");
    else if (ret > 0)
      q_printf("%% <e>Invalid %u%s argument \"%s\"</>\r\n", ret, number_english_ending(ret), ret < aa->argc ? aa->argv[ret] : "FIXME:");
  }
  // its ok to unref null pointer
  userinput_unref(aa);
  vTaskDelete(NULL);
}

// "KEYWORD& ARG1 ARG2 .. ARGn"
// Executes commands in a background (commands which names end with "&").
// This one used by espshell_command() processor to execute &-commands 
//
static int exec_in_background(argcargv_t *aa_current) {

  TaskHandle_t ignored;

  if (aa_current == NULL)  //must not happen
    abort();

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
    if (shell_task != NULL)
      abort();  // must not happen

    // on multicore processors use another core: if Arduino uses Core1 then
    // espshell will be on core 0.
    shell_core = xPortGetCoreID();
    if (portNUM_PROCESSORS > 1)
      shell_core = shell_core ? 0 : 1;
    if (pdPASS != xTaskCreatePinnedToCore((TaskFunction_t)espshell_task, NULL, STACKSIZE, NULL, tskIDLE_PRIORITY, &shell_task, shell_core))
      q_print("% ESPShell failed to start its task\r\n");
  } else {

    // wait until user code calls Serial.begin()
    while (!console_isup())
      delay(CONSOLE_UP_POLL_DELAY);

    HELP(q_printf(WelcomeBanner));

    // read & execute commands until "exit ex" is entered
    while (!Exit) {
      espshell_command(readline(prompt));
      delay(1);
    }
    HELP(q_print(Bye));

    // Make espshell restart possible
    Exit = false;
    vTaskDelete((shell_task = NULL));
  }
}


//"suspend"
// suspends the Arduino loop() task
static int cmd_suspend(int argc, char **argv) {
  vTaskSuspend(loopTaskHandle);
  return 0;
}

//"resume"
// Resume previously suspended loop() task
static int cmd_resume(int argc, char **argv) {
  vTaskResume(loopTaskHandle);
  return 0;
}

//"kill TASK_ID"
// kill a espshell task.
// TODO: make async tasks have their TASK_ID mapped to small numbers starting from 1.
static int cmd_kill(int argc, char **argv) {

  if (argc < 2)
    return -1;

  unsigned int taskid = hex2uint32(argv[1]);
  if (taskid < 0x3ff00000) {
    HELP(q_print("% Task id is a hex number, something like \"3ffb0030\" or \"0x40005566\"\r\n"));
    return 1;
  }

  TaskHandle_t handle = (TaskHandle_t)taskid;
  // Sorry but no
  if (shell_task == handle) {
    q_print(Failed);
    return 0;
  }

  // Try to finish the task via notification. This allows commands
  // to print their intermediate results: one of examples is "count&" command:
  // being killed it will print out packets count arrived so far
  //
  xTaskNotify(handle, 0, eNoAction);

  //undocumented
  if (argc > 2) {
    if (!q_strcmp(argv[2], "terminate")) {
      vTaskDelete(handle);
      HELP(q_printf("%% Terminated: \"%p\"\r\n", handle));
    }
  }
  return 0;
}
#endif
