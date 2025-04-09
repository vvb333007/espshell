/* 
 * This file is a part of the ESPShell Arduino library (Espressif's ESP32-family CPUs)
 *
 * Latest source code can be found at Github: https://github.com/vvb333007/espshell/
 * Stable releases: https://github.com/vvb333007/espshell/tags
 *
 * Feel free to use this code as you wish: it is absolutely free for commercial and 
 * non-commercial, education purposes.  Credits, however, would be greatly appreciated.
 *
 * Author: Viacheslav Logunov <vvb333007@gmail.com>
 */

#if COMPILING_ESPSHELL
#  if WITH_ESPCAM

#define ESPCAM_XCLK_MAX 27000000 // 27MHz maximum

//camera pinout PWDN RESET XCLK SIOD SIOC D7 D6 D5 D4 D3 D2 D1 D0 VSYNC HREF PCLK
//camera up [MODEL | custom] [clock FREQUENCY] [i2c NUM]
//camera settings
//  gain auto|(0..30)
//  balance none|auto|sunny|cloudy|office|home
//  exposure auto [(-2..2)]
//  exposure (0..1200)
//  brightness (-2..2)
//  saturation (-2..2)
//  contrast (-2..2)
//  sharpness (-2..2)
//  size vga|svga|xga|hd|sxga|uxga
//  quality (2..63)
//show camera models
//show camera pinout [MODEL | custom]
//show camera settings
//show camera sensor

static camera_config_t config;      // camera config TODO: rename
static camera_fb_t *cam_fb = NULL;  // last captured picture
static bool cam_good = false;       // initialized or not

// These are convars to make it possible to resolve potential resource conflict with user sketch:
// if user sketch uses LEDC_CHANNEL_0 or LEDC_TIMER_0 then espshell can be configured to use other channel & timer
// Camera also may conflict with PWM module of the shell since PWM is built around LEDC.
//
static int8_t cam_ledc_chan = LEDC_CHANNEL_0;
static int8_t cam_ledc_timer = LEDC_TIMER_0;

// Known camera/board models: pins database
// This one must be kept in sync with ESP-IDF camera driver
//
static const struct campins {
  const char *model;     // e.g. "ai-thinker"
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
  {"df-firebeetle2-s3",  {-1,-1,   45,     1,2,    48,46,8,7,4,41,40,39,      6,42,5}},
  {"df-romeo-s3",        {-1,-1,   45,     1,2,    48,46,8,7,4,41,40,39,      6,42,5}},
  // Must be the last entry
  {NULL,                 {0,0,      0,    0,0,    0,0,0,0,0,0,0,0,           0,0,0}}
};

// This entry is for "custom" camera model. It must be initialized with "camera pinout" before "camera up custom"
// command can be used
//
static struct campins Custom = {"custom", {0,0,      0,    0,0,    0,0,0,0,0,0,0,0,           0,0,0}};

static const char * const Camres[] = {
  [FRAMESIZE_96X96] = "96x96",
  [FRAMESIZE_QQVGA] = "160x120",
  [FRAMESIZE_128X128] = "128x128",
  [FRAMESIZE_QCIF] = "176x144",
  [FRAMESIZE_HQVGA] = "240x176",
  [FRAMESIZE_240X240] = "240x240",
  [FRAMESIZE_QVGA] = "320x240",
  [FRAMESIZE_320X320] = "320x320",
  [FRAMESIZE_CIF] = "400x296",
  [FRAMESIZE_HVGA] = "480x320",
  [FRAMESIZE_VGA] = "640x480",
  [FRAMESIZE_SVGA] = "800x600",
  [FRAMESIZE_XGA] = "1024x768",
  [FRAMESIZE_HD] = "1280x720",
  [FRAMESIZE_SXGA] = "1280x1024",
  [FRAMESIZE_UXGA] = "1600x1200",
  [FRAMESIZE_FHD] = "1920x1080",
  [FRAMESIZE_P_HD] = " 720x1280",
  [FRAMESIZE_P_3MP] = " 864x1536",
  [FRAMESIZE_QXGA] = "2048x1536",
  [FRAMESIZE_QHD] = "2560x1440",
  [FRAMESIZE_WQXGA] = "2560x1600",
  [FRAMESIZE_P_FHD] = "1080x1920",
  [FRAMESIZE_QSXGA] = "2560x1920",
  [FRAMESIZE_5MP] = "2592x1944"
};

static inline const char *cam_resolution(int i) {
  return ( i>= 0 && i < FRAMESIZE_INVALID) ? Camres[i] : "unknown";
}

// Fill relevant portions of camera_config_t structure
// according to the camera model
//
static bool cam_config_fill_pins(camera_config_t *cc, const char *model) {

  if (cc && model) {
    const struct campins *cp = NULL;

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

// Show pins assignment for an arbitrary camera_config_t
//
static void cam_show_pinout(const camera_config_t *cc) {
  if (cc) {
    q_printf( "%% Pins assignment (Camera pin : ESP32 pin)\r\n"
              "%% Power Down : %d\r\n"
              "%% Reset      : %d\r\n"
              "%% XCLK       : %d\r\n"
              "%% I2C_SDA    : %d\r\n"
              "%% I2C_SCL    : %d\r\n"
              "%% D7..D0 (or Y9..Y2) : %d, %d, %d, %d, %d, %d, %d, %d\r\n"
              "%% VSYNC      : %d\r\n"
              "%% HREF       : %d\r\n"
              "%% PCLK       : %d\r\n",
              cc->pin_pwdn,
              cc->pin_reset,
              cc->pin_xclk,
              cc->pin_sccb_sda,
              cc->pin_sccb_scl,
              cc->pin_d7, cc->pin_d6, cc->pin_d5, cc->pin_d4, cc->pin_d3, cc->pin_d2, cc->pin_d1, cc->pin_d0,
              cc->pin_vsync,
              cc->pin_href,
              cc->pin_pclk);
  }
}

//show camera models
//show camera pinout [MODEL | custom]
//show camera settings
static int cmd_show_camera(int argc, char **argv) {

    sensor_t *cam;
    camera_sensor_info_t *info;

  if (argc < 3)
    return CMD_MISSING_ARG;

  // "show camera models"
  if (!q_strcmp(argv[2],"models")) {
    q_print("% Known boards:\r\n");
    int i;
    for (i = 0; Campins[i].model ;i++)
      q_printf("%% %u. \"%s\"\r\n",i + 1,Campins[i].model);
    q_printf("%% %u. \"custom\"\r\n",i + 1);

    HELP(q_print( "%\r\n"
                  "% Use model names from the list above for \"camera up\" and \"show camera pinout\"\r\n"
                  "% Configure custom pinout with \"camera pinout\", apply with \"camera up custom\"\r\n"));
    return 0;
  }

  // "show camera pinout MODEL | custom"
  // "show camera pinout"
  if (!q_strcmp(argv[2],"pinout")) {
    if (argc == 3) {
      if (!cam_good) {
        q_print("% <e>Camera model name is expected</>\r\n");
        return CMD_MISSING_ARG;
      }
      // Print current pinout
      cam_show_pinout(&config);
    } else {
      camera_config_t tmp = { 0 };
      if (cam_config_fill_pins(&tmp,argv[3]))
        cam_show_pinout(&tmp);
      else {
        q_print("% Unknown camera model / keyword\r\n");
        return 3;
      }
    }
    return 0;
  }

  // "show camera settings"
  // TODO: Make output human-readable
  // TODO: read OV docs
  //
  if (!q_strcmp(argv[2],"settings")) {
    if (!cam_good)
      goto initialize_camera_first;

    if ((cam = esp_camera_sensor_get()) != NULL) 
      q_printf(
        "%% Current settings:\r\n"
        "%% Frame size: %s, scaling: %s, binning: %s\r\n"
        "%% Quality: %d, Beughtness: %d, Contrast: %d, Saturation: %d, Sharpness: %d\r\n"
        "%% Denoise factor: %d, Special effects: %d\r\n"
        "%% \r\n"
        "%% WB mode: %d, AutoWB: %d, AWB Gain: %d\r\n"
        "%% \r\n"
        "%% AEC: %d, AEC2: %d, AE Level: %d, AEC Value: %d\r\n"
        "%% \r\n"
        "%% AGC: %d, AGC Gain: %d, Gain ceiling: %d\r\n"
        "%% \r\n"
        "%% BPC: %d, WPC: %d, LENC: %d, HMIRROR: %d, VFLIP: %d\r\n",
        cam_resolution(cam->status.framesize),
        cam->status.scale ? "Yes" : "No",
        cam->status.binning ? "Yes" : "No",
        cam->status.quality,cam->status.brightness,cam->status.contrast, cam->status.saturation, cam->status.sharpness,
        cam->status.denoise, cam->status.special_effect,
        cam->status.wb_mode, cam->status.awb, cam->status.awb_gain,
        cam->status.aec, cam->status.aec2, cam->status.ae_level, cam->status.aec_value,
        cam->status.agc, cam->status.agc_gain, cam->status.gainceiling,
        cam->status.bpc,cam->status.wpc,cam->status.lenc, cam->status.hmirror, cam->status.vflip);
      
        /*
    uint8_t raw_gma;
    uint8_t dcw;
    uint8_t colorbar;
        */
    return 0;
  }

  // "show camera sensor"
  if (!q_strcmp(argv[2],"sensor")) {

    if (!cam_good)
      goto initialize_camera_first;

    if ((cam = esp_camera_sensor_get()) != NULL) {
      q_printf(" %% <r>Camera module information:           </>\r\n"
                "%% Camera ID (MIDH=%x, MIDL=%x, PID=%x, VER=%x\r\n)"
                "%% I2C slave address: %x; Main clock (XCLK) is %.1f MHz\r\n",
                    cam->id.MIDH, cam->id.MIDL, cam->id.PID, cam->id.VER,
                    cam->slv_addr, (float)cam->xclk_freq_hz / 1000000.0f);

      if ((info = esp_camera_sensor_get_info(&cam->id)) != NULL) 
        q_printf( "%% Sensor model is \"%s\"\r\n"
                  "%% Max resolution: %d, JPEG support: %s\r\n",
                  info->name,
                  info->max_size,
                  info->support_jpeg ? "Yes" : "No");
      
      return 0;
    }
    q_print("% <e>Can not access camera sensor information</>\r\n");
    return CMD_FAILED;
  }
  return 2;

initialize_camera_first:
  q_print("%% Initialize camera first, using \"camera up\" command\r\n");
  return CMD_FAILED;
}

//"camera pinout PWDN RESET XCLK SDA SCL D7 D6 D5 D4 D3 D2 D1 D0 VSYNC HREF PCLK"
static int cmd_cam_pinout(int argc, char **argv) {

  if (argc < 18) {
    HELP(q_print( "% Syntax is:\r\n"
                  "% <b>camera pinout</> <o>PWDN RESET</> <i>XCLK</> <o>SDA SCL</> <g>D7 D6 D5 D4 D3 D2 D1 D0</> <i>VSYNC HREF PCLK</>\r\n"
                  "% or, if you prefer Y-names:\r\n"
                  "% <b>camera pinout</> <o>PWDN RESET</> <i>XCLK</> <o>SDA SCL</> <g>Y9 Y8 Y7 Y6 Y5 Y4 Y3 Y2</> <i>VSYNC HREF PCLK</>\r\n"));
    return CMD_MISSING_ARG;
  }

  int i;
  // read pin numbers (starting from 3rd argument: "0:camera 1:pinout 2:NUM")
  for (i = 2; i < 18; i++)
    Custom.pins[i - 2] = q_atoi(argv[i],-1); // -1 means "don't use this pin", so it is safe choice for the default value

  if (i < argc)
    q_print("% Trailing arguments were ignored\r\n");

  return 0;
}

//"gain auto"
//"gain 0..30"
static int cmd_camera_set_gain(int argc, char **argv) {

  sensor_t *cam;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((cam = esp_camera_sensor_get()) == NULL) {
    q_print(Failed);
    return 0;
  }

  if (!q_strcmp(argv[1], "auto")) {
    cam->set_gain_ctrl(cam, 1);  // auto gain
    HELP(q_printf("%% Camera gain: auto\n\r"));
  } else if (isnum(argv[1])) {
    unsigned int val = atol(argv[1]);  // manual gain 0..30
    if (val > 30)
      return 1;
    cam->set_gain_ctrl(cam, 0);   // auto gain off
    cam->set_agc_gain(cam, val);  //
    HELP(q_printf("%% Camera gain: manual, %u\n\r", val));
  } else
    return 1; // not numeric or "auto"

  return 0;
}


//"balance auto|sunny|cloudy|office|home|none"
static int cmd_camera_set_balance(int argc, char **argv) {

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
static int cmd_camera_set_exposure(int argc, char **argv) {

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

      ae_shift = q_atoi(argv[2],-3);
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
  exposure = q_atoi(argv[1],-1);
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
//"compression 2..63"
//
static int cmd_camera_set_qbcss(int argc, char **argv) {

  int val;
  sensor_t *cam;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!isnum(argv[1])) {
    q_print("% Integer value expected\n\r");
    return 1;
  }

  val = q_atoi(argv[1], -3);

  // Check input arguments
  if (!q_strcmp(argv[0], "compression")) {  // quality has range from 2 to 63. Values smaller than 2 cause random lockups
    if (val < 2 || val > 63) {
      HELP(q_print("% Compression value is in the range [2..63] (smaller number=better quality)\r\n"));
      return 1;
    }
  } else {
    if (val < -2 || val > 2) {
      HELP(q_printf("%% %s value must be in the range [-2..2] (0 = no shift)\r\n",argv[0]));
      return 1;
    }
  }

  if ((cam = esp_camera_sensor_get()) == NULL) {
    q_print(Failed);
    return 0;
  }
  if (!q_strcmp(argv[0], "compression")) cam->set_quality(cam, val);
  else if (!q_strcmp(argv[0], "brightness")) cam->set_brightness(cam, val);
  else if (!q_strcmp(argv[0], "contrast")) cam->set_contrast(cam, val);
  else if (!q_strcmp(argv[0], "saturation")) cam->set_saturation(cam, val);
  else if (!q_strcmp(argv[0], "sharpness")) cam->set_sharpness(cam, val);
  else q_printf("%%  <e>Unexpected token \"%s\"</>\n\r", argv[0]);

  return 0;
}

//"size vga|svga|xga|uxga|hd|sxga"
//
static int cmd_camera_set_size(int argc, char **argv) {

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
  else if (!q_strcmp(argv[1], "fhd")) size = FRAMESIZE_FHD;
  else if (!q_strcmp(argv[1], "qxga")) size = FRAMESIZE_QXGA;
  else if (!q_strcmp(argv[1], "qhd")) size = FRAMESIZE_QHD;
  else if (!q_strcmp(argv[1], "wqxga")) size = FRAMESIZE_WQXGA;
  else if (!q_strcmp(argv[1], "qsxga")) size = FRAMESIZE_QSXGA;
  else if (!q_strcmp(argv[1], "5mp")) size = FRAMESIZE_5MP;
  else return 1;

  int err = cam->set_framesize(cam, size);
  VERBOSE(q_printf("Err code: %d\r\n",err));

  return 0;
}


//"capture"
//
//framebuffer stored in cam_fb. there are 2 fb
//total. Don't know if holding a framebuffer is
//good idea or not. May be should make a copy and
//return the fb asap.
//
static int cmd_camera_capture(int argc, char **argv) {

  if (cam_fb)
    esp_camera_fb_return(cam_fb);

  if ((cam_fb = esp_camera_fb_get()) == NULL)
    q_print(Failed);

  return 0;
}

//"filesize"
//
// return captured frame size in bytes. frame
// must be captured with cmd_camera_capture()
//
static int cmd_camera_filesize(int argc, char **argv) {

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
static int cmd_camera_transfer(int argc, char **argv) {

  unsigned char *p;
  cmd_camera_filesize(argc, argv);
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
static int cmd_camera_down(int argc, char **argv) {

  if (cam_good) {
    cam_good = false;
    if (cam_fb) {
      esp_camera_fb_return(cam_fb);
      cam_fb = NULL;
    }
    esp_camera_deinit();
    HELP(q_print("% Camera deinitialized\n\r"));
    q_delay(100);
    if (config.pin_pwdn >= 0) {  // Enable POWER_DOWN
      digitalForceWrite(config.pin_pwdn, HIGH);
      HELP(q_printf("%% Camera power down (GPIO#%d is HIGH)\n\r", config.pin_pwdn));
    }
    
  }
  return 0;
}

//"camera up [MODEL|custom] [clock FREQ] [i2c NUM]"
// powerup & initialize the camera.
// If no arguments were supplied, then ESPShells assumes that camera is initialized somewhere else
// in user sketch, and tries to access camera using camera API. 
//
static int cmd_cam_up(int argc, char **argv) {

  esp_err_t err;
  const char *model = NULL;
  unsigned int xclk = 16000000,i = 2; //TODO: DEF_CAMERA_XCLOCK
  signed char i2c = -1;
  bool has_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;

  if (cam_good)  // already initialized
    return 0;

  // No arguments to "camera up": special case
  if (argc < 3) {
    sensor_t *cam;
    q_print("% Assuming that camera is initialized by sketch, verifying...");
    if ((cam = esp_camera_sensor_get()) != NULL) {
      q_print("Yes, it is\r\n");
      cam_good = true;
    } else
      q_print("No, it isn't\r\n"
              "% Use \"camera up MODEL\" with model name that matches your board:\r\n"
              "% (list of supported boards: \"show camera models\"), or use custom pinout\r\n"
              "% with commands \"camera pinout\" and \"camera up custom\"\r\n");
    return CMD_FAILED;
  }

  while(i < argc) {
    if (!q_strcmp(argv[i],"clock")) {
      if (i + 1 >= argc) {
        q_print("% <e>Camera clock frequency is expected, in Hz</>\r\n");
        return CMD_MISSING_ARG;
      }
      i++;

      xclk = q_atol(argv[i],xclk);
      // xclk given in MHz? undocumented
      if (xclk <= 100)
        xclk *= 1000000;
      if (xclk > ESPCAM_XCLK_MAX) {
        xclk = ESPCAM_XCLK_MAX;
        q_print("%% XCLK is adjusted to its maxumum, " xstr(ESPCAM_XCLK_MAX) );
      }
    } else if (!q_strcmp(argv[i],"i2c")) {
      if (i + 1 >= argc) {
        q_print("% <e>I2C bus number is expected</>\r\n");
        return CMD_MISSING_ARG;
      }
      i++;
      i2c = q_atoi(argv[i],-1); 
    } else
      model = argv[i];
    i++;
  }

  if (model == NULL) {
    // wild guess
#ifdef CONFIG_IDF_TARGET_ESP32S2
    model = "esp32s2-cam-board";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    model = "xiao-s3";
#elif defined(CONFIG_IDF_TARGET_ESP32)
    model = "ai-thinker";
#else
    return -1;
#endif
    HELP(q_printf("%% Auto-selected camera pinout: \"%s\"\n\r"
                  "%% Wrong model? use \"camera up MODEL\"\r\n",model));
  }
  
  if (!cam_config_fill_pins(&config, model)) {
    q_printf("%% Unknown/unsupported camera model \"%s\"\r\n",model);
    return 0;
  }

  VERBOSE(q_printf("%% Camera UP: Model=%s, XCLK=%u, I2C Bus=%d\r\n", model, xclk, i2c));

  // i2c bus number specified: reset SDA pin number
  if (i2c >= 0) {
    config.sccb_i2c_port = i2c;
    config.pin_sccb_sda = UNUSED_PIN;
    //config.pin_sccb_scl = UNUSED_PIN;
  } else
    config.sccb_i2c_port = -1; // -1 == ignored, 

  config.ledc_channel = cam_ledc_chan;
  config.ledc_timer = cam_ledc_timer;

  config.xclk_freq_hz = xclk;  //20MHz or 10MHz for OV2640 double FPS, 16MHz for S2/S3 EDMA experimental mode

  
  if (has_psram) {
    config.pixel_format = PIXFORMAT_JPEG; // TODO: support RGB modes also
    config.frame_size = FRAMESIZE_UXGA; // fits both OV2 & OV5
    config.jpeg_quality = 4;
    config.fb_count = 2;  // if more than one, i2s runs in continuous mode. Use only with JPEG
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.pixel_format = PIXFORMAT_JPEG; // TODO: support RGB modes also
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  q_printf("%% Selected resolution:%s, JPEG comp:%u, (uses %u fbuffer%s in %s), grab: %s\r\n", 
            cam_resolution(config.frame_size),
            config.jpeg_quality,
            PPA(config.fb_count),
            config.fb_location == CAMERA_FB_IN_PSRAM ? "PSRAM" : "DRAM",
            config.grab_mode == CAMERA_GRAB_LATEST ? "latest" : "when empty");

  if (config.pin_pwdn >= 0) {  // Disable POWER_DOWN
    //pinForceMode(config.pin_pwdn, OUTPUT);
    digitalForceWrite(config.pin_pwdn, LOW);
    HELP(q_printf("%% Camera power up (GPIO%d is LOW)\n\r", config.pin_pwdn));
    q_delay(100);
  }

  sensor_t *s = NULL;
  if (ESP_OK == (err = esp_camera_init(&config))) {
    cam_good = true;
    if ((s = esp_camera_sensor_get()) != NULL) {
      s->set_gain_ctrl(s, 1);      // auto gain on
      s->set_exposure_ctrl(s, 1);  // auto exposure on
      s->set_awb_gain(s, 1);       // Auto White Balance enable (0 or 1)
      HELP(q_printf("%% Camera is on; Gain=auto, exposure=auto, white balance=auto\n\r"));
      return 0;
    } else
      HELP(q_printf("%% Camera is on\n\r"));
  }

  q_printf( "%% Camera init failed (error code %x)\n\r"
            "%% Check if selected camera model (\"%s\") matches your board\r\n ",err, model);
  return 0;
}


//"camera ARG1 ARG2 ... ARGn"
// All camera commands are processed here, except for the "show camera"
static int cmd_cam(int argc, char **argv) {

  int err = 0;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[1], "pinout"))
    return cmd_cam_pinout(argc, argv);


  if (!q_strcmp(argv[1], "up"))
    return cmd_cam_up(argc, argv);

  // any other camera commands require camera to be initialized
  if (!cam_good) {
    q_print("% Initialize camera first (\"camera up\" command)\n\r");
    return 0;
  }

  if (!q_strcmp(argv[1], "settings")) change_command_directory(0, keywords_espcam, PROMPT_ESPCAM, "camera"); else
  if (!q_strcmp(argv[1], "capture")) err = cmd_camera_capture(argc, argv); else
  if (!q_strcmp(argv[1], "filesize")) err = cmd_camera_filesize(argc, argv); else
  if (!q_strcmp(argv[1], "transfer")) err = cmd_camera_transfer(argc, argv); else
  if (!q_strcmp(argv[1], "down")) err = cmd_camera_down(argc, argv);  else err = 1;  // unrecoginzed argument

  return err;
}
#  endif // #if WITH_ESPCAM
#endif // #if COMPILING_ESPSHELL

