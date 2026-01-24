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
// ESPShell uses the LEDC peripheral to generate PWM: 8 or 16 channels,
// depending on the ESP32 model. Unfortunately, adjacent channels
// (i.e. channel 0 & 1, 2 & 3, etc.) share the same frequency. This is because
// there are only 4 timers per 8 channels.
//
// ESPShell uses EVEN channel numbers, ensuring that all PWM frequencies
// are independent.
//
// This approach, however, reduces the number of simultaneously active
// generators from 8 to 4 (on ESP32-S3; from 16 to 8 on ESP32).
// This behavior can be changed by setting /pwm_ch_inc/ to 1: you'll get
// twice as many PWM channels, but adjacent channels will then run at
// the same frequency.
//
// BUG: There is an issue where the ESP32 sometimes fails to start PWM at low
//      frequencies (around 100 Hz).
//      Workaround: start PWM at 10 kHz first, then switch to a lower
//      frequency.
//      This only happens right after flashing or rebooting.
//      Never observed on ESP32-S3.
//
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

// Human-readable name of the clock source used by the PWM subsystem.
//
// Arduino Core (as of 3.1.2) tries to use XTAL whenever possible,
// falling back to LEDC_AUTO_CLK on ESP32.
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
// Other clock sources are not supported at the moment: I don't have
// the appropriate hardware to test them, and I don't want to write
// code that "probably works".
//
// The RC_FAST clock is inaccurate and runs at a relatively low frequency.
// Its actual frequency varies between CPU models and ranges from
// 8 to 17.5 MHz.
    case LEDC_AUTO_CLK:            return "AUTO";    
    default:                       return "???";
  };
  
}

// Which hardware actually generates PWM?
//
// For now, this is fixed to LEDC. However, RMT and MCPWM can also be used
// to generate PWM with certain special properties, or any other suitable
// hardware that may appear in future Espressif SoCs.
//
static const char *pwm_hardware_used() {
  return "LEDC";
}

// PWM is a peripheral clocked from an external source.
//
// These clock sources may differ between CPU models; the most common
// ones are listed in pwm_clock_source(). Arduino Core tries to select
// XTAL whenever possible (i.e. when supported by the SoC and IDF).
//
// If XTAL is not supported as a clock source for LEDC, Arduino Core
// falls back to LEDC_AUTO_CLK, which provides no information about
// its actual frequency.
//
// Here we force the PWM clock source to either XTAL or APB — a small
// hack that should not affect sketch execution.
//
static uint32_t pwm_source_clock_frequency() {

  ledc_clk_cfg_t src = ledcGetClockSource();

#if SOC_LEDC_SUPPORT_APB_CLOCK        
  if (src == LEDC_USE_APB_CLK) return APBFreq * 1000000; else
#endif    

#if SOC_LEDC_SUPPORT_XTAL_CLOCK    
  if (src == LEDC_USE_XTAL_CLK) return XTALFreq * 1000000; else
#endif

#if SOC_LEDC_SUPPORT_REF_TICK
  if (src == LEDC_USE_REF_TICK) return 1 * 1000000; else
#endif

#if SOC_LEDC_SUPPORT_RC_FAST_CLOCK        
  if (src == case LEDC_USE_RC_FAST_CLK) return SOC_CLK_RC_FAST_FREQ_APPROX; else
#endif
    // "AUTO" clock source (or something we don't support). Select something appropriate instead.
    // We prefer XTAL because it provides a stable, known frequency.
    //
    // APB is faster than XTAL, but its frequency can change, and we don't
    // have any callbacks to track that :-/

  do {
#if SOC_LEDC_SUPPORT_XTAL_CLOCK
    src = LEDC_USE_XTAL_CLK;    // best stability, max 40MHz
#elif SOC_LEDC_SUPPORT_APB_CLOCK
    src = LEDC_USE_APB_CLK;     // best speed/resolution (80Mhz)
#elif SOC_LEDC_SUPPORT_RC_FAST_CLOCK
    src = LEDC_USE_RC_FAST_CLK; // 8 or 17.5 MHz RC oscillator, +-5% accuracy
#elif SOC_LEDC_SUPPORT_REF_TICK
    src = LEDC_USE_REF_TICK;    // "approximately 1 MHz", worst case
#else
#error "No APB, XTAL, RC_FAST or even REF_TICK support in LEDC :-/"
#endif
    if (ledcSetClockSource(src))
      return pwm_source_clock_frequency();
  } while (0);

  return 40000000UL; // Safe fallback: can't just return zero here
}



// Enable (freq > 0) or disable (freq == 0) PWM generation on the given pin.
//
// Frequency must be in the range (0..10,000,000 Hz). Duty is a floating-point
// value in the range [0..1]. Depending on the frequency, a different LEDC
// resolution may be chosen (unless the /ledc_res/ config variable is set).
//
// If /chan/ is less than zero, the channel number is auto-selected.
// Auto-selection is a simple round-robin over available PWM channels,
// with increments of 1 or 2 (default).
//
// To change the frequency and/or duty on the same pin, there is no need
// to "disable" PWM before calling "enable" again — it is safe to call
// enable multiple times on the same pin/channel.
//
static int pwm_enable_channel(unsigned int pin, unsigned int freq, float duty, signed char chan) {

  unsigned int resolution;      // Channel duty resolution, bits
  static int channel = 0;       // LEDC channel# to use
  unsigned int duty_abs;        // Scaled duty parameter
  unsigned long ledc_clock = 0; // LEDC clock frequency.

  // <0 means "auto"
  if (chan >= 0)
    channel = chan;

  // Only real GPIOs can participiate in PWM generation
  if (!pin_exist(pin) || pin_isvirtual(pin))
    return -1;

  // Disable channel completely. Reset GPIO to its default state and function
  if (freq == 0) {
    ledcDetach(pin);
    pinMode(pin, OUTPUT);
    VERBOSE(q_print("% PWM is disabled\r\n"));
    return 0;
  }

  // Frequency is specified
  // Clamp arguments. (we don't do it in cmd_pwm() for a reason! ("pin pwm" needs this))
  //
  if (freq > PWM_MAX_FREQUENCY)
    freq = PWM_MAX_FREQUENCY;

  if (duty > 1.0f)
    duty = 1.0f;

  // Determine the frequency of the currently used LEDC clock.
  // We need this to calculate the optimal duty resolution.
  //
  // If the /ledc_res/ config variable is set, don't attempt any calculations;
  // use the configured resolution instead.
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
                    "% 0.1 Hz, 50% duty, pin2: \"<i>pin 2 high delay 5000 low delay 5000 loop inf &</>\"\r\n"));
      
      return -1;
    }
  } else
    resolution = ledc_res;
  VERBOSE(q_printf("%% Selected duty cycle resolution is %u bits, LEDC channel is %u\r\n",resolution, channel));  

  // Calculate the absolute duty value.
  // Duty is in the range [0..1], so we scale it to fit the desired bit width.
  //
  duty_abs = (unsigned int)(duty * (float )((1 << resolution) - 1) + 0.5f); //  roundup duty cycle value;

  // If the channel is already running at the requested PWM frequency,
  // just update the duty cycle — don't stop the output.
  if (ledcReadFreq(pin) == freq) {
    if (ledcWrite(pin, duty_abs)) {
      VERBOSE(q_printf("%% PWM on pin#%u, %u Hz (%.1f%% duty cycle) is enabled\r\n",pin,freq,duty));
      return 0;
    }
    // FALL THROUGH
  }
  // Full circuit:
  ledcDetach(pin);
  pinMode(pin, OUTPUT); // TODO: investigate if it can be replaced with pinForceMode(pin, OUTPUT_ONLY)
  
  if (freq) {
        
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
      q_printf("%% Failed to set the absolute duty cycle value to %u\r\n",duty_abs);
    } else
      q_printf("%% Failed to attach to the LEDC (channel=%u, resolution=%u, freq=%u, duty_abs=%u)\r\n",channel,resolution,freq,duty_abs);

    // Regardless of the failure reason, if the requested frequency is below
    // 10 Hz, inform the user about alternative ways to generate low-frequency PWM.
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

static inline __attribute__((always_inline, unused)) int pwm_disable_channel(unsigned int pin) {
  return pwm_enable_channel(pin,0,0,-1);
}

// Handles the "show pwm" command.
// Displays the currently active PWM generators and their parameters.
//
// TODO: Implemented via the periman API — needs a review.
//
static int cmd_show_pwm(UNUSED int argc, UNUSED char **argv) {

  const char *hw = pwm_hardware_used();
  // Header
  q_print("%      -- Currently active PWM generators --\r\n"
          "%<r>  GPIO | Frequency | DutyAbs | Duty (%) | HW channel  </>\r\n"
          "% ------+-----------+---------+----------+-------------\r\n");

  // Go through all existing pins and try to fetch PWM parameters using ArduinoCore API
  for (uint8_t pin = 0; pin < NUM_PINS; pin++)
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
          
          q_printf("%%   %2u  |  %8lu |  %6lu |    %5u | %s%u\r\n",pin, freq, duty, percent, hw, channel);
          
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
      HELP(q_print( "% Must be an integer number. For frequencies below 1Hz please use\r\n"
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
        
        // If the user enters garbage (i.e. a non-numeric value, or a numeric value
        // less than -1), reject it with an error.
        //
        // Valid channel numbers are in the range -1..PWM_CHANNELS_NUM-1
        // (ESP32: -1..15, ESP32-S3: -1..7).
        //
        // A channel number of -1 means "auto-select channel".
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
