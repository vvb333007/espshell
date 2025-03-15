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
// This behaviour can be changed by setting LEDC_CHANNEL_INC to 1: you'll get 2 times more PWM channels but adjacent
// channels will be oscillating at the same frequencies
//
#if COMPILING_ESPSHELL


// TODO: "show pwm" - display a table with pins, frequencies, duties, LEDC channel numbers

#ifndef LEDC_CHANNEL_INC
#  define LEDC_CHANNEL_INC 2
#endif

#ifndef LEDC_CHANNEL_MAX
#  define LEDC_CHANNEL_MAX SOC_LEDC_CHANNEL_NUM
#endif

//ESP32 has HS mode support while ESP32S3 doesn't
// TODO: test channels 8..16 (slow group)
#ifdef SOC_LEDC_SUPPORT_HS_MODE
#  define LEDC_CHANNELS (LEDC_CHANNEL_MAX * 2)
#else
#  define LEDC_CHANNELS (LEDC_CHANNEL_MAX * 1)
#endif

EXTERN uint32_t getXtalFrequencyMhz();
EXTERN uint32_t getApbFrequency();

static signed char ledc_res = 0; // Duty resolution override

// Enable or disable (freq==0) PWM generation on given pin. 
// Frequency must be in range (0..10 000 000 Hz), duty is floating point number in range [0..1]. Depending on the frequency
// different LEDC resolution may be choosen. This function cycles through PWM channels and selects only EVEN channel number for
// its operation to be able to generate 4 different frequencies at the same time.
//
// This function is used by command "pwm" (see cmd_pwm()) and it is also used by "pin" command (see cmd_pin())
//
static int pwm_enable(unsigned int pin, unsigned int freq, float duty/*TODO1.0: , channel*/) {

  unsigned int resolution;    // channel duty resolution, bits
  static int channel = 0;     // LEDC channel# to use
  unsigned int duty_abs;    // Scaled duty parameter

  if (!pin_exist(pin))
    return -1;

  if (freq) {
    // Clamp arguments
    if (freq > PWM_MAX_FREQUENCY)
      freq = PWM_MAX_FREQUENCY;

    if (duty > 1.0f)
      duty = 1.0f;

    unsigned long ledc_clock = 0; // LEDC clock frequency.

    // Find out the frequency of currently used LEDC clock: we need it to calculate optiomal duty resolution.
    // There are many different clock sources however XTAL & APB are mmost frequently used
    if (ledc_res < 1) {
      switch( ledcGetClockSource() ) {
        case LEDC_USE_APB_CLK:  ledc_clock = getApbFrequency(); break;                   // usually 80mhz
        case LEDC_USE_XTAL_CLK: ledc_clock = (getXtalFrequencyMhz() * 1000000UL); break;  // usually 40mhz
        default:
          q_print("% Unusual LEDC clock source: can't autoselect duty resolution\r\n"
                  "% Use \"var ledc_res X\" to force X-bit resolution\r\n");
          return -1;
      }
    }

    if (ledc_res > 0)
      resolution = ledc_res;
    else if ((resolution = ledc_find_suitable_duty_resolution(ledc_clock, freq)) == 0) {
        q_printf("%% <e>Can not find suitable duty resolution for the requested frequency</>\r\n"
                 "%% Frequency is either too high or too low(SRC_CLK=%lu Hz, PWM_FREQ=%u Hz)\r\n", ledc_clock, freq);

        // ESP32 can go down to 1 Hz, while ESP32S3 can't go below 3Hz. Others from the family probably have their low limits as well
        if (freq < 10) {
          HELP(q_print("%\r\n% You can use \"<i>pin</>\" command to generate low-frequency PWM:\r\n"
                      "% Example 1 Hz, 70% duty PWM on pin0 \"<i>pin 0 high 700 low 300 loop 999999</>\"\r\n"));
        }
        return -1;
    }
    VERBOSE(q_printf("%% Selected duty cycle resolution is %u bits, LEDC channel is %u\r\n",resolution, channel));  
  }

  ledcDetach(pin);      // ignore ret val
  pinMode(pin, OUTPUT); // TODO: investigate if it can be replaced with pinForceMode(pin, OUTPUT_ONLY)

  if (freq) {
    // duty is in the range of [0..1], so we scale it up to fit desired bit width
    duty_abs = (unsigned int)(duty * ((1 << resolution) - 1));
    // TODO: add manual channel setting
    if (ledcAttachChannel(pin, freq, resolution, channel)) {
      if (ledcWrite(pin, duty_abs)) {
        // Advance to the next channel
        if ((channel += LEDC_CHANNEL_INC) >= LEDC_CHANNELS)
          channel = 0;
        return 0;
      }
      ledcDetach(pin);
      q_printf("%% Failed to set the duty cycle value to %u\r\n",duty_abs);
    } else
      q_printf("%% Failed to attach to LEDC (channel=%u, resolution=%u, freq=%u, duty_abs=%u)\r\n",channel,resolution,freq,duty_abs);
    return -1;
  }
  return 0;
}

// "show pwm"
static int cmd_show_pwm(int argc, char **argv) {


  q_print("% Currently active PWM generators:\r\n"
          "%  GPIO | Frequency |  Duty   \r\n"
          "% ------+-----------+---------\r\n");
  for (int pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
    if (pin_exist_silent(pin)) {
      uint32_t freq;
      if ((freq = ledcReadFreq(pin)) != 0)
#pragma GCC diagnostic ignored "-Wformat"                
        q_printf("%%   % 2lu  |  % 8lu | % 5lu \r\n",pin, freq, ledcRead(pin));
#pragma GCC diagnostic warning "-Wformat"                  
    }

    return 0;
}


// Handles all of these:
//"pwm PIN FREQ [DUTY]"   - pwm on
//"pwm PIN"               - pwm off
//"pwm PIN 0"             - pwm off  <-- undoc
//"pwm PIN off"           - pwm off  <-- undoc
//
static int cmd_pwm(int argc, char **argv) {

  unsigned int freq = 0;
  float duty = 0.5f;
  unsigned pin;

  if (argc < 2) 
    return CMD_MISSING_ARG;

  // first parameter is pin number
  pin = q_atol(argv[1], BAD_PIN);  

  //frequency is the second one
  if (argc > 2)
    if ((freq = q_atol(argv[2], 0)) > PWM_MAX_FREQUENCY)
      HELP(q_print("% Frequency will be adjusted to its maximum which is " xstr(PWM_MAX_FREQUENCY) "] Hz\r\n"));

  // duty is the third. process it only if frequency is non-zero
  if (freq != 0) {
    if (argc > 3) {
      duty = q_atof(argv[3], -1);
      if (duty < 0 || duty > 1) {
        HELP(q_print("% <e>Duty cycle, a number in range [0.000 .. 1.000] is expected</>\r\n"));
        return 3;
      }
    }
  }

  // don't check the return code as pwm_enable() do diagnostic printfs
  pwm_enable(pin, freq, duty); 

  return 0;
}
#endif // #if COMPILING_ESPSHELL
