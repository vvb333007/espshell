AiThinker ESP32Cam (and its clones) commands
--------------------------------------------

To add ESP32Cam commands to the espshell just copy contens of this directory (3 files) to your
espshell.c file directory. Then edit espshell.c file: uncomment line //#define ESPCAM and recompile
your project.

Explore new commands: "camera ?". 

Enter camera configuration subtree: "camera settings" (or "cam set")

Standart command sequence to take shot is:
"camera init"
"camera capture"

Captured picture filesize:
"camera filesize"

Transfer picture over UART:
"camera download"
Once "camera download" command is entered the ESPCam replies with filesize and follows with
picture content encoded in hex ascii bytes. It is up to user to decode this. Pictures are JPEG
files

More commands is under "camera ?"
