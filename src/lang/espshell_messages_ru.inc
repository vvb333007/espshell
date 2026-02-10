static const char *Failed =
    "% <e>Ошибка</>\r\n";

static const char *i2cIsDown =
    "%% I2C%u не инициализирован. Используйте команду \"up RX TX FREQ\" для инициализации.\r\n";

static const char *uartIsDown =
    "%% UART%u не инициализирован. Используйте команду \"up RX TX SPEED\" для инициализации.\r\n";

#if WITH_HELP
static const char *WelcomeBanner =
    "\033[H\033[2J%\r\n"
    "% ESPShell " ESPSHELL_VERSION "\r\n"
    "% Введите '?' и нажмите <Enter> для справки\r\n"
    "% Нажмите <Ctrl+L>, чтобы очистить экран, включить цвета и показать подсказку дня\r\n";

static const char *Bye =
    "% До встречи!\r\n";

static const char *MultipleEntries =
    "% Обнаружено несколько путей.\r\n"
    "% Если это не то, что вы хотели, попробуйте заключить пути с пробелами в кавычки.\r\n";

static const char *VarOops =
    "<e>% Упс :-(</>\r\n"
    "% Нет зарегистрированных переменных.\r\n"
    "% Попробуйте следующее:\r\n"
    "%  <i>1. Добавьте include \"espshell.h\" в ваш скетч</>\r\n"
    "%  <i>2. Используйте макрос \"convar_add()\" для регистрации переменных</>\r\n"
    "%\r\n"
    "% После регистрации переменными можно управлять командой \"var\"\r\n"
    "% прямо во время работы скетча.\r\n";
#endif


static const char *Error_Arg =
    "%% <e>Недопустимый аргумент (%u%s, \"%s\")</>\r\n";

static const char *Error_Missing_Arg =
    "%% <e>Неверное количество аргументов. Помощь по команде: \"? %s\"</>\r\n";

static const char *Error_Command_Not_Found =
    "%% <e>\"%s\": команда не найдена</>\r\n"
    "%% Введите \"?\", чтобы посмотреть список доступных команд\r\n";

static const char *Command_Finished =
    "\r\n% Завершено: \"<i>";

static const char *Task_Started =
    "%% Фоновая задача запущена (CPU#%u)\r\n"
    "%% Команда \"<i>kill %p</>\" прервет выполнение задачи\r\n";
