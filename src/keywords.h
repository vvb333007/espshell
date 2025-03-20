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
 
#if COMPILING_ESPSHELL

// -- Shell commands handlers (prototypes) --
//
// These are called by espshell_command() processor in order to execute commands. Function names are pretty selfdescriptive:
// command handler function names always start with "cmd_" and then follows with either command name (e.g. cmd_pin) or command 
// directory + command name (e.g. cmd_files_write()).
//
// Handlers have access to user input via argc/argv mechanism. Return value is 0 if it was success, or contains an index of a failed
// argument. Other return codes are in keywords_defs.h, see CMD_FAILED, CMD_SUCCESS.
//
// For the code simplicity, the success value is returned as 0, not as CMD_SUCCESS. If the return value is CMD_FAIL, then it is up to handler
// to give an explanation of a problem. ESPShell prints error messages for other error codes and keeps silent for CMD_FAIL

// Camera commands
#if WITH_ESPCAM
static int cmd_cam(int, char **);
static int cmd_camera_set_gain(int argc, char **argv);
static int cmd_camera_set_balance(int argc, char **argv);
static int cmd_camera_set_exposure(int argc, char **argv);
static int cmd_camera_set_qbcss(int argc, char **argv);
static int cmd_camera_set_size(int argc, char **argv);
static int cmd_camera_capture(int argc, char **argv);
static int cmd_camera_filesize(int argc, char **argv);
static int cmd_camera_transfer(int argc, char **argv);
static int cmd_camera_down(int argc, char **argv);
#endif // WITH_ESPCAM

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
static int cmd_pin_show(int, char **);

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

// "show" commands
static int cmd_show(int, char **);
static int cmd_show_address(int, char **);
static int cmd_show_pwm(int, char **);

// common entries
static int cmd_exit(int, char **);
#if WITH_HELP
static int cmd_question(int, char **);
#endif

// hidden commands, misc.c
static int cmd_history(int, char **);
#if WITH_COLOR
static int cmd_colors(int, char **);
#endif
static int cmd_tty(int, char **);

// -- Shell Commands --
//
// ESPShell commands are entries to /keywords_.../ arrays. Each entry starts and ends with "{" and "}" brackets.
// Command keyword, command handler function and expected number of arguments are on the same line as opening brace
// Help lines (/full/ and /brief/) are enclosed with HELPK macro which expands to empty strings if ESPShell is compiled
// with its help system off (see WITH_HELP flag in espshell.h)
//
// There are number of keyword arrays: keywords_main is the main command array, keyword_uart and keyword_i2c are other
// examples. Switching between arrays is possible via change_command_directory() function
//


// UART commands.
// These are available after executing "uart 2" (or any other uart interface)
//
static const struct keywords_t keywords_uart[] = {

  // defined in keywords_defs.h, contains common entries, like "exit" or "?"
  KEYWORDS_BEGIN

  { "up", cmd_uart_up, 3,
    HELPK("% \"<b>up</> <i>RX TX SPEED</> <o>[BITS] [no|even|odd] [1|1.5|2]</>\"\r\n" 
          "%\r\n"
          "% Initialize an UART interface on pins RX/TX, baudrate SPEED\r\n"
          "% <i>RX</>    - Pin to use as RX\r\n"
          "% <i>TX</>    - Pin to use as TX\r\n"
          "% <i>SPEED</> - 9600, 115200 or any other standart baudrate\r\n"
          "% Three optional parameters are:\r\n"
          "% <o>BITS</>      - Number of data bits: 5,6,7 or 8. Default is 8\r\n"
          "% Parity    - \"<o>no</>\", \"<o>even</>\" or \"<o>odd</>\"\r\n"
          "% Stop bits - 1,2 or 1.5 stop bits\r\n"
          "%\r\n"
          "% Ex.: <b>up 18 19 115200</> - Setup uart on pins rx=18, tx=19, at speed 115200\r\n"
          "% Ex.: <b>up 18 19 115200 <i>8 even 1.5</> - Eight bits, 1.5 stopbits, even parity" ),
    HELPK("Initialize uart (pins/speed)") },
    { "up", cmd_uart_up, 4, HIDDEN_KEYWORD },
    { "up", cmd_uart_up, 5, HIDDEN_KEYWORD },
    { "up", cmd_uart_up, 6, HIDDEN_KEYWORD },

  { "baud", cmd_uart_baud, 1,
    HELPK("% \"<b>baud</> <i>SPEED</>\"\r\n"
          "%\r\n"
          "% Set speed for the uart (uart must be initialized)\r\n"
          "% Ex.: <b>baud 115200</> - Set uart baud rate to 115200"),
    HELPK("Set baudrate") },

  { "down", cmd_uart_down, NO_ARGS,
    HELPK("% \"<b>down</>\"\r\n"
          "%\r\n"
          "% Shutdown interface, detach pins"),
    HELPK("Shutdown") },

  { "read", cmd_uart_read, NO_ARGS,
    HELPK("% \"<b>read</>\"\r\n"
          "%\r\n"
          "% Read bytes (available) from uart interface X"),
    HELPK("Read data from UART") },

  { "tap", cmd_uart_tap, NO_ARGS,
    HELPK("% \"<b>tap</>\r\n"
          "%\r\n"
          "% Bridge the UART IO directly to/from shell\r\n"
          "% User input will be forwarded to uart X;\r\n"
          "% Anything UART X sends back will be forwarded to the user"),
    HELPK("Talk to connected device") },

  { "write", cmd_uart_write, MANY_ARGS,
    HELPK("% \"<b>write</> <i>TEXT</>\"\r\n"
          "%\r\n"
          "% Send an ascii/hex string(s) to UART interface\r\n"
          "% <b>TEXT</> can include spaces, escape sequences: \\n, \\r, \\\\, \\t and \r\n"
          "% hexadecimal numbers \\AB (A and B are hexadecimal digits)\r\n"
          "%\r\n"
          "% Ex.: \"<b>write ATI\\n\\rMixed\\20Text and \\20\\21\\ff\"</>"),
    HELPK("Send bytes over this UART") },

  // contains common entries and a NULL entry at the end
  KEYWORDS_END
};

//I2C subderictory keywords
//
static const struct keywords_t keywords_i2c[] = {

  KEYWORDS_BEGIN

  { "up", cmd_i2c_up, 3,
    HELPK("% \"<b>up</> <i>SDA SCL CLOCK</>\"\r\n"
          "%\r\n"
          "% Initialize I2C interface X, use pins SDA/SCL, clock rate CLOCK\r\n"
          "% Ex.: up 21 22 100000 - enable i2c at pins sda=21, scl=22, 100kHz clock"),
    HELPK("Initialize interface (pins and speed)") },

  { "clock", cmd_i2c_clock, 1,
    HELPK("% \"<b>clock</> <i>SPEED</>\"\r\n"
          "%\r\n"
          "% Set I2C master clock (i2c must be initialized)\r\n"
          "% Ex.: clock 100000 - Set i2c clock to 100kHz"),
    HELPK("Set clock") },

  { "scan", cmd_i2c_scan, NO_ARGS,
    HELPK("% \"<b>scan</>\"\r\n"
          "%\r\n"
          "% Scan I2C bus X for devices. Interface must be initialized!"),
    HELPK("Scan i2c bus for devices") },

  { "write", cmd_i2c_write, MANY_ARGS,
    HELPK("% \"<b>write</> <i>ADDR D1<i> [<o>D2 ... Dn</>]</>\"\r\n"
          "%\r\n"
          "% Write bytes D1..Dn (hex values) to address ADDR on I2C bus X\r\n"
          "% Ex.: <b>write 0x57 0 0xff</> - write 2 bytes to address 0x57: 0 and 255"),
    HELPK("Send bytes to the device") },

  { "read", cmd_i2c_read, 2,
    HELPK("% \"<b>read</> <i>ADDR SIZE</></>\"\r\n"
          "%\r\n"
          "% Read SIZE bytes from a device at address ADDR\r\n"
          "% Ex.: read 0x68 7 - read 7 bytes from device address 0x68"),
    HELPK("Read data from an I2C device") },

  { "down", cmd_i2c_down, NO_ARGS,
    HELPK("% \"<b>down</>\"\r\n"
          "%\r\n"
          "% Shutdown I2C interface X"),
    HELPK("Shutdown i2c interface") },


  KEYWORDS_END
};

#if WITH_SPI
//spi subderictory keywords list
//
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

//'sequence' subderictory keywords list
//
static const struct keywords_t keywords_sequence[] = {

  KEYWORDS_BEGIN

  { "eot", cmd_seq_eot, 1,
    HELPK("% \"<b>eot</> <i>high|low</>\"\r\n"
          "%\r\n"
          "% End of transmission: pull the line high or low at the\r\n"
          "% end of a sequence. Default is \"low\""),
    HELPK("End-of-Transmission pin state") },

  { "tick", cmd_seq_tick, 1,
    HELPK("% \"<b>tick</> <i>TIME</>\"\r\n"
          "%\r\n"
          "% Set the sequence tick time: defines a resolution of a pulse sequence.\r\n"
          "% Expressed in microseconds, can be anything between 0.0125 and 3.2\r\n"
          "% Ex.: <b>tick 0.1</> - set resolution to 0.1 microsecond (i.e. 1 tick = 0.1 usec)"),
    HELPK("Set resolution") },

  { "zero", cmd_seq_zeroone, 2,
    HELPK("% \"<b>zero</> <i>LEVEL/DURATION</> [<o>LEVEL2/DURATION2</>]\"\r\n"
          "%\r\n"
          "% Define a logic \"0\"\r\n"
          "% Ex.: <b>zero 0/50</>      - 0 is a level: LOW for 50 ticks\r\n"
          "% Ex.: <b>zero 1/50 0/20</> - 0 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks"),
    HELPK("Define a zero") },

  { "zero", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

  { "one", cmd_seq_zeroone, 2,
    HELPK("% \"<b>one</> <i>LEVEL/DURATION</> [<o>LEVEL2/DURATION2</>]\"\r\n"
          "%\r\n"
          "% Define a logic \"1\"\r\n"
          "% Ex.: <b>one 1/50</>       - 1 is a level: HIGH for 50 ticks\r\n"
          "% Ex.: <b>one 1/50 0/20</>  - 1 is a pulse: HIGH for 50 ticks, then LOW for 20 ticks"),
    HELPK("Define an one") },

  { "one", cmd_seq_zeroone, 1, HIDDEN_KEYWORD },  //1 arg command

  { "bits", cmd_seq_bits, 1,
    HELPK("% \"<b>bits</> <i>STRING</>\"\r\n"
          "%\r\n"
          "% A bit pattern to be used as a sequence. STRING must contain only 0s and 1s\r\n"
          "% Overrides previously set \"levels\" command\r\n"
          "% See commands \"one\" and \"zero\" to define \"1\" and \"0\"\r\n"
          "%\r\n"
          "% Ex.: <b>bits 11101000010111100</>  - 17 bit sequence"),
    HELPK("Set pattern to transmit") },

  { "levels", cmd_seq_levels, MANY_ARGS,
    HELPK("% \"<b>levels</> <o>L1/D1 L2/D2 ... Ln/Dn</>\"\r\n"
          "%\r\n"
          "% A bit pattern to be used as a sequnce. L is either 1 or 0 and \r\n"
          "% D is the duration measured in ticks [0..32767] (see \"tick\" command) \r\n"
          "% Overrides previously set \"bits\" command\r\n"
          "%\r\n"
          "% Ex.: <b>levels 1/50 0/20 1/100 0/500<b>  - HIGH 50 ticks, LOW 20, HIGH 100\r\n"
          "%                                            and 0 for 500 ticks\r\n"
          "% Ex.: <b>levels 1/32767 1/17233 0/32767 0/7233</> - HIGH for 50000 ticks,\r\n"
          "%                                                    LOW for 40000 ticks"),
    HELPK("Set levels to transmit") },

  { "modulation", cmd_seq_modulation, 3,
    HELPK("% \"<b>modulation</> <i>FREQ</> [ <o>DUTY [low|high]</> ]\"\r\n"
          "%\r\n"
          "% Enables/disables an output signal modulation with frequency FREQ\r\n"
          "% Optional parameters are: DUTY (from 0 to 1) and LEVEL (either high or low)\r\n"
          "%\r\n"
          "% Ex.: <b>modulation 100</>         - modulate all 1s with 100Hz, 50% duty cycle\r\n"
          "% Ex.: <b>modulation 100 0.3 low</> - modulate all 0s with 100Hz, 30% duty cycle\r\n"
          "% Ex.: <b>modulation 0</>           - disable modulation\r\n"),
    HELPK("Enable/disable modulation") },

  { "modulation", cmd_seq_modulation, 2, HIDDEN_KEYWORD },
  { "modulation", cmd_seq_modulation, 1, HIDDEN_KEYWORD },

  { "show", cmd_seq_show, 0, "Show sequence", NULL },

  KEYWORDS_END
};

#if WITH_FS
// Filesystem commands. this commands subdirectory is enabled
// with "files" command /cmd_files_if()/
//
static const struct keywords_t keywords_files[] = {

  KEYWORDS_BEGIN

  { "mount", cmd_files_mount_sd, 6,
    HELPK("% \"<b>mount vspi|hspi|fspi MISO MOSI CLK CS</> <o>[SPI_FREQ] [/MOUNT_POINT]</>\"\r\n"
          "%\r\n"
          "% Mount a FAT filesystem located on SD card connected to SPI bus\r\n"
          "%\r\n"
          "% <i>1st argument</>: SPI bus to use (<i>hspi</> is the safest choise)\r\n"
          "% <i>MISO, MOSI, CLK</> and <i>CS</> are SPI pins to use (19,23,18 and 5 for example)\r\n"
          "% <o>SPI_FREQ</> : optional parameter, SPI frequency in kHz (20000 if not set)\r\n"
          "% <o>/MOUNT_POINT</> - A path, starting with \"/\" where filesystem will be mounted.\r\n"
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
    HELPK("% \"<b>mount</>\"\r\n"
          "%\r\n"
          "% Command \"mount\" <u>without arguments</> displays information about partitions\r\n"
          "% and mounted file systems (mount point, FS type, total/used counters)"),
    NULL },

  { "mount", cmd_files_mount, 2,
    HELPK("% \"<b>mount LABEL</> <o>[/MOUNT_POINT]</>\"\r\n"
          "%\r\n"
          "% Mount a filesystem located on built-in SPI FLASH\r\n"
          "%\r\n"
          "% <i>LABEL</>        - SPI FLASH partition label\r\n"
          "% <o>/MOUNT_POINT</> - A path, starting with \"/\" where filesystem will be mounted.\r\n"
          "% If mount point is omitted then \"/\" + LABEL will be used as a mountpoint\r\n"
          "%\r\n"
          "% Ex.: mount ffat /fs - mount partition \"ffat\" at directory \"/fs\"\r\n"
          "% Ex.: mount ffat     - mount partition \"ffat\" at directory \"/ffat\""),
    NULL },

    { "mount", cmd_files_mount, 1, HIDDEN_KEYWORD },


  { "unmount", cmd_files_unmount, 1,
    HELPK("% \"<b>unmount</> <o>[/MOUNT_POINT]</>\"\r\n"
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
 

  KEYWORDS_END
};
#endif  //WITH_FS

// root directory commands
// These commands are available immediately after espshell startup, they are also available inside of
// of any other subderictory
//
static const struct keywords_t keywords_main[] = {

  KEYWORDS_BEGIN

  { "uptime", cmd_uptime, NO_ARGS,
    HELPK("% \"<b>uptime</>\"\r\n% Shows time passed since last boot; shows restart cause"), "System uptime" },

  // System commands
  { "cpu", cmd_cpu_freq, 1,
    HELPK("% \"<b>cpu FREQ</>\"\r\n% Set CPU frequency to FREQ Mhz"), "Set/show CPU parameters" },

  { "cpu", cmd_cpu, NO_ARGS,
    HELPK("% \"<b>cpu</>\"\r\n% Show CPUID and CPU/XTAL/APB frequencies"), NULL },

  { "suspend", cmd_suspend, NO_ARGS,
    HELPK("% \"<b>suspend</>\"\r\n% Suspend sketch execution (Hotkey: Ctrl+C). Resume with \"resume\""), "Suspend sketch/task execution" },

  { "suspend", cmd_suspend, 1,
    HELPK("% \"<b>suspend <i>TASK_ID</>\"\r\n% Suspend an arbitrary FreeRTOS task"), NULL },

  { "resume", cmd_resume, NO_ARGS,
    HELPK("% \"<b>resume</>\"\r\n% Resume sketch execution"), "Resume sketch/task execution" },

  { "resume", cmd_resume, 1,
    HELPK("% \"<b>resume <i>TASK_ID</>\"\r\n% Resume an arbitrary FreeRTOS task"), NULL },

  { "kill", cmd_kill, 2,
    HELPK("% \"<b>kill <o>[-term|-kill|-9|-15] <i>TASK_ID</>\"\r\n"
          "% Send a signal to an arbitrary task\r\n"
          "% If <i>-9</> (or <i>-kill</>) option is used then task is deleted (unsafe)\r\n"
          "% No options, <i>-term</> or <i>-15</>: ask a task to finish (safe)\r\n"
          "% Examples:\r\n"
          "% kill 0x3fff0000      Terminates tasks in a safe way (using task notifications)\r\n"
          "% kill -9 0x3fff0000 - Terminates tasks forcefully (task deletion)"),"Kill tasks" },

  { "kill", cmd_kill, 1, HIDDEN_KEYWORD },

  { "reload", cmd_reload, NO_ARGS,
    HELPK("% \"<b>reload</>\"\r\n% Restarts CPU"), "Reset CPU" },

  { "nap", cmd_nap, 1,
    HELPK("% \"<b>nap SEC</>\"\r\n%\r\n% Put the CPU into light sleep mode for SEC seconds."), "CPU sleep" },

  { "nap", cmd_nap, NO_ARGS,
    HELPK("% \"nap\"\r\n%\r\n% Put the CPU into light sleep mode, wakeup by console"), NULL },

  // Interfaces (UART,I2C, RMT, FileSystem..)
  { "iic", cmd_i2c_if, 1,
    HELPK("% \"<b>iic X</>\" \r\n%\r\n"
          "% Enter I2C interface X configuration mode \r\n"
          "% Ex.: iic 0 - configure/use interface I2C0"),
    "I2C commands" },
#if WITH_SPI
#  warning "SPI submodule is barely functional and is under development now"
  { "spi", cmd_spi_if,1,
    HELPK("% \"<b>spi [fspi|hspi|vspi]</>\" \r\n%\r\n"
          "% Enter SPI interface configuration mode \r\n"
          "% Ex.: spi vspi - configure/use interface SPI3 (VSPI)"),
    HELPK("SPI commands") },
#endif
  { "uart", cmd_uart_if, 1,
    HELPK("% \"<b>uart X</>\"\r\n"
          "%\r\n"
          "% Enter UART interface X configuration mode\r\n"
          "% Ex.: uart 1 - configure/use interface UART 1"),
    "UART commands" },

  { "sequence", cmd_seq_if, 1,
    HELPK("% \"<b>sequence X</>\"\r\n"
          "%\r\n"
          "% Create/configure a sequence\r\n"
          "% Ex.: sequence 0 - configure Sequence0"),
    "Sequence configuration" },

#if WITH_FS
  { "files", cmd_files_if, NO_ARGS,
    HELPK("% \"<b>files</>\"\r\n"
          "%\r\n"
          "% Enter files & file system operations mode"),
    "File system access" },
#endif

  // "show iomux" goes first, to define a /.brief/ for subsequent entries.
  // There is MANY_ARGS modifier even though number of arguments is known and is 1: this is just to route all "show ARG1 ARG2 ... ARGn"
  // commands to cmd_show(). It simply means that this command always matches any command starting with "show ".
  //
  // Other "show" commands below are lacking a command handler pointer and are here just for help lines
  // 
  { "show", cmd_show, MANY_ARGS,              // <---  this handler catches all "show" command variants
    HELPK("% \"<b>show <i>iomux</>\"\r\n"
          "%\r\n"
          "% Display IO_MUX functions available for each pin\r\n"
          "% Displays an IO_MUX function currently assigned for every pin"),"Display system information"},

  // Entries below are only for the /full/ help line (/brief/ line is copied from the first "show" entry 
  { "show", HELP_ONLY,
    HELPK("% \"<b>show <i>cpuid</>\"\r\n"
          "%\r\n"
          "% Display CPU ID information, used components versions and uptime\r\n"
          "% CPU temperature in Celsius is also displayed"),NULL},

  { "show", HELP_ONLY,
    HELPK("% \"<b>show <i>pwm</>\"\r\n"
          "%\r\n"
          "% Display currently active PWM generators:\r\n"
          "% GPIO number, frequency and duty cycle"),NULL},

  { "show",  HELP_ONLY,
    HELPK("% \"<b>show <i>counters</>\"\r\n"
          "%\r\n"
          "% Pulse counters / frequency meters states and values\r\n"
          "% Depending on a SoC used it may be up to 8 hardware counters"),NULL},

  { "show", HELP_ONLY,
    HELPK("% \"<b>show <i>sequence</> NUMBER</>\"\r\n"
          "%\r\n"
          "% Display sequence configuration for given index:\r\n"
          "% \"show sequence 6\"  - display Sequence #6 configuration"),NULL},

  { "show", HELP_ONLY,
    HELPK("% \"<b>show <i>mount</> [<o>/PATH</>]\"\r\n"
          "%\r\n"
          "% Display information about mounted filesystems, partitions.\r\n"
          "% \"show mount\"           - display filesystem information\r\n"
          "% \"show mount /my_disk\"  - display information about mountpoint \"/my_disk\""), NULL},

  // shadowed entry. For helptext only
  { "show", HELP_ONLY,
    HELPK("% \"<b>show <i>memory</> <i>ADDRESS</> [<o>COUNT</>] [<o>unsigned|signed|char|int|short|float|void *</>]\"\r\n"
          "%\r\n"
          "% Display COUNT elements starting from the memory address ADDRESS\r\n"
          "% Data type can be provided (e.g. \"show mem 0x3fff0000 10 unsigned int\")\r\n"
          "% to hint espshell on how to display individual data items. \r\n"
          "% Default data type is \"unsigned char\"\r\n"
          "% Address is either decimal or hex (with or without leading \"0x\")\r\n%\r\n"
          "% COUNT is optional and its default value is 256 bytes. Can be decimal or hex\r\n"
          "% NOTE: if type specifier is used and COUNT is not set, then COUNT defaults to 1\r\n"
          "%\r\n"
          "% \"<b>show <i>memory</>\"\r\n"
          "%\r\n"
          "% Display HEAP information / availability"),NULL},

  // Shell input/output settings
  { "tty", cmd_tty, 1, HIDDEN_KEYWORD },

  { "echo", cmd_echo, 1,
    HELPK("% \"<b>echo [on|off|silent]</>\"\r\n"
          "% Echo user input on/off (default is on)\r\n"
          "% Without arguments displays current echo state\r\n"), 
    HELPK("Enable/Disable user input echo") },

  { "echo", cmd_echo, NO_ARGS, HIDDEN_KEYWORD },  //hidden command, displays echo status

  // 1 arg "pin" command is declared first, because cmd_pin() has MANY_ARGS and will match any pin command otherwhise
  { "pin", cmd_pin_show, 1,
    HELPK("% \"<b>pin X</>\"\r\n"
          "% Show pin X configuration and digital value\r\n"
          "% Ex.: \"pin 2\" - show GPIO2 information"), 
    HELPK("GPIO commands") },

  { "pin", cmd_pin, MANY_ARGS,
    HELPK("% \"<b>pin X [<o>ARG1 | ARG2 | ... | ARGn]*</>\"\r\n"
          "% Manipulate pin (GPIO) state, configuration, level, signal routing, etc\r\n"
          "% Accepts a list of keywords (or just 1 keyword): \r\n"
          "%\r\n"
          "% \"<i>high</>\" & \"<i>low</>\"  - Set pin to \"1\" or \"0\"\r\n"
          "% \"<i>up</>\", \"<i>down</>\", \"<i>out</>\", \"<i>in</>\" and \"<i>open</>\" - enable PULL_UP, PULL_DOWN\r\n"
          "%                     OUTPUT, INPUT or OPEN_DRAIN mode for the pin\r\n"
          "% \"<i>save</>\" & \"<i>load</>\" - Save / Restore pin configuration\r\n"
          "% \"<i>read</>\" & \"<i>aread</>\" - Perform digital or analog read\r\n"
          "% \"<i>pwm</>\"Enable PWM signal on the pin (generator)\r\n"
          "% \"<i>sequence</>\"  - Send an RMT (IR_Remote) sequence\r\n"
          "% \"<i>matrix</>\" & \"<i>iomux</>\" - GPIO_Matrix and IO_MUX functions\r\n"
          "% \"<i>hold</>\" & \"<i>release</>\" - freeze/unfreeze pin state and level.\r\n"          
          "% \"<i>delay</>\"  - Next keyword will be delayed\r\n"
          "% \"<i>loop</>\"  - Execute whole \"pin\" command multiple times\r\n"
          "%\r\n"
          "% Examples:\r\n%\r\n"
          "% pin 1 read aread         -pin1: read digital and then analog values\r\n"
          "% pin 1 in out up          -pin1 is INPUT and OUTPUT with PULLUP\r\n"
          "% pin 1 save high load     -save pin state, set HIGH(1), restore pin state\r\n"
          "% pin 1 high               -pin1 set to logic \"1\"\r\n"
          "% pin 1 high delay 100 low -set pin1 to logic \"1\", after 100ms to \"0\"\r\n"
          "% pin 1 pwm 5000 0.3       -set 5kHz, 30% duty square wave output\r\n"
          "% pin 1 pwm 0 0            -disable PWN on GPIO1\r\n"
          "% pin 1 high delay 500 low delay 500 loop 10 - Blink a led 10 times\r\n%\r\n"
          "% (see \"docs/Pin_Commands.txt\" for more details & examples)\r\n"),
    NULL },

  // PWM generation
  { "pwm", cmd_pwm, 4,
    HELPK("% \"<b>pwm PIN</> <o>FREQ</> [<o>DUTY</> [CHANNEL]]\"\r\n"
          "% \"<b>pwm PIN</> <i>off</>\"\r\n"
          "%\r\n"
          "% Start or stop a PWM generator on pin PIN, frequency FREQ Hz and duty cycle\r\n"
          "%of DUTY. Keywords \"<b>off</>\" or \"0\" are used to stop PWM output.\r\n"
          "% FREQ is in range [0 .. " xstr(PWM_MAX_FREQUENCY) "] Hz\r\n"
          "%\r\n"
          "% DUTY is optional (0.5 (50%) if omitted); DUTY range is [0.00 .. 1.00])\r\n"
          "% Note that above 150kHz duty resolution may drops to 8 bits, at 10MHz it is 2 bits\r\n"
          "% Resolution is autoselected but can be overriden with \"var ledc_res BITS\"\r\n"
          "%\r\n"
          "% CHANNEL is optional parameter, selects PWM channel to be used (0..15)\r\n"
          "%Examples:\r\n"
          "% pwm <g>2 <i>1000</>      - enable PWM of 1kHz, 50% duty cycle on pin 2\r\n"
          "% pwm <g>2</> <i>100 0.15</> - enable PWM of 100 Hz, 15% duty cycle on pin 2\r\n"
          "% pwm <g>2</>                 - disable PWM on pin 2\r\n"
          "% pwm <g>2</> <i>off</>             - same as above"),  "PWM output" },

  { "pwm", cmd_pwm, 3, HIDDEN_KEYWORD }, // pwm PIN FREQ DUTY
  { "pwm", cmd_pwm, 2, HIDDEN_KEYWORD }, // pwm PIN FREQ, pwm PIN off, pwm PIN 0
  { "pwm", cmd_pwm, 1, HIDDEN_KEYWORD }, // pwm PIN

  // Pulse counting/frequency meter
  { "count", cmd_count, MANY_ARGS,
    HELPK("% \"<b>count PIN</> [<o>NUMBER</>] [<o>trigger</> | <o>filter LENGTH</>]*\"\r\n%\r\n"
          "% Count pulses on pin PIN for NUMBER milliseconds (default value is 1 second)\r\n"
          "% Optional \"trigger\" keyword suspends the counter until the first pulse\r\n"
          "% Optional \"filter LEN\" keyword ignores pulses <u>shorter than</> LEN nanoseconds\r\n"
          "%\r\n"
          "% Examples: \r\n"
          "% \"<b>count 4</>\"             - Count pulses & measure frequency on GPIO4 for 1000ms\r\n"
          "% \"<b>count 4 2000</>\"        - Same as above but measurement time is 2 seconds\r\n"
          "% \"<b>count 4 filter 100</>\"  - Count pulses, discarding those <u>shorter than</> 100ns\r\n"
          "% \"<b>count 4 999999 &</>\"    - Count pulses in <u>a background</> for ~1000 seconds\r\n"
          "% \"<b>count 4 trigger</>\"     - Wait for the first pulse, then start to count\r\n"
          "% \"<b>count 4 2000 trig &</>\" - Wait for the 1st pulse pulse, then start to count pulses for\r\n"
          "%                                 2 seconds in a background"), "Pulse counter" },

  { "count", HELP_ONLY,
    HELPK("% \"<b>count PIN</> <i>clear</>\"\r\n"
          "% Clear counters associated with pin PIN. These may be stopped, running or in \"trigger\" state\r\n"
          "%\r\n"
          "% \"<b>count 4 clear</>\"       - Clear all counters associated with GPIO4\r\n"), NULL },
    
#if WITH_ESPCAM
  { "camera", cmd_cam, MANY_ARGS, 
    HELPK("% \"<b>camera</> <i>up</> [<o>MODEL | custom</>] [<o>clock FREQUENCY</>] [<o>i2c NUMBER</>]\r\n" \
          "% Detect & initialize the camera\n\r" \
          "%\n\r" \
          "% <i>MODEL</>    - The camera model; Supported models list is here: \"show camera models\"\n\r" \
          "%            Use word \"custom\" to specify custom camera model. (see \"camera pinout\")\r\n" \
          "% clock <i>HZ</> - Set XCLK frequency, Hertz. Default value is 16000000 (16Mhz)\n\r" \
          "% i2c <i>NUM</>  - Use existing i2c interface, ignore SDA & SCL pins\r\n%\r\n"
          "%\r\n"
          "%Examples:\r\n"
          "% camera up <i>ai-thinker</>  - Initialize Ai-Thinker ESP32Cam\r\n"
          "% camera up <i>custom</> clock <i>20000000</>  - Initialize custom pinout camera at 20Mhz"), HELPK("Camera commands")
  },

  { "camera", HELP_ONLY,
    HELPK("% \"<b>camera</> <i>down|settings|capture|filesize|transfer</>\" - Camera commands:\n\r" \
          "%\n\r" \
          "% <b>settings</> - Enter camera setting\n\r" \
          "% <b>capture</>  - Capture a single shot\n\r" \
          "% <b>filesize</> - Display last captured shot file size\n\r" \
          "% <b>transfer</> - Transmit the last shot over uart\n\r" \
          "% <b>down</>     - Camera shutdown & power-off"), NULL },


  { "camera", HELP_ONLY,
    HELPK("% \"<b>camera</> <i>pinout</> <g>PWDN RESET XCLK SDA SCL D7 D6 D5 D4 D3 D2 D1 D0 VSYNC HREF PCLK</>\r\n"
          "% Set custom pinout for the camera model \"<i>custom</>\"\r\n"
          "% Later it can be initialized with \"cam up custom\"\n\r"
          "%\n\r"
          "% Command requires 16 arguments (pin numbers): i2c bus, data pins, etc\r\n"
          "% If your board don't have / don't use certain pins then use \"-1\" as\r\n"
          "% a pin number to disable it.\r\n"
          "% Example:\r\n"
          "% camera <i>pinout</> <g>-1 -1 1 2 3 4 5 6 7 8 9 10 11 12 13 14</>\r\n"
          "% In example above, pins PWDN & RESET are not used and thus set to -1\r\n"
          "%\r\n"
          "% This custom pinout can be activated via <i>camera up custom</>\r\n"
          "% Note that pin names D7..D0 are synonyms for Y9..Y2: D0=Y2, D1=Y3 ..."), NULL},

#endif //WITH_ESPCAM
  // TODO: split helplines between different entries
  { "var", cmd_var, 2,
    HELPK("% \"<b>var</> [<o>VARIABLE_NAME</> [<o>NEW_VALUE</>]]</>\"\r\n%\r\n"
          "% Set sketch variable to new value\r\n"
          "% VARIABLE_NAME is the variable name (\"var\" to see the list of all vars)\r\n"
          "% NEW_VALUE can be integer or float point values, positive or negative\r\n"
          "%\r\n"
          "% Examples:\r\n"
          "% <b>var</>             - List all registered sketch variables\r\n"
          "% <b>var</> <i>button1</>     - Display current value of \"button1\" sketch variable\r\n"
          "% <b>var</> <i>angle</> <g>-12.3</> - Set sketch variable \"angle\" to \"-12.3\"\r\n"
          "%\r\n"
          "% Note#1: Partial (shortened) variable names can be used\r\n"
          "% Note#2: Use prefix \"0x\" for hex, \"0\" for octal or \"0b\" for binary numbers"), "Sketch variables" },

  { "var", cmd_var, 1,
    HELPK("% \"<b>var</> <g>NUMBER</>\"\r\n"
          "%\r\n"
          "% Display a NUMBER in differtent bases and perform unsafe C-style\r\n"
          "% cast of a memory content\r\n"
          "% NUMBER can be anything that converts to a number. Use \"0b\",\"0x\" or \"0\"\r\n"
          "% prefixes to enter binary, hexadecimal or octal numbers."
          "%\r\n"
          "% Examples:\r\n"
          "% var -1234       - Get information on a decimal number -1234\r\n"
          "% var 0x1234      -                 on a hex number..\r\n"
          "% var 01234       -                 on an octal number..\r\n"
          "% var 0b1001110   - and on a binary number"), NULL },

  { "var", cmd_var_show, NO_ARGS, HIDDEN_KEYWORD },


  { "history", cmd_history, 1, HIDDEN_KEYWORD },
  { "history", cmd_history, NO_ARGS, HIDDEN_KEYWORD },
#if WITH_COLOR
  { "colors", cmd_colors, 1, HIDDEN_KEYWORD },
  { "colors", cmd_colors, NO_ARGS, HIDDEN_KEYWORD },
#endif

  KEYWORDS_END
};

#if WITH_ESPCAM
// Commands for dealing with video camera (ai-thinker, m5stack, dfrobot etc or custom)
// Main commands are located in keywords_main and handled by cmd_cam(). Keywords below is extra commands 
// under "camera settings" subdirectory
//
// Handlers are implemented in espcam.h
//
static struct keywords_t keywords_espcam[] = {

  KEYWORDS_BEGIN

  { "gain", cmd_camera_set_gain, 1, 
    HELPK("\"<b>gain</> <i>auto</>|(0..30)\"\n\r"
          "% Set camera sensetivity (auto or 0..30)"), "Gain" },

  { "balance", cmd_camera_set_balance, 1, 
    HELPK("% <b>balance</> <i>none</>|<i>auto</>|<i>sunny</>|<i>cloudy</>|<i>office</>|<i>home</>\n\r"
          "% Set camera White Balance correction"),  "White balance" },

  { "exposure", cmd_camera_set_exposure, 2, 
    HELPK("% exposure auto [-2..2]\n\r"
          "% \n\r"
          "% Set camera exposure mode to auto & optional AE shift:\r\n"
          "% exposure auto     - set exposure to auto\r\n"
          "% exposure auto -2  - same as above + correction factor of -2"), "Exposure" },

  { "exposure", cmd_camera_set_exposure, 1, 
    HELPK("% exposure (0..1200)\n\r"
          "%\n\r"
          "% Set manual camera exposure:\r\n"
          "% exposure 800 - Set exposure parameter to 800"),"Exposure" },


  { "brightness", cmd_camera_set_qbcss, 1,
      HELPK("% brightness (-2..2)\n\r"
          "%\n\r"
          "% Set brightness correction (gamma):\r\n"
          "% exposure -1 - Make things darker"), "Brightness" },

  { "saturation", cmd_camera_set_qbcss, 1,
      HELPK("% saturation (-2..2)\n\r"
          "%\n\r"
          "% Set saturation correction (vivid colors):\r\n"
          "% saturation 2 - Set saturation to its maximum"), "Saturation" },

  { "contrast", cmd_camera_set_qbcss, 1,
      HELPK("% contrast (-2..2)\n\r"
          "%\n\r"
          "% Set contrast correction:\r\n"
          "% contrast 2 - Set contrast parameter to its maximum"), "Contrast" },

  { "sharpness", cmd_camera_set_qbcss, 1,
      HELPK("% sharpness (-2..2)\n\r"
          "%\n\r"
          "% Set image sharpness correction:\r\n"
          "% sharpness -2 - Decrease sharpness to its minimum"), "Sharpness" },

  { "compression", cmd_camera_set_qbcss, 1,
      HELPK("% compression (2..63)\n\r"
          "%\n\r"
          "% Set JPEG compression factor (2 - best quality, 63 - smallest size)\r\n"
          "% compression 4 - Higher picture quality, larger file"), "compression" },

  { "size", cmd_camera_set_size, 1, 
    HELPK("% \"size vga|svga|xga|hd|sxga|uxga\"\n\r"
          "\n\r"
          "% Set picture size:\n\r"
          "% vga  - 640x480\n\r"
          "% svga - 800x600\n\r"
          "% xga  - 1024x768\n\r"
          "% hd   - 1280x720\n\r"
          "% sxga - 1280x1024\n\r"
          "% uxga - 1600x1200 (Default)"), "Resolution / Picture size" },

  KEYWORDS_END
};
#endif // WITH_ESPCAM

//current keywords list in use and a barrier to protect it.
static BARRIER(keywords_mux);
static const struct keywords_t *keywords = keywords_main;


// Called from cmd_uart_if(), cmd_i2c_if(),cmd_seq_if() and cmd_files_if and others to set a new command list (command directory); 
// displays user supplied text,  returns a pointer to the keywords tree used before
//
static const struct keywords_t *change_command_directory(
                                    unsigned int context,         // An arbitrary number which will be stored until next directory change
                                    const struct keywords_t *dir, // New command list 
                                    const char *prom,             // New prompt
                                    const char *text) {           // User-defined text which will be displayed after entering new directory
  const struct keywords_t *old_dir;

  barrier_lock(keywords_mux);

  Context = context;
  old_dir = keywords;
  keywords = dir;
  prompt = prom;

  barrier_unlock(keywords_mux);

  if (text) {
    HELP(q_printf("%% Entering %s mode. Ctrl+Z or \"exit\" to return\r\n"
                  "%% Main commands are still available (but not visible in \"?\" command list)\r\n", text));
  }

  return old_dir;
}


//"exit"
//"exit exit"
// exits from a command subderictory or closes the shell ("exit exit")
//
static int cmd_exit(int argc, char **argv) {
  // Change directory to main, leave Context untouched, restore main prompt
  // If "exit" was executed from the main tree, then either exit the shell or display a hint
  if (change_command_directory(Context, keywords_main, PROMPT, NULL) == keywords_main) {
    if (argc > 1 && !q_strcmp(argv[1], "exit"))
      Exit = true;
    else {
      HELP(q_print("% Not in a subdirectory; (to close the shell use \"exit ex\")\r\n"));
    }
  }
  return 0;
}
#endif // #if COMPILING_ESPSHELL

