/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#if COMPILING_ESPSHELL

// Shell commands handlers.
//
// These are called by espshell_command() processor in order to execute commands. Function names are pretty selfdescriptive:
// command handler function names always start with "cmd_" and then follows with either command name (e.g. cmd_pin) or command 
// directory + command name (e.g. cmd_files_write()).
//
// Handlers have access to user input via argc/argv mechanism. Return value is 0 if it was success, or contains an index of a failed
// argument. return value of -1 means "not enough/too many arguments"

#if WITH_ESPCAM
static int cmd_cam(int, char **);
#endif

//i2c commands
static int cmd_i2c_if(int, char **);
static int cmd_i2c_clock(int, char **);
static int cmd_i2c_up(int, char **);
static int cmd_i2c_down(int, char **);
static int cmd_i2c_read(int, char **);
static int cmd_i2c_write(int, char **);
static int cmd_i2c_scan(int, char **);
#if WITH_SPI
// spi commands
static int cmd_spi_if(int, char **);
static int cmd_spi_clock(int, char **);
static int cmd_spi_up(int, char **);
static int cmd_spi_down(int, char **);
static int cmd_spi_write(int, char **);
#endif //WITH_SPI
// uart commands
static int cmd_uart_if(int, char **);
static int cmd_uart_baud(int, char **);
static int cmd_uart_tap(int, char **);
static int cmd_uart_up(int, char **);
static int cmd_uart_down(int, char **);
static int cmd_uart_read(int, char **);
static int cmd_uart_write(int, char **);
#if WITH_FS
// filesystem commands
static int cmd_files_if(int, char **);
static int cmd_files_mount0(int, char **);
static int cmd_files_mount(int, char **);
#  if WITH_SD
static int cmd_files_mount_sd(int, char **);
#  endif
static int cmd_files_unmount(int, char **);
static int cmd_files_cd(int, char **);
static int cmd_files_ls(int, char **);
static int cmd_files_rm(int, char **);
static int cmd_files_mv(int, char **);
static int cmd_files_cp(int, char **);
static int cmd_files_write(int, char **);
static int cmd_files_insdel(int, char **);
static int cmd_files_mkdir(int, char **);
static int cmd_files_cat(int, char **);
static int cmd_files_touch(int, char **);
static int cmd_files_format(int, char **);
#endif  //WITH_FS
// automation
static int cmd_echo(int, char **);

// system
static int cmd_suspend(int, char **);
static int cmd_resume(int, char **);
static int cmd_kill(int, char **argv);
static int cmd_cpu(int, char **);
static int cmd_cpu_freq(int, char **);
static int cmd_uptime(int, char **);

static int NORETURN cmd_reload(int, char **);
static int cmd_nap(int, char **);


// pin-realated commands: pwm, pulse counter and pin
static int cmd_pwm(int, char **);
static int cmd_count(int, char **);
static int cmd_pin(int, char **);

// RMT sequences
static int cmd_seq_if(int, char **);
static int cmd_seq_eot(int argc, char **argv);
static int cmd_seq_modulation(int argc, char **argv);
static int cmd_seq_zeroone(int argc, char **argv);
static int cmd_seq_tick(int argc, char **argv);
static int cmd_seq_bits(int argc, char **argv);
static int cmd_seq_levels(int argc, char **argv);
static int cmd_seq_show(int argc, char **argv);

// sketch variables
static int cmd_var(int, char **);
static int cmd_var_show(int, char **);

// generic "show" command
static int cmd_show(int, char **);

// common entries. these are in misc.c
static int exit_command_directory(int, char **);
#if WITH_HELP
static int cmd_question(int, char **);
#endif

// hidden commands, misc.c
static int cmd_history(int, char **);
#if WITH_COLOR
static int cmd_colors(int, char **);
#endif
static int cmd_tty(int, char **);

// Custom uart commands (uart subderictory or uart command tree)
// Those displayed after executing "uart 2" (or any other uart interface)
//
static const struct keywords_t keywords_uart[] = {

  KEYWORDS_BEGIN

  { "up", cmd_uart_up, 3,
    HELPK("% \"<*>up RX TX BAUD</>\"\r\n"
          "%\r\n"
          "% Initialize uart interface X on pins RX/TX,baudrate BAUD, 8N1 mode\r\n"
          "% Ex.: <*>up 18 19 115200</> - Setup uart on pins rx=18, tx=19, at speed 115200"),
    HELPK("Initialize uart (pins/speed)") },

  { "baud", cmd_uart_baud, 1,
    HELPK("% \"<*>baud SPEED</>\"\r\n"
          "%\r\n"
          "% Set speed for the uart (uart must be initialized)\r\n"
          "% Ex.: <*>baud 115200</> - Set uart baud rate to 115200"),
    HELPK("Set baudrate") },

  { "down", cmd_uart_down, NO_ARGS,
    HELPK("% \"<*>down</>\"\r\n"
          "%\r\n"
          "% Shutdown interface, detach pins"),
    HELPK("Shutdown") },

  { "read", cmd_uart_read, NO_ARGS,
    HELPK("% \"<*>read</>\"\r\n"
          "%\r\n"
          "% Read bytes (available) from uart interface X"),
    HELPK("Read data from UART") },

  { "tap", cmd_uart_tap, NO_ARGS,
    HELPK("% \"<*>tap</>\\r\n"
          "%\r\n"
          "% Bridge the UART IO directly to/from shell\r\n"
          "% User input will be forwarded to uart X;\r\n"
          "% Anything UART X sends back will be forwarded to the user"),
    HELPK("Talk to device connected") },

  { "write", cmd_uart_write, MANY_ARGS,
    HELPK("% \"<*>write TEXT</>\"\r\n"
          "%\r\n"
          "% Send an ascii/hex string(s) to UART interface\r\n"
          "% <*>TEXT</> can include spaces, escape sequences: \\n, \\r, \\\\, \\t and \r\n"
          "% hexadecimal numbers \\AB (A and B are hexadecimal digits)\r\n"
          "%\r\n"
          "% Ex.: \"<*>write ATI\\n\\rMixed\\20Text and \\20\\21\\ff\"</>"),
    HELPK("Send bytes over this UART") },

  KEYWORDS_END
};

//TAG:keywords_iic
//TAG_keywords_i2c
//i2c subderictory keywords list
//cmd_exit() and cmd_i2c_if are responsible for selecting keywords list
//to use
static const struct keywords_t keywords_i2c[] = {

  KEYWORDS_BEGIN

  { "up", cmd_i2c_up, 3,
    HELPK("% \"<*>up SDA SCL CLOCK</>\"\r\n"
          "%\r\n"
          "% Initialize I2C interface X, use pins SDA/SCL, clock rate CLOCK\r\n"
          "% Ex.: up 21 22 100000 - enable i2c at pins sda=21, scl=22, 100kHz clock"),
    HELPK("Initialize interface (pins and speed)") },

  { "clock", cmd_i2c_clock, 1,
    HELPK("% \"<*>clock SPEED</>\"\r\n"
          "%\r\n"
          "% Set I2C master clock (i2c must be initialized)\r\n"
          "% Ex.: clock 100000 - Set i2c clock to 100kHz"),
    HELPK("Set clock") },

  { "scan", cmd_i2c_scan, NO_ARGS,
    HELPK("% \"<*>scan</>\"\r\n"
          "%\r\n"
          "% Scan I2C bus X for devices. Interface must be initialized!"),
    HELPK("Scan i2c bus for devices") },

  { "write", cmd_i2c_write, MANY_ARGS,
    HELPK("% \"<*>write ADDR D1 [D2 ... Dn]</>\"\r\n"
          "%\r\n"
          "% Write bytes D1..Dn (hex values) to address ADDR on I2C bus X\r\n"
          "% Ex.: <*>write 0x57 0 0xff</> - write 2 bytes to address 0x57: 0 and 255"),
    HELPK("Send bytes to the device") },

  { "read", cmd_i2c_read, 2,
    HELPK("% \"<*>read ADDR SIZE</>\"\r\n"
          "%\r\n"
          "% Read SIZE bytes from a device at address ADDR\r\n"
          "% Ex.: read 0x68 7 - read 7 bytes from device address 0x68"),
    HELPK("Read data from an I2C device") },

  { "down", cmd_i2c_down, NO_ARGS,
    HELPK("% \"<*>down</>\"\r\n"
          "%\r\n"
          "% Shutdown I2C interface X"),
    HELPK("Shutdown i2c interface") },


  KEYWORDS_END
};
#if WITH_SPI
//TAG_keywords_spi
//spi subderictory keywords list
static const struct keywords_t keywords_spi[] = {

  KEYWORDS_BEGIN

  { "up", cmd_spi_up, 3,
    HELPK("% \"up MOSI MISO CLK\"\r\n"
          "%\r\n"
          "% Initialize SPI interface in MASTER mode, use pins MOSI/MISO/CLK\r\n"
          "% Ex.: up 23 19 18 - Initialize SPI at pins 23,19,18"),
    HELPK("Initialize interface") },

  { "clock", cmd_spi_clock, 1,
    HELPK("% \"clock SPEED\"\r\n"
          "%\r\n"
          "% Set SPI master clock (SPI must be initialized)\r\n"
          "% Ex.: clock 1000000 - Set SPI clock to 1 MHz"),
    HELPK("Set clock") },

  { "write", cmd_spi_write, MANY_ARGS,
    HELPK("% \"write CHIP_SELECT D1 [D2 ... Dn]\"\r\n"
          "%\r\n"
          "% Write bytes D1..Dn (hex values) to SPI bus whicle setting CHIP_SELECT pin low\r\n"
          "% Ex.: write 4 0 0xff - write 2 bytes, CS=4"),
    HELPK("Send bytes to the device") },


  { "down", cmd_spi_down, NO_ARGS,
    HELPK("% \"down\"\r\n"
          "%\r\n"
          "% Shutdown SPI interface X"),
    HELPK("Shutdown SPI interface") },


  KEYWORDS_END
};
#endif //WITH_SPI

//TAG:keywords_seq
//'sequence' subderictory keywords list
static const struct keywords_t keywords_sequence[] = {

  KEYWORDS_BEGIN

  { "eot", cmd_seq_eot, 1,
    HELPK("% \"<*>eot</> <1>high|low</>\"\r\n"
          "%\r\n"
          "% End of transmission: pull the line high or low at the\r\n"
          "% end of a sequence. Default is \"low\""),
    HELPK("End-of-Transmission pin state") },

  { "tick", cmd_seq_tick, 1,
    HELPK("% \"<*>tick TIME</>\"\r\n"
          "%\r\n"
          "% Set the sequence tick time: defines a resolution of a pulse sequence.\r\n"
          "% Expressed in microseconds, can be anything between 0.0125 and 3.2\r\n"
          "% Ex.: <*>tick 0.1</> - set resolution to 0.1 microsecond"),
    HELPK("Set resolution") },

  { "zero", cmd_seq_zeroone, 2,
    HELPK("% \"<*>zero LEVEL/DURATION [LEVEL2/DURATION2]</>\"\r\n"
          "%\r\n"
          "% Define a logic \"0\"\r\n"
          "% Ex.: <*>zero 0/50</>      - 0 is a level: LOW for 50 ticks\r\n"
          "% Ex.: <*>zero 1/50 0/20</> - 0 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks"),
    HELPK("Define a zero") },

  { "zero", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

  { "one", cmd_seq_zeroone, 2,
    HELPK("% \"<*>one LEVEL/DURATION [LEVEL2/DURATION2]</>\"\r\n"
          "%\r\n"
          "% Define a logic \"1\"\r\n"
          "% Ex.: <*>one 1/50</>       - 1 is a level: HIGH for 50 ticks\r\n"
          "% Ex.: <*>one 1/50 0/20</>  - 1 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks"),
    HELPK("Define an one") },

  { "one", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

  { "bits", cmd_seq_bits, 1,
    HELPK("% \"<*>bits STRING</>\"\r\n"
          "%\r\n"
          "% A bit pattern to be used as a sequence. STRING must contain only 0s and 1s\r\n"
          "% Overrides previously set \"levels\" command\r\n"
          "% See commands \"one\" and \"zero\" to define \"1\" and \"0\"\r\n"
          "%\r\n"
          "% Ex.: <*>bits 11101000010111100</>  - 17 bit sequence"),
    HELPK("Set pattern to transmit") },

  { "levels", cmd_seq_levels, MANY_ARGS,
    HELPK("% \"<*>levels L1/D1 L2/D2 ... Ln/Dn</>\"\r\n"
          "%\r\n"
          "% A bit pattern to be used as a sequnce. L is either 1 or 0 and \r\n"
          "% D is the duration measured in ticks [0..32767] \r\n"
          "% Overrides previously set \"bits\" command\r\n"
          "%\r\n"
          "% Ex.: <*>levels 1/50 0/20 1/100 0/500<*>  - HIGH 50 ticks, LOW 20, HIGH 100 and 0 for 500 ticks\r\n"
          "% Ex.: <*>levels 1/32767 1/17233 0/32767 0/7233</> - HIGH for 50000 ticks, LOW for 40000 ticks"),
    HELPK("Set levels to transmit") },

  { "modulation", cmd_seq_modulation, 3,
    HELPK("% \"<*>modulation FREQ</> [<*>DUTY</> [<1>low|high</>]]\"\r\n"
          "%\r\n"
          "% Enables/disables an output signal modulation with frequency FREQ\r\n"
          "% Optional parameters are: DUTY (from 0 to 1) and LEVEL (either high or low)\r\n"
          "%\r\n"
          "% Ex.: <*>modulation 100</>         - modulate all 1s with 100Hz, 50% duty cycle\r\n"
          "% Ex.: <*>modulation 100 0.3 low</> - modulate all 0s with 100Hz, 30% duty cycle\r\n"
          "% Ex.: <*>modulation 0</>           - disable modulation\r\n"),
    HELPK("Enable/disable modulation") },

  { "modulation", cmd_seq_modulation, 2, HIDDEN_KEYWORD },
  { "modulation", cmd_seq_modulation, 1, HIDDEN_KEYWORD },

  { "show", cmd_seq_show, 0, "Show sequence", NULL },

  KEYWORDS_END
};

#if WITH_FS
// Filesystem commands. this commands subdirectory is enabled
// with "files" command /cmd_files_if()/
//TAG:keywords_files
//
static const struct keywords_t keywords_files[] = {

  KEYWORDS_BEGIN

  { "mount", cmd_files_mount_sd, 6,
    HELPK("% \"<*>mount vspi|hspi|fspi MISO MOSI CLK CS</> <1>[SPI_FREQ] [/MOUNT_POINT]</>\"\r\n"
          "%\r\n"
          "% Mount a FAT filesystem located on SD card connected to SPI bus\r\n"
          "%\r\n"
          "% <i>1st argument</>: SPI bus to use (<i>hspi</> is the safest choise)\r\n"
          "% <i>MISO, MOSI, CLK</> and <i>CS</> are SPI pins to use (19,23,18 and 5 for example)\r\n"
          "% <1>SPI_FREQ</> : optional parameter, SPI frequency in kHz (20000 if not set)\r\n"
          "% <1>/MOUNT_POINT</> - A path, starting with \"/\" where filesystem will be mounted.\r\n"
          "% If mount point is omitted then autogenerated name will be used, like \"scard4\"\r\n"
          "%\r\n"
          "% Ex.: mount vspi 19 23 18 4 /sdcard  - Mount an SD card located on VSPI pins 19,\r\n"
          "%                                       23, 18 and 4.\r\n"
          "% Ex.: mount spi3 19 23 18 4 400      - Same as above but SPI bus is at 400kHz\r\n"
          "% Ex.: mount spi1 19 23 18 4 1000 /sd - 1 MHz SPI bus, mount to \"/sd\" directory\r\n"),

    HELPK("Mount partition/Show partition table") },

  { "mount", cmd_files_mount_sd, 7, HIDDEN_KEYWORD },
  { "mount", cmd_files_mount_sd, 5, HIDDEN_KEYWORD },


  { "mount", cmd_files_mount0, NO_ARGS,
    HELPK("% \"<*>mount</>\"\r\n"
          "%\r\n"
          "% Command \"mount\" **without arguments** displays information about partitions\r\n"
          "% and mounted file systems (mount point, FS type, total/used counters)"),
    NULL },

  { "mount", cmd_files_mount, 2,
    HELPK("% \"<*>mount LABEL</> <1>[/MOUNT_POINT]</>\"\r\n"
          "%\r\n"
          "% Mount a filesystem located on built-in SPI FLASH\r\n"
          "%\r\n"
          "% <i>LABEL</>        - SPI FLASH partition label\r\n"
          "% <1>/MOUNT_POINT</> - A path, starting with \"/\" where filesystem will be mounted.\r\n"
          "% If mount point is omitted then \"/\" + LABEL will be used as a mountpoint\r\n"
          "%\r\n"
          "% Ex.: mount ffat /fs - mount partition \"ffat\" at directory \"/fs\"\r\n"
          "% Ex.: mount ffat     - mount partition \"ffat\" at directory \"/ffat\""),
    NULL },

    { "mount", cmd_files_mount, 1, HIDDEN_KEYWORD },


  { "unmount", cmd_files_unmount, 1,
    HELPK("% \"<*>unmount</> <1>[/MOUNT_POINT]</>\"\r\n"
          "%\r\n"
          "% Unmount file system specified by its mountpoint\r\n"
          "% If mount point is omitted then current (by CWD) filesystem is unmounted\r\n"),
    HELPK("Unmount partition") },

  { "unmount", cmd_files_unmount, NO_ARGS, HIDDEN_KEYWORD },
  { "umount", cmd_files_unmount, 1, HIDDEN_KEYWORD },        // for unix folks
  { "umount", cmd_files_unmount, NO_ARGS, HIDDEN_KEYWORD },  // for unix folks

  { "ls", cmd_files_ls, 1,
    HELPK("% \"ls [PATH]\"\r\n"
          "%\r\n"
          "% Show directory listing at PATH given\r\n"
          "% If PATH is omitted then current directory list is shown"),
    HELPK("List directory") },

  { "ls", cmd_files_ls, 0, HIDDEN_KEYWORD },

  { "cd", cmd_files_cd, MANY_ARGS,
    HELPK("% \"cd [PATH|..]\"\r\n"
          "%\r\n"
          "% Change current directory. Paths having .. (i.e \"../dir/\") are not supported\r\n"
          "%\r\n"
          "% Ex.: \"cd\"            - change current directory to filesystem's root\r\n"
          "% Ex.: \"cd ..\"         - go one directory up\r\n"
          "% Ex.: \"cd /ffat/test/  - change to \"/ffat/test/\"\r\n"
          "% Ex.: \"cd test2/test3/ - change to \"/ffat/test/test2/test3\"\r\n"),
    HELPK("Change directory") },

  { "rm", cmd_files_rm, MANY_ARGS,
    HELPK("% \"rm PATH1 [PATH2 PATH3 ... PATHn]\"\r\n"
          "%\r\n"
          "% Remove files or a directories with files.\r\n"
          "% When removing directories: removed with files and subdirs"),
    HELPK("Delete files/dirs") },

  { "mv", cmd_files_mv, 2,
    HELPK("% \"mv SOURCE DESTINATION\\r\n"
          "%\r\n"
          "% Move or Rename file or directory SOURCE to DESTINATION\r\n"
          "%\r\n"
          "% Ex.: \"mv /ffat/dir1 /ffat/dir2\"             - rename directory \"dir1\" to \"dir2\"\r\n"
          "% Ex.: \"mv /ffat/fileA.txt /ffat/fileB.txt\"   - rename file \"fileA.txt\" to \"fileB.txt\"\r\n"
          "% Ex.: \"mv /ffat/dir1/file1 /ffat/dir2\"       - move file to directory\r\n"
          "% Ex.: \"mv /ffat/fileA.txt /spiffs/fileB.txt\" - move file between filesystems\r\n"),
    HELPK("Move/rename files and/or directories") },

  { "cp", cmd_files_cp, 2,
    HELPK("% \"cp SOURCE DESTINATION\\r\n"
          "%\r\n"
          "% Copy file SOURCE to file DESTINATION.\r\n"
          "% Files SOURCE and DESTINATION can be on different filesystems\r\n"
          "%\r\n"
          "% Ex.: \"cp /ffat/test.txt /ffat/test2.txt\"       - copy file to file\r\n"
          "% Ex.: \"cp /ffat/test.txt /ffat/dir/\"            - copy file to directory\r\n"
          "% Ex.: \"cp /ffat/dir_src /ffat/dir/\"             - copy directory to directory\r\n"
          "% Ex.: \"cp /spiffs/test.txt /ffat/dir/test2.txt\" - copy between filesystems\r\n"),
    HELPK("Copy files/dirs") },

  { "write", cmd_files_write, MANY_ARGS,
    HELPK("% \"write FILENAME [TEXT]\"\r\n"
          "%\r\n"
          "% Write an ascii/hex string(s) to file\r\n"
          "% TEXT can include spaces, escape sequences: \\n, \\r, \\\\, \\t and \r\n"
          "% hexadecimal numbers \\AB (A and B are hexadecimal digits)\r\n"
          "%\r\n"
          "% Ex.: \"write /ffat/test.txt \\n\\rMixed\\20Text and \\20\\21\\ff\""),
    HELPK("Write strings/bytes to the file") },

  { "append", cmd_files_write, MANY_ARGS,
    HELPK("% \"append FILENAME [TEXT]\"\r\n"
          "%\r\n"
          "% Append an ascii/hex string(s) to file\r\n"
          "% Escape sequences & ascii codes are accepted just as in \"write\" command\r\n"
          "%\r\n"
          "% Ex.: \"append /ffat/test.txt \\n\\rMixed\\20Text and \\20\\21\\ff\""),
    HELPK("Append strings/bytes to the file") },

  { "insert", cmd_files_insdel, MANY_ARGS,
    HELPK("% \"insert FILENAME LINE_NUM [TEXT]\"\r\n"
          "% Insert TEXT to file FILENAME before line LINE_NUM\r\n"
          "% \"\\n\" is appended to the string being inserted, \"\\r\" is not\r\n"
          "% Escape sequences & ascii codes accepted just as in \"write\" command\r\n"
          "% Lines are numbered starting from 0. Use \"cat\" command to find out line numbers\r\n"
          "%\r\n"
          "% Ex.: \"insert 0 /ffat/test.txt Hello World!\""),
    HELPK("Insert lines to text file") },

  { "delete", cmd_files_insdel, 3,
    HELPK("% \"delete FILENAME LINE_NUM [COUNT]\"\r\n"
          "% Delete line LINE_NUM from a text file FILENAME\r\n"
          "% Optionsl COUNT argument is the number of lines to remove (default is 1)"
          "% Lines are numbered starting from 1. Use \"cat -n\" command to find out line numbers\r\n"
          "%\r\n"
          "% Ex.: \"delete 10 /ffat/test.txt\" - remove line #10 from \"/ffat/test.txt\""),
    HELPK("Delete lines from a text file") },

  { "delete", cmd_files_insdel, 2, HIDDEN_KEYWORD },

  { "mkdir", cmd_files_mkdir, MANY_ARGS,
    HELPK("% \"mkdir PATH1 [PATH2 PATH3 ... PATHn]\"\r\n"
          "%\r\n"
          "% Create empty directories PATH1 ... PATHn\r\n"),
    HELPK("Create directories") },

  { "cat", cmd_files_cat, MANY_ARGS,
    HELPK("% \"cat [-n|-b] PATH [START [COUNT]] [uart NUM]\"\r\n"
          "%\r\n"
          "% Display (or send by UART) a binary or text file PATH\r\n"
          "% -n : display line numbers\r\n"
          "% -b : file is binary (mutually exclusive with \"-n\" option)\r\n"
          "% PATH  : path to the file\r\n"
          "% START : text file line number (OR binary file offset if \"-b\" is used)\r\n"
          "% COUNT : number of lines to display (OR bytes for \"-b\" option)\r\n"
          "% NUM   : UART interface number to transmit file to\r\n"
          "%\r\n"
          "% Examples:\r\n"
          "% cat file              - display file \"file\"\r\n"
          "% cat -n file           - display file \"file\" + line numbers\r\n"
          "% cat file 34           - display text file starting from line 34 \r\n"
          "% cat file 900 10       - 10 lines, starting from line 900 \r\n"
          "% cat -b file           - display binary file (formatted output)\r\n"
          "% cat -b file 0x1234    - display binary file starting at offset 0x1234\r\n"
          "% cat -b file 999 0x400 - 999 bytes starting from offset 1024 of a binary file\r\n"
          "% cat file uart 1       - transmit a text file over UART1, strip \"\\r\" if any\r\n"
          "% cat -b file uart 1    - transmit file over UART1 \"as-is\" byte by byte"),
    HELPK("Display/transmit text/binary file") },

  { "touch", cmd_files_touch, MANY_ARGS,
    HELPK("% \"touch PATH1 [PATH2 PATH3 ... PATHn]\"\r\n"
          "%\r\n"
          "% Ceate new files or \"touch\" existing\r\n"),
    HELPK("Create/touch files") },

  { "format", cmd_files_format, 1,
    HELPK("% \"format [LABEL]\"\r\n"
          "%\r\n"
          "% Format partition LABEL. If LABEL is omitted then current working\r\n"
          "% directory is used to determine partition label"),
    HELPK("Erase old & create new filesystem") },

  { "format", cmd_files_format, 0, HIDDEN_KEYWORD },
  { "format&", cmd_files_format, 1, HIDDEN_KEYWORD },
  { "format&", cmd_files_format, 0, HIDDEN_KEYWORD },

  KEYWORDS_END
};
#endif  //WITH_FS

// root directory commands
//TAG:keywords_main
static const struct keywords_t keywords_main[] = {

  KEYWORDS_BEGIN

  { "uptime", cmd_uptime, NO_ARGS,
    HELPK("% \"<*>uptime</>\"\r\n% Shows time passed since last boot; shows restart cause"), "System uptime" },

  // System commands
  { "cpu", cmd_cpu_freq, 1,
    HELPK("% \"<*>cpu FREQ</>\"\r\n% Set CPU frequency to FREQ Mhz"), "Set/show CPU parameters" },

  { "cpu", cmd_cpu, NO_ARGS,
    HELPK("% \"<*>cpu</>\"\r\n% Show CPUID and CPU/XTAL/APB frequencies"), NULL },

  { "suspend", cmd_suspend, NO_ARGS,
    HELPK("% \"<*>suspend</>\"\r\n% Suspend sketch execution (Hotkey: Ctrl+C). Resume with \"resume\"\r\n"), "Suspend sketch execution" },

  { "resume", cmd_resume, NO_ARGS,
    HELPK("% \"<*>resume</>\"\r\n% Resume sketch execution\r\n"), "Resume sketch execution" },

  { "kill", cmd_kill, 2,
    HELPK("% \"<*>kill [-term|-kill|-9|-15] TASK_ID</>\"\r\n"
          "% Send a signal to a task. Default is SIGTERM (safely stop)\r\n"
          "% If -9 or -kill option is used then task is deleted (use with care!)"), 
    "Kill tasks" },

  { "kill", cmd_kill, 1, HIDDEN_KEYWORD },

  { "reload", cmd_reload, NO_ARGS,
    HELPK("% \"<*>reload</>\"\r\n% Restarts CPU"), "Reset CPU" },

  { "nap", cmd_nap, 1,
    HELPK("% \"<*>nap SEC</>\"\r\n%\r\n% Put the CPU into light sleep mode for SEC seconds."), "CPU sleep" },

  { "nap", cmd_nap, NO_ARGS,
    HELPK("% \"nap\"\r\n%\r\n% Put the CPU into light sleep mode, wakeup by console"), NULL },

  // Interfaces (UART,I2C, RMT, FileSystem..)
  { "iic", cmd_i2c_if, 1,
    HELPK("% \"<*>iic X</>\" \r\n%\r\n"
          "% Enter I2C interface X configuration mode \r\n"
          "% Ex.: iic 0 - configure/use interface I2C0"),
    "I2C commands" },
#if 0
  { "spi", cmd_spi_if,1,
    HELPK("% \"<*>spi [fspi|hspi|vspi]</>\" \r\n%\r\n"
          "% Enter SPI interface configuration mode \r\n"
          "% Ex.: spi vspi - configure/use interface SPI3 (VSPI)"),
    HELPK("SPI commands") },
#endif
  { "uart", cmd_uart_if, 1,
    HELPK("% \"<*>uart X</>\"\r\n"
          "%\r\n"
          "% Enter UART interface X configuration mode\r\n"
          "% Ex.: uart 1 - configure/use interface UART 1"),
    "UART commands" },

  { "sequence", cmd_seq_if, 1,
    HELPK("% \"<*>sequence X</>\"\r\n"
          "%\r\n"
          "% Create/configure a sequence\r\n"
          "% Ex.: sequence 0 - configure Sequence0"),
    "Sequence configuration" },

#if WITH_FS
  { "files", cmd_files_if, NO_ARGS,
    HELPK("% \"<*>files</>\"\r\n"
          "%\r\n"
          "% Enter files & file system operations mode"),
    "File system access" },
#endif

  // Show funcions (more will be added)
  { "show", cmd_show, 2,
    HELPK("% \"<*>show <i>iomux</>\"\r\n"
          "%\r\n"
          "% Display IO_MUX functions available for each pin\r\n"
          "% \"show iomux\"  - display IOMUX function names"),"Display information"},

  { "show", cmd_show, 2,
    HELPK("% \"<*>show <i>sequence</> NUMBER</>\"\r\n"
          "%\r\n"
          "% Display sequence configuration for given index:\r\n"
          "% \"show sequence 6\"  - display Sequence #6 configuration"),"Display information"},

  // shadowed entry
  { "show", cmd_show, 2,
    HELPK("% \"<*>show <i>mount</> [<1>/PATH</>]\"\r\n"
          "%\r\n"
          "% Display information about mounted filesystems, partitions.\r\n"
          "% \"show mount\"           - display filesystem information\r\n"
          "% \"show mount /my_disk\"  - display information about mountpoint \"/my_disk\""), NULL},

  // shadowed entry. For helptext only
  { "show", cmd_show, 2,
    HELPK("% \"<*>show <i>memory</> [<1>ADDRESS</>] [<1>COUNT</>]\"\r\n"
          "%\r\n"
          "% Display COUNT bytes starting from the memory address ADDRESS\r\n"
          "% Address is either decimal or hex (with or without leading \"0x\")\r\n%\r\n"
          "% COUNT is optional and its default value is 256 bytes. Can be decimal or hex\r\n"
          "%\r\n"
          "% <*>\"show <i>memory</>\"\r\n"
          "%\r\n"
          "% Display HEAP information / availability"),NULL},

  { "show", cmd_show, 3, HIDDEN_KEYWORD },
  { "show", cmd_show, 1, HIDDEN_KEYWORD },

  // Shell input/output settings
  { "tty", cmd_tty, 1, HIDDEN_KEYWORD },

  { "echo", cmd_echo, 1,
    HELPK("% \"<*>echo [on|off|silent]</>\"\r\n"
          "% Echo user input on/off (default is on)\r\n"
          "% Without arguments displays current echo state\r\n"), 
    HELPK("Enable/Disable user input echo") },

  { "echo", cmd_echo, NO_ARGS, HIDDEN_KEYWORD },  //hidden command, displays echo status

  // Generic pin commands
  { "pin", cmd_pin, 1,
    HELPK("% \"<*>pin X</>\"\r\n"
          "% Show pin X configuration and digital value\r\n"
          "% Ex.: \"pin 2\" - show GPIO2 information"), 
    HELPK("Pins (GPIO) commands") },

  { "pin", cmd_pin, MANY_ARGS,
    HELPK("% \"<*>pin X [hold|release|up|down|out|in|open|high|low|save|load|read|aread|delay|loop|pwm|seq|iomux]*</>...\"\r\n"
          "% Multifunction command which can:\r\n"
          "%  1. Set/Save/Load pin configuration and settings\r\n"
          "%  2. Enable/disable PWM and pattern generation on pin\r\n"
          "%  3. Set/read digital and/or analog pin values\r\n"
          "%\r\n"
          "% Multiple arguments must be separated with spaces, see examples below:\r\n%\r\n"
          "% Ex.: pin 1 read aread         -pin1: read digital and then analog values\r\n"
          "% Ex.: pin 1 in out up          -pin1 is INPUT and OUTPUT with PULLUP\r\n"
          "% Ex.: pin 1 save high load     -save pin state, set HIGH(1), restore pin state\r\n"
          "% Ex.: pin 1 high               -pin1 set to logic \"1\"\r\n"
          "% Ex.: pin 1 high delay 100 low -set pin1 to logic \"1\", after 100ms to \"0\"\r\n"
          "% Ex.: pin 1 pwm 2000 0.3       -set 5kHz, 30% duty square wave output\r\n"
          "% Ex.: pin 1 pwm 0 0            -disable generation\r\n"
          "% Ex.: pin 1 high delay 500 low delay 500 loop 10 - Blink a led 10 times\r\n%\r\n"
          "% (see \"docs/Pin_Commands.txt\" for more details & examples)\r\n"),
    NULL },

  // PWM generation
  { "pwm", cmd_pwm, 3,
    HELPK("% \"<*>pwm X [FREQ [DUTY]]</>\"\r\n"
          "%\r\n"
          "% Start PWM generator on pin X, frequency FREQ Hz and duty cycle of DUTY\r\n"
          "% Maximum frequency is 312000Hz, and DUTY is in range [0..1] with 0.123 being\r\n"
          "% a 12.3% duty cycle\r\n"
          "%\r\n"
          "% DUTY is optional and its default value is 50% (if not specified) and\r\n"
          "% its resolution is 0.005 (0.5%)"
          "%\r\n"
          "% Ex.: pwm 2 1000     - enable PWM of 1kHz, 50% duty on pin 2\r\n"
          "% Ex.: pwm 2          - disable PWM on pin 2\r\n"
          "% Ex.: pwm 2 6400 0.1 - enable PWM of 6.4kHz, duty cycle of 10% on pin 2\r\n"),
    "PWM output" },

  { "pwm", cmd_pwm, 2, HIDDEN_KEYWORD },
  { "pwm", cmd_pwm, 1, HIDDEN_KEYWORD },

  // Pulse counting/frequency meter
  { "count", cmd_count, 3,
    HELPK("% \"<*>count PIN clear</>]\"\r\n"
          "% \"<*>count PIN</> [<1>DURATION</>] [<*>trigger</>]\"\r\n%\r\n"
          "% Count pulses on pin PIN within DURATION time, time is measured in\r\n"
          "% milliseconds, optional. Default is 1000\r\n"
          "% The \"trigger\" keyword is pauses the counter until pulses start to come\r\n"
          "%\r\n"
          "% Ex.: \"<*>count 4</>\"         - Count pulses & measure frequency on pin4 for 1000ms\r\n"
          "% Ex.: \"<*>count 4 2000</>\"    - Same as above but measurement time is 2 seconds\r\n"
          "% Ex.: \"<*>count 4 999999 &</>\"- Count pulses in background for 1000 seconds\r\n"
          "% Ex.: \"<*>count 4 trigger</>\" - Wait for the pulse, then start to count\r\n"
          "% Ex.: \"<*>count 4 clear</>\"   - Set counter to 0 (running or stopped are ok)\r\n"
          "% Ex.: \"<*>count 4 2000 trigger &</>\" - Wait for the pulse, then start to count for\r\n"
          "%                                   2 seconds in a background"),
    "Pulse counter" },

  { "count", cmd_count, 2, HIDDEN_KEYWORD },   //hidden with 2 arg
  { "count", cmd_count, 1, HIDDEN_KEYWORD },   //hidden with 1 arg

#if WITH_ESPCAM
  { "camera", cmd_cam, 1, 
    HELPK("% \"camera up|down|settings|capture|filesize|transfer\" - Camera commands:\n\r" \
          "%\n\r" \
          "% setting  - Enter ESPCam setting\n\r" \
          "% capture  - Capture a single shot (JPEG)\n\r" \
          "% filesize - Display last captured shot file size\n\r" \
          "% transfer - Transmit the last shot over uart\n\r" \
          "% up       - Detect & initialize the camera\n\r" \
          "% down     - Camera shutdown & power-off"),
   HELPK("ESP32Cam commands") },
#endif

  { "var", cmd_var, 2,
    HELPK("% \"<*>var [VARIABLE_NAME] [NUMBER]</>\"\r\n%\r\n"
          "% Set/display sketch variable \r\n"
          "% VARIABLE_NAME is the variable name, optional argument\r\n"
          "% NUMBER can be integer or float point values, positive or negative, optional argument\r\n"
          "%\r\n"
          "% Ex.: \"var\"             - List all registered sketch variables\r\n"
          "% Ex.: \"var button1\"     - Display current value of \"button1\" sketch variable\r\n"
          "% Ex.: \"var angle -12.3\" - Set sketch variable \"angle\" to \"-12.3\"\r\n"
          "% Ex.: \"var 1234\"        - Display a decimal number as hex, float, int etc.\r\n"
          "% Ex.: \"var 0x1234\"      - -- // hex // --\r\n"
          "% Ex.: \"var 01234\"       - -- // octal // --\r\n"
          "% Use prefix \"0x\" for hex, \"0\" for octal or \"0b\" for binary numbers"),
    "Sketch variables" },

  { "var", cmd_var_show, 1, HIDDEN_KEYWORD },
  { "var", cmd_var_show, NO_ARGS, HIDDEN_KEYWORD },


  { "history", cmd_history, 1, HIDDEN_KEYWORD },
  { "history", cmd_history, 0, HIDDEN_KEYWORD },
#if WITH_COLOR
  { "colors", cmd_colors, 1, HIDDEN_KEYWORD },
  { "colors", cmd_colors, 0, HIDDEN_KEYWORD },
#endif

  KEYWORDS_END
};


//TAG:keywords
//current keywords list to use
static const struct keywords_t *keywords = keywords_main;


// Called by cmd_uart_if, cmd_i2c_if,cmd_seq_if, cam_settings and cmd_files_if to
// set new command list (command directory) and displays user supplied text
// /Context/ - arbitrary number which will be stored
// /dir/     - new keywords list (one of keywords_main[], count"[] tc)
// /prom/    - prompt to use
// /text/    - text to be displayed when switching command directory
//
static void change_command_directory(unsigned int context, const struct keywords_t *dir, const char *prom, const char *text) {
  Context = context;
  keywords = dir;
  prompt = prom;
  HELP(q_printf("%% Entering %s mode. Ctrl+Z or \"exit\" to return\r\n", text));
  HELP(q_print("% Hint: Main commands are still avaiable (but not visible in \"?\" command list)\r\n"));
}


//"exit"
//"exit exit"
// exists from command subderictory or closes the shell ("exit exit")
//
static int exit_command_directory(int argc, char **argv) {

  if (keywords != keywords_main) {
    // restore prompt & keywords list to use
    keywords = keywords_main;
    prompt = PROMPT;
  } else
    // close espshell. mounted filesystems are left mounted, background commands are left running
    // memory is not freed. It all can/will be reused on espshell restart via espshell_start() call
    if (argc > 1 && !q_strcmp(argv[1], "exit"))
      Exit = true;
  return 0;
}
#endif // #if COMPILING_ESPSHELL

