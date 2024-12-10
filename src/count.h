/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Burdzhanadze <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#if COMPILING_ESPSHELL

#define PULSE_WAIT 1000      // default counting time, 1000ms
#define PCNT_OVERFLOW 20000  // PCNT interrupt every 20000 pulses


// PCNT interrupt handler. Called every 20 000 pulses 2 times :).
// i do not know why. should read docs better
static unsigned int count_overflow = 0;

static void IRAM_ATTR pcnt_interrupt(void *arg) {
  count_overflow++;
  PCNT.int_clr.val = BIT(PCNT_UNIT_0);
}


//TAG:count
//"count PIN [DELAY_MS [pos|neg|both]]"
//
static int pcnt_channel = PCNT_CHANNEL_0;
static int pcnt_unit = PCNT_UNIT_0;

static int cmd_count(int argc, char **argv) {

  pcnt_config_t cfg = { 0 };
  unsigned int pin, wait = PULSE_WAIT;
  int16_t count;

  if (!pin_exist((cfg.pulse_gpio_num = pin = q_atol(argv[1], 999))))
    return 1;

  cfg.ctrl_gpio_num = -1;  // don't use "control pin" feature
  cfg.channel = pcnt_channel;
  cfg.unit = pcnt_unit;
  cfg.pos_mode = PCNT_COUNT_INC;
  cfg.neg_mode = PCNT_COUNT_DIS;
  cfg.counter_h_lim = PCNT_OVERFLOW;

  // user has provided second argument?
  if (argc > 2) {
    if ((wait = q_atol(argv[2], DEF_BAD)) == DEF_BAD)
      return 2;

    //user has provided 3rd argument?
    if (argc > 3) {
      if (!q_strcmp(argv[3], "pos")) { /* default*/
      } else if (!q_strcmp(argv[3], "neg")) {
        cfg.pos_mode = PCNT_COUNT_DIS;
        cfg.neg_mode = PCNT_COUNT_INC;
      } else if (!q_strcmp(argv[3], "both")) {
        cfg.pos_mode = PCNT_COUNT_INC;
        cfg.neg_mode = PCNT_COUNT_INC;
      } else
        return 3;
    }
  }

  q_printf("%% Counting pulses on GPIO%d...", pin);
  if (is_foreground_task())
    HELP(q_print("(press <Enter> to stop counting)"));
  q_print(CRLF);


  pcnt_unit_config(&cfg);
  pcnt_counter_pause(PCNT_UNIT_0);
  pcnt_counter_clear(PCNT_UNIT_0);
  pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_H_LIM);
  pcnt_isr_register(pcnt_interrupt, NULL, 0, NULL);
  pcnt_intr_enable(PCNT_UNIT_0);

  // reset & start counting for /wait/ milliseconds
  count_overflow = 0;
  pcnt_counter_resume(PCNT_UNIT_0);
  wait = delay_interruptible(wait);
  pcnt_counter_pause(PCNT_UNIT_0);

  pcnt_get_counter_value(PCNT_UNIT_0, &count);

  pcnt_event_disable(PCNT_UNIT_0, PCNT_EVT_H_LIM);
  pcnt_intr_disable(PCNT_UNIT_0);

  count_overflow = count_overflow / 2 * PCNT_OVERFLOW + count;
  q_printf("%% %u pulses in %.3f seconds (%.1f Hz)\r\n", count_overflow, (float)wait / 1000.0f, count_overflow * 1000.0f / (float)wait);
  return 0;
}
#endif // #if COMPILING_ESPSHELL
