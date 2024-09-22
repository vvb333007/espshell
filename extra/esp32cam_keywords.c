// This is part of espshell (github/vvb333007/espshell) project.
//
#if COMPILING_ESPSHELL

// command entries for main command tree

{ "camera", cmd_cam, 1, 
#if WITH_HELP
                        "% \"camera settings|capture|filesize|download|init\" Camera commands:\n\r" \
                        "%\n\r" \
                        "% setting  - enter ESPCam setting\n\r" \
                        "% capture  - capture a single shot (JPEG)\n\r" \
                        "% filesize - print last captured shot file size\n\r" \
                        "% download - transmit the last shot over uart\n\r" \
                        "% init     - detect & initialize camera\n\r" \
                        "% deinit   - free espcam resources",
#else
                        "",
#endif
                        "ESP32Cam commands" },


#endif // inside of espshell.c