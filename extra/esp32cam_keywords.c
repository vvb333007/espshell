// This is part of espshell (github/vvb333007/espshell) project.
//
#if COMPILING_ESPSHELL

// command entries for main command tree

{ "camera", cmd_cam, 1, HELP("% \"camera up|down|settings|capture|filesize|transfer\" - Camera commands:\n\r" \
                        "%\n\r" \
                        "% setting  - Enter ESPCam setting\n\r" \
                        "% capture  - Capture a single shot (JPEG)\n\r" \
                        "% filesize - Display last captured shot file size\n\r" \
                        "% transfer - Transmit the last shot over uart\n\r" \
                        "% up       - Detect & initialize the camera\n\r" \
                        "% down     - Camera shutdown & power-off"),
                        "ESP32Cam commands" },


#endif // inside of espshell.c
