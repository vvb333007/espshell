MISC USEFUL THINGS HERE
-----------------------


espshell.h
----------

File contains some definitions from ESPShell which can be used in user sketch to:

1. Override default ESPShell compile-time settings: set custom startup uart number,
   customize some "features", stack or history buffer sizes etc. All possible overrides
   are explained in espshell.h (commented out section)

2. Register global and static sketch variables (int, char, float, pointers etc simple 
   types) to be accessible from the shell: by using "var" command one can set and 
   display variable values while sketch is executed.

   Use "convar_add(Variable_Name)" macro to register all variables of interest. 
   There is an example of its use in example_blink.ino which registers 4 variables 
   to play with


3. ESPshell uses its own versions of digitalRead(), digitalWrite() and pinMode() :
   they bypass Arduino Core's Periphireal Manager (periman) and thus can read digital
   values of any pin (i.e. UART_TX pin or I2C SDC pin); can set flags without pin being 
   reconfigured to GPIO Matrix's "simple GPIO input" - IO_MUX-safe pinMode2().


When compiled, ESPShell searches for its include (i.e. "espshell.h") file in sketch 
directory and in "extra/" subdirectory of sketch directory. If found - "espshell.h" is 
then used by ESPshell to get some compile time setting overrides (there is number of 
them commented out in the "epshell.h") 



memtest.c
---------

File is used by ESPShell when MEMTEST macro is defined either in espshell.c or espshell.h.
This file is a memory tracking module which keeps track of memory usage by ESPShell and helps
detect memory leaks (in ESPShell) and perform some extra checks on pointers being freed: 
every allocated memory has 2 bytes padding (0x55, 0xaa) which is checked by free() to see
if there were buffer overruns.

You need it if you are ESPShell developer.



esp32cam_prototypes.h, esp32cam_keywords.c and esp32cam_handlers.c 
------------------------------------------------------------------

Three files are extra commands for use with AiThinker ESP32Cam (and its clones).

These add esp camera commands to the main command tree, enabling user to initialize 
and deinitialize camera (with poweroffpoweroff), taking a picture, change the camera 
settings and sending picture (a JPEG file) over the uart for further processing. 

Another purpose which these 3 files serve is to provide an example on how to write 
extension commands for espshell

Disclaimer:
-----------
Camera commands were implemented to use with particular setup where 2 ESP32 work 
together, (one is ESP32 Devkit and another is ESP32Cam) taking pictures, transferring 
them to another ESP32 and sending over LTE. Could it be useful for general public or 
not - I have no idea, I am just leaving it here.


How to add ESP32Cam support in ESPShell:
----------------------------------------

To add ESP32Cam commands to the espshell uncomment "#define ESPCAM" line in "espshell.h".
All 3 files and/or "espshell.h" can be kept in "extra/" directory (default) or they can 
be put together with "espshell.c" inside sketch directory


What about other ESP+Camera boards? Are they supported?
-------------------------------------------------------

Everything is possible. Go to esp32cam_handlers.c and edit cam_init() function: there
are pin assignments. Different boards are different in their pin assignments. Default
values are for AiThinker ESP32Cam or its clones


Camera commands
---------------

Explore new commands: "camera ?", or "cam ?" (remember it is ok to shorten commands and 
its arguments in ESPShell)

Enter camera configuration subtree: "camera settings" (or "cam set")

Typical command sequence to take shot is:

"camera up"
"camera capture"
"camera filesize"
"camera transfer"
....
"camera down"


Captured picture filesize:
"camera filesize" - prints out captured picture (JPEG) filesize, something like "% 1234567" followed by CR,LF

Transfer picture over UART:
"camera transfer"
Once "camera download" command is entered the ESPCam replies with filesize and follows with
picture content encoded in hex ascii bytes (it is up to user to decode this). Pictures are JPEG
files

More commands are under "camera ?"
a
