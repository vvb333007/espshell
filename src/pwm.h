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

#define MAGIC_FREQ 312000  // max allowed frequency for the "pwm" command. TODO: cover all frequency ranges up to 10MHz

// Enable or disable (freq==0) PWM generation on given pin. 
// Frequency must be in range (0..312kHz), duty is floating point number in range [0..1]. Depending on the frequency
// different LEDC resolution may be choosen.
//
// This function is used by command "pwm" (see cmd_pwm()) and it is also used by "pin" command (see cmd_pin())
//
static int pwm_enable(unsigned int pin, unsigned int freq, float duty) {

  int resolution = 8;

  if (!pin_exist(pin))
    return -1;

  // Sanity checks on arguments
  if (freq > MAGIC_FREQ) freq = MAGIC_FREQ;
  if (duty > 1.0f) duty = 1.0f;

  // Choose 10 bit duty resolution if frequency is low enough
  if (freq < 78722) resolution = 10;

  // pinMode() calls periman's deinit()stuff effectively detaching a pin from the LEDC driver
  // so we don't need to call ledcDetach()
  pinMode(pin, OUTPUT);
  //ledcDetach(pin);

  if (freq) {
    ledcAttach(pin, freq, resolution);
    ledcWrite(pin, (unsigned int)(duty * ((1 << resolution) - 1)));
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

    if (freq > MAGIC_FREQ)
      HELP(q_print("% Frequency will be adjusted to its maximum which is " xstr(MAGIC_FREQ) "] Hz\r\n"));
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

