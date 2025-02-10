<pre>                                                                                  
                      ,-.----.                                                    
    ,---,.  .--.--.   \    /  \              ,---,                ,--,    ,--,    
  ,'  .' | /  /    '. |   :    \           ,--.' |              ,--.'|  ,--.'|    
,---.'   ||  :  /`. / |   |  .\ :          |  |  :              |  | :  |  | :    
|   |   .';  |  |--`  .   :  |: | .--.--.  :  :  :              :  : '  :  : '    
:   :  |-,|  :  ;_    |   |   \ :/  /    ' :  |  |,--.   ,---.  |  ' |  |  ' |    
:   |  ;/| \  \    `. |   : .   /  :  /`./ |  :  '   |  /     \ '  | |  '  | |    
|   :   .'  `----.   \;   | |`-'|  :  ;_   |  |   /' : /    /  ||  | :  |  | :    
|   |  |-,  __ \  \  ||   | ;    \  \    `.'  :  | | |.    ' / |'  : |__'  : |__  
'   :  ;/| /  /`--'  /:   ' |     `----.   \  |  ' | :'   ;   /||  | '.'|  | '.'| 
|   |    \'--'.     / :   : :    /  /`--'  /  :  :_:,''   |  / |;  :    ;  :    ; 
|   :   .'  `--'---'  |   | :   '--'.     /|  | ,'    |   :    ||  ,   /|  ,   /  
|   | ,'              `---'.|     `--'---' `--''       \   \  /  ---`-'  ---`-'   
`----'                  `---`                           `----'                    
                                                                
</pre>

WHAT IS THIS:
-------------

 This is a debugging/development tool (a library for Arduino framework) for use 
 with Arduino projects on *ESP32 hardware*.

 Provides a command line interface (CLI) on serial port running in parallel 
 to your Arduino sketch. It is not standalone program - this tool attaches
 to the user sketch (at compile time) and enchances any sketch (even empty one)
 with a shell. ESPShell has ability to pause/resume sketch execution.

 User can enter and execute commands (there are many built-in commands) in a way 
 similar to Linux shell while their sketch is running. ESPShell can be used 
 either from Arduino IDE Serial Monitor or any other communication software like
 *PuTTY* or *TeraTerm*. Linux users have plenty of communication software to 
 choose from but even "cu" utility can do the job.

 This library can be useful for: 

 1. Developers who are interfacing new I2C or UART devices as espshell has 
    commands to create/delete hardware interfaces, send/receive data and so on. 

    Interfacing GPS (uart-based) chips or GSM modems, making libraries for I2C 
    devices.

    Boards, which control number of relays, can be easily tested/debugged with 
    one single command.

    Displaying/Changing sketch variables.

    Changing pin or interface parameters (uart speed for example, or pull mode 
    for the pin) while sketch is running. Saves your time on 
    countless "run/change/recompile/upload" cycles: wrong variable value? Now 
    you can change it.
 
 2. Beginners who wish to play with hardware without actually writing any code

 3. Arduino-compatible board makers: preinstalled shell can be used in number 
    of ways: production testing, users now can play with a hardware without 
    writing any code. (as an example: it is possible to make various 
    "blinking-led" or "relay-on/off" demos just using ESPShell commands

HOW TO INSTALL
--------------

 This library is available for installation from **Arduino Library Manager.**:
 Click the **Library  Manager** icon and type "espshell" in a search box. 
 Choose latest version and click "INSTALL".

 **For those who wish to install it manually** (say, latest source code from GitHub)
 here are instructions:

 Create folder /YourSketchBook/libraries/espshell
 Copy library content (i.e. /docs, /src, /examples, etc) to that folder
 Restart Arduino IDE


HOW TO USE IT IN MY PROJECT?
----------------------------

 Add #include "espshell.h" to your sketch
 Compile and upload as usual
 Open Serial Monitor, type "?" and press **Enter**
 (**NOTE: SerialMonitor is not the best option. Preferred way is to use
    a dedicated terminal software like **Tera Term**)

WHAT IS ESPSHELL MEMORY/CPU FOOTPRINT?
----------------------------------

Latest GitHub version I checked was:

  Code size (i.e. binary size): +81kbytes to the sketch
  Data size (data + BSS): +2kbytes
  There are also tiny portions of code with IRAM_ATTR (means they are in IRAM permanently)

ESPShell executes on another core (on multicore systems) to minimize interference to 
sketch execution


DOCUMENTATION
-------------

    English (mostly up to date) is in "espshell/docs/"
                        and
    Russian (outdated) is in "espshell/docs/ru_RU/"

    Files named "Commands.txt" and "Pin_Commands.txt" are essential chapters
    and it is a good idea to spend some of your time reading it.

ESPSHELL DEVELOPMENT
--------------------

    ESPShell code is written in pure C, developer friendly and well-commented. 
    There are few areas which calls for attention and they are can be found as 
    "TODO:" throughout the code. There are docs/PROBLEMS.txt and docs/PLANS.txt
    for further reading

    The code itself is quite simple and linear, with non-obvious parts well 
    commented. Please note that author is not a native English speaker and thats
    why comments in the code have mistakes, language misuse, etc.
