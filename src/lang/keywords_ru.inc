#pragma once

// Russian UTF-8 localization
//
KEYWORDS_DECL(uart) {
  KEYWORDS_BEGIN 

{ "up", cmd_uart_up, 3,
  HELPK("% \"<b>up</> <i>RX TX SPEED</> <o>[BITS] [no|even|odd] [1|1.5|2]</>\"\r\n" 
        "%\r\n"
        "% Инициализировать интерфейс UART на пинах RX/TX с заданной скоростью SPEED\r\n"
        "% <i>RX</>    - Пин, используемый как RX\r\n"
        "% <i>TX</>    - Пин, используемый как TX\r\n"
        "% <i>SPEED</> - 9600, 115200 или любая другая стандартная скорость\r\n"
        "% Три необязательных параметра:\r\n"
        "%  <o>BITS</>      - Количество бит данных: 5, 6, 7 или 8 (по умолчанию 8)\r\n"
        "%  Parity    - \"<o>no</>\", \"<o>even</>\" или \"<o>odd</>\"\r\n"
        "%  Stop bits - 1, 2 или 1.5 стоп-бита\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>up 18 19 115200</> - Настроить UART на пинах RX=18, TX=19, скорость 115200\r\n"
        "%   <i>up 18 19 115200 <i>8 even 1.5</> - 8 бит данных, 1.5 стоп-бита, чётная чётность" ),
  HELPK("Инициализация UART (пины/скорость)") },

    // Variants of "up" with 4,5 and 6 arguments: same handler
    { "up", cmd_uart_up, 4, HIDDEN_KEYWORD },
    { "up", cmd_uart_up, 5, HIDDEN_KEYWORD },
    { "up", cmd_uart_up, 6, HIDDEN_KEYWORD },

    // TODO: this is a workaround to catch "up", not "uptime". Should generate "missing argument error"
    { "up", cmd_uart_up, 0, HIDDEN_KEYWORD },

{ "baud", cmd_uart_baud, 1,
  HELPK("% \"<b>baud</> <i>SPEED</>\"\r\n"
        "%\r\n"
        "% Установить скорость UART (UART должен быть инициализирован)\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>baud 115200</> - Установить скорость UART в 115200"),
  HELPK("Установить скорость UART") },

  // Command with no arguments:
{ "down", cmd_uart_down, NO_ARGS,
  HELPK("% \"<b>down</>\"\r\n"
        "%\r\n"
        "% Отключить интерфейс и освободить пины"),
  HELPK("Отключить UART") },

{ "show", cmd_show_uart, NO_ARGS,
  HELPK("% \"<b>show</>\"\r\n"
        "%\r\n"
        "% Показать параметры UART"),
  HELPK("Показать настройки") },

{ "read", cmd_uart_read, NO_ARGS,
  HELPK("% \"<b>read</>\"\r\n"
        "%\r\n"
        "% Прочитать доступные байты из интерфейса UART X"),
  HELPK("Чтение данных из UART") },

{ "tap", cmd_uart_tap, NO_ARGS,
  HELPK("% \"<b>tap</>\r\n"
        "%\r\n"
        "% Соединить ввод/вывод UART напрямую с shell\r\n"
        "% Ввод пользователя будет передаваться в UART X;\r\n"
        "% Все данные, отправляемые UART X, будут передаваться пользователю"),
  HELPK("Общение с подключённым устройством") },

  // Command that accepts any number of arguments, including zero.
  // If you have several entries with the same command name (i.e. a group), which only differ in their
  // "required arguments number", then the MANY_ARGS entry must be the last entry in the group:
  // Example:
  // {"write", ... , 1,          <--- called on "write ARG"
  // {"write", ... , 3,          <--- called on "write ARG ARG ARG"
  // {"write", ... , MANY_ARGS,  <--- last entry in the "write" group, called on all other "write" variants
  //
  // Doing it like this:
  //
  // {"write", ... , 1,          <--- called on "write ARG"
  // {"write", ... , MANY_ARGS,
  // {"write", ... , 3,          <--- called on "write ARG ARG ARG"
  //
  // will block 3-arg variant from executing. 
  //
    
  //
{ "write", cmd_uart_write, MANY_ARGS,
  HELPK("% \"<b>write</> <i>TEXT</>\"\r\n"
        "%\r\n"
        "% Отправить ASCII/HEX строку(и) в интерфейс UART\r\n"
        "% <b>TEXT</> может содержать пробелы и escape-последовательности: \\n, \\r, \\\\, \\t и \r\n"
        "% шестнадцатеричные байты вида \\AB (A и B — шестнадцатеричные цифры)\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>write ATI\\n\\rMixed\\20Text and \\20\\21\\ff</>"),
  HELPK("Отправить данные через UART") },

  // KEYWORDS_END contains common entries (like "exit") and a NULL entry at the very end
  // If this part is omitted  then NULL entry must be declared manually (see "alias" directory
  // at the beghinning of this file)
  KEYWORDS_END
};

// Final step: register our command tree.
// Skipping of this step does not affect espshell operation (affects colors in command "?" display only)
KEYWORDS_REG(uart);

//I2C subderictory keywords
//
KEYWORDS_DECL(iic) {

  KEYWORDS_BEGIN

{ "up", cmd_i2c_up, 3,
  HELPK("% \"<b>up</> <i>SDA SCL CLOCK</>\"\r\n"
        "%\r\n"
        "% Инициализировать интерфейс I2C X, используя пины SDA/SCL, с тактовой частотой CLOCK\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>up 21 22 100000</> - включить I2C на пинах SDA=21, SCL=22, частота 100 кГц"),
  HELPK("Инициализация интерфейса (пины и скорость)") },

// TODO: workaround against "uptime"
{ "up", cmd_i2c_up, 0, HIDDEN_KEYWORD },

{ "clock", cmd_i2c_clock, 1,
  HELPK("% \"<b>clock</> <i>SPEED</>\"\r\n"
        "%\r\n"
        "% Установить тактовую частоту мастера I2C (I2C должен быть инициализирован)\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>clock 100000</> - установить частоту I2C в 100 кГц"),
  HELPK("Установить частоту") },

{ "scan", cmd_i2c_scan, NO_ARGS,
  HELPK("% \"<b>scan</>\"\r\n"
        "%\r\n"
        "% Просканировать шину I2C на наличие устройств (I2C должен быть инициализирован)"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>scan</> - сканировать шину I2C"),
  HELPK("Сканирование шины I2C") },

{ "write", cmd_i2c_write, MANY_ARGS,
  HELPK("% \"<b>write</> <i>ADDR D1<i> [<o>D2 ... Dn</>]</>\"\r\n"
        "%\r\n"
        "% Записать байты D1..Dn (шестнадцатеричные значения) по адресу ADDR на шине I2C X\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>write 0x57 0 0xff</> - записать 2 байта по адресу 0x57: 0 и 255"),
  HELPK("Отправить байты устройству") },

{ "read", cmd_i2c_read, 2,
  HELPK("% \"<b>read</> <i>ADDR SIZE</></>\"\r\n"
        "%\r\n"
        "% Прочитать SIZE байт с устройства по адресу ADDR\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>read 0x68 7</> - прочитать 7 байт с устройства по адресу 0x68"),
  HELPK("Чтение данных с I2C-устройства") },

{ "down", cmd_i2c_down, NO_ARGS,
  HELPK("% \"<b>down</>\"\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>down</> - деинициализировать интерфейс I2C"),
  HELPK("Отключить интерфейс I2C") },


  KEYWORDS_END
};
KEYWORDS_REG(iic)

#if WITH_SPI
//SPI subderictory keywords
//No use cases so far, so it is disabled by default
KEYWORDS_DECL(spi) {

  KEYWORDS_BEGIN

{ "up", cmd_spi_up, 3,
  HELPK("% \"<b>up</> <i>MOSI MISO CLK</>\"\r\n"
        "%\r\n"
        "% Инициализировать интерфейс SPI в режиме MASTER, используя пины MOSI/MISO/CLK\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>up 23 19 18</> - инициализировать SPI на пинах 23, 19, 18"),
  HELPK("Инициализация интерфейса") },

{ "up", cmd_spi_up, 0, HIDDEN_KEYWORD },

{ "clock", cmd_spi_clock, 1,
  HELPK("% \"<b>clock</> <i>SPEED</>\"\r\n"
        "%\r\n"
        "% Установить тактовую частоту мастера SPI (SPI должен быть инициализирован)\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>clock 1000000</> - установить частоту SPI в 1 МГц"),
  HELPK("Установить частоту") },

{ "write", cmd_spi_write, MANY_ARGS,
  HELPK("% \"<b>write</> <i>CHIP_SELECT</> <g>D1</> [<o>D2 D3 ... Dn</>]\"\r\n"
        "%\r\n"
        "% Записать байты D1..Dn (шестнадцатеричные значения) на шину SPI при установке пина CHIP_SELECT в низкий уровень\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>write 4 0 0xff</> - записать 2 байта, CS=4"),
  HELPK("Отправить байты устройству") },


{ "down", cmd_spi_down, NO_ARGS,
  HELPK("% \"<b>down</>\"\r\n"
        "%\r\n"
        "% Отключить интерфейс SPI X"),
  HELPK("Отключить интерфейс SPI") },


  KEYWORDS_END
};
KEYWORDS_REG(spi)
#endif //WITH_SPI

//RMT (sequence) subderictory keywords
//
KEYWORDS_DECL(sequence) {

  KEYWORDS_BEGIN

{ "tick", cmd_seq_tick, 1,
  HELPK("% \"<b>tick</> <i>TIME</>\"\r\n"
        "%\r\n"
        "% Установить время тика последовательности: задаёт разрешение импульсной последовательности.\r\n"
        "% Задаётся в микросекундах и может принимать значения от 0.0125 до 3.2\r\n"
        "%\r\n"
        "% Все длительности (команды one, zero, head, tail, levels) задаются\r\n"
        "% в тиках, а не в микросекундах.\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>tick 0.1</> - установить разрешение 0.1 микросекунды (т.е. 1 тик = 0.1 мкс)"),
  HELPK("Установить разрешение") },

{ "zero", cmd_seq_zeroone, 2,
  HELPK("% \"<b>zero</> <i>LEVEL/DURATION</> [<o>LEVEL2/DURATION2</>]\"\r\n"
        "%\r\n"
        "% Определяет логический \"0\"\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>zero 0/50</>      - 0 — это уровень: LOW в течение 50 тиков\r\n"
        "%   <i>zero 1/50 0/20</> - 0 — это импульс: HIGH в течение 50 тиков, затем LOW в течение 20 тиков"),
  HELPK("Определить логический 0") },

  { "zero", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

{ "one", cmd_seq_zeroone, 2,
  HELPK("% \"<b>one</> <i>LEVEL/DURATION</> [<o>LEVEL2/DURATION2</>]\"\r\n"
        "%\r\n"
        "% Определяет логическую \"1\"\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>one 1/50</>       - 1 — это уровень: HIGH в течение 50 тиков\r\n"
        "%   <i>one 1/50 0/20</>  - 1 — это импульс: HIGH в течение 50 тиков, затем LOW в течение 20 тиков"),
  HELPK("Определить логическую 1") },

  { "one", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

{ "head", cmd_seq_zeroone, 2,
  HELPK("% \"<b>head L/D L/D</>\"\r\n"
        "%\r\n"
        "% Символ, передаваемый перед основным потоком данных"),
  HELPK("Заголовок последовательности") },

{ "tail", cmd_seq_zeroone, 1,
  HELPK("% \"<b>tail L/D</>\"\r\n"
        "%\r\n"
        "% Символ, передаваемый после основного потока данных"),
  HELPK("Хвост последовательности") },


{ "bits", cmd_seq_bits, 1,
  HELPK("% \"<b>bits</> <i>STRING</>\"\r\n"
        "%\r\n"
        "% Битовый шаблон, используемый как последовательность. STRING должен содержать только 0 и 1\r\n"
        "% Переопределяет ранее заданные команды \"levels\" и \"bytes\"\r\n"
        "% См. команды \"one\" и \"zero\" для определения \"1\" и \"0\"\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>bits 11101000010111100</>  - 17-битная последовательность"),
  HELPK("Задать передаваемые биты") },

{ "bytes", cmd_seq_bytes, MANY_ARGS,
  HELPK("% \"<b>bytes</> D0 D1 ... Dn\"\r\n"
        "% \"<b>bytes</> none\"\r\n"
        "%\r\n"
        "% Байтовый шаблон, используемый как последовательность. D0, D1.. Dn — шестнадцатеричные байты\r\n"
        "% Переопределяет ранее заданные команды \"levels\" и \"bits\"\r\n"
        "% См. команды \"one\" и \"zero\" для определения \"1\" и \"0\"\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>bytes a9 18 8f f cd 15</>  - 6-байтовая последовательность\r\n"
        "%   <i>bytes none</>  - очистить bits, levels и bytes"),
  HELPK("Задать передаваемые байты") },


{ "levels", cmd_seq_levels, MANY_ARGS,
  HELPK("% \"<b>levels</> <i>L1/D1</> [<o>L2/D2 ... Ln/Dn</>]\"\r\n"
        "%\r\n"
        "% Битовый шаблон, используемый как последовательность. L — это 1 или 0,\r\n"
        "% D — длительность, измеряемая в тиках [0..32767] (см. команду \"tick\")\r\n"
        "% Переопределяет ранее заданные команды \"bits\" и \"bytes\"\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "% <i>levels 1/50 0/20 1/100 0/500</>  - HIGH 50 тиков, LOW 20, HIGH 100\r\n"
        "%                                          и LOW 500 тиков\r\n"
        "% <i>levels 1/32767 1/17233 0/32767 0/7233</> - HIGH в течение 50000 тиков,\r\n"
        "%                                                  LOW в течение 40000 тиков"),
  HELPK("Задать передаваемые уровни") },

{ "modulation", cmd_seq_modulation, 3,
  HELPK("% \"<b>modulation</> <i>FREQ</> [<o>DUTY</> [<o>low|high</>] ]\"\r\n"
        "% \"<b>modulation</> <i>off</>\"\r\n"
        "%\r\n"
        "% Включает/выключает модуляцию выходного сигнала с частотой FREQ\r\n"
        "% Необязательные параметры: DUTY (от 0 до 1) и LEVEL (high или low)\r\n"
        "% Типичные ИК-приемники используют несущую 38 кГц с коэффициентом заполнения 33%,\r\n"
        "% при этом модулируется высокий уровень\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>modulation 38000</>         - модулировать все 1 с частотой 38 кГц, DUTY 33%\r\n"
        "%   <i>modulation 38000 0.5 low</> - модулировать все 0 с частотой 38 кГц, DUTY 50%\r\n"
        "%   <i>modulation off</>           - отключить модуляцию\r\n"
        "%   <i>modulation 0</>             - отключить модуляцию"),
  HELPK("Включить/выключить модуляцию") },
  
  { "modulation", cmd_seq_modulation, 2, HIDDEN_KEYWORD },
  { "modulation", cmd_seq_modulation, 1, HIDDEN_KEYWORD },

{ "eot", cmd_seq_eot, 1,
  HELPK("% \"<b>eot</> <i>high|low</>\"\r\n"
        "%\r\n"
        "% Конец передачи: установить линию в высокий или низкий уровень\r\n"
        "% по завершении последовательности. По умолчанию — \"low\""),
  HELPK("Уровень линии после завершения передачи") },

  { "loop", cmd_seq_loop, MANY_ARGS
  
  ,
HELPK("% \"<b>loop</> [<o>NUM</>]\"\r\n"
      "%\r\n"
      "% Зациклить последовательность NUM раз или бесконечно (без аргументов)\r\n"
      "% Если NUM > 1, последовательность будет повторяться NUM раз (аппаратно)\r\n"
      "% Чтобы отключить зацикливание, установите счётчик в \"1\"\r\n"
      "%\r\n"
      "% <u>Examples:</>\r\n"
      "%   <i>loop</>     : Бесконечный цикл\r\n"
      "%   <i>loop 5</>   : Повторить 5 раз\r\n"
      "%   <i>loop 1</>   : Отключить зацикливание\r\n"
      "%   <i>loop off</> : Отключить зацикливание"),
HELPK("Зацикливание последовательности") },

{ "show", cmd_show_sequence, NO_ARGS,
  HELPK("% \"<b>show</>\"\r\n"
        "%\r\n"
        "% Показать <u>эту</> последовательность; сокращение для \"show sequence NUM\""),
  HELPK("Показать последовательность") },

  // we have "save" but we don't have "load" here: the "load" command is a global command "exec" which simply
  // reads text files line by line and executes them until any errors are detected
  // This allows for modules (e.g. sequence, alias, ifcond, ...) to save their current configuration to a
  // text file for later execution. Saved files is a shell script.
#if WITH_FS  
{ "save", cmd_seq_save, 1,
  HELPK("% \"<b>save /FILE_NAME</>\"\r\n"
        "%\r\n"
        "% ВНИМАНИЕ: файловая система должна быть смонтирована перед использованием\r\n"
        "% этой функции (см. команду \"files\" и команды группы \"files\")\r\n"
        "%\r\n"
        "% Сохранить <u><b>эту</> последовательность в/из текстового файла; текстовый файл\r\n"
        "% можно отредактировать, после чего его содержимое можно загрузить обратно\r\n"
        "% (см. глобальную команду \"exec\"), либо скопировать и вставить в командную строку.\r\n"
        "% Захваченные последовательности (см. \"capture\") также могут быть сохранены"),
  HELPK("Сохранить последовательность в файл") },
#endif // WITH_FS    
#if NOT_YET
{ "decode", cmd_seq_decode, 1,
  HELPK("% \"<b>decode [TOLERANCE]</>\"\r\n"
        "%\r\n"
        "% Попытка декодировать \"levels\" в битовую строку согласно описаниям импульсов\r\n"
        "% в алфавите \"one\" и \"zero\", с допуском по временным характеристикам\r\n"
        "% вокруг заданных значений +/- TOLERANCE процентов (по умолчанию 20%%)\r\n"
        "%\r\n"
        "% Последовательность должна быть инициализирована: либо вручную,\r\n"
        "% либо командой \"capture\", либо загружена из файла глобальной командой \"exec\".\r\n"
        "% <u>Пример:</>\r\n"
        "%   <i>profile nec</> : загрузить пресет протокола NEC\r\n"
        "%   <i>decode 10</>   : декодировать как протокол NEC (допуск по таймингам ±10%%)\r\n"
        "% Если ожидаемая длительность импульса составляет 100 мкс,\r\n"
        "% то любой импульс длительностью от 90 до 110 мкс будет считаться совпадением"),
  HELPK("Декодировать последовательность") },

  { "decode", cmd_seq_decode, 0, HIDDEN_KEYWORD },

{ "profile", cmd_seq_profile, 1,
  HELPK("% \"<b>profile <i>nec|lg|lg32|samsung|1wire|neopixel|none</>\"\r\n"
        "%\r\n"
        "% Загрузить предустановки для различных ИК-протоколов:\r\n"
        "% Загружаются заголовок и хвост последовательности, параметры модуляции,\r\n"
        "% определения \"one\" и \"zero\", а также период тика"
        ),
  HELPK("Профили таймингов") },
#endif
{ "capture", cmd_seq_capture, MANY_ARGS,
  HELPK("% \"<b>capture PIN [NUM_SYMBOLS | all] [TIMEOUT_MS]</>\"\r\n"
        "%\r\n"
        "% Захватить последовательность импульсов на GPIO# PIN\r\n"
        "% После задания алфавита \"one\" и \"zero\" можно попытаться декодировать\r\n"
        "% битовую строку (см. команду \"decode\")"),
  HELPK("Захват последовательности") },
  

  KEYWORDS_END
};
KEYWORDS_REG(sequence)



#if WITH_WIFI
// Dummy group "wifi".
// "wifi ap|sta" switches current command directory to either "ap" or "sta", but never to the "wifi"
// It is here purely for colorer and does not affect espshell operation
KEYWORDS_DECL(wifi) { 
  { NULL, NULL, 0, NULL, NULL }
};
KEYWORDS_REG(wifi)

// "wifi sta" command subdirectory
// WiFi STA
KEYWORDS_DECL(sta) {

  KEYWORDS_BEGIN

{ "up", cmd_wifi_up, NO_ARGS,
  HELPK("% \"<b>up SSID|BSSID [PASSWORD] [reconnect]</>\"\r\n"
        "% \"<b>up</>\"\r\n" 
        "%\r\n"
        "% Подключиться к точке доступа (SSID или BSSID), используя пароль PASSWORD. Конфигурацию\r\n"
        "% (например, IP, DHCP, NTP) рекомендуется выполнять <u>заранее</>. Если в системе уже\r\n"
        "% загружена конфигурация WiFi (из NVS или пользовательского скетча), можно использовать\r\n"
        "% короткую форму команды \"up\" (без аргументов)\r\n"
        "%\r\n"
        "% Используйте команду \"scan\", чтобы узнать доступные SSID и BSSID\r\n"
        "% Используйте команду \"show wifi sta\", чтобы показать системную STA-конфигурацию WiFi\r\n"
        "% Используйте команду \"wifi storage flash\" для включения автосохранения конфигурации\r\n"
        "%\r\n"
        "%<u>Примеры</>:\r\n"
        "%  up \"Home Network\" jfhod786    - подключение по SSID с паролем\r\n"
        "%  up \"Home Network\" \"\" auto   - подключение по SSID без пароля, с автопереподключением\r\n"
        "%  up 0044:ff55:02ae jfhod786 auto - подключение по BSSID, автопереподключение\r\n"
        "%  up                              - подключение с использованием системной/скетч-конфигурации\r\n"
        "%"),
  HELPK("Инициализация и запуск WiFi интерфейса") },

    { "up", cmd_wifi_up, MANY_ARGS, HIDDEN_KEYWORD },


{ "down", cmd_wifi_down, NO_ARGS,
  HELPK("% \"<b>down</>\"\r\n" 
        "%\r\n"
        "% Остановить и деинициализировать интерфейс WiFi"),
  HELPK("Отключить WiFi интерфейс") },

{ "scan", cmd_wifi_scan, MANY_ARGS,
  HELPK("% \"<b>scan</> [<o>active</> | <o>passive</> | <o>bssid AABB:CCDD:EEFF</>]*\"\r\n" 
        "%\r\n"
        "% Сканировать и отобразить доступные сети.\r\n"
        "% Выполняется пассивное или активное сканирование (по умолчанию — \"active\")\r\n"
        "% и выводится список найденных сетей либо, если задан параметр \"bssid\",\r\n"
        "% подробная информация о конкретной точке доступа\r\n"
        "% <u>Примеры</>:\r\n"
        "% <i>scan</>                          : Активное сканирование (опрос AP)\r\n"
        "% <i>scan passive</>                  : Пассивное сканирование (прослушивание AP)\r\n"
        "% <i>scan bssid 0001:2233:4455</>     : Активное сканирование конкретной AP\r\n"
        "% <i>scan b 0001:2233:4455 passive</> : Пассивное сканирование конкретной AP"),
  HELPK("Сканирование WiFi сетей") },

{ "ntp", cmd_wifi_ntp, MANY_ARGS,
  HELPK("% \"<b>ntp</> [<o>dhcp</>|<o>HOST</>] [<o>enable</>|<o>disable</>]</>\"\r\n" 
        "%\r\n"
        "% Настройка клиента SNTP:\r\n"
        "%  <i>dhcp</>    - Получать список серверов SNTP из ответа DHCP\r\n"
        "%  <i>HOST</>    - Использовать сервер SNTP HOST (IP-адрес или имя хоста)\r\n"
        "%  <i>disable</> - Отключить SNTP\r\n"
        "%  <i>enable</>  - Включить SNTP\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   ntp dhcp         - использовать DHCP для получения NTP-серверов\r\n"
        "%   ntp dhcp enable  - использовать DHCP и включить клиент SNTP\r\n"
        "%   ntp pool.ntp.org - использовать pool.ntp.org как NTP-сервер\r\n"
        "%   ntp 2.2.2.2 dhcp - использовать DHCP и статические адреса серверов\r\n"
        "%   ntp disable      - отключить клиент SNTP\r\n"
        "%"
        ),
  HELPK("Настройки SNTP") },

{ "mac", cmd_wifi_mac, 1,
  HELPK("% \"<b>mac</> <i>MAC-ADDRESS</>|default\"\r\n" 
        "%\r\n"
        "% Установить MAC-адрес интерфейса:\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   mac 0001123456af\r\n"
        "%   mac 00:01:12:34:56:af\r\n"
        "%   mac 0001:1234:56af"),
  HELPK("Настройки MAC (STA)") },

{ "hostname", cmd_wifi_hostname, 1,
  HELPK("% \"<b>hostname</> <i>TEXT</>\r\n" 
        "%\r\n"
        "% Установить имя хоста (макс. 31 символ; допускаются буквы, цифры и точки)\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   hostname mars.local\r\n"
        "%   hostname Moon2"),
  HELPK("Имя хоста") },

  { "hostname", cmd_wifi_hostname, NO_ARGS, HIDDEN_KEYWORD},

{ "ip", cmd_wifi_ip_address, MANY_ARGS,
  HELPK("% \"<b>ip address</> dhcp\"\r\n" 
        "% \"<b>ip address</> A.B.C.D/PREFIX [gw <o>A.B.C.D</>] [dns <o>A.B.C.D</> [<o>A.B.C.D</>]]\"\r\n" 
        "%\r\n"
        "% Задать IP-адрес интерфейса: статический или через DHCP\r\n"
        "% <i>A.B.C.D/PREFIX</>        : IP-адрес и длина префикса, например\r\n"
        "%                            \"192.168.0.1/24\" — это адрес 192.168.0.1 и маска сети\r\n"
        "%                            \"255.255.255.0\" (24-битная маска)\r\n"
        "% <i>dhcp</>                  : использовать DHCP для получения IP-параметров\r\n"
        "% <i>gw A.B.C.D</>            : указать шлюз по умолчанию\r\n"
        "% <i>dns A.B.C.D [A.B.C.D]</> : указать DNS-сервер(а)\r\n"
        "%\r\n"
        "%<u>Примеры</>:\r\n"
        "%   <i>ip address dhcp</>            : Получить адрес автоматически\r\n"
        "%   <i>ip address 192.168.0.2/24</>  : Статический IP с маской 255.255.255.0\r\n"
        "%   <i>ip ad 1.1.0.2/8 dns 8.8.8.8</>: Статический IP и DNS\r\n"
        "%   <i>ip ad dhcp dns 8.8.8.8</>     : Динамический IP, статический DNS\r\n"
        "%   <i>ip ad dhcp gw 192.168.0.1</>  : Динамический IP-адрес, статический шлюз</>\r\n"
        "%"),
  HELPK("IP-параметры") },
  KEYWORDS_END
};
KEYWORDS_REG(sta)

// WiFi AP
KEYWORDS_DECL(ap) {

  KEYWORDS_BEGIN

{ "up", cmd_wifi_up, MANY_ARGS,
  HELPK("% \"<b>up [NETWORK_NAME [PASSWORD] [max-sta NUM]</>\"\r\n" 
        "%\r\n"
        "% Создать точку доступа с именем NETWORK_NAME, используя пароль PASSWORD,\r\n"
        "% с ограничением максимального числа подключений до NUM.\r\n"
        "%\r\n"
        "% ПРИМЕЧАНИЕ: используйте \"\" в качестве NETWORK_NAME для создания скрытой сети\r\n"
        "%\r\n"
        "%<u>Примеры</>:\r\n"
        "%  up                          - создать AP, используя системную/сохранённую конфигурацию WiFi\r\n"
        "%  up IoT_Network              - создать открытую сеть IoT_Network\r\n"
        "%  up Home jfhod786 max-sta 1  - сеть \"Home\" с паролем, для 1 клиента\r\n"
        "%  up Home \"\" max-sta 1      - сеть \"Home\" без пароля, для 1 клиента\r\n"
        "%  up \"\" jfhod786            - скрытая сеть с паролем"
        ),
  HELPK("Инициализация и запуск WiFi интерфейса") },

{ "down", cmd_wifi_down, NO_ARGS,
  HELPK("% \"<b>down</>\"\r\n" 
        "%\r\n"
        "% Остановить и отключить точку доступа WiFi"),
  HELPK("Отключить WiFi интерфейс") },

{ "scan", cmd_wifi_scan, MANY_ARGS, //TODO: get rid of bssid keyword
  HELPK("% \"<b>scan</> [active|passive|bssid <o>AABB:CCDD:EEFF</>]*\"\r\n" 
        "%\r\n"
        "% Сканировать и отобразить доступные сети.\r\n"
        "% Выполняется пассивное или активное сканирование (по умолчанию — \"active\")\r\n"
        "% и выводится список найденных сетей либо, если задан параметр \"bssid\",\r\n"
        "% подробная информация о конкретной точке доступа\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%\r\n"
        "% <i>scan</>                          : Активное сканирование (опрос AP)\r\n"
        "% <i>scan passive</>                  : Пассивное сканирование (прослушивание AP)\r\n"
        "% <i>scan bssid 0001:2233:4455</>     : Активное сканирование конкретной AP\r\n"
        "% <i>scan b 0001:2233:4455 passive</> : Пассивное сканирование конкретной AP"),
  HELPK("Сканирование WiFi сетей") },

{ "mac", cmd_wifi_mac, 1,
  HELPK("% \"<b>mac</> <i>MAC-ADDRESS</>|default\"\r\n" 
        "%\r\n"
        "% Установить MAC-адрес интерфейса:\r\n"
        "% ПРИМЕЧАНИЕ: бит 0 первого октета адреса должен быть равен нулю\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   mac 0101123456af       - некорректный адрес (бит 0 установлен)\r\n"
        "%   mac 0001123456af       - корректный адрес\r\n"
        "%   mac 00:01:12:34:56:af  - формат Microsoft\r\n"
        "%   mac 0001:1234:56af     - формат Cisco"),
  HELPK("Настройки MAC (точка доступа)") },

{ "hostname", cmd_wifi_hostname, 1,
  HELPK("% \"<b>hostname</> [<i>TEXT</>]\r\n" 
        "%\r\n"
        "% Установить/показать имя хоста (для данного интерфейса)\r\n"
        "% Без аргументов : показать текущее имя хоста интерфейса\r\n"
        "% TEXT           : установить имя хоста для интерфейса\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>hostname</>            : Показать имя хоста\r\n"
        "%   <i>hostname mars.local</> : Установить имя хоста \"mars.local\"\r\n"
        "%   <i>hostname Moon2</>      : Установить имя хоста \"Moon2\"\r\n"
        "%"),
  HELPK("Имя хоста (для интерфейса)") },
  { "hostname", cmd_wifi_hostname, NO_ARGS, HIDDEN_KEYWORD},
  
{ "dhcp", cmd_wifi_dhcp, 3,
  HELPK("% \"<b>dhcp A.B.C.D [<o>NUM</>] [<o>LEASE</>]\r\n" 
        "% \"<b>dhcp enable|disable\r\n" 
        "%\r\n"
        "% Задать диапазон IP-адресов для DHCP и запустить/остановить сервер\r\n"
        "% Пул IP-адресов DHCP должен находиться в подсети интерфейса\r\n"
        "% <i>ПРИМЕЧАНИЕ 1: При изменении диапазона адресов DHCP</> убедитесь, что\r\n"
        "% <i> IP-адрес интерфейса находится в той же подсети</> что и новый диапазон DHCP!\r\n"
        "%\r\n"
        "%<u>Примеры (адрес AP — 192.168.4.1)</>:\r\n"
        "%   <i>dhcp 192.168.4.8</>        : выдавать адреса, начиная с 192.168.4.8\r\n"
        "%   <i>dhcp 192.168.4.2 2 3600</> : доступно только 2 IP-адреса, аренда на 1 час\r\n"
        "%   <i>dhcp enable</>             : включить DHCP-сервер на интерфейсе AP\r\n"
        "%"),
  HELPK("Настройки DHCP-сервера") },
  { "dhcp", cmd_wifi_dhcp, 2, HIDDEN_KEYWORD },
  { "dhcp", cmd_wifi_dhcp, 1, HIDDEN_KEYWORD },

{ "ip", cmd_wifi_ip_address, MANY_ARGS,
  HELPK("% \"<b>ip address</> A.B.C.D/PREFIX [dns <o>A.B.C.D</> [<o>A.B.C.D</>]]\"\r\n" 
        "%\r\n"
        "% Задать используемый IP-адрес интерфейса\r\n"
        "% <i>A.B.C.D/PREFIX</>        : IP-адрес и длина префикса\r\n"
        "% <i>dns A.B.C.D [A.B.C.D]</> : Переопределить DNS (по умолчанию используется DNS STA)\r\n"
        "%\r\n"
        "%<u>Примеры</>:\r\n"
        "%   <i>ip address 192.168.0.2/24</>         : Статический IP с маской 255.255.255.0\r\n"
        "%   <i>ip ad 192.168.0.2/24 dns 8.8.8.8</>  : Статический IP и статический DNS\r\n"
        "%   <i>ip ad dhcp dns 8.8.8.8 dns 4.4.4.4</>: Динамический IP и два статических DNS\r\n"
        "%"),
  HELPK("IP-параметры") },
  

{ "natp", cmd_wifi_natp, MANY_ARGS,
  HELPK("% \"<b>natp</> enable|disable\"\r\n" 
        "% \"<b>natp</> add|delete tcp|udp EXT_PORT A.B.C.D LOCAL_PORT\"\r\n" 
        "%\r\n"
        "% <i>A.B.C.D</>     : Локальный хост (в подсети AP)\r\n"
        "% <i>LOCAL_PORT</>  : Локальный порт (\"внутренний\", со стороны AP)\r\n"
        "% <i>EXT_PORT</>    : Внешний порт (\"внешний\", со стороны STA)\r\n"
        "%\r\n"
        "% 1. Включить или отключить NAT/P для WiFi-клиентов\r\n"
        "% 2. Создать/удалить статические пробросы портов: сопоставить client_IP:PORT\r\n"
        "%    с \"внешним\" портом, например сделать web/ftp-сервер клиента\r\n"
        "%    доступным извне\r\n"
        "%    При этом AP выступает в роли LAN-интерфейса, а STA — в роли WAN-интерфейса\r\n"
        "%\r\n"
        "%<u>Примеры</>:\r\n"
        "% <i>natp enable</>                 : Включить NAT/P\r\n"
        "% <i>natp disable</>                : Отключить NAT/P (по умолчанию)\r\n"
        "% <i>natp add 80 192.168.4.5 8080</>: Пробросить 192.168.4.5:8080 <-> STA:80\r\n"
        "% <i>natp del 80</>                 : Удалить проброс\r\n"
        "%"),
  HELPK("Настройки NAT/P") },

    { "natp", cmd_wifi_natp, 3, HIDDEN_KEYWORD },
    { "natp", cmd_wifi_natp, 2, HIDDEN_KEYWORD },
    { "natp", cmd_wifi_natp, 1, HIDDEN_KEYWORD },


{ "kick", cmd_wifi_kick, 1,
  HELPK("% \"<b>kick all</>\r\n" 
        "% \"<b>kick AID</>>\r\n" 
        "%\r\n"
        "% Деаутентифицировать подключённую станцию по её AID. Соответствие AID–MAC\r\n"
        "% клиентов можно посмотреть в выводе команды \"show wifi clients\"\r\n"
        "% (см. колонку \"AID\")\r\n"
        "%\r\n"
        "%<u>Примеры</>:\r\n"
        "%   <i>kick all</>  : Отключить (deauth) всех клиентов\r\n"
        "%   <i>kick 1</>    : Отключить (deauth) клиента с AID №1\r\n"
        "%"),
  HELPK("Отключение клиентов") },


  KEYWORDS_END
};
KEYWORDS_REG(ap)
#endif //WITH_WIFI

// esp32-alias>
//
#if WITH_ALIAS
KEYWORDS_DECL(alias) {

  KEYWORDS_BEGIN
    
{ "delete", cmd_alias_delete, 1,
  HELPK("% \"<b>delete</> [all | LINE]\"\r\n" 
        "%\r\n"
        "% Удалить строки из алиаса:\r\n"
        "% 1. Без аргументов: удалить последнюю строку\r\n"
        "% 2. Один аргумент — ключевое слово \"all\": удалить все строки\r\n"
        "% 3. LINE — номер строки (используйте \"list\" для просмотра номеров)\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>delete</>     - удалить последнюю добавленную команду из алиаса\r\n"
        "%   <i>delete all</> - удалить всё\r\n"
        "%   <i>delete 4</>   - удалить строку №4"),
  HELPK("Удаление строк") },

  { "delete", cmd_alias_delete,NO_ARGS, HIDDEN_KEYWORD },

{ "list", cmd_alias_list, NO_ARGS,
  HELPK("% \"<b>list</>\"\r\n" 
        "%\r\n"
        "% Показать текущее содержимое алиаса"),
  HELPK("Показать содержимое") },

{ "quit", cmd_alias_quit, NO_ARGS,
  HELPK("% \"<b>quit</>\r\n" 
        "%\r\n"
        "% Выйти из режима настройки алиаса.\r\n"),
  HELPK("Выход из редактора алиаса") },

{ "*TEXT*", cmd_alias_asterisk, MANY_ARGS,
  HELPK("% \"<b>COMMAND ARG1 ARG2 ... ARGn</>\"\r\n" 
        "%\r\n"
        "% Любая команда с любым количеством аргументов"),
  HELPK("Добавить любую команду в алиас") },

  {
    NULL, NULL, 0, NULL, NULL
  }
};

// Register "alias" command directory (global variable keywords_alias)
KEYWORDS_REG(alias)
#endif //WITH_ALIAS


#if WITH_FS
// Filesystem commands. this commands subdirectory is enabled
// with the "files" command /cmd_files_if()/
//
KEYWORDS_DECL(files) {

  KEYWORDS_BEGIN


{ "mount", cmd_files_mount0, NO_ARGS,
  HELPK("% \"<b>mount</>\"\r\n"
        "%\r\n"
        "% Команда \"mount\" <u>без аргументов</> выводит информацию о разделах\r\n"
        "% и смонтированных файловых системах (точка монтирования, тип ФС,\r\n"
        "% общий и использованный объём)\r\n"
        ),
  HELPK("Монтирование разделов / таблица разделов") },

{ "mount", cmd_files_mount, 2,
  HELPK("% \"<b>mount</> <i>LABEL</> [<o>/MOUNT_POINT</>]\"\r\n"
        "%\r\n"
        "% Смонтировать файловую систему, расположенную во встроенной SPI FLASH\r\n"
        "%\r\n"
        "% <i>LABEL</>        - метка (label) раздела SPI FLASH\r\n"
        "% <o>/MOUNT_POINT</> - путь, начинающийся с \"/\", в который будет смонтирована ФС\r\n"
        "% Если точка монтирования не указана, используется \"/\" + LABEL\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>mount ffat /fs</> - смонтировать раздел \"ffat\" в каталог \"/fs\"\r\n"
        "%   <i>mount ffat</>     - смонтировать раздел \"ffat\" в каталог \"/ffat\""),
  NULL },

  { "mount", cmd_files_mount, 1, HIDDEN_KEYWORD },

{ "mount", cmd_files_mount_sd, 6,
  HELPK("% \"<b>mount <i>vspi|hspi|fspi</> <i>MISO MOSI CLK CS</> [<o>FREQ</>] [<o>/MOUNTPOINT</>]\"\r\n"
        "%\r\n"
        "% Смонтировать FAT-файловую систему на SD-карте, подключённой к SPI-шине\r\n"
        "%\r\n"
        "% <i>vspi, hspi, fspi</> : используемая SPI-шина (<i>hspi</> — самый безопасный вариант)\r\n"
        "% <i>MISO, MOSI, CLK, CS</> : используемые SPI-пины (например, 19, 23, 18 и 5)\r\n"
        "% <o>FREQ</> : необязательный параметр, частота SPI в кГц (по умолчанию 20000)\r\n"
        "% <o>/MOUNTPOINT</> : путь, начинающийся с \"/\", в который будет смонтирована ФС\r\n"
        "%\r\n"
        "% Если точка монтирования не указана, будет использовано автоматически\r\n"
        "% сгенерированное имя, например \"scard4\"\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>mount hspi 19 23 18 4 1000 /sd</> - SPI2 (HSPI) 1 МГц, монтирование в \"/sd\"\r\n"
        "%   <i>mount hspi 19 23 18 4 400</>      - То же самое, но SPI работает на 400 кГц\r\n"
        "%   <i>mount vspi 19 23 18 4 /sdcard</>  - Смонтировать SD-карту на VSPI,\r\n"
        "%                                       пины 19, 23, 18 и 4"),
  NULL },

  { "mount", cmd_files_mount_sd, 7, HIDDEN_KEYWORD },
  { "mount", cmd_files_mount_sd, 5, HIDDEN_KEYWORD },

{ "unmount", cmd_files_unmount, 1,
  HELPK("% \"<b>unmount</> [<o>/MOUNT_POINT</>]\"\r\n"
        "%\r\n"
        "% Размонтировать файловую систему, указанную по точке монтирования\r\n"
        "% Если точка монтирования не указана, будет размонтирована файловая система,\r\n"
        "% соответствующая текущему рабочему каталогу\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>unmount</>          - размонтировать текущую файловую систему\r\n"
        "%   <i>unmount /ffat</>    - размонтировать по пути точки монтирования\r\n"
        "%   <i>unmount /ff/dir1</> - точка монтирования определяется по указанному пути\r\n"
        ),
  HELPK("Размонтировать раздел") },

  { "unmount", cmd_files_unmount, NO_ARGS, HIDDEN_KEYWORD },
  { "umount", cmd_files_unmount, 1, HIDDEN_KEYWORD },        // for unix folks
  { "umount", cmd_files_unmount, NO_ARGS, HIDDEN_KEYWORD },  // for unix folks

{ "ls", cmd_files_ls, 1,
  HELPK("% \"<b>ls</> [<o>PATH</>]\"\r\n"
        "%\r\n"
        "% Показать содержимое каталога по указанному PATH\r\n"
        "% Если PATH не указан, выводится содержимое текущего каталога\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>ls</>              - содержимое текущего каталога\r\n"
        "%   <i>ls ../dir</>       - относительный путь\r\n"
        "%   <i>ls /ffat/dir</>    - абсолютный путь\r\n"
        "%   <i>ls /</>            - список точек монтирования\r\n"
        ),
  HELPK("Список каталога") },

  { "ls", cmd_files_ls, 0, HIDDEN_KEYWORD },

{ "cd", cmd_files_cd, MANY_ARGS,
  HELPK("% \"<b>cd</> [<o>PATH | ..</>]\"\r\n"
        "%\r\n"
        "% Сменить текущий каталог\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>cd</>                 - перейти в корень файловой системы\r\n"
        "%   <i>cd ..</>              - перейти на уровень выше\r\n"
        "%   <i>cd /ffat/test/</>     - перейти в каталог \"/ffat/test/\"\r\n"
        "%   <i>cd ../test2/test3/</> - перейти в каталог \"/ffat/test2/test3\"\r\n"
        ),
  HELPK("Сменить каталог") },

{ "rm", cmd_files_rm, MANY_ARGS,
  HELPK("% \"<b>rm</> <i>PATH1</> [<o>PATH2 PATH3 ... PATHn</>]\"\r\n"
        "%\r\n"
        "% Удалить файлы или каталоги вместе с их содержимым.\r\n"
        "% При удалении каталогов: все файлы и подкаталоги также удаляются\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>rm /ffat/dir1</>  - удалить каталог \"/ffat/dir1\" со всем содержимым\r\n"
        "%   <i>rm .</>           - удалить содержимое текущего каталога, сохранив сам каталог\r\n"
        ),
  HELPK("Удаление файлов/каталогов") },

{ "mv", cmd_files_mv, 2,
  HELPK("% \"<b>mv</> <i>SOURCE DESTINATION</>\"\r\n"
        "%\r\n"
        "% Переместить или переименовать файл или каталог SOURCE в DESTINATION\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>mv /ffat/dir1 /ffat/dir2</>             - переименовать каталог \"dir1\" в \"dir2\"\r\n"
        "%   <i>mv /ffat/fileA.txt /ffat/fileB.txt</>   - переименовать файл \"fileA.txt\" в \"fileB.txt\"\r\n"
        "%   <i>mv /ffat/dir1/file1 /ffat/dir2</>       - переместить файл в каталог\r\n"
        "%   <i>mv . ../dir/</>                         - переместить содержимое каталога в другой каталог\r\n"
        "%   <i>mv /ffat/fileA.txt /spiffs/fileB.txt</> - переместить файл между файловыми системами\r\n"
        ),
  HELPK("Перемещение/переименование файлов и каталогов") },

{ "cp", cmd_files_cp, 2,
  HELPK("% \"<b>cp</> <i>SOURCE DESTINATION</>\"\r\n"
        "%\r\n"
        "% Копировать файл SOURCE в DESTINATION.\r\n"
        "% Файлы SOURCE и DESTINATION могут находиться в разных файловых системах\r\n"
        "% Каталоги создаются автоматически\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>cp /ffat/test.txt test2.txt</>         - копировать файл в файл\r\n"
        "%   <i>cp test.txt ../dir/</>                 - копировать файл в каталог\r\n"
        "%   <i>cp /ffat/dir_src /ffat/dir/</>         - копировать каталог в каталог\r\n"
        "%   <i>cp . ../dir/</>                        - копировать содержимое каталога\r\n"
        "%   <i>cp /spiffs/test.txt /ffat/test2.txt</> - копировать между файловыми системами\r\n"
        ),
  HELPK("Копирование файлов/каталогов") },

{ "write", cmd_files_write, MANY_ARGS,
  HELPK("% \"<b>write</> <i>FILENAME</> [<o>TEXT</>]\"\r\n"
        "%\r\n"
        "% Записать ASCII/hex-строку(и) в файл\r\n"
        "% TEXT может содержать пробелы и escape-последовательности: \\n, \\r, \\\\, \\t,\r\n"
        "% а также шестнадцатеричные байты вида \\AB (A и B — шестнадцатеричные цифры)\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>write /ffat/test.txt \\n\\rMixed\\20Text and \\20\\21\\ff</>"),
  HELPK("Запись строк/байтов в файл") },

{ "append", cmd_files_write, MANY_ARGS,
  HELPK("% \"<b>append</> <i>FILENAME</> [<o>TEXT</>]\"\r\n"
        "%\r\n"
        "% Дописать ASCII/hex-строку(и) в конец файла\r\n"
        "% Escape-последовательности и ASCII-коды поддерживаются так же,\r\n"
        "% как и в команде \"write\"\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>append /ffat/test.txt \\n\\rMixed\\20Text and \\20\\21\\ff</>"),
  HELPK("Добавление строк/байтов в файл") },

{ "insert", cmd_files_insdel, MANY_ARGS,
  HELPK("% \"<b>insert</> <i>FILENAME LINE_NUM</> [<o>TEXT</>]\"\r\n"
        "%\r\n"
        "% Вставить TEXT в файл FILENAME перед строкой LINE_NUM\r\n"
        "% К вставляемой строке автоматически добавляется \"\\n\",\r\n"
        "% но \"\\r\" не добавляется\r\n"
        "% Escape-последовательности и ASCII-коды поддерживаются так же,\r\n"
        "% как и в команде \"write\"\r\n"
        "% Нумерация строк начинается с 1. Для определения номеров строк\r\n"
        "% используйте команду \"cat\"\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>insert /ffat/test.txt 1 Hello World!</> - вставить перед строкой #1"
        ),
  HELPK("Вставка строк в текстовый файл") },

{ "delete", cmd_files_insdel, 3,
  HELPK("% \"<b>delete</> <i>FILENAME LINE_NUM</> [<o>COUNT</>]\"\r\n"
        "%\r\n"
        "% Удалить строку LINE_NUM из текстового файла FILENAME\r\n"
        "% Необязательный аргумент COUNT задаёт количество строк для удаления\r\n"
        "% (по умолчанию — 1)\r\n"
        "% Нумерация строк начинается с 1. Для просмотра номеров строк\r\n"
        "% используйте команду \"cat -n\"\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>delete /ffat/test.txt 10</>   - удалить строку #10 из \"/ffat/test.txt\"\r\n"
        "%   <i>delete /ffat/test.txt 10 3</> - удалить строки #10, #11 и #12"
        ),
  HELPK("Удаление строк из текстового файла") },

  { "delete", cmd_files_insdel, 2, HIDDEN_KEYWORD },

{ "mkdir", cmd_files_mkdir, MANY_ARGS,
  HELPK("% \"<b>mkdir</> <i>PATH1</> [<o>PATH2 PATH3 ... PATHn</>]\"\r\n"
        "%\r\n"
        "% Создать пустые директории PATH1 ... PATHn\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>mkdir ../a/b/c</>    - создать ../a/b/c\r\n"
        "%   <i>mkdir ../a ../b ../c</> - создать ../a, ../b и ../c"
        ),
  HELPK("Создание директорий") },

{ "cat", cmd_files_cat, MANY_ARGS,
  HELPK("% \"<b>cat</> [<o>-n|-b</>] <i>PATH</> [<o>START</> [<o>COUNT</>]] [<o>uart NUM</>]\"\r\n"
        "%\r\n"
        "% Показать (или передать через UART) бинарный или текстовый файл по пути PATH\r\n"
        "% -n : показывать номера строк\r\n"
        "% -b : рассматривать файл как бинарный\r\n"
        "%      (взаимоисключающе с опцией \"-n\")\r\n"
        "% PATH  : путь к файлу\r\n"
        "% START : номер строки для текстовых файлов\r\n"
        "%         (или смещение в байтах для бинарных файлов при использовании \"-b\")\r\n"
        "% COUNT : количество строк для вывода\r\n"
        "%         (или количество байт для режима \"-b\")\r\n"
        "% NUM   : номер UART-интерфейса, в который передавать файл\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>cat file</>              - показать файл \"file\"\r\n"
        "%   <i>cat -n file</>           - показать файл с номерами строк\r\n"
        "%   <i>cat file 34</>           - показать текстовый файл начиная со строки 34\r\n"
        "%   <i>cat file 900 10</>       - показать 10 строк, начиная со строки 900\r\n"
        "%   <i>cat -b file</>           - показать бинарный файл (форматированный вывод)\r\n"
        "%   <i>cat -b file 0x1234</>    - показать бинарный файл со смещения 0x1234\r\n"
        "%   <i>cat -b file 999 0x400</> - 999 байт, начиная со смещения 1024\r\n"
        "%   <i>cat file uart 1</>       - передать текстовый файл через UART1\r\n"
        "%                                (символы \"\\r\" будут отброшены)\r\n"
        "%   <i>cat -b file uart 1</>    - передать файл через UART1 \"как есть\", побайтно"
        ),
  HELPK("Просмотр/передача текстовых и бинарных файлов") },

{ "touch", cmd_files_touch, MANY_ARGS,
  HELPK("% \"<b>touch</> <i>PATH1</> [<o>PATH2 PATH3 ... PATHn</>]\"\r\n"
        "%\r\n"
        "% Создать новые файлы или обновить временные метки существующих\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>touch file1.txt ../file2.txt</> - создать/обновить два файла"
        ),
  HELPK("Создание/обновление файлов") },

{ "format", cmd_files_format, 1,
  HELPK("% \"<b>format</> [<o>LABEL</>]\"\r\n"
        "%\r\n"
        "% Форматировать раздел LABEL. Если LABEL не указан, используется\r\n"
        "% текущая рабочая директория для определения имени раздела\r\n"
        "%\r\n"
        "% <i>ПРИМЕЧАНИЕ</>: SD-карта по SPI пока не поддерживается\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>format</>           - форматировать текущий раздел\r\n"
        "%   <i>format ffat</>      - форматировать раздел \"ffat\"\r\n"
        "%   <i>format /abc/dir2</> - форматировать раздел, в котором находится /abc/dir2\r\n"
        ),
  HELPK("Форматирование файловой системы") },

  { "format", cmd_files_format, 0, HIDDEN_KEYWORD },
 

  KEYWORDS_END
};
KEYWORDS_REG(files)
#endif  //WITH_FS

#if WITH_NVS
// NVS editor/viewer commands
KEYWORDS_DECL(nvs) {

  KEYWORDS_BEGIN

{ "ls", cmd_nvs_ls, MANY_ARGS,
  HELPK("% \"<b>ls [PATH]</>\"\r\n"
        "%\r\n"
        "% Показать список ключей в пространстве имён PATH. Если PATH не указан,\r\n"
        "% отображается содержимое текущего пространства имён.\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>ls /</>      - показать список пространств имён\r\n"
        "%   <i>ls phy</>    - показать записи пространства имён \"phy\"\r\n"
        "%   <i>ls ../phy</> - показать записи пространства имён \"phy\"\r\n"
        "%   <i>ls</>        - в зависимости от текущего пространства имён:\r\n"
        "%                     либо показать список ключей/значений,\r\n"
        "%                     либо список пространств имён"),
  HELPK("Показать содержимое NVS") },

{ "cd", cmd_nvs_cd, 1,
  HELPK("% \"<b>cd [<o>PATH</>]/</>\"\r\n"
        "%\r\n"
        "% Сменить текущее пространство имён. Чтобы создать новое пространство имён,\r\n"
        "% достаточно выполнить \"cd\" в него — оно будет создано автоматически.\r\n"
        "% Обратите внимание: пустые пространства имён могут быть удалены системой.\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>cd /</>    - перейти в корневое пространство имён\r\n"
        "%   <i>cd ..</>   - перейти в корневое пространство имён\r\n"
        "%   <i>cd phy</>  - перейти в пространство имён \"phy\"\r\n"
        "%   <i>cd /phy</> - перейти в пространство имён \"phy\""),
  HELPK("Сменить пространство имён") },


{ "rm", cmd_nvs_rm, 1,
  HELPK("% \"<b>rm</> <i>* | NAMESPACE | KEY</>\"\r\n"
        "%\r\n"
        "% Удаление записей NVS: пространств имён и/или пар ключ/значение\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>rm phy</>  - удалить пространство имён или ключ в текущем пространстве имён\r\n"
        "%   <i>rm /phy</> - удалить пространство имён \"phy\"\r\n"
        "%   <i>rm *</>    - удалить содержимое текущего каталога\r\n"
        "%                 выполнение этой команды в корневом каталоге (\"/\")\r\n"
        "%                 полностью очищает NVS\r\n"
        "%   <i>rm /</>    - полностью стереть всё хранилище NVS\r\n"
        ),
  HELPK("Удалить записи") },

{ "new", cmd_nvs_new, MANY_ARGS,
  HELPK("% \"<b>new</> NAME (unsigned | char | short | long )* [<o>VALUE</>]\"\r\n"
        "%\r\n"
        "% Создать новый ключ и, при необходимости, установить его значение\r\n"
        "% Также поддерживаются два специальных типа:\r\n"
        "%   \"char *\"  - создаёт строковое значение\r\n"
        "%   \"char []\" - создаёт бинарный блок данных\r\n"
        "%\r\n"
        "% Если параметр <o>VALUE</> не указан, все вновь созданные ключи\r\n"
        "% инициализируются нулём для скалярных типов\r\n"
        "% или пустым буфером для строк и бинарных данных\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>new Version char</>              - создать пустой ключ \"Version\"\r\n"
        "%   <i>new Z unsigned long long</>      - 64-битный беззнаковый ключ \"Z\"\r\n"
        "%   <i>new Var2 char * \"Hello World\"</> - создать и установить строковое значение\r\n"
        "%   <i>new Var3 char[]</>               - создать пустой бинарный блок\r\n"
        "%   <i>new Var4 char[] \\10\\20\\30</>   - создать и установить бинарное значение"
        ),
  HELPK("Создать ключи") },


{ "set", cmd_nvs_set, MANY_ARGS,          // TEXT can be quoted (1 arg) or as is (many args)
  HELPK(
        "% \"<b>set KEY TEXT</>\"\r\n"
        "%\r\n"
        "% Установить новое значение для ключа KEY. Числовые значения вводятся напрямую.\r\n"
        "% Строки и бинарные данные могут задаваться либо ASCII-текстом,\r\n"
        "% либо с использованием специальных escape-последовательностей \\AB,\r\n"
        "% где AB — байт со значением 0xAB (см. примеры ниже).\r\n"
        "%\r\n"
        "% Обратите внимание: новые записи можно создавать командой \"new\".\r\n"
        "% Для просмотра существующих ключей используйте команду \"ls\".\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>set Name 10</>                   - установить целочисленное значение\r\n"
        "%   <i>set Name -234567810</>           - установить целочисленное значение\r\n"
        "%   <i>set Str \"Text with  spaces\"</> - установить строку\r\n"
        "%   <i>set Str Some other text</>       - установить строку\r\n"
        "%   <i>set Str \"\"</>                   - установить пустую строку\r\n"
        "%   <i>set Blob \\12\\bb\\fd\\55\\44\\55</> - установить бинарные данные\r\n"
      ),
  HELPK("Установить новое значение") },

{ "dump", cmd_nvs_dump, 1,
  HELPK("% \"<b>dump KEY</>\"\r\n"
        "%\r\n"
        "% Показать бинарные данные и длинные строки,\r\n"
        "% для которых вывода команды \"ls\" недостаточно\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>dump Blob</>   - вывести содержимое ключа \"Blob\" (бинарные данные)\r\n"
        "%   <i>dump My_Str</> - вывести содержимое ключа \"My_Str\" (строка с \\0 в конце)\r\n"
        ),
  HELPK("Показать бинарные данные") },

#if WITH_FS
{ "export", cmd_nvs_export, 2,
  HELPK("% \"<b>export [NVS_PATH] FILE_PATH</>\"\r\n"
        "%\r\n"
        "% Экспортировать весь раздел NVS или отдельные пространства имён в текстовый файл\r\n"
        "% Для сохранения файлов файловая система должна быть смонтирована;\r\n"
        "% данные дописываются в конец файла, а не перезаписывают его!\r\n"
        "% NVS_PATH и FILE_PATH могут быть относительными или абсолютными (начинаться с \"/\")\r\n"
        "% Используйте \"*\" или \".\" для указания текущего пространства имён\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>export / /ffat/nvs.txt</>        - экспортировать все записи NVS в текстовый файл\r\n"
        "%   <i>export phy /ffat/nvs_phy.txt</>  - экспортировать записи NVS из пространства имён \"phy\"\r\n"
        "%   <i>export nvs_phy.txt</>            - экспортировать текущее пространство имён NVS в nvs_phy.csv\r\n"
        "%   <i>export . test.txt</>             - то же самое, что выше\r\n"
        "%   <i>export * ../test.txt</>          - то же самое, но путь к файлу относительный"
        ),
  HELPK("Экспорт NVS") },

  { "export", cmd_nvs_export, 1, HIDDEN_KEYWORD },

{ "import", cmd_exec, 1,
  HELPK("% \"<b>import FILE_PATH</>\"\r\n"
        "%\r\n"
        "% Импортировать записи NVS из текстового файла (созданного командой \"export\")\r\n"
        "% Для чтения файлов файловая система должна быть смонтирована\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>import /ffat/nvs.txt</> - импортировать из /ffat/nvs.txt\r\n"
        "%   <i>import nvs.txt</>       - импортировать из $CWD/nvs.txt\r\n"
        ),
  HELPK("Импорт NVS") },
#endif //WITH_FS
  KEYWORDS_END
};
KEYWORDS_REG(nvs);
#endif //WITH_NVS

// root directory commands
// These commands are available immediately after espshell startup, they are also available inside of
// of any other subderictory
//
KEYWORDS_DECL(main) {  

  KEYWORDS_BEGIN
#if WITH_TIME  
{ "time", cmd_time, MANY_ARGS,
  HELPK(
        "% \"<b>time</>\" <i>set</> (<i>YEAR</>|<i>MONTH</>|<i>DAY</>|<i>TIME</>|<b>am|pm</>)*\r\n"
        "%\r\n"
        "% Установить или скорректировать системное время.\r\n"
        "% Порядок аргументов не имеет значения; пропущенные значения остаются без изменений.\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>time set 2025 april</>     : изменить год и месяц\r\n"
        "%   <i>time set 20 sep 11:20</>   : установить день, месяц и время\r\n"
        "%   <i>time set 1:2:23 am 2025</> : установить время и год (12-часовой формат)\r\n"
        "%   <i>time set 1</>              : установить день месяца\r\n"
        "%   <i>time set 2025</>           : установить год\r\n"
      ),
  HELPK("Установка системного времени") },

{ "time", HELP_ONLY,
  HELPK(
        "% \"<b>time</>\" <i>zone TIMESPEC</>|none\r\n"
        "%\r\n"
        "% Установить часовой пояс (смещение времени) или сбросить его к значению по умолчанию.\r\n"
        "% TIMESPEC состоит из чисел и спецификаторов времени:\r\n"
        "% например: \"+7 hours 5 minutes\" или \"-45 minutes 5\"\r\n"
        "% Добавление знака минус к любому компоненту делает всё смещение отрицательным.\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>time zone 1</>             : часовой пояс +0100 UTC\r\n"
        "%   <i>time zone -1 hour 45 min</>: часовой пояс -0145 UTC\r\n"
        "%   <i>time zone none</>          : без смещения времени"
  ), NULL },

#endif //WITH_TIME
{ "uptime", cmd_uptime, NO_ARGS,
  HELPK("% \"<b>uptime</>\"\r\n"
        "%\r\n"
        "% Отображает время, прошедшее с последней загрузки, причину перезагрузки,\r\n"
        "% счётчик загрузок прошивки, количество переходов в sleep и т.п.\r\n"
        ), 
  HELPK("Время работы системы") },

// cpu FREQ
{ "cpu", cmd_cpu, 1,
  HELPK("% \"<b>cpu</> <i>NUMBER</>\"\r\n"
        "%\r\n"
        "% Установить частоту CPU в NUMBER МГц\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>cpu 160</> - установить частоту CPU в 160 МГц"
        ),
  HELPK("Установка/просмотр частоты CPU") },

// cpu
{ "cpu", cmd_cpu, NO_ARGS,
  HELPK("% \"<b>cpu</>\"\r\n"
        "%\r\n"
        "% Показать поддерживаемые частоты CPU"), NULL },

{ "suspend", cmd_suspend, NO_ARGS,
  HELPK("% \"<b>suspend</>\"\r\n"
        "%\r\n"
        "% Приостановить выполнение sketch'а (горячая клавиша: Ctrl+C).\r\n"
        "% Возобновить выполнение командой \"resume\""),
  HELPK("Приостановка выполнения задач") },

{ "suspend", cmd_suspend, 1,
  HELPK("% \"<b>suspend</> <i>TASK_ID|TASK_NAME</>\"\r\n"
        "%\r\n"
        "% Приостановить выполнение произвольной задачи FreeRTOS.\r\n"
        "% Задача может быть указана по имени или ID (ID можно получить\r\n"
        "% с помощью команды \"show tasks\")."),
  NULL },

{ "priority", cmd_priority, 1,
  HELPK("% \"<b>priority</> <i>NUM <o>[TASK_ID | TASK_NAME]</>\"\r\n"
        "%\r\n"
        "% Изменить приоритет произвольной задачи FreeRTOS\r\n"
        "% Задача указывается по имени или ID (ID можно получить\r\n"
        "% с помощью команды \"show tasks\").\r\n"
        "% Если TASK_ID / TASK_NAME не указан, изменяется приоритет <u>текущей задачи</>.\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>priority 10</>              - установить приоритет вызывающей задачи в 10\r\n"
        "%   <i>priority 10 esp_timer</>    - установить приоритет системной задачи esp_timer\r\n"
        "%   <i>priority 10 \"Tmr Svc\"</>   - задача с пробелом в имени\r\n"
        "%   <i>priority 10 0x45652431</>   - установить приоритет задачи с ID 0x45652431\r\n"
       ),
  HELPK("Изменение приоритета задачи") },

  { "priority", cmd_priority, 2, HIDDEN_KEYWORD },

{ "resume", cmd_resume, NO_ARGS,
  HELPK("% \"<b>resume</>\"\r\n"
        "%\r\n"
        "% Возобновить выполнение скетча"),
  HELPK("Возобновить выполнение скетча/задачи") },

{ "resume", cmd_resume, 1,
  HELPK("% \"<b>resume <i>TASK_ID | TASK_NAME</>\"\r\n"
        "%\r\n"
        "% Возобновить выполнение произвольной задачи FreeRTOS\r\n"
        "% Задача указывается по имени или ID (ID можно получить\r\n"
        "% с помощью команды \"show tasks\")."
        ),
  NULL },

{ "kill", cmd_kill, 2,
  HELPK("% \"<b>kill <o>[-term|-kill|-9|-15] <i>TASK_ID | TASK_NAME</>\"\r\n"
        "%\r\n"
        "% Отправить задаче сигнал <i>TERMinate</> или принудительно завершить её\r\n"
        "% Если используется опция <i>-9</>, <i>-kill</> или <i>-k</>, задача удаляется (небезопасно):\r\n"
        "% используйте эти опции только для задач, которые невозможно остановить иначе\r\n"
        "% (например, системных задач вроде esp_timer или ipc0).\r\n"
        "%\r\n"
        "% Без опций, либо с <i>-term</>, <i>-t</> или <i>-15</>:\r\n"
        "% запрашивается корректное завершение задачи (безопасно):\r\n"
        "% это способ по умолчанию и предпочтительный вариант остановки.\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>kill 0x3fff0000</>    - корректное завершение задачи (через уведомления)\r\n"
        "%   <i>kill pin</>           - корректное завершение фоновой команды espshell \"pin\"\r\n"
        "%   <i>kill -9 loopTask</>   - принудительное удаление задачи Arduino loop()\r\n"
        "%   <i>kill -k 0x3fff0000</> - принудительное завершение задачи (удаление задачи)"
        ),
  HELPK("Завершение задач") },

  { "kill", cmd_kill, 1, HIDDEN_KEYWORD },

{ "reload", cmd_reload, NO_ARGS,
  HELPK("% \"<b>reload</>\"\r\n"
        "%\r\n"
        "% Перезапускает CPU, выполняет программную перезагрузку"),
  HELPK("Перезагрузка CPU") },

{ "nap", cmd_nap, NO_ARGS,
  HELPK("% \"<b>nap</> [<o>deep</>]\"\r\n"
        "%\r\n"
        "% Переводит CPU в режим лёгкого или глубокого сна\r\n"
        "% Возобновление работы происходит по заданному будильнику\r\n"
        "% (см. <i>\"nap alarm\"</>)."
        ),
  HELPK("Сон CPU и будильники") },

  { "nap", cmd_nap, 1, HIDDEN_KEYWORD },

{ "nap", cmd_nap_alarm, MANY_ARGS,
  HELPK("% \"<b>nap alarm</> <i>uart</> NUM [<o>THRESHOLD</>]\"\r\n"
        "% \"<b>nap alarm</> TIMESPEC\"\r\n"
        "% \"<b>nap alarm</> <i>low</>|<i>high</> NUM1 [ NUM2 NUM3 .. NUMn]\"\r\n"
        "% \"<b>nap alarm</> <i>disable-all</>\"\r\n"
        "%\r\n"
        "% Настройка или сброс источников пробуждения CPU и/или длительности сна\r\n"
        "% Обратите внимание: можно настроить несколько источников пробуждения,\r\n"
        "% запуская эту команду несколько раз (например, пробуждение по UART + таймеру).\r\n"
        "% Чтобы отключить все источники пробуждения, используйте \"disable-all\".\r\n"
        "%\r\n"
        "% TIMESPEC — строка, задающая интервал времени, состоящий из дней,\r\n"
        "% часов, минут, секунд и миллисекунд:\r\n"
        "%   <i>4 days 1 hour 666 minutes ...</>\r\n"
        "\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>nap alarm dis</>       : Отключить все будильники\r\n"
        "%   <i>nap alarm uart 1</>    : Пробуждение по UART, 3 положительных фронта на линии RX\r\n"
        "%   <i>nap alarm uart 1 10</> : Пробуждение по UART, порог RX — 10 положительных фронтов\r\n"
        "%   <i>nap alarm 1 day 1 hour 6 sec</> : пробуждение через 25 часов и 6 секунд\r\n"
        "%   <i>nap alarm low 1</>     : Пробуждение, если GPIO#1 в состоянии LOW (по уровню)\r\n"
        "%   <i>nap alarm high 2</>    : Пробуждение, если GPIO#2 в состоянии HIGH (по уровню)\r\n"
#if CONFIG_IDF_TARGET_ESP32
        "%   <i>nap alarm low 1 2 3</> : Пробуждение, когда ВСЕ GPIO 1, 2 и 3 находятся в состоянии LOW\r\n"
#else          
        "%   <i>nap alarm low 1 2 3</> : Пробуждение, когда ЛЮБОЙ из GPIO 1, 2 или 3 находится в состоянии LOW\r\n"
#endif
        "%  <i>nap alarm high 1 2 3</> : Пробуждение, когда ЛЮБОЙ из GPIO 1, 2 или 3 находится в состоянии HIGH\r\n"
        ), NULL },

  // Interfaces (UART,I2C, RMT, FileSystem..)
{ "iic", cmd_i2c_if, 1,
  HELPK("% \"<b>iic</> <i>I2C_NUM</>\"\r\n"
        "%\r\n"
        "% Вход в режим настройки интерфейса I2C (i2c0, i2c1, ...)\r\n"
        "% I2C_NUM — номер шины [0..1], которую нужно настроить.\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>iic 0</>     - Войти в режим I2C, использовать шину I2C №0"),
  "Команды I2C" },

  // alias for iic. we need it for is_subdirectory()
  { "i2c", cmd_i2c_if, 1, HIDDEN_KEYWORD },
   

#if WITH_ALIAS
{ "alias", cmd_alias_if, 1,
  HELPK("% \"<b>alias</> <i>NAME</>\"\r\n"
        "%\r\n"
        "% Создать именованный псевдоним команды и войти в режим его редактирования\r\n"
        "% Каждая команда, введённая в режиме редактирования alias, просто записывается в этот alias.\r\n"
        "% Для выхода из режима редактирования alias используйте команду \"quit\"\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>alias Motor_On</> - Создать alias с именем \"Motor_On\""),
  HELPK("Псевдонимы команд") },
#endif


#if WITH_SPI
#  warning "SPI submodule is barely functional and is under development now"
{ "spi", cmd_spi_if, 1,
  HELPK("% \"<b>spi</> [<o>fspi|hspi|vspi</>]\" \r\n"
        "%\r\n"
        "% Войти в режим конфигурации интерфейса SPI \r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>spi vspi</> - настроить/использовать интерфейс SPI3 (VSPI)"),
  HELPK("Команды SPI") },
#endif

{ "uart", cmd_uart_if, 1,
  HELPK("% \"<b>uart</> <i>UART_NUM</>\"\r\n"
        "%\r\n"
        "% Войти в режим конфигурации интерфейса UART (uart0, uart1 и uart2)\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>uart 0</>     - Войти в режим UART, использовать uart0"
      ),
  HELPK("Команды UART") },


{ "sequence", cmd_seq_if, 1,
  HELPK("% \"<b>sequence</> <i>NUM</>\"\r\n"
        "%\r\n"
        "% Создать или настроить импульсную последовательность, которую затем можно\r\n"
        "% воспроизвести на любом GPIO.\r\n"
        "% NUM — номер последовательности в диапазоне [0.." xstr(SEQUENCES_NUM) "].\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>sequence 5</>     - Войти в режим редактирования последовательности (Seq#5)"
        ),
  HELPK("Настройка импульсных последовательностей") },
    

#if WITH_FS
{ "files", cmd_files_if, NO_ARGS,
  HELPK("% \"<b>files</>\"\r\n"
        "%\r\n"
        "% Войти в режим работы с файлами и файловыми системами"
        ),
  HELPK("Доступ к файловой системе") },
#endif

#if WITH_NVS
{ "nvs", cmd_nvs_if, MANY_ARGS,
  HELPK("% \"<b>nvs [PARTITION_NAME]</>\"\r\n"
        "%\r\n"
        "% Войти в редактор/просмотрщик NVS (Non-Volatile Storage).\r\n"
        "% Если PARTITION_NAME не указан, используется NVS-раздел по умолчанию.\r\n"
        "%\r\n"
        "% <u>Examples:</u>\r\n"
        "%   <i>nvs</>           - открыть NVS по умолчанию\r\n"
        "%   <i>nvs CustomNVS</> - открыть NVS в разделе \"CustomNVS\"\r\n"
        ),
  HELPK("Доступ к NVS (энергонезависимое хранилище)") },
#endif

  // "show iomux" goes first, to define a /.brief/ for subsequent entries.
  // There is MANY_ARGS modifier even though number of arguments is known and is 1: this is just to route all "show ARG1 ARG2 ... ARGn"
  // commands to cmd_show(). It simply means that this command always matches any command starting with "show ".
  //
  // Other "show" commands below are lacking a command handler pointer and are here just for help lines
  // 
{ "show", cmd_show, MANY_ARGS,              // <--- этот обработчик ловит все варианты команды "show"
  HELPK("% \"<b>show <i>iomux</>\"\r\n"
        "%\r\n"
        "% Показать доступные функции IO_MUX для каждого пина\r\n"
        "% Также отображается текущая IO_MUX-функция, назначенная каждому пину"),
  "Отображение системной информации" },

// Записи ниже используются только для /полной/ строки помощи
// (/краткая/ строка копируется из первой записи \"show\")
#if WITH_WIFI
{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>wifi ap|sta</> NUM\"\r\n"
        "%\r\n"
        "% Показать параметры WiFi-интерфейса (AP или STA)"),
  NULL },
#endif

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>nap</> NUM\"\r\n"
        "%\r\n"
        "% Показать параметры сна CPU"),
  NULL },

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>uart</> NUM\"\r\n"
        "%\r\n"
        "% Показать настройки и параметры UART"),
  NULL },

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>tasks</>\"\r\n"
        "%\r\n"
        "% Показать ID задач FreeRTOS и дополнительную информацию\r\n"
        "% Эти ID можно использовать в командах \"kill\", \"suspend\" и других"),
  NULL },

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>cpuid</>\"\r\n"
        "%\r\n"
        "% Показать информацию о CPU, версиях используемых компонентов и uptime\r\n"
        "% Также отображается температура CPU в градусах Цельсия"),
  NULL },

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>pwm</>\"\r\n"
        "%\r\n"
        "% Показать активные PWM-генераторы:\r\n"
        "% номер GPIO, частота и скважность"),
  NULL },

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>pin</> ARG1 ARG2 .. ARGn\"\r\n"
        "%\r\n"
        "% Показать информацию о пинах ARG1, ARG2, ..., ARGn\r\n"
        "% одновременно"),
  NULL },

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>counters</>\"\r\n"
        "%\r\n"
        "% Состояние и значения счетчиков импульсов / измерителей частоты\r\n"
        "% В зависимости от используемого SoC может быть до 8 аппаратных счетчиков"),
  NULL },

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>sequence</> NUMBER\"\r\n"
        "%\r\n"
        "% Показать конфигурацию последовательности с указанным номером:\r\n"
        "% \"show sequence 6\"  - показать конфигурацию последовательности №6"),
  NULL },

#if WITH_FS
{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>mount</> [<o>/PATH</>]\"\r\n"
        "%\r\n"
        "% Показать информацию о смонтированных файловых системах и разделах\r\n"
        "% \"show mount\"           - показать информацию о файловых системах\r\n"
        "% \"show mount /my_disk\"  - показать информацию о точке монтирования \"/my_disk\""),
  NULL },
#endif

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>memory</>\"\r\n"
        "%\r\n"
        "% Показать информацию о HEAP и доступной памяти"),
  NULL },

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>memory</> <i>ADDRESS</> [<o>COUNT</>] "
        "[<o>unsigned|signed|char|int|short|float|void *</>]\"\r\n"
        "%\r\n"
        "% Показать COUNT элементов, начиная с адреса памяти ADDRESS\r\n"
        "% Можно указать тип данных (например: "
        "\"show mem 0x3fff0000 10 unsigned int\"),\r\n"
        "% чтобы подсказать espshell, как отображать отдельные элементы данных.\r\n"
        "% Тип данных по умолчанию — \"unsigned char\"\r\n"
        "% Адрес может быть задан в десятичном или шестнадцатеричном виде\r\n"
        "% (с префиксом \"0x\" или без него)\r\n"
        "%\r\n"
        "% COUNT является необязательным параметром, значение по умолчанию — 256 байт.\r\n"
        "% Может задаваться в десятичном или шестнадцатеричном виде\r\n"
        "% ПРИМЕЧАНИЕ: если указан тип данных, а COUNT не задан,\r\n"
        "% то COUNT по умолчанию равен 1"),
  NULL},


#if WITH_ESPCAM
{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>camera</> <i>models | resolutions | settings | sensor</>\"\r\n"
        "%\r\n"
        "% <i>models</>     : Показать список поддерживаемых плат\r\n"
        "% <i>resolutions</>: Показать список поддерживаемых разрешений\r\n"
        "% <i>settings</>   : Показать текущие настройки камеры\r\n"
        "% <i>sensor</>     : Показать низкоуровневую информацию о сенсоре камеры\r\n"
        "% Пример: \"show camera settings\""),
  NULL},

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>camera</> <i>pinout</> [<o>MODEL</> | <o>custom</>]\"\r\n"
        "%\r\n"
        "% Показать распиновку камеры для модели MODEL\r\n"
        "% Имя модели можно опустить — тогда будет показана текущая распиновка\r\n"
        "% В качестве имени модели можно указать \"custom\",\r\n"
        "% чтобы отобразить пользовательскую распиновку камеры"),
  NULL},
#endif

#if WITH_ALIAS
{ "show", HELP_ONLY,
  HELPK("% \"<b>show</> <i>alias</> [<o>ALIAS_NAME</>]\"\r\n"
        "%\r\n"
        "% \"show alias\"      - Показать список настроенных алиасов\r\n"
        "% \"show alias NAME\" - Показать содержимое алиаса NAME"),
  NULL},

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>if</> [<o>NUM</>]\"\r\n"
        "%\r\n"
        "% Показать краткий список условий \"if\"\r\n"
        "% При указании ID условия отображаются подробности"),
  NULL},

{ "show", HELP_ONLY,
  HELPK("% \"<b>show <i>every</> [<o>NUM</>]\"\r\n"
        "%\r\n"
        "% Показать краткий список условий \"every\"\r\n"
        "% При указании ID условия отображаются подробности"),
  NULL},
#endif

  { "hostid", cmd_hostid, MANY_ARGS, HIDDEN_KEYWORD },

  // Switch espshell's input to another UART
  { "tty", cmd_tty, MANY_ARGS, HIDDEN_KEYWORD },

  { "echo", cmd_echo, NO_ARGS, HIDDEN_KEYWORD },  //hidden command, displays echo status

{ "echo", cmd_echo, MANY_ARGS,
  HELPK("% \"<b>echo</> <i>on | off | silent</>\"\r\n"
        "% \"<b>echo</> [<o>-n</>] TEXT\"\r\n"
        "% \"<b>echo</>\"\r\n"
        "%\r\n"
        "% Управление эхо пользовательского ввода и выводом (по умолчанию: on)\r\n"
        "% При вызове без аргументов отображает текущее состояние эхо.\r\n"
        "% Также может использоваться для вывода текста TEXT.\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%  <i>echo on</>               : включить эхо пользовательского ввода\r\n"
        "%  <i>echo off</>              : выключить эхо пользовательского ввода\r\n"
        "%  <i>echo silent</>           : отключить весь вывод espshell\r\n"
        "%  <i>echo Hello, World!</>    : вывести \"Hello, World!\\r\\n\"\r\n"
        "%  <i>echo -n Hello, World!</> : вывести \"Hello, World!\""),
  HELPK("Включение/отключение эхо пользовательского ввода") },

  

  // 0 and 1 arg "pin" commands are declared first, because cmd_pin() has MANY_ARGS and will match any pin command otherwhise
{ "pin", cmd_pin, NO_ARGS,
  HELPK("% \"<b>pin</>\"\r\n"
        "%\r\n"
        "% Отобразить доступные/зарезервированные пины и общую информацию о GPIO\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>pin</> - показать доступность GPIO"),
  HELPK("Команды GPIO") },

{ "pin", cmd_show_pin, 1,
  HELPK("% \"<b>pin</> <i>PIN_NUM</>\"\r\n"
        "%\r\n"
        "% Показать конфигурацию GPIO и его цифровое значение для PIN_NUM\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>pin 2</> - показать информацию о GPIO2"),
  NULL },

{ "pin", cmd_pin, MANY_ARGS,
  HELPK("% \"<b>pin</> <i>PIN_NUM</> [<o>ARG1 | ARG2 | ... | ARGn]*</>\"\r\n"
        "%\r\n"
        "% Управление состоянием пина (GPIO): конфигурация, уровень, маршрутизация сигналов и т.п.\r\n"
        "% Принимает список ключевых слов (или одно ключевое слово):\r\n"
        "%\r\n"
        "% \"<i>high</>\" & \"<i>low</>\"       - установить уровень пина в 1 или 0\r\n"
        "% \"<i>up</>\" & \"<i>down</>\"        - включить PULL_UP или PULL_DOWN\r\n"
        "% \"<i>out</>\", \"<i>in</>\", \"<i>open</>\"  - режим OUTPUT, INPUT или OPEN_DRAIN\r\n"
        "% \"<i>save</>\" & \"<i>load</>\"      - сохранить / восстановить конфигурацию пина\r\n"
        "% \"<i>read</>\" & \"<i>aread</>\"     - цифровое или аналоговое чтение\r\n"
        "% \"<i>pwm</>\"                - включить PWM-сигнал на пине\r\n"
        "% \"<i>sequence</>\"           - отправить RMT-последовательность (ИК-пульт)\r\n"
        "% \"<i>matrix</>\" & \"<i>iomux</>\"   - функции GPIO Matrix и IO_MUX\r\n"
        "% \"<i>hold</>\" & \"<i>release</>\"   - зафиксировать / освободить состояние и уровень пина\r\n"
        "% \"<i>delay</>\"              - задержка перед выполнением следующего ключевого слова\r\n"
        "% \"<i>NUMBER</>\"             - переключиться на другой пин; последующие аргументы применяются к нему\r\n"
        "% \"<i>loop</>\"               - выполнить всю команду \"pin\" несколько раз\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%\r\n"
        "%  <i>pin 1 read</>               - GPIO1: прочитать цифровое значение\r\n"
        "%  <i>pin 1 read 2 read</>        - GPIO1 и GPIO2: прочитать цифровые значения\r\n"
        "%  <i>pin 1 read aread</>         - GPIO1: цифровое чтение, затем аналоговое\r\n"
        "%  <i>pin 1 in out up</>          - GPIO1 в режиме INPUT и OUTPUT с PULLUP\r\n"
        "%  <i>pin 1 save high load</>     - сохранить состояние, установить HIGH(1), восстановить состояние\r\n"
        "%  <i>pin 1 high</>               - установить GPIO1 в логическую \"1\"\r\n"
        "%  <i>pin 1 high delay 100 low</> - установить \"1\", через 100 мс — \"0\"\r\n"
        "%  <i>pin 1 pwm 5000 0.3</>       - меандр 5 кГц с заполнением 30%%\r\n"
        "%  <i>pin 1 pwm 0 0</>            - отключить PWM на GPIO1\r\n"
        "%  <i>pin 1 low high loop inf</>  - генерация меандра с максимальной скоростью\r\n"
        "%  <i>pin 1 hi del 500 lo del</> 500 loop 10 - мигание светодиода 10 раз\r\n"
        "%  <i>pin 1 matrix in 33</>       - GPIO1 — источник периферийного сигнала №33\r\n"
        "%  <i>pin 1 matrix</>             - сброс подключений GPIO Matrix для пина"
        ),
  NULL },

  // PWM generation
{ "pwm", cmd_pwm, 4,
  HELPK("% \"<b>pwm <i>PIN</> [<o>FREQ</> [<o>DUTY</> [<o>CHANNEL</>] ] ]\"\r\n"
        "% \"<b>pwm <i>PIN off</>\"\r\n"
        "%\r\n"
        "% Запуск или остановка PWM-генератора на пине PIN с частотой FREQ (Гц)\r\n"
        "% и коэффициентом заполнения DUTY.\r\n"
        "% Ключевые слова \"<b>off</>\" или \"0\" останавливают PWM-выход.\r\n"
        "% FREQ задаётся в диапазоне [0 .. " xstr(PWM_MAX_FREQUENCY) "] Гц.\r\n"
        "%\r\n"
        "% Параметр DUTY необязателен (по умолчанию 0.5 = 50%%); допустимый диапазон [0.00 .. 1.00].\r\n"
        "% Разрешение по заполнению выбирается автоматически, но может быть переопределено\r\n"
        "% с помощью \"var ledc_res BITS\".\r\n"
        "%\r\n"
        "% Параметр CHANNEL необязателен и задаёт аппаратный PWM-канал\r\n"
        "% (0..15 для ESP32 или 0..7 для ESP32-S3).\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>pwm 2 1000</>       - включить PWM 1 кГц с заполнением 50%% на пине 2\r\n"
        "%   <i>pwm 2 100 0.15</>   - включить PWM 100 Гц с заполнением 15%% на пине 2\r\n"
        "%   <i>pwm 2 100 0.15 4</> - 100 Гц, 15%%, пин 2, аппаратный канал 4\r\n"
        "%   <i>pwm 2</>            - отключить PWM на пине 2\r\n"
        "%   <i>pwm 2 0</>          - то же самое\r\n"
        "%   <i>pwm 2 off</>        - то же самое"),
  HELPK("PWM-выход") },

  { "pwm", cmd_pwm, 3, HIDDEN_KEYWORD }, // pwm PIN FREQ DUTY
  { "pwm", cmd_pwm, 2, HIDDEN_KEYWORD }, // pwm PIN FREQ, pwm PIN off, pwm PIN 0
  { "pwm", cmd_pwm, 1, HIDDEN_KEYWORD }, // pwm PIN

  // Pulse counting/frequency meter
{ "count", cmd_count, MANY_ARGS,
  HELPK("% \"<b>count <i>PIN</> [<o>NUMBER</>| <o>infinite</> | <o>trigger</> | <o>filter LENGTH</>]*\"\r\n"
        "%\r\n"
        "% Подсчёт импульсов на пине PIN в течение NUMBER миллисекунд\r\n"
        "% (по умолчанию — 1 секунда).\r\n"
        "% Ключевое слово <i>infinite</> делает время измерения очень большим\r\n"
        "% (практически бесконечным).\r\n"
        "% Необязательный параметр \"trigger\" приостанавливает подсчёт до появления\r\n"
        "% первого импульса.\r\n"
        "% Необязательный параметр \"filter LEN\" игнорирует импульсы,\r\n"
        "% <u>короче чем</> LEN наносекунд.\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>count 4</>             - подсчёт импульсов и измерение частоты на GPIO4 в течение 1000 мс\r\n"
        "%   <i>count 4 2000</>        - то же самое, но время измерения — 2 секунды\r\n"
        "%   <i>count 4 filter 100</>  - подсчёт импульсов с игнорированием импульсов <u>короче</> 100 нс\r\n"
        "%   <i>count 4 infinite &</>  - непрерывный подсчёт импульсов в <u>фоновом</u> режиме\r\n"
        "%   <i>count 4 trigger</>     - ожидание первого импульса, затем начало подсчёта\r\n"
        "%   <i>count 4 trigger 2000 filter 300 &</> - ожидание первого импульса, затем подсчёт\r\n"
        "%     импульсов в течение 2 секунд в фоновом режиме с игнорированием импульсов\r\n"
        "%     короче 300 нс\r\n"
        "%   <i>count 4 2000 filter 300 trig &</>"),
  HELPK("Счётчик импульсов") },

{ "count", HELP_ONLY,
  HELPK("% \"<b>count <i>PIN</> <i>clear</>\"\r\n"
        "%\r\n"
        "% Очистить счётчики, связанные с пином PIN.\r\n"
        "% Счётчики могут быть остановлены, запущены или находиться в состоянии \"trigger\".\r\n"
        "% <u>Пример:</>\r\n"
        "%   <i>count 4 clear</> - очистить все счётчики, связанные с GPIO4\r\n"
        ),
  NULL },
    
#if WITH_ESPCAM
{ "camera", cmd_camera_if, MANY_ARGS, 
  HELPK("% \"<b>camera</>\"\r\n"
        "%\r\n"
        "% Войти в режим настройки камеры"),
  HELPK("Команды камеры") },

#endif //WITH_ESPCAM
  
{ "var", cmd_var, 2,
  HELPK("% \"<b>var</> <i>ИМЯ_ПЕРЕМЕННОЙ</> [<o>НОВОЕ_ЗНАЧЕНИЕ</>]\"\r\n"
        "%\r\n"
        "%  a. Показать значение переменной скетча\r\n"
        "%  b. Установить новое значение переменной скетча\r\n"
        "% ИМЯ_ПЕРЕМЕННОЙ — имя переменной (используйте \"var\" без аргументов, чтобы увидеть список)\r\n"
        "% НОВОЕ_ЗНАЧЕНИЕ может быть целым или вещественным числом, положительным или отрицательным\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>var button1</>     - показать текущее значение переменной скетча \"button1\"\r\n"
        "%   <i>var angle -12.3</> - установить значение переменной скетча \"angle\" в -12.3\r\n"
        "%\r\n"
        "% Примечание #1: допускается использование сокращённых имён переменных\r\n"
        "% Примечание #2: используйте префиксы \"0x\" для шестнадцатеричных,\r\n"
        "%                \"0\" для восьмеричных и \"0b\" для двоичных чисел\r\n"
  ),
  HELPK("Переменные скетча") },

{ "var", cmd_var, 1,
  HELPK("% \"<b>var</> <i>ЧИСЛО</>\"\r\n"
        "%\r\n"
        "% Отобразить ЧИСЛО в различных системах счисления и выполнить небезопасное\r\n"
        "% приведение типов в стиле C для содержимого памяти.\r\n"
        "% ЧИСЛО может быть любым значением, приводимым к числовому типу. Используйте\r\n"
        "% префиксы \"0b\", \"0x\" или \"0\" для ввода двоичных, шестнадцатеричных\r\n"
        "% или восьмеричных чисел соответственно.\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>var -1234</>     - информация о десятичном числе -1234\r\n"
        "%   <i>var 0x1234</>    - ...о шестнадцатеричном числе\r\n"
        "%   <i>var 01234</>     - ...о восьмеричном числе\r\n"
        "%   <i>var 0b1001110</> - ...о двоичном числе\r\n"
        ),
  NULL },

{ "var", cmd_var_show, NO_ARGS,
  HELPK("% \"<b>var</>\"\r\n"
        "%\r\n"
        "% Показать список переменных скетча, доступных из оболочки\r\n"
        "% Переменные должны быть зарегистрированы с помощью макроса convar_add()\r\n"
        "%\r\n"
        "% <u>Пример:</>\r\n"
        "%   <i>var</> - показать список переменных\r\n"
        ),
  NULL },
#if WITH_ALIAS

{ "if", cmd_if, MANY_ARGS,
  HELPK("% \"<b>if <i>rising|falling</> PIN [<o>low|high PIN</>]* [<o>max-exec NUM</>] [<o>rate-limit NUM</>] <i>exec</> ALIAS</>\"\r\n"
        "%\r\n"
        "% Отслеживать фронт (rising) или спад (falling) сигнала на GPIO и выполнять скрипты\r\n"
        "% Дополнительно могут быть заданы <u>условия по уровню</> других GPIO (см. примеры)\r\n"
        "%\r\n"
        "%   <i>max-exec</> NUM   : выполнить это условие не более NUM раз\r\n"
        "%   <i>rate-limit</> NUM : минимальный интервал (мс) между двумя последовательными\r\n"
        "%                          срабатываниями\r\n"
        "%   <i>exec</> ALIAS     : алиас команды \"exec\"\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "% Выполнить алиас \"Comm\" при спаде сигнала на GPIO #5\r\n"
        "%\r\n"
        "%   <i>if falling 5 exec Comm</>\r\n"
        "%\r\n"
        "% Выполнить алиас \"Comm\" при фронте сигнала на GPIO #5, когда GPIO #6 находится\r\n"
        "% в состоянии LOW, а GPIO #10 — в состоянии HIGH, но не чаще одного раза в секунду\r\n"
        "% и не более пяти раз всего:\r\n"
        "%\r\n"
        "%   <i>if rising 5 low 6 high 10 max-exec 5 rate-limit 1000 exec Comm</>\r\n"
        "%\r\n"
        "% ПРИМЕЧАНИЕ: если указанный алиас не существует, он будет создан автоматически\r\n"
        "%             (пустым)\r\n"
        ),
  HELPK("Условные события GPIO") },

{ "if", HELP_ONLY,
  HELPK("% \"<b>if <i>low|high</> PIN [<o>low|high PIN</>]* [<o>max-exec NUM</>] [<o>poll NUM</>] <i>exec</> ALIAS</>\"\r\n"
        "%\r\n"
        "% Создать условие, которое периодически проверяется (опрос, polling)\r\n"
        "% Прерывания не используются; максимальная частота срабатывания ограничена\r\n"
        "% интервалом опроса\r\n"
        "%   <i>max-exec</> NUM : выполнить это условие не более NUM раз\r\n"
        "%   <i>poll</>     NUM : интервал опроса в миллисекундах (по умолчанию: 1000 мс)\r\n"
        "%   <i>exec</>     ALIAS : алиас, который будет выполнен\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>if low 5 poll 10000 exec Comm</> : если GPIO 5 в состоянии LOW (проверка каждые 10 с)\r\n"
        "%   <i>if low 5 high 10 high 11 exec Comm</> : если GPIO 5 — LOW и GPIO 10/11 — HIGH\r\n"
        "%   <i>if low 5 max-exec 5 poll 1 exec Comm</> : выполнить до пяти раз, опрос каждые 1 мс\r\n"
        ),
  NULL },

{ "if", HELP_ONLY,
  HELPK("% \"<b>if delete NUM | all | poll | gpio NUM</>\"\r\n"
        "% \"<b>every delete NUM | all</>\"\r\n"
        "%\r\n"
        "% Удалить условие \"if\" или \"every\": по его ID, по номеру GPIO,\r\n"
        "% только условия с опросом (poll) или все сразу.\r\n"
        "% (ПРИМЕЧАНИЕ: используйте \"show ifs\", чтобы вывести список всех условий\r\n"
        "% и узнать их ID)\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <b>if delete <i>6</></>      : удалить условие №6\r\n"
        "%   <b>if delete <i>all</></>    : удалить все условия (all — значит ВСЕ)\r\n"
        "%   <b>if delete <i>gpio 7</></> : удалить условия rising/falling, привязанные к GPIO 7\r\n"
        "%   <b>if delete <i>poll</></>   : удалить все условия с ключевым словом \"poll\"\r\n"
        "%   <b>every delete <i>8</></>   : удалить условие every №8\r\n"
        ),
  NULL },

{ "if", HELP_ONLY,
  HELPK("% \"<b>if clear NUM | all | poll | gpio NUM</>\"\r\n"
        "% \"<b>every clear NUM | all</>\"\r\n"
        "%\r\n"
        "% Очистить счётчики (количество срабатываний и временные метки)\r\n"
        "% для указанного условия \"if\" или \"every\"\r\n"
        "% Очистка возможна по ID условия, по номеру GPIO,\r\n"
        "% только для условий с опросом (poll) или для всех сразу\r\n"
        "% Основное назначение — сброс условий, достигших лимита \"max-exec\"\r\n"
        "%\r\n"
        "% Используйте \"<i>show ifs</>\", чтобы посмотреть список всех условий\r\n"
        "% и узнать их ID\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>if clear 6</>        : очистить счётчики условия №6\r\n"
        "%   <i>if clear all</>      : очистить счётчики всех условий (all — значит ВСЕ)\r\n"
        "%   <i>if clear gpio 7</>   : очистить счётчики условий rising/falling для GPIO#7\r\n"
        "%   <i>if clear poll</>     : очистить счётчики условий с ключевым словом \"poll\"\r\n"
        "%   <i>every clear 8</>     : очистить счётчики условия every №8\r\n"
        ),
  NULL },

{ "if", HELP_ONLY,
  HELPK("% \"<b>if <i>disable</>|<i>enable</> NUM|<i>all</>\"\r\n"
        "% \"<b>every <i>disable</>|<i>enable</> NUM|<i>all</>\"\r\n"
        "%\r\n"
        "% Включить или отключить выполнение условия \"if\" или \"every\"\r\n"
        "% по его ID\r\n"
        "% (ПРИМЕЧАНИЕ: используйте \"show ifs\", чтобы увидеть список условий\r\n"
        "% и узнать их ID)\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <b>if disable <i>6</></>    : отключить условие №6\r\n"
        "%   <b>if disable <i>all</></>  : отключить все условия\r\n"
        "%   <b>if enable <i>all</></>   : включить обработку всех условий \"if\"\r\n"
        "%   <b>every disable <i>8</></> : отключить условие every №8\r\n"
        ),
  NULL },

{ "if", HELP_ONLY,
  HELPK("% \"<b>if save <i>ID</>|<i>*</> /FILENAME</>\"\r\n"
        "% \"<b>every save <i>ID</>|<i>*</> /FILENAME</>\"\r\n"
        "%\r\n"
        "% Сохранить выражения if/every в файл\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <b>if save <i>* /ffat/test.txt</></>  : сохранить все правила \"if\"\r\n"
        "%   <b>every save <i>1 /ffat/test2</></>  : сохранить правило \"every\" №1"
        ),
  NULL },

// TODO: must use read_timespec()
{ "every", cmd_if, MANY_ARGS,
  HELPK("% \"<b>every <i>TIME</> [<o>delay MILLIS</>] [<o>max-exec NUM</>] exec <i>ALIAS</></>\"\r\n"
        "%\r\n"
        "% Периодические события\r\n"
        "% TIME задаётся числом с указанием единиц времени:\r\n"
        "% \"milliseconds|seconds|minutes|hours|days\"\r\n"
        "%\r\n"
        "%   <i>max-exec</> NUM   : выполнить это условие не более NUM раз\r\n"
        "%   <i>delay</> MILLIS  : отложить первое выполнение на указанное время\r\n"
        "%   <i>exec</> ALIAS    : алиас, который будет выполнен\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>every 1 min 5 sec exec alias my_alias</>\r\n"
        "%   <i>every 5 sec delay 50000 exec my_alias</>\r\n"
        "%   <i>every 5 sec delay 50000 max-exec 7 exec my_alias</>"
        ),
  HELPK("Периодические события") },
#endif //ALIAS
#if WITH_ALIAS || WITH_FS
{ "exec", cmd_exec, MANY_ARGS,
  HELPK("% \"<b>exec NAME [NAME NAME ... NAME]</>\"\r\n"
        "%\r\n"
        "% a) Выполнить алиас (или несколько алиасов, если указано несколько NAME)\r\n"
        "%    Алиасы — это <u>списки команд</u>; используйте \"alias\" для их создания или редактирования\r\n"
        "%\r\n"
        "% b) Выполнить shell-скрипт из файловой системы — файл с именем /NAME\r\n"
        "%    Файловая система должна быть смонтирована, а /FILE_NAME должен начинаться с \"/\"\r\n"
        "%    (путь к файлу должен быть абсолютным, отсюда ведущий \"/\")\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>exec motor_on</>                              : выполнить алиас \"motor_on\"\r\n"
        "%   <i>exec \"/ff/Desktop/New Folder/script.cfg\"</> : выполнить скрипт\r\n"
        "%   <i>exec /ff/file.txt /ff/file2.txt motor_off</>  : файлы и алиасы\r\n"
        ),
  HELPK("Выполнение скриптов и алиасов") },
#endif // WITH_FS || WITH_ALIAS

#if WITH_HISTORY
  { "history", cmd_history, 1, HIDDEN_KEYWORD },
  { "history", cmd_history, NO_ARGS, HIDDEN_KEYWORD },
#endif  

#if WITH_COLOR
  { "colors", cmd_colors, 1, HIDDEN_KEYWORD },
  { "colors", cmd_colors, NO_ARGS, HIDDEN_KEYWORD },
#endif
#if WITH_WIFI
{ "wifi", cmd_wifi_if, 1,
  HELPK("% \"<b>wifi ap|sta</>\"\r\n"
        "%\r\n"
        "% Вход в режим настройки WiFi STA или WiFi AP:\r\n"
        "%   \"<i>wifi sta</>\" — режим станции (WiFi-клиент)\r\n"
        "%   \"<i>wifi ap</>\"  — режим SoftAP (точка доступа WiFi)\r\n"
      ),
  HELPK("Команды интерфейса WiFi") },

{ "wifi", cmd_wifi_if, 2,
  HELPK("% \"<b>wifi storage flash|ram</>\"\r\n"
        "%\r\n"
        "% Переключение между RAM и FLASH для хранения конфигурации WiFi,\r\n"
        "% чтобы после перезагрузки команду \"up\" можно было вызывать без аргументов:\r\n"
        "% SSID, BSSID, пароли и т.п. будут восстановлены из FLASH.\r\n"
        "% Хранилище по умолчанию — RAM (вся конфигурация теряется после перезагрузки).\r\n"
        "%\r\n"
        "% <u>Примеры</>:\r\n"
        "%   <i>wifi storage flash</> — включить автосохранение настроек STA/AP\r\n"
        "%   <i>wifi storage ram</>   — хранить настройки только в RAM\r\n"
      ),
  NULL },

  { "wifi", cmd_wifi_if, 2, // "wifi log" is handled by cmd_wifi_if as well
    HELPK("% \"<b>wifi log [enable|disable]</>\"\r\n"
          "%\r\n"
          "% Enable or disable WiFi event display: e.g., connection, disconnection,\r\n"
          "% IP address obtained, and so on. This can be annoying, so logging is\r\n"
          "% disabled by default\r\n"
          "%\r\n"
          "% <u>Examples</>:\r\n"
          "% \"<i>wifi log enable</>\" - Enable logging"
        ),
    HELPK("WiFi interface commands") },
#endif
  // User-specific command.
  { "misc", cmd_misc, MANY_ARGS, HIDDEN_KEYWORD },
  KEYWORDS_END
};
KEYWORDS_REG(main);

#if WITH_ESPCAM
// Commands for dealing with video camera (ai-thinker, m5stack, dfrobot etc or custom)
// Handlers are implemented in espcam.h
//
KEYWORDS_DECL(camera) {

  KEYWORDS_BEGIN

{ "up", cmd_camera_up, MANY_ARGS, 
  HELPK("% \"<b>up</> [<o>MODEL | custom | clock HZ | i2c NUMBER</>]*\r\n"
        "%\r\n"
        "% Обнаружить и инициализировать камеру\n\r"
        "%\n\r"
        "% <i>MODEL</>    — модель камеры; список поддерживаемых моделей см. в \"show camera models\"\n\r"
        "%            Используйте слово \"custom\" для задания пользовательской модели камеры (см. \"pinout\")\r\n"
        "% <i>clock HZ</> — задать частоту XCLK в герцах. Значение по умолчанию — 16000000 (16 МГц)\n\r"
        "% <i>i2c NUM</>  — использовать существующий интерфейс I2C, игнорируя пины SDA и SCL\r\n%\r\n"
        "%\r\n"
        "% <u>Примеры:</>\r\n"
        "%   <i>up ai-thinker</> — инициализировать Ai-Thinker ESP32Cam\r\n"
        "%   <i>up</>            — считается, что камера уже инициализирована в скетче и используется как есть\r\n"
        "%   <i>up custom clock <i>20000000</>  — инициализировать камеру с пользовательским пин-аутом на 20 МГц"
        ),
  HELPK("Команды камеры")
},

{ "mode", cmd_camera_mode, 1,
  HELPK("% \"<b>mode</> jpeg|grayscale|555|565\"\n\r"
        "%\n\r"
        "% Установить режим захвата. Должен быть задан до команды \"up\""),
  "Режим захвата" },

{ "down", cmd_camera_down, NO_ARGS,
  HELPK("% \"<b>down</>\"\n\r"
        "%\n\r"
        "% Деинициализировать и сбросить (и/или отключить питание камеры)"),
  "Отключение камеры" },

{ "capture", cmd_camera_capture, NO_ARGS,
  HELPK("% \"<b>capture</>\"\n\r"
        "%\n\r"
        "% Захватить кадр"),
  "Захват кадра" },

{ "filesize", cmd_camera_filesize, NO_ARGS,
  HELPK("% \"<b>filesize</>\"\n\r"
        "%\n\r"
        "% Показать размер последнего захваченного кадра в байтах"
  ),
  "Размер файла кадра" },

{ "save", cmd_camera_save, 1,
  HELPK("% \"<b>save</> <i>/FILENAME</> [<o>base64 | ascii-hex</>]\"\n\r"
        "% \"<b>save</> <i>uart NUM</> [<o>base64 | ascii-hex</>]\"\n\r"
        "%\n\r"
        "% Передать последний захваченный кадр по указанному UART или сохранить его в файл\n\r"
        "% Перед сохранением файла файловая система должна быть смонтирована\n\r"
        "% Перед передачей данных UART должен быть инициализирован\r\n"
        "%\n\r"
        "% Дополнительные флаги ascii-hex и base64 используются для преобразования изображения\n\r"
        "% в текстовый ASCII-формат\r\n"
        "%   <o>ascii-hex</> — поток шестнадцатеричных байтов: \"AB07876A...\"\r\n"
        "%   <o>base64</>   — кодирование Base64, как следует из названия\r\n"
        "%\n\r"
        "% Символ доллара (\"$\") в имени файла автоматически заменяется\n\r"
        "% последовательным псевдослучайным числом\n\r"
        "% Основное назначение — автоматизация сохранения нескольких кадров\n\r"
        "% <u>Examples:</>\n\r"
        "%   <i>save</> /ffat/pix/12.jpg            : сохранить в файл\n\r"
        "%   <i>save uart</> 1                      : передать \"как есть\" по UART1\n\r"
        "%   <i>save</> /ffat/pix/file$.jpg         : file0, file1, ...\n\r"
        "%   <i>save uart</> 1 <o>ascii-hex</>      : передать, преобразовав в ASCII\n\r"
  ),
  "Сохранение / передача захваченных данных" },

{ "pinout", cmd_camera_pinout, 16,
  HELPK("% \"<b>pinout</> <o>PWDN RESET</> XCLK <o>SDA SCL</> <i>D7 D6 D5 D4 D3 D2 D1 D0 VSYNC HREF PCLK</>\r\n"
        "%\r\n"
        "% Задать пользовательскую распиновку для модели камеры \"<i>custom</>\"\r\n"
        "% В дальнейшем может использоваться командой \"camera up custom\"\n\r"
        "%\n\r"
        "% Команда требует 16 аргументов (номера GPIO): шина I2C, линии данных и т.п.\r\n"
        "% Если на плате отсутствуют или не используются некоторые сигналы,\r\n"
        "% используйте \"-\" (минус) вместо номера пина (или \"-1\" — тоже допустимо).\r\n"
        "%   <i>PWDN</>   — GPIO управления питанием камеры (LOW = включено)\r\n"
        "%   <i>RESET</>  — GPIO сброса камеры\r\n"
        "%   <i>XCLK</>   — GPIO источника высокочастотного тактового сигнала камеры\r\n"
        "%   <i>SDA, SCL</> — пины подключения интерфейса I2C камеры\r\n"
        "%   <i>D7..D0</> (или <i>Y9..Y2</>) — пины шины данных камеры\r\n"
        "%   <i>VSYNC HREF PCLK</> — GPIO стандартных CMOS-сигналов камеры\t\n"
        "%\r\n"
        "% <u>Example:</>\r\n"
        "%   <b>pinout</> <i>- - 1 2 3 4 5 6 7 8 9 10 11 12 13 14</>\r\n"
        "%\r\n"
        "% В примере выше пины PWDN и RESET не используются и заданы как \"-\"\r\n"
        "%\r\n"
        "% Эта пользовательская распиновка активируется командой <i>camera up custom</>"
  ),
  "Пользовательская распиновка камеры" },

{ "pinout", HELP_ONLY,
  HELPK("% \"<b>pinout</> <i>KEYWORD</> <o>VALUE1 VALUE2 ... VALUEn</>\"\r\n"
        "%\r\n"
        "% Задать пользовательскую распиновку для модели камеры \"<i>custom</>\"\r\n"
        "% То же самое, что и команда выше, но позволяет задавать отдельные пины\n\r"
        "% с помощью различных ключевых слов (KEYWORD):\n\r"
        "%\n\r"
        "%   <i>power PWDN RST</> : задать пины PWDN и RST. Используйте \"-\", если не применяется\r\n"
        "%   <i>xclk NUM</>       : задать номер GPIO, к которому подключен XCLK\r\n"
        "%   <i>i2c SDA SCL</>    : пины шины I2C / SCCB\r\n"
        "%   <i>data D7 D6 D5 D4 D3 D2 D1 D0</> (или <i>Y9..Y2</>) : пины шины данных\r\n"
        "%   <i>vsync NUM</>      : номер GPIO, к которому подключен VSYNC\r\n"
        "%   <i>href NUM</>       : номер GPIO, к которому подключен HREF\r\n"
        "%   <i>pclk NUM</>       : номер GPIO, к которому подключен PCLK\r\n"
        "%\r\n"
        "% <u>Examples:</>\r\n"
        "%   <b>pinout</> <i>power 32 -</> : PWDN = GPIO32, RST не подключен\r\n"
        "%   <b>pinout</> <i>i2c 10 11</>  : SDA = GPIO10, SCL = GPIO11, шина управления камерой\r\n"
        "%   <b>pinout</> <i>data 0 1 2 3 4 5 6 7</> : GPIO0..7 — шина данных\r\n"
        "% Эта пользовательская распиновка активируется командой <i>camera up custom</>"
  ), NULL },


  { "pinout", cmd_camera_pinout, 2, HIDDEN_KEYWORD }, // "pinout KEY VALUE"
  { "pinout", cmd_camera_pinout, 3, HIDDEN_KEYWORD }, // "pinout KEY VALUE1 VALUE2"
  { "pinout", cmd_camera_pinout, 9, HIDDEN_KEYWORD }, // "pinout KEY D7 D6 D5 D4 D3 D2 D1 D0"


{ "gain", cmd_camera_gain, 1, 
  HELPK("% \"<b>gain</> <i>auto</>|(0..30)\"\n\r"
        "%\r\n"
        "% Задать чувствительность камеры (авто или значение 0..30)"),
  "Усиление (Gain)"
},

{ "balance", cmd_camera_balance, 1, 
  HELPK("% <b>balance</> <i>none</>|<i>auto</>|<i>sunny</>|<i>cloudy</>|<i>office</>|<i>home</>\n\r"
        "%\r\n"
        "% Задать коррекцию баланса белого"),
  "Баланс белого"
},

{ "exposure", cmd_camera_exposure, 2, 
  HELPK("% <b>exposure</> <i>auto</> [<o>-2..2</>]\n\r"
        "%\n\r"
        "% Установить автоматическую экспозицию с дополнительной коррекцией (AE shift):\r\n"
        "% <u>Examples:</>\r\n"
        "%   <i>exposure auto</>    - включить автоэкспозицию\r\n"
        "%   <i>exposure auto -2</> - автоэкспозиция с коррекцией -2"),
  "Экспозиция"
},

{ "exposure", cmd_camera_exposure, 1, 
  HELPK("% <b>exposure</> (<i>0..1200</>)\n\r"
        "%\n\r"
        "% Установить экспозицию в ручном режиме:\r\n"
        "% <u>Example:</>\r\n"
        "%   <i>exposure 800</> - установить параметр экспозиции в 800"),
  "Экспозиция"
},

{ "brightness", cmd_camera_qbcss, 1,
  HELPK("% <b>brightness</> (<i>-2..2</>)\n\r"
        "%\n\r"
        "% Задать коррекцию яркости (гамма):\r\n"
        "% <u>Example:</>\r\n"
        "%   <i>brightness -1</> - сделать изображение темнее"),
  "Яркость"
},

{ "saturation", cmd_camera_qbcss, 1,
  HELPK("% <b>saturation</> (<i>-2..2</>)\n\r"
        "%\n\r"
        "% Задать коррекцию насыщенности (vivid colors):\r\n"
        "% <u>Example:</>\r\n"
        "%   <i>saturation 2</> - максимальная насыщенность"),
  "Насыщенность"
},

{ "contrast", cmd_camera_qbcss, 1,
  HELPK("% <b>contrast</> (<i>-2..2</>)\n\r"
        "%\n\r"
        "% Задать коррекцию контрастности:\r\n"
        "% <u>Example:</>\r\n"
        "%   <i>contrast 2</> - максимальный контраст"),
  "Контраст"
},

{ "sharpness", cmd_camera_qbcss, 1,
  HELPK("% <b>sharpness</> (<i>-2..2</>)\n\r"
        "%\n\r"
        "% Задать коррекцию резкости изображения:\r\n"
        "% <u>Example:</>\r\n"
        "%   <i>sharpness -2</> - минимальная резкость"),
  "Резкость"
},

{ "compress", cmd_camera_qbcss, 1,
  HELPK("% <b>compression</> (<i>2..63</>)\n\r"
        "%\n\r"
        "% Задать коэффициент JPEG-сжатия (2 — лучшее качество, 63 — минимальный размер)\r\n"
        "% <u>Example:</>\r\n"
        "%   <i>compression 4</> - более высокое качество, больший файл"),
  "Сжатие JPEG"
},

{ "size", cmd_camera_size, 1, 
  HELPK("% \"<b>size</> vga|svga|xga|hd|sxga|uxga...\"\n\r"
        "%\n\r"
        "% Задать размер изображения:\n\r"
        "% vga   - 640x480\n\r"
        "% svga  - 800x600\n\r"
        "% xga   - 1024x768\n\r"
        "% hd    - 1280x720\n\r"
        "% sxga  - 1280x1024\n\r"
        "% uxga  - 1600x1200\r\n"
        "% fhd   - 1920x1080\r\n"
        "% qxga  - 2048x1536\r\n"
        "% qhd   - 2560x1440\r\n"
        "% wqxga - 2560x1600\r\n"
        "% qsxga - 2560x1920\r\n"
        "% 5mp   - 2592x1944"),
  "Разрешение / размер изображения"
},

  KEYWORDS_END
};
KEYWORDS_REG(camera);
#endif // WITH_ESPCAM


static const char *Subdir_message = "%% Вход в режим %s. Ctrl+Z или \"exit\", чтобы выйти из режима\r\n"
                            "%% Основные команды доступны, хоть и не отображаются по \"?\"\r\n";

static const char *Exit_message =   "% Не в каталоге команд; (чтобы закрыть шелл, введите \"exit ex\")\r\n";


