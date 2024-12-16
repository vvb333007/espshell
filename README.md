BIG FAT WARNING: Documentation is a bit outdated in the part related to installation.
                 Docs are up to date (I hope so) in parts related to commands and
                 general use. Recently the code structure was changed (moved from single
                 file to library)


ESP32Shell for the Arduino Framework by vvb333007 <vvb@nym.hush.com>

<br>
  Русская документация находится тут: https://github.com/vvb333007/espshell/tree/main/docs/ru_RU/
 


WHAT IS THIS:
-------------
 This is a debugging/development tool for use with Arduino projects on
 ESP32 hardware. 

 Provides a command line interface (CLI) on serial port running in parallel 
 to your Arduino sketch. It is not standalone program - this tool attaches
 to the user sketch (at compile time) and enchances any sketch (even empty one)
 with a shell.


 User can enter and execute commands (there are many built-in commands) in a way 
 similar to Linux shell while their sketch is running. ESPShell can be used either
 from Arduino IDE Serial Monitor or any other communication software like PuTTY
 or TeraTerm.


 It can be useful for developers who are interfacing new I2C or UART devices
 as espshell has commands to create/delete hardware interfaces, send/receive
 data. Interfacing GPS (uart-based) chips or GSM modems, making libraries for
 I2C devices
 
 
 
HOW TO USE IT IN ARDUINO PROJECT:
---------------------------------
 This tool DOES NOT REQUIRE user to do ANY changes to their existing
 sketch/project, except for adding "#include "espshell.h" somewhere in your
 sketch: once this is done ESPShell starts automatically when your sketch 
 starts. You can interact with ESPShell using either Arduino IDE's Serial 
 Monitor but for better experience a proper terminal software is reccomended


SHELL BRIEF DESCRIPTION
-------------------------------------------

 Shell commands include i2c, uart, pin manipulation, tone generator
 and a pulse counter. There are some basic 'information' commands:
 memory usage, cpuid, pin; commands for execution flow control: suspend/resume
 main Arduino loop(), restart and light sleep

 Full list of commands is available by typing "?" and pressing <Enter>

 For uarts it is possible to talk directly to the device connected to the uart
 (a GSM modem for example), send/receive strings or bytes/special characters,
 configuring pins/baudrate. In conjunction with "suspend/resume" commands it
 is possible to reconfigure given uart without letting the main sketch to notice
 that. Full description of UART commands can be found in UART_Commands

 i2c commands include 'i2c scan', read/write commands and interface configuration:
 pins to use and clock frequency. More details on I2C commands in "docs/I2C_Commands.txt"

 There are some limited execution control over user sketches: shell allows to
 suspend/resume main loop(), keeps track of uptime, allow changing of CPU frequency,
 entering the light sleep mode and waking up.

 Pin commands allow for changing pin modes, writing/reading  values, changing pullup
 modes, displaying current pin configuration. There are commands to generate PWM
 signal on arbitrary pin, and commands to count pulses arrived on given pin.
 Details in "docs/Pin_Commands.txt"

 Refer to "docs/Tone_Generator_And_Counter.txt" on how to use squarewave generator and
 count pulses on arbitrary pin

 For pattern generation please read "docs/Pulse_Generator.txt"

 For ESP32Cam support read "extra/README.md"

Example session: initialize I2C interface and read I2C EEPROM by means of ESPShell:

  ![ESPShell I2C EEPROM example](https://github.com/vvb333007/espshell/blob/main/docs/Screenshot_EEPROM_I2C_Read.jpg?raw=true)

MEMORY FOOTPRINT/OVERHEAD
-------------------------

ESPShell uses about 60Kbytes of program memory and around 5Kbytes of variables 
(i.e. RAM).
There is also ESPShell task stack size of 5kbytes (can be changed see STACKSIZE 
macro in espshell.c). So total RAM usage is around 10kbytes and flash usage is 
around 60kbytes.


Disabling the ESPShell help system (WITH_HELP macro in espshell.h or espshell.c)
saves about 2Kbytes by decreased code, and another 18Kbytes from removed help strings.
There are other macro definition which can be used to further decrease memory usage:
one can disable filesystem support (all, or unused only), disable command history
support, decrease number of available sequences or/and mountpoints



