MISC USEFUL THINGS HERE
-----------------------




espshell.h
----------

File contains some definitions from ESPShell which can be used in user sketch:

1. Register global and static sketch variables to be accessible from 
   the shell: by using "var" command one can set and display variable values.
   Use "espshell_var()" macro to register all variables of interest


2. Improved versions of digitalRead() and pinMode() : they bypass Arduino Core's
   Periphireal Manager (periman) so can be used on live working interface pins:
   for example one can read UART_TX pin or set output flag on ESP32's reserved
   pin 6 without system crash as it would happen when using pinMode(6,.)

Copy this file along with espshell.c to your sketch directory and #include the
.h file in your sketch if you want to use some if it internal functions or be able
to set/display your sketch variables 
   


AiThinker ESP32Cam (and its clones) commands
--------------------------------------------

These add some commands to the main command tree, enabling user to initialize and
deinitialize camera (poweroff), taking a picture, change the camera settings and
sending picture (a JPEG file) over the uart for further processing.

These commands were implemented to use with particular setup where 2 ESP32 work together,
(one is ESP32 Devkit and another is ESP32Cam) taking pictures, transferring them to
another ESP32 and sending over LTE. Could it be useful or not - i have no idea.


To add ESP32Cam commands to the espshell just copy contens of this directory (3 files) to your
espshell.c file directory. Then edit espshell.c file: uncomment line //#define ESPCAM and recompile
your project.

Explore new commands: "camera ?". 

Enter camera configuration subtree: "camera settings" (or "cam set")

Typical command sequence to take shot is:

"camera init"
"camera capture"
"camera filesize"
"camera download"
....
"camera deinit"


Captured picture filesize:
"camera filesize" - prints out captured picture (JPEG) filesize, something like "% 1234567" followed by CR,LF

Transfer picture over UART:
"camera download"
Once "camera download" command is entered the ESPCam replies with filesize and follows with
picture content encoded in hex ascii bytes (it is up to user to decode this). Pictures are JPEG
files

More commands is under "camera ?"

Can it be used with other ESP32-based camera boards?
----------------------------------------------------

Yes it can be used however you need to modify pin numbers in esp32cam_handler.c : locate cam_init() function
and look into it: there are pin assignments. Change them to fit your camera/board models and recompiles

