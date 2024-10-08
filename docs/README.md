ESP32Shell for the Arduino Framework by vvb333007 <vvb@nym.hush.com>

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
 sketch/project.
 
 Follow these steps:
 
 a. Copy espshell.c to your sketch directory (where your .ino file is).
 
 b. Compile & upload your sketch as usual. (espshell will gain control 
    automatically on startup)
    
 c. Enjoy! Now you can access the command line interface via Arduino 
    IDE's Serial Monitor or ( better ) thru terminal software: Linux cu, 
    Windows TeraTerm or PuTTY.

WHAT IS IN "EXTRA" and "DOCS" FOLDERS? DO I NEED THEM?
------------------------------------------------------

To use ESPShell you don't need them. Folder "docs" contains espshell
documentation and examples, and "extra" contains optional files for
accessing additional ESPShell functionality: espshell API header file
and AiThinker ESPCam extension commands. Any files you decide to use
must be copied along with espshell.c to your sketch directory. Read the
"docs/README.md" for more details on that



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
 pins to use and clock frequency. More details on I2C commands in I2C_Commands.txt

 There are some limited execution control over user sketches: shell allows to
 suspend/resume main loop(), keeps track of uptime, allow changing of CPU frequency,
 entering the light sleep mode and waking up.

 Pin commands allow for changing pin modes, writing/reading  values, changing pullup
 modes, displaying current pin configuration. There are commands to generate PWM
 signal on arbitrary pin, and commands to count pulses arrived on given pin.
 Details in Pin_Commands.txt

 Refer to Tone_Generator_And_Counter.txt on how to use squarewave generator and
 count pulses on arbitrary pin

 For pattern generation please read Pulse_Generator.txt

 For ESP32Cam support read extra/README.md

  ![ESPShell I2C EEPROM example](https://github.com/vvb333007/espshell/blob/main/docs/Screenshot_EEPROM_I2C_Read.jpg?raw=true)
 
MEMORY FOOTPRINT/OVERHEAD
-------------------------

ESPShell uses about 30Kbytes of program memory and around 2Kbytes of variables. 
There is also ESPShell task stack size of 5kbytes (can be changed see STACKSIZE 
macro in espshell.c)


NOTE:
-----
This shell is about hardware. Thats why ere are no commands like "ping" or any other
higher level commands. There are million commands to implement but I only implement those
i use in my debugging/development process. What commands I miss right now is triggers &
pattern generation :). Also I try to keep it small: it is just 1 file and I want to
keep it like that

The shell itself is one single .c file; you can use supplied example_blink.ino sketch as an 
example: it is a simple LED blink sketch.

