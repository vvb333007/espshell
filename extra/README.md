AiThinker ESP32Cam (and its clones) commands
--------------------------------------------

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
and look into it: there are pin assignments. Change them to fit your camera/board models and recompile

