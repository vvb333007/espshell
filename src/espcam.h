/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#if COMPILING_ESPSHELL
#  if WITH_ESPCAM

//camera pinout PWDN RESET XCLK SIOD SIOC D7 D6 D5 D4 D3 D2 D1 D0 VSYNC HREF PCLK
//camera up [MODEL | custom] [clock FREQUENCY] [i2c NUM]
//show camera models
//show camera pinout [MODEL | custom]
//

static camera_config_t config;      // camera config
static camera_fb_t *cam_fb = NULL;  // last captured picture
static bool cam_good = false;       // initialized or not

// Known camera/board models: pins database
// This one must be kept in sync with ESP-IDF camera driver
static const struct campins {
  const char *model;     // "aithinker"
  signed char pins[16];  // PWDN,RESET,  XCLK,  SIOD,SIOC,  D7,D6,D5,D4,D3,D2,D1,D0,  VSYNC,HREF,PCLK
} Campins[ ] = {
  {"wrover-kit",         {-1,-1,   21,   26,27,   35,34,39,36,19,18,5,4,    25,23,22}},
  {"esp-eye",            {-1,-1,    4,   18,23,   36,37,38,39,35,14,13,34,   5,27,25}}, // led on gpio22
  {"m5stack-psram",      {-1,15,   27,   25,23,   19,36,18,39,5,34,35,32,   22,26,22}},
  {"m5stack-v2-psram",   {-1,15,   27,   25,23,   19,36,18,39,5,34,35,32,   22,26,21}},
  {"m5stack-wide",       {-1,15,   27,   22,23,   19,36,18,39,5,34,35,32,   25,26,21}}, // led on gpio2
  {"m5stack-esp32cam",   {-1,15,   27,   25,23,   19,36,18,39,5,34,35,17,   22,26,21}},
  {"m5stack-unicam",     {-1,15,   27,   25,23,   19,36,18,39,5,34,35,32,   22,26,21}},
  {"m5stack-cams3",      {-1,21,   11,   17,41,   13,4,10,5,7,16,15,6,      42,18,12}}, // led on gpio14
  {"ai-thinker",         {32,-1,    0,   26,27,   35,34,39,36,21,19,18,5,   25,23,22}}, // hi power led on gpio4, ordinary led on gpio33(red)
  {"ttgo-t-journal",     {0,15,    27,   25,23,   19,36,18,39,5,34,35,17,   22,26,21}},
  {"xiao-s3",            {-1,-1,   10,   40,39,   48,11,12,14,16,18,17,15,  38,47,13}},
  {"esp32-cam-board",    {32,33,    4,   18,23,   36,19,21,39,35,14,13,34,   5,27,25}}, // 
  {"esp32-hcam-board",   {32,33,    4,   18,23,   36,19,21,39,13,14,35,34,   5,27,25}}, // Connections through the header
  {"esp32s2-cam-board",  {1,2,     42,   41,18,    16,39,40,15,13,5,12,14,    38,4,3}},
  {"esp32s2-hcam-board", {1,2,     42,   41,18,    16,39,40,15,12,5,13,14,    38,4,3}},   // Connections through the header
  {"esp32s3-cam-lcd",    {-1,-1,   40,   17,18,   39,41,42,12,3,14,47,13,   21,38,11}},
  {"esp32s3-eye",        {-1,-1,   15,     4,5,    11,9,8,10,12,18,17,16,     6,7,13}},
  {"firebeetle2-s3",     {-1,-1,   45,     1,2,    48,46,8,7,4,41,40,39,      6,42,5}},
  {"romeo-s3",           {-1,-1,   45,     1,2,    48,46,8,7,4,41,40,39,      6,42,5}},
  // Must be the last entry
  {NULL,                 {0,0,      0,    0,0,    0,0,0,0,0,0,0,0,           0,0,0}}
};

// This entry is for "custom" camera model. It must be initialized with "camera pinout" before "camera up custom"
// command can be used
//
static struct campins Custom = {"custom", {0,0,      0,    0,0,    0,0,0,0,0,0,0,0,           0,0,0}};

// Fill relevant portions of camera_config_t structure
// according to the camera model
//
static bool cam_config_fill_pins(camera_config_t *cc, const char *model) {

  if (cc && model) {
    struct campins *cp = NULL;

    // Special case: "custom" camera model
    if (!q_strcmp(model,"custom"))
      cp = &Custom;
    else
    // Find out pin numbers for the camera model
      for (int i = 0; Campins[i].model ;i++)
        if (!q_strcmp(model,Campins[i].model)) {
          cp = &Campins[i];
          break;
        }

    // Unknown / unsupported camera
    if (cp == NULL)
      return false;
        
    cc->pin_pwdn = cp->pins[0];
    cc->pin_reset = cp->pins[1];

    cc->pin_xclk = cp->pins[2];

    cc->pin_sccb_sda = cp->pins[3];
    cc->pin_sccb_scl = cp->pins[4];

    cc->pin_d7 = cp->pins[5];
    cc->pin_d6 = cp->pins[6];
    cc->pin_d5 = cp->pins[7];
    cc->pin_d4 = cp->pins[8];
    cc->pin_d3 = cp->pins[9];
    cc->pin_d2 = cp->pins[10];
    cc->pin_d1 = cp->pins[11];
    cc->pin_d0 = cp->pins[12];

    cc->pin_vsync = cp->pins[13];
    cc->pin_href = cp->pins[14];
    cc->pin_pclk = cp->pins[15];
    return true;
  }
  return false;
}

//show camera models
//show camera pinout [MODEL | custom]
//show camera settings
static int cmd_cam_show(int argc, char **argv) {

  if (argc < 3)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[2],"models")) {
      q_print("% Supported boards:\r\n");
      for (int i = 0; Campins[i].model ;i++)
        q_printf("%% %u. \"%s\"\r\n",i,Campins[i].model);
      return 0;
  } else if (!q_strcmp(argv[2],"pinout")) {
      if (argc == 3) {
        if (!cam_good) {
          q_print("% <e>Camera model name is expected</>\r\n");
          return CMD_MISSING_ARG;
        }
        // Print current pinout
        cam_show_pinout(&config);
      } else {
        struct camera_config_t tmp = { 0 };
        if (cam_config_fill_pins(&tmp,argv[3]))
          cam_show_pinout(&tmp);
        else
          q_print("% Unknown camera model / keyword\r\n");
      }
  } else if (!q_strcmp(argv[2],"settings")) {
    // TODO: implement
  } else
    return 2;

  return 0;
}

//"camera pinout PWDN RESET XCLK SDA SCL D7 D6 D5 D4 D3 D2 D1 D0 VSYNC HREF PCLK"
static int cmd_cam_pinout(int argc, char **argv) {

  if (argc < 18) {
    q_printf("%% <b>camera pinout</> <i>PWDN RESET XCLK SDA SCL D7 D6 D5 D4 D3 D2 D1 D0 VSYNC HREF PCLK</>\r\n");
    return CMD_MISSING_ARG;
  }

  for (int i = 2; i < 18; i++)
    Custom.pins[i - 2] = q_atoi(argv[i],-1); // -1 means "don't use this pin", so it is safe choice for the default value

  if (i < argc)
    q_print("% Trailing arguments were ignored\r\n");

  return 0;
}

//"gain auto"
//"gain 0..30"
static int cmd_cam_set_gain(int argc, char **argv) {

  sensor_t *cam;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((cam = esp_camera_sensor_get()) == NULL) {
    q_print(Failed);
    return 0;
  }

  if (!q_strcmp(argv[1], "auto")) {
    cam->set_gain_ctrl(cam, 1);  // auto gain
#if WITH_HELP
    q_printf("%% Camera gain: auto\n\r");
#endif
  } else if (isnum(argv[1])) {
    unsigned int val = atol(argv[1]);  // manual gain 0..30
    if (val > 30)
      return 1;
    cam->set_gain_ctrl(cam, 0);   // auto gain off
    cam->set_agc_gain(cam, val);  //
#if WITH_HELP
    q_printf("%% Camera gain: manual, %u\n\r", val);
#endif

  } else
    return 1;
  return 0;
}

//"balance auto|sunny|cloudy|office|home|none"
static int cam_set_balance(int argc, char **argv) {

  sensor_t *cam;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((cam = esp_camera_sensor_get()) == NULL) {
    q_print(Failed);
    return 0;
  }

  int wb = 1, awb = 1, wbm = 0;

  if (!q_strcmp(argv[1], "none")) awb = wb = 0;
  else if (!q_strcmp(argv[1], "auto")) {} 
  else if (!q_strcmp(argv[1], "sunny")) wbm = 1;
  else if (!q_strcmp(argv[1], "cloudy")) wbm = 2;
  else if (!q_strcmp(argv[1], "office")) wbm = 3;
  else if (!q_strcmp(argv[1], "home")) wbm = 4;
  else return 1;

  cam->set_whitebal(cam, wb);  //FIXME: read datasheet
  cam->set_awb_gain(cam, awb);
  cam->set_wb_mode(cam, wbm);
#if WITH_HELP
  q_printf("%% White balance: %s, Auto WB: %s, WB mode: %d\n\r", wb ? "yes" : "no", awb ? "yes" : "no", wbm);
#endif

  return 0;
}

//"exposure auto"
//"exposure auto -2..2"
//"exposure 0..1200"
//
static int cam_set_exposure(int argc, char **argv) {

  int exposure, ae_shift = 0;
  sensor_t *cam;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((cam = esp_camera_sensor_get()) == NULL) {
    q_print(Failed);
    return 0;
  }

  // auto exposure with optional AE shift [-2..2]
  if (!q_strcmp(argv[1], "auto")) {
    cam->set_exposure_ctrl(cam, 1);
    if (argc > 2) {
      if (!isnum(argv[2]))
        return 2;

      ae_shift = atoi(argv[2]);
      if (ae_shift < -2 || ae_shift > 2)
        return 2;
    }
    cam->set_ae_level(cam, ae_shift);
#if WITH_HELP
    q_printf("%% Exposure: auto, AE compensation: %d\n\r", ae_shift);
#endif
    return 0;
  }

  // manual exposure
  if (!isnum(argv[1]))
    return 1;
  exposure = atoi(argv[1]);
  if (exposure < 0 || exposure > 1200)
    return 1;

  cam->set_exposure_ctrl(cam, 0);
  cam->set_aec_value(cam, exposure);
#if WITH_HELP
  q_printf("%% Manual exposure %d set\n\r", exposure);
#endif

  return 0;
}

//"brightness|saturation|constrast?sharpness -2..2"
//"quality 2..63"
//
static int cam_set_qbcss(int argc, char **argv) {

  int val;
  sensor_t *cam;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!isnum(argv[1])) {
    q_print("% Integer value expected\n\r");
    return 1;
  }

  val = atoi(argv[1]);

  if (!q_strcmp(argv[0], "quality")) {  // quality has range from 2 to 63. Values smaller than 2
                                        // cause random lockups
    if (val < 2 || val > 63)
      return 1;
  } else {
    if (val < -2 || val > 2)
      return 1;
  }

  if ((cam = esp_camera_sensor_get()) == NULL) {
    q_print(Failed);
    return 0;
  }
  if (!q_strcmp(argv[0], "quality")) cam->set_quality(cam, val);
  else if (!q_strcmp(argv[0], "brightness")) cam->set_brightness(cam, val);
  else if (!q_strcmp(argv[0], "contrast")) cam->set_contrast(cam, val);
  else if (!q_strcmp(argv[0], "saturation")) cam->set_saturation(cam, val);
  else if (!q_strcmp(argv[0], "sharpness")) cam->set_sharpness(cam, val);
  else q_printf("%%  unexpected token \"%s\"\n\r", argv[0]);

  return 0;
}

//"size vga|svga|xga|uxga|hd|sxga"
//
static int cam_set_size(int argc, char **argv) {

  sensor_t *cam;
  int size;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((cam = esp_camera_sensor_get()) == NULL) {
    q_print(Failed);
    return 0;
  }

  if (!q_strcmp(argv[1], "vga")) size = FRAMESIZE_VGA;
  else if (!q_strcmp(argv[1], "svga")) size = FRAMESIZE_SVGA;
  else if (!q_strcmp(argv[1], "xga")) size = FRAMESIZE_XGA;
  else if (!q_strcmp(argv[1], "hd")) size = FRAMESIZE_HD;
  else if (!q_strcmp(argv[1], "sxga")) size = FRAMESIZE_SXGA;
  else if (!q_strcmp(argv[1], "uxga")) size = FRAMESIZE_UXGA;
  else return 1;

  cam->set_framesize(cam, size);

  return 0;
}



// extra commands under camera settings subdirectory
static struct keywords_t keywords_espcam[] = {

  KEYWORDS_BEGIN

  { "gain", cmd_cam_set_gain, 1, HELPK("\"gain auto|(0..30)\"\n\r"
                                                "% Set camera sensetivity (auto or 0..30)"),
                  "Gain" },

  { "balance", cam_set_balance, 1, HELPK("% whitebalance none|auto|sunny|cloudy|office|home\n\r"
                                        "% Set camera WB mode"),
    "White balance" },

  { "exposure", cam_set_exposure, 2, HELPK("% exposure auto [-2..2]\n\r"
                                          "% \n\r"
                                          "% Set camera exposure mode to auto & optional AE shift"),
    "Exposure" },
  { "exposure", cam_set_exposure, 1, HELPK("% exposure 0..1200\n\r"
                                          "%\n\r"
                                          "% Set camera exposure manually"),
    "Exposure" },


  { "brightness", cam_set_qbcss, 1, HELPK("% Adjust brightness: -2..2"),
    "Brightness" },

  { "saturation", cam_set_qbcss, 1, HELPK("% \"saturation X\" - Adjust saturation: -2..2"),
    "Saturation" },

  { "contrast", cam_set_qbcss, 1, HELPK("% \"contrast X\" - Adjust contrast: -2..2"),
    "Contrast" },

  { "sharpness", cam_set_qbcss, 1, HELPK("% \"sharpness\" - Adjust sharpness: -2..2"),
    "Sharpness" },

  { "size", cam_set_size, 1, HELPK("% \"size vga|svga|xga|hd|sxga|uxga\"\n\r\n\r"
                                  "% Set frame size:\n\r"
                                  "% vga  - 640x480\n\r"
                                  "% svga - 800x600\n\r"
                                  "% xga  - 1024x760\n\r"
                                  "% hd   - 1280x720\n\r"
                                  "% sxga - 1280x1024\n\r"
                                  "% uxga - 1600x1200 (Default)"),
    "Resolution" },

  { "quality", cam_set_qbcss, 1, HELPK("% \"quality 2..63\"\n\r"
                                      "% Set JPEG quality:\n\r"
                                      "% 2 - high ... 63 - low"),
    "Picture quality" },


  KEYWORDS_END
};

//"capture"
//
//framebuffer stored in cam_fb. there are 2 fb
//total. Don't know if holding a framebuffer is
//good idea or not. May be should make a copy and
//return the fb asap.
//
static int cam_capture(int argc, char **argv) {

  if (cam_fb)
    esp_camera_fb_return(cam_fb);

  if ((cam_fb = esp_camera_fb_get()) == NULL)
    q_print(Failed);

  return 0;
}

//"filesize"
//
// return captured frame size in bytes. frame
// must be captured with cam_capture()
//
static int cam_filesize(int argc, char **argv) {

  unsigned int len = 0;
  if (cam_fb)
    len = cam_fb->len;
  q_printf("%% %u\n\r", len);
  return 0;
}

//"camera transfer"
//
// really slow byte-by-bute sender: we dont want
// reciever fifo overrun
//
static int cam_transfer(int argc, char **argv) {

  unsigned char *p;
  cam_filesize(argc, argv);
  if (cam_fb && cam_fb->len) {
    p = (unsigned char *)cam_fb->buf;
    for (int i = 0; i < cam_fb->len; i++)
      q_printf("%02x", p[i]);
  }
  return 0;
}

//"camera down"
//
// camera deinit. framebuffers are freed.
// POWERDOWN must be performed separately
// using "pin" commands
// POWERDOWN pin of ESP32Cam is GPIO32:
//
static int cam_down(int argc, char **argv) {

  if (cam_good) {
    cam_good = false;
    if (cam_fb) {
      esp_camera_fb_return(cam_fb);
      cam_fb = NULL;
    }
    esp_camera_deinit();
#if WITH_HELP
    q_print("% Camera deinitialized\n\r");
#endif
    q_delay(100);
    if (config.pin_pwdn >= 0) {  // Enable POWER_DOWN

      pinMode(config.pin_pwdn, OUTPUT);
      digitalWrite(config.pin_pwdn, HIGH);
#if WITH_HELP
      q_printf("%% Camera power down (GPIO#%d is HIGH)\n\r", config.pin_pwdn);
#endif
    }
    // TODO: use POWER_DOWN or RESET pins if available
  }
  return 0;
}

//"camera up [MODEL|custom] [xclk FREQ] [i2c NUM]"
// powerup & initialize the camera
//
static int cmd_cam_up(int argc, char **argv) {

  esp_err_t err;
  const char *model = NULL;
  unsigned int xclk = 0;

  if (cam_good)  // already initialized
    return 0;

  if (argc > 2)
    model = argv[2];

  if (argc > 3)
    xclk = q_atol(argv[3],DEF_BAD);
  else if (isnum(model)) {
    xclk = q_atol(model, DEF_BAD);
    model = NULL;
  }

  if (xclk == DEF_BAD) {
    q_print("% <e>Frequency in hertz is expected; 0 means autoselect</>\r\n");
    return 0;
  }

  if (xclk == 0)
    xclk = 16000000; // 16Mhz enables EDMA on S3

  if (model == NULL)
#ifdef CONFIG_IDF_TARGET_ESP32S2
    model = "esp32s2-cam-board";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    model = "xiao-s3";
#elif defined(CONFIG_IDF_TARGET_ESP32)
    model = "ai-thinker";
#else
    return -1;
#endif
  
  if (!cam_config_fill_pins(&config, model)) {
    q_printf("%% Unknown/unsupported camera model \"%s\"\r\n",model);
    return 0;
  }

  VERBOSE(q_printf("%% %s goes up with XCLK=%u\r\n",model,xclk));

  config.ledc_channel = LEDC_CHANNEL_0; //TODO: use channel #4
  config.ledc_timer = LEDC_TIMER_0;

  config.xclk_freq_hz = xclk;  // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)

  // TODO: code below must be rewritten. 
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_UXGA;
  config.jpeg_quality = 4;
  config.fb_count = 2;  // if more than one, i2s runs in continuous mode. Use only with JPEG

  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (config.pin_pwdn >= 0) {  // Disable POWER_DOWN
    //gpio_hold_dis(config.pin_pwdn);
    pinMode(config.pin_pwdn, OUTPUT);
    digitalWrite(config.pin_pwdn, LOW);
    //gpio_hold_en(config.pin_pwdn);
#if WITH_HELP
    q_printf("%% Camera power up (GPIO%d is LOW)\n\r", config.pin_pwdn);
#endif
    q_delay(100);
  }


  sensor_t *s = NULL;
  if (ESP_OK == (err = esp_camera_init(&config))) {

    s = esp_camera_sensor_get();
    if (s != NULL) {
      s->set_gain_ctrl(s, 1);      // auto gain on
      s->set_exposure_ctrl(s, 1);  // auto exposure on
      s->set_awb_gain(s, 1);       // Auto White Balance enable (0 or 1)
      cam_good = true;
#if WITH_HELP
      q_printf("%% Camera is on\n\r");
#endif
      return 0;
    }
  }
  q_print(Failed);
  //q_printf("%% Camera init code=%d, sensor=%p\n\r",err,s);
  return 0;
}


//"camera ARG1 ARG2 ... ARGn"
//TAG:cam
static int cmd_cam(int argc, char **argv) {

  int err = 0;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[1], "pinout"))
    return cmd_cam_pinout(argc, argv);


  if (!q_strcmp(argv[1], "up"))
    return cmd_cam_up(argc, argv);

  if (!cam_good) {
    q_printf("%% Initialize camera first (\"camera up\" command)\n\r");
    return 0;
  }
  // "camera settings"
  if (!q_strcmp(argv[1], "settings")) change_command_directory(0, keywords_espcam, PROMPT_ESPCAM, "ESP32Cam");
  // camera capture
  else if (!q_strcmp(argv[1], "capture")) err = cam_capture(argc, argv);
  // camera filesize
  else if (!q_strcmp(argv[1], "filesize")) err = cam_filesize(argc, argv);
  // camera transfer
  else if (!q_strcmp(argv[1], "transfer")) err = cam_transfer(argc, argv);
  // camera down
  else if (!q_strcmp(argv[1], "down")) err = cam_down(argc, argv);
  else err = 1;  // unrecoginzed argument

  return err;
}
#  endif // #if WITH_ESPCAM
#endif // #if COMPILING_ESPSHELL

