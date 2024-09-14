 * ESP32Shell for the Arduino Framework by vvb333007 <vvb@nym.hush.com>
 * Uses editline library (see copyright information below)
 *
 * WHAT IS THIS:
 * -------------
 * This is a debugging/development tool for use with Arduino projects on
 * ESP32 hardware. Provides a command line interface (CLI) on serial port
 * running in parallel to user Arduino sketch. This tool assumes that Arduino's
 * Serial is uart0 at startup
 * 
 * User can enter and execute commands in a way similar to Linux shell: either
 * using Arduino IDE's Serial Monitor or better using a proper serial communication
 * software (TeraTerm, PuTTY or similar or Windows or 'cu' on Linux)
 *
 * See Settings1.jpg and Settings2.jpg for preferred settings of ArduinoIDE Serial
 * Monitor and TeraTerm. Please note that some features will be unavailable if using
 * Arduino Serial Monitor: you will not be able to send Ctrl+C and Ctrl+Z sequences
 * which are useful in debugging GSM modems
 *  
 * HOW TO USE IT IN ARDUINO PROJECT:
 * ---------------------------------
 * This tool does not require user to do ANY changes to their existing
 * sketch/project.
 * Follow these steps:
 * a. Copy espshell.c to your sketch directory (where your .ino file is).
 * b. Compile & upload your sketch as usual. (espshell will gain control 
 *    automatically on startup)
 * c. Enjoy! Now you can access the command line interface via Arduino 
 *     IDE's Serial Monitor or ( better ) thru terminal software: Linux cu, 
 *     Windows TeraTerm or PuTTY.
 *
 *
 * SHELL BRIEF DESCRIPTION AND EXAMPLES OF USE
 * -------------------------------------------
 * 
 * Shell commands include i2c, uart, pin manipulation, tone generator
 * and a pulse counter. There are some basic 'information' commands:
 * memory usage, cpuid, pin; commands for execution flow control: suspend/resume
 * main Arduino loop(), restart and light sleep
 * 
 * Full list of commands is available by typing "?" and pressing <Enter>
 * Description of arguments to the command can be obtained by typing a command
 * with "?" like this:
 * 
 * --------------------Example screen shot------------------------------------
 * esp32#>pin ?
 *
 * "pin X (pullup|pulldown|out|in|analog|open|high|low){1,}"
 * Set GPIO pin number X mode (INPUT, OUTPUT, PULL-UP etc) and level (HIGH or LOW).
 * Ex.: pin 18 out high       - set pin18 to OUTPUT logic "1"
 * Ex.: pin 18 in pullup open - set pin18 INPUT, PULL-UP, OPEN_DRAIN
 * 
 * 
 * "pin X" read|aread
 * Perform digital or analog read on pin X
 * Ex.: pin 18 aread
 * 
 * "pin X"
 * Show current state,mode and logic value (low/high) of the pin X.
 * Ex.: pin 18
 * esp32#>
 * -------------------- End ------------------------------------
 * 
 * For uarts it is possible to talk directly to the device connected to the uart
 * (a GSM modem for example), send/receive strings or bytes/special characters,
 * configuring pins/baudrate. In conjunction with "suspend/resume" commands it
 * is possible to reconfigure given uart without letting the main sketch to notice
 * that
 *
 * i2c commands include 'i2c scan', read/write commands and interface configuration:
 * pins to use and clock frequency 
 * 
 * There are some limited execution control over user sketches: shell allows to
 * suspend/resume main loop(), keeps track of uptime, allow changing of CPU frequency,
 * entering the light sleep mode and waking up.
 *
 * Pin commands allow for changing pin modes, writing/reading  values, changing pullup
 * modes, displaying current pin configuration. There are commands to generate PWM
 * signal on arbitrary pin, and commands to count pulses arrived on given pin
 *
 * NOTE:
 * -----
 * This shell is about hardware. Thats why ere are no commands like "ping" or any other
 * higher level commands. There are million commands to implement but I only implement those
 * i use in my debugging/development process. What commands I miss right now is triggers &
 * pattern generation :). Also I try to keep it small: it is just 1 file and I want to
 * keep it like that
 *
 * EXAMPLES:
 * ---------
 * Some real life examples of I2C and UART commands. The hardware setup is: ESP32 generic
 * devkit(ESP32-WROOM-32D, 30 pin breakout board), with SIMCOM SIM7600E modem on pins 18(RX),
 * 19(TX) and  DS3231+EEPROM breakout board on pins 21 (SDA),22(SCL)
 *
 *  
 * Example #1:
 * A DS3231 RTC clock chip is connected to pins 21 and 22. Check if device is accessible via
 * I2C, read the current time
 * 
 * --------------------- Example screenshot -----------------------------------
 * esp32#>
 * esp32#>iic 0                       // enter I2C interface 0 
 * esp32-i2c#>up 21 22 100000         // setup I2C 0 on pins SDA=21, SCL=22 at speed of 100kHz
 * esp32-i2c#>scan                    // scan the bus
 * Scanning I2C bus 0...
 * Device found at address 57         // EEPROM chip found
 * Device found at address 68         // DS3231 chip found
 * 2 devices found
 * esp32-i2c#>write 68 0              // I2C write
 * Sending 1 bytes over I2C0
 * esp32-i2c#>read 68 7               // I2C read 7 bytes from DS3231
 * I2C0 received 7 bytes:
 * 39 05 20 05 13 09 24               // seconds, minutes, hours, day-of-week, day, month, year
 * esp32-i2c#>down                    // shutdown the interface
 * esp32-i2c#>exit                    // exit i2c mode
 * esp32#>
 * -------------------- End ------------------------------------
 *
 * Example #2: Communicate with SIM7600E GSM modem connected to pins 18 (RX) 
 * and 19 (TX). Issue ATI commands, read the output and exit
 *
 * -------------------- Example screenshot -----------------------------------
 *
 * esp32#>uart 1
 * esp32-uart#>up 18 19 115200       // setup UART1 on pins 18/19 baudrate is 115200
 * esp32-uart#>tap                   // talk to device (connected to UART1) directly
 * Tapping to UART1, CTRL+C to exit
 * ATI                               // user input sent to modem
 * Manufacturer: SIMCOM INCORPORATED // modem reply
 * Model: SIMCOM_SIM7600E
 * Revision: SIM7600M21-A_V1.1
 * IMEI: 861005075537800
 * +GCAP: +CGSM
 *
 * OK
 * 
 * Exit                              // CTRL+C pressed
 * esp32#>
 * -------------------- End ------------------------------------
 *
 * Example #3: Communicate with a device over uart2, send and receive text
 * (alternative to the "uart tap"). Device connected to  pins 21 and 22 is
 * SIMCOM LTE modem SIM7600E:
 * 
 * -------------------- Example screenshot -----------------------------------
 * esp32#>uart 2
 * esp32-uart#>write \ff\ff\CC\r\n    // send junk bytes: 0xff 0xff 0xcc 0x0d 0x0a. ignored by SIMCOM
 * 5 bytes sent 
 * esp32-uart#>write ATI\r\n          // send ATI<CR><LF>
 * 5 bytes sent
 * esp32-uart#>read                   // read reply
 * ATI
 * Manufacturer: SIMCOM INCORPORATED
 * Model: SIMCOM_SIM7600E
 * Revision: SIM7600M21-A_V1.1
 * IMEI: 861005075537800
 * +GCAP: +CGSM
 *
 * OK
 * 
 * 137 bytes read
 * esp32#>
 * -------------------- End ------------------------------------
 *
 *
 *  Enjoy!
