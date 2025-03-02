/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


// -- PWM module --
//
// Enables/disables a PWM signal on arbitrary pin.
// Unfortunately, the way it is done in ESP32 you can't have different pins generating different frequencies.
// What you can have is the same frequency but different duty for every pin.
//
// PWM can be enabled either by "pwm"  command or "pin pwm" command. 
//
#if COMPILING_ESPSHELL

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

// We assume here that Arduino Core selects XTAL as a clock source whenever it is possible. At least Arduino Core 3.0.7 does that.
// If this is not the case then Arduino Core uses CLK_AUTO which **probably** translates to CLK_APB
// We need to know the source clock frequency to find out maximum duty bitwidth allowed for given frequency
//
// TODO: which clock source is used for slow speed channels? Are they capable of 10Mhz?
//
#ifdef SOC_LEDC_SUPPORT_XTAL_CLOCK
   EXTERN uint32_t getXtalFrequencyMhz();
#  define SRC_CLK (getXtalFrequencyMhz() * 1000000UL)
#else
   EXTERN uint32_t getApbFrequency();
#  define SRC_CLK getApbFrequency()
#endif

static unsigned char ledc_res = 0; // convar

// Enable or disable (freq==0) PWM generation on given pin. 
// Frequency must be in range (0..10 000 000 Hz), duty is floating point number in range [0..1]. Depending on the frequency
// different LEDC resolution may be choosen. This function cycles through PWM channels and selects only EVEN channel number for
// its operation to be able to generate 4 different frequencies at the same time. TODO: should we add a channel_number argument to the command?
//
// This function is used by command "pwm" (see cmd_pwm()) and it is also used by "pin" command (see cmd_pin())
//
static int pwm_enable(unsigned int pin, unsigned int freq, float duty) {

  unsigned int resolution;
  static int channel = 0;

  if (!pin_exist(pin))
    return -1;

  // Clamp arguments
  if (freq > PWM_MAX_FREQUENCY)
    freq = PWM_MAX_FREQUENCY;

  if (duty > 1.0f)
    duty = 1.0f;

  // Find suitable duty resolution
  if (ledc_res)
    resolution = ledc_res;
  else if ((resolution = ledc_find_suitable_duty_resolution(SRC_CLK, freq)) == 0) {
      q_printf("%% <e>Can not find suitable duty resolution for the requested frequency</>\r\n"
               "%% Frequency is either too high or too low(SRC_CLK=%u Hz, PWM_FREQ=%u Hz)\r\n", (unsigned int)SRC_CLK, freq);
      return -1;
  }

  VERBOSE(q_printf("%% Selected duty cycle resolution is %u bits\r\n",resolution));  

  ledcDetach(pin);
  pinMode(pin, OUTPUT);

  if (freq) {
    if (ledcAttachChannel(pin, freq, resolution, channel)) {
      // duty is in the range of [0..1], so we scale it up to fit desired bit width
      duty = (duty * ((1 << resolution) - 1));
      if (ledcWrite(pin, (unsigned int)duty)) {
        // TODO: refactor the code to track frequencies: if there are two pins at the same frequency with differend duty cycles
        // then we can allocate adjacent channels for this
        if ((channel += 2) >= LEDC_CHANNELS)
          channel = 0;
        return 0;
      }
      ledcDetach(pin);
      q_printf("%% Failed to set the duty cycle value to %u\r\n",(unsigned int)duty);
    }
    return -1;
  }
  return 0;
}


//
//"pwm PIN FREQ [DUTY]"   - pwm on
//"pwm PIN"               - pwm off
static int cmd_pwm(int argc, char **argv) {

  unsigned int freq = 0;
  float duty = 0.5f;
  unsigned pin;

  if (argc < 2) return CMD_MISSING_ARG;     // missing arg
  pin = q_atol(argv[1], 999);  // first parameter is pin number

  //frequency is the second one (optional, can't be zero)
  if (argc > 2) {
    if ((freq = q_atol(argv[2], 0)) == 0)
      return 2;

    if (freq > PWM_MAX_FREQUENCY)
      HELP(q_print("% Frequency will be adjusted to its maximum which is " xstr(PWM_MAX_FREQUENCY) "] Hz\r\n"));
  }

  // duty is the third argument (optional)
  if (argc > 3) {
    duty = q_atof(argv[3], -1);
    if (duty < 0 || duty > 1) {
      HELP(q_print("% <e>Duty cycle is a number in range [0..1] (0.01 means 1% duty)</>\r\n"));
      return 3;
    }
  }

  if (pwm_enable(pin, freq, duty) < 0)
    q_print(Failed);

  return 0;
}
#endif // #if COMPILING_ESPSHELL

