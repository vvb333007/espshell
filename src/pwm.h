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

// -- PWM module --
//
// Enables/disables a PWM signal on an arbitrary pin.
//
// ESPShell uses LEDC periferial to generate PWM: 8 or 16 channels, depending on ESP32 model. Unfortunately
// adjacent channels (i.e. channel0 & 1, 2 & 3 ..) will have the same frequency: this is because there are
// 4 timers per 8 channels. ESPShell uses EVEN numbers for channels thus  ensuring that all frequencies will 
// be independed.
//
// This approach, however, reduces the number of simultaneously working generators from 8 to 4 (on ESP32S3)
// This behaviour can be changed by setting /pwm_ch_inc/ to 1: you'll get 2 times more PWM channels but adjacent
// channels will be oscillating at the same frequency
//
// BUG BUG BUG: There is a bug when ESP32 can't start PWM if lower (100Hz) frequencies are requested
//              workaround: start 10kHz PWM first, then start any lower frequency. Only happens 
//              after flashing/rebooting sequence.  Never seen on ESP32-S3.
//
// TODO: investigate MCPWM module for PWM generation
#if COMPILING_ESPSHELL


#ifndef LEDC_CHANNEL_MAX
#  define LEDC_CHANNEL_MAX SOC_LEDC_CHANNEL_NUM
#endif

//ESP32 has HS mode support while ESP32S3 doesn't
#ifdef SOC_LEDC_SUPPORT_HS_MODE
#  define PWM_CHANNELS_NUM (LEDC_CHANNEL_MAX * 2)
#else
#  define PWM_CHANNELS_NUM (LEDC_CHANNEL_MAX * 1)
#endif


static signed char ledc_res = 0; // Duty resolution override. ConVar
static int pwm_ch_inc = 2;       // PWM channel increment. Set to 1 to have more channels. ConVar

// Human-readable name of a clock source used for PWM machinery.
// Arduino Core (as of 3.1.2) tries to use XTAL whenever possible, falling back to LEDC_AUTO_CLK on ESP32.
// 
static const char *pwm_clock_source() {
  switch( ledcGetClockSource() ) {
#if SOC_LEDC_SUPPORT_APB_CLOCK    
    case LEDC_USE_APB_CLK:         return "APB";
#endif    
#if SOC_LEDC_SUPPORT_XTAL_CLOCK
    case LEDC_USE_XTAL_CLK:        return "XTAL";
#endif    
#if SOC_LEDC_SUPPORT_REF_TICK    
    case LEDC_USE_REF_TICK:        return "REF_TICK";
#endif
#if SOC_LEDC_SUPPORT_RC_FAST_CLOCK
    case LEDC_USE_RC_FAST_CLK:     return "RC_FAST";
#endif
    // other than that we do not support at the moment: I do not have appropriate hardware to test it
    // and I don't want to write code which is "probably working"
    // RC_FAST clock is inaccuare and of a low frequency. Different CPU models has its value ranging from 8 to 17.5MHz
    case LEDC_AUTO_CLK:            return "AUTO";    
    default:                       return "???";
  };
  
}

// Which hardware does actual PWM?
// It is fixed to LEDC for now, however there are RMT and MCPWM which can be used to generate
// PWM with some special properties
//
static const char *pwm_hardware_used() {
  return "LEDC";
}

// PWM is a peripherial, which is clocked from an external source.
// These sources can be different from one cpu model to another, most common are
// listed in pwm_clock_source(). Arduino Core tries to select XTAL as a clock source whenever possible (i.e. supported by SoC & IDF).
// If XTAL is not supported as a clock source for LEDC then Arduino Core selects LEDC_AUTO_CLK which bears zero information
// on its frequency;
// 
// Here we force PWM clock source to be either XTAL or APB - a small hack which should not affect sketch execution
// 
static uint32_t pwm_source_clock_frequency() {

  switch( ledcGetClockSource() ) {
#if SOC_LEDC_SUPPORT_APB_CLOCK        
    case LEDC_USE_APB_CLK:     return APBFreq * 1000000;
#endif    
#if SOC_LEDC_SUPPORT_XTAL_CLOCK    
    case LEDC_USE_XTAL_CLK:    return XTALFreq * 1000000;
#endif
#if SOC_LEDC_SUPPORT_REF_TICK
    case LEDC_USE_REF_TICK:    return 1 * 1000000;
#endif
#if SOC_LEDC_SUPPORT_RC_FAST_CLOCK        
    case LEDC_USE_RC_FAST_CLK: return SOC_CLK_RC_FAST_FREQ_APPROX;
#endif    
    case LEDC_AUTO_CLK:
#if SOC_LEDC_SUPPORT_XTAL_CLOCK                   // best stability, max 40MHz
      ledcSetClockSource(LEDC_USE_XTAL_CLK);
#elif SOC_LEDC_SUPPORT_APB_CLOCK                  // best speed/resolution (80Mhz)
      ledcSetClockSource(LEDC_USE_APB_CLK);
#elif SOC_LEDC_SUPPORT_RC_FAST_CLOCK              // 8 or 17.5 MHz RC oscillator, +-5% accuracy
      ledcSetClockSource(LEDC_USE_RC_FAST_CLK);
#elif SOC_LEDC_SUPPORT_REF_TICK                   // "approximately 1 MHz"
      ledcSetClockSource(LEDC_USE_REF_TICK);
#else
#     warning "No APB, XTAL, RC_FAST or even REF_TICK support in LEDC :-/"
      return 0;
#endif      
      return pwm_source_clock_frequency(); //TODO: make this recursion safe: what if ledcSetClockSource() does not change clock source from AUTO to XTAL?
    default: //fallthrough
  };

  return 0;
}

// Enable (freq > 0) or disable (freq == 0) PWM generation on given pin. 
// Frequency must be in range (0..10 000 000 Hz), duty is floating point number in range [0..1]. Depending on the frequency
// different LEDC resolution may be choosen. 
//
// If /chan/ is less than zero then channel number is autoselected. Autoselecting is a simple cycling through available
// PWM channels with increments of 1 or 2(Default).
//
// 
static int pwm_enable_channel(unsigned int pin, unsigned int freq, float duty, signed char chan) {

  unsigned int resolution;  // Channel duty resolution, bits
  static int channel = 0;   // LEDC channel# to use
  unsigned int duty_abs;    // Scaled duty parameter

  // <0 means "auto"
  if (chan >= 0)
    channel = chan;

  if (!pin_exist(pin))
    return -1;

  if (freq) {
    // Clamp arguments. (we don't do it in cmd_pwm() for a reason! ("pin pwm" needs this))
    if (freq > PWM_MAX_FREQUENCY)
      freq = PWM_MAX_FREQUENCY;

    if (duty > 1.0f)
      duty = 1.0f;

    unsigned long ledc_clock = 0; // LEDC clock frequency.

    // Find out the frequency of currently used LEDC clock: we need it to calculate optimal duty resolution.
    // Don't do anything if /ledc_res/ is set.
    //
    if (ledc_res < 1) {
      if ((ledc_clock = pwm_source_clock_frequency()) == 0) {
          q_print("% Unusual LEDC clock source: can't autoselect duty resolution\r\n"
                  "% Use \"var ledc_res 8\" to force 8-bit resolution (as an example)\r\n");
          return -1;
      }

      if ((resolution = ledc_find_suitable_duty_resolution(ledc_clock, freq)) == 0) {
        q_printf("%% <e>Can not find suitable duty resolution for the requested frequency</>\r\n"
                 "%% Frequency is either too high or too low(SRC_CLK=%lu Hz, PWM_FREQ=%u Hz)\r\n", ledc_clock, freq);

        // ESP32 can go down to 1 Hz, while ESP32S3 can't go below 3Hz. Others from the family probably have their low limits as well
print_hint_and_exit:        
        if (freq < 10)
          HELP(q_print("%\r\n% You can use \"<i>pin</>\" command to generate low-frequency PWM:\r\n"
                      "% <u>Examples:</>\r\n"
                      "% 1 Hz, 70% duty PWM on pin0: \"<i>pin 0 high delay 700 low delay 300 loop inf &</>\"\r\n"
                      "% 0.1 Hz, 50% duty, pin2: \"<i>pin 2 high delay 5000 low delay 5000 loop inf&</>\"\r\n"));
        
        return -1;
      }
    } else
      resolution = ledc_res;
 
    VERBOSE(q_printf("%% Selected duty cycle resolution is %u bits, LEDC channel is %u\r\n",resolution, channel));  
  }

  ledcDetach(pin);
  pinMode(pin, OUTPUT); // TODO: investigate if it can be replaced with pinForceMode(pin, OUTPUT_ONLY)

  if (freq) {
    // duty is in the range of [0..1], so we scale it up to fit desired bit width
    duty_abs = (unsigned int)(duty * (float )((1 << resolution) - 1) + 0.5f); //  roundup duty cycle value;
        
    if (ledcAttachChannel(pin, freq, resolution, channel)) {
      if (ledcWrite(pin, duty_abs)) {
        VERBOSE(q_printf("%% PWM on pin#%u, %u Hz (%.1f%% duty cycle, channel#%u) is enabled\r\n",pin,freq,duty,channel));
        
        // Advance to the next channel.
        // Setting pwm_ch_inc to 0 will cause channel number to remain the same unless 4th argument is passed
        channel += pwm_ch_inc;
        if (channel >= PWM_CHANNELS_NUM)
          channel = 0;
        else if (channel < 0)
          channel = PWM_CHANNELS_NUM - 1;
        
        if (chan >= 0) { // 4th argument triggers helpline
          HELP(q_printf("%% PWM channel %u is to be used next, if not explicitly set\r\n", channel));
        }
        return 0;
      }
      ledcDetach(pin);
      q_printf("%% Failed to set absolute duty cycle value to %u\r\n",duty_abs);
    } else
      q_printf("%% Failed to attach to LEDC (channel=%u, resolution=%u, freq=%u, duty_abs=%u)\r\n",channel,resolution,freq,duty_abs);

    // No matter what was the failure: if the frequency requested is below 10Hz, tell user about
    // other ways of low frequency PWM generation
    goto print_hint_and_exit;
  }
  return 0;
}

// Same as above but autoselects PWM channel number.
// Used by "pin" command (see cmd_pin())
//
static inline int pwm_enable(unsigned int pin, unsigned int freq, float duty) {
  return pwm_enable_channel(pin, freq, duty, -1);
}

// Handles "show pwm"
// Displays PWM generators currently active and their parameters
// It is done via periman API and needs a review
//
static int cmd_show_pwm(UNUSED int argc, UNUSED char **argv) {

  const char *hw = pwm_hardware_used();
  // Header
  q_print("%      -- Currently active PWM generators --\r\n"
          "%<r>  GPIO | Frequency | DutyAbs | Duty (%) | HW channel  </>\r\n"
          "% ------+-----------+---------+----------+-------------\r\n");

  // Go through all existing pins and try to fetch PWM parameters using ArduinoCore API
  for (int pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
    if (pin_exist_silent(pin)) {

      uint32_t freq,       // PWM frequency as read "from the pin"
               duty,       // PWM duty --
               duty_max;   // Maximum possible duty (absolute value) for given frequency
      uint8_t  channel,    // PWM channel that is associated with given pin
               percent;    // PWM %, calculated

      if ((freq = ledcReadFreq(pin)) != 0) {
        ledc_channel_handle_t *bus; 
        // TODO: Need a semaphore here. Arduino Core should have one
        if ((bus = (ledc_channel_handle_t *)perimanGetPinBus(pin, ESP32_BUS_TYPE_LEDC)) != NULL) {
          // these local variables will be eliminated by GCC; they are here to make the code more readable
          duty_max = (1 << bus->channel_resolution) - 1;
          channel = bus->channel;
          duty = ledcRead(pin);
          percent = (unsigned)(((float)duty / (float)duty_max) * 100.0f);
          WD()
          q_printf("%%   % 2lu  |  % 8lu |  % 6lu |    % 5u | %s%u\r\n",pin, freq, duty, percent, hw, channel);
          WE()
        }
      }
    }

    q_printf("%%\r\n%% PWM clock source is \"%s\", (running at %lu Hz)\r\n",
              pwm_clock_source(), 
              pwm_source_clock_frequency());

    return 0;
}


// Handles all of these:
//"pwm PIN FREQ [DUTY [CHANNEL]]"   - pwm on
//"pwm PIN"               - pwm off
//"pwm PIN 0"             - pwm off  <-- undoc
//"pwm PIN off"           - pwm off  <-- undoc
//
static int cmd_pwm(int argc, char **argv) {

  unsigned int freq = 0;     // frequency to set. 0 - means PWM stop
  float        duty = 0.5f;  // duty cycle [0..1]
  unsigned     pin;          

  if (argc < 2) 
    return CMD_MISSING_ARG;

  // first parameter is the pin number
  // we don't assert pin_exist() here since it is done in pwm_enable_channel()
  pin = q_atol(argv[1], BAD_PIN);  

  //frequency is the second argument. optional.
  if (argc > 2) {
    
    if ((freq = q_atol(argv[2], 0)) > PWM_MAX_FREQUENCY)
      HELP(q_print("% Frequency will be adjusted to its maximum which is " xstr(PWM_MAX_FREQUENCY) " Hz\r\n"));
    // was it attempt to use floating point number? 
    if (q_findchar(argv[2],'.')) {
      HELP(q_print( "% Must be integer number. For frequencies below 1Hz please use\r\n"
                    "%\"pin X high delay Y low delay Y loop inf &\" command\r\n"));
      return 2;
    }
  }

  // Duty is the third optional argument. 
  // Process it only if frequency is non-zero
  if (freq != 0) {
    if (argc > 3) {
      duty = q_atof(argv[3], -1);
      if (duty < 0 || duty > 1) {
        HELP(q_print("% <e>Duty cycle is a number in range [0..1] (default is 0.5, i.e. 50%)</>\r\n"));
        return 3;
      }
    }

    // Is PWM (LEDC) channel specified? Fourth optional argument.
    if (argc > 4) {
      if (isnum(argv[4])) {
        
        // If user enters garbage (i.e. non-numeric value, or it is numeric but less than "-1") then we reject it with error
        // Numbers from -1 to PWM_CHANNELS_NUM-1 are ok. (ESP32 valid numbers are -1..15, ESP32-s3 only has -1..7).
        // Channel number of "-1" means "channel autoselect".
        signed char channel = q_atoi(argv[4],-2);
        if (channel > -2 && channel < PWM_CHANNELS_NUM ) {
          pwm_enable_channel(pin, freq, duty, channel);
          return 0;
        }
      }
      HELP(q_printf("%% <e>Channel number [0..%d] is expected, instead of \"%s\"</>\r\n", PWM_CHANNELS_NUM - 1, argv[4]));
      return 4;
    }
  }

  // don't check the return code as pwm_enable() does diagnostic printfs
  pwm_enable(pin, freq, duty); 

  return 0;
}
#endif // #if COMPILING_ESPSHELL
