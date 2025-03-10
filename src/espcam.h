/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

// TODO: priority. get rid of all Arduino function calls. Either rewrite code to use ESP-IDF or lift required Arduino Core code.
// TODO: Library must compile in plain ESP-IDF environment

#if COMPILING_ESPSHELL

static camera_config_t config;      // camera config
static camera_fb_t *cam_fb = NULL;  // last captured picture
static bool cam_good = false;       // initialized or not


//"gain auto"
//"gain 0..30"
static int cam_set_gain(int argc, char **argv) {

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
  else if (!q_strcmp(argv[1], "auto")) {
  } else if (!q_strcmp(argv[1], "sunny")) wbm = 1;
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

  if (!q_strcmp(argv[0], "quality")) {  // quality has range from 2 to 63. Better than 2
                                        // causes random lockups
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

  { "gain", cam_set_gain, 1, HELPK("\"gain auto|(0..30)\"\n\r"
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
  }
  return 0;
}

//"camera up"
// powerup & initialize the camera
//
// NOTE: Pin numbers are for AiThinker ESPCam.
static int cam_up() {


  esp_err_t err;

  if (cam_good)  // already initialized
    return 0;

  pinMode(4, OUTPUT);   // High power LED
  pinMode(33, OUTPUT);  // Small red LED

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;

  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.xclk_freq_hz = 20000000;  // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)

  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;

  config.pin_pwdn = 32;
  config.pin_reset = -1;

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

  if (argc != 2)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[1], "up"))
    return cam_up(argc, argv);

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
#endif // #if COMPILING_ESPSHELL

