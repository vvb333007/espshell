
Three files are extra commands for use with AiThinker ESP32Cam (and its clones).

These files add esp camera commands to the main command tree, enabling user to initialize 
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
