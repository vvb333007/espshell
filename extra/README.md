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

Can it be used with other ESP32-based camera boards?
----------------------------------------------------

Yes it can be used however you need to modify pin numbers in esp32cam_handler.c : locate cam_init() function
and look into it: there are pin assignments. Change them to fit your camera/board models and recompiles

How POWER_DOWN pin is controlled?
---------------------------------

Manually, via "pin" command. By default camera is in POWER_ON state, but you can
power down it via pin 32 (Ai Thinker ESP32Cam): "pin 32 out low" and "pin 32 out high". Note if your
camera was initialized ("camera init") then powering down will deinitialize it without telling
the shell about doing so: if camera was initialized and then powered down, then the proper
power-on sequence is:

"camera deinit"
"pin 32 out high"
"camera init"
