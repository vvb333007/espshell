ESP32Shell for the Arduino Framework by vvb333007 <vvb@nym.hush.com>

WHAT IS THIS:
-------------
 This is a debugging/development tool for use with Arduino projects on
 ESP32 hardware. Provides a command line interface (CLI) on serial port
 running in parallel to user Arduino sketch. This tool assumes that Arduino's
 Serial is uart0 at startup

 User can enter and execute commands (there are many built-in commands) in a way 
 similar to Linux shell: either using Arduino IDE's Serial Monitor or better using
 proper serial communication software (TeraTerm, PuTTY or similar on Windows or 
 'cu' on Linux)
 
 Screenshots Settings1.jpg and Settings2.jpg shows preferred settings of ArduinoIDE 
 Serial Monitor and TeraTerm. Please note that some features will be unavailable if 
 using Arduino Serial Monitor: you will not be able to send Ctrl+C and Ctrl+Z sequences
 which are useful in debugging GSM modems
 
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


NOTE:
-----
This shell is about hardware. Thats why ere are no commands like "ping" or any other
higher level commands. There are million commands to implement but I only implement those
i use in my debugging/development process. What commands I miss right now is triggers &
pattern generation :). Also I try to keep it small: it is just 1 file and I want to
keep it like that

The shell itself is one single .c file; you can use supplied espshell.ino sketch as an 
example: it is a simple LED blink sketch.

