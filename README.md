
WHAT IS THIS:
-------------
 This is a debugging/development tool (a library for Arduino framework) for use 
 with Arduino projects on *ESP32 hardware*.

 Provides a command line interface (CLI) on serial port running in parallel 
 to your Arduino sketch. It is not standalone program - this tool attaches
 to the user sketch (at compile time) and enchances any sketch (even empty one)
 with a shell.

 User can enter and execute commands (there are many built-in commands) in a way 
 similar to Linux shell while their sketch is running. ESPShell can be used either
 from Arduino IDE Serial Monitor or any other communication software like *PuTTY*
 or *TeraTerm*. Linux users have plenty of comm software but even "cu" utility 
 is ok

 This library can be useful for: 

 1. Developers who are interfacing new I2C or UART devices as espshell has commands 
    to create/delete hardware interfaces, send/receive data. 

    Interfacing GPS (uart-based) chips or GSM modems, making libraries for I2C devices.

    Simulating relays behaviour (using "pin" command)

 2. Beginners who wish to play with hardware without actually writing the code

 3. Arduino-compatible board makers: they can pre-install the espshell to their boards

 Written in pure C, can be easily integrated with both C and C++ code


1. Installation (Preinstalled Arduino IDE with esp32 board support package from Espressif is expected):

    1. Copy library folder ("espshell") as is to your <SketchDirectory>/libraries/
    2. Restart Arduino IDE

2. Usage: 

    1. Add #include "espshell.h" to your sketch
    2. Compile, upload
    3. Open terminal monitor, type "?" and press <Enter>

3. Documentation (a bit outdated here and there):

    1. English (most up to date) is in "espshell/docs/"
    2. Russian (is up to date when I have time) is in "espshell/docs/ru_RU/"

4. Development:

    ESPShell code is written in pure C, developer friendly and well-commented. There are few areas which calls for
    attention and they are can be found as TODO: throughout the code. There are docs/PROBLEMS.txt and docs/PLANS.txt
    for further reading

