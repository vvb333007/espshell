/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


// -- pin (GPIO) manipulation --
// Command "pin" (and her async version "pin&") is a tiny command processor itself: one can enter multiple arguments
// to the "pin" command to form a sort of a microcode program which gets executed.

#if COMPILING_ESPSHELL

// Structure used to save/load pin states by "pin X save"/"pin X load".
//
static struct {
  uint8_t flags;  // INPUT,PULLUP,...
  bool value;     // digital value
  uint16_t sig_out;
  uint16_t fun_sel;
  int bus_type;  //periman bus type.
} Pins[SOC_GPIO_PIN_COUNT];


// IO_MUX function code --> human readable text mapping
// Each ESP32 variant has its own mapping but I made tables
// only for ESP32 and ESP32S3/2 simply because I have these boards
//
// Each pin of ESP32 can carry a function: be either a GPIO or be an periferial pin:
// SD_DATA0 or UART_TX etc.
//
//TODO: add support for other Espressif ESP32 variants
//

#ifdef CONFIG_IDF_TARGET_ESP32
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][6] = {
  // ESP32 pins can be assigned one of 6 functions via IO_MUX
  { "GPIO0", "CLK_OUT1", "GPIO0", "3", "4", "EMAC_TX_CLK" },
  { "U0TXD", "CLK_OUT3", "GPIO1", "3", "4", "EMAC_RXD2" },
  { "GPIO2", "HSPIWP", "GPIO2", "HS2_DATA0", "SD_DATA0", "5" },
  { "U0RXD", "CLK_OUT2", "GPIO3", "3", "4", "5" },
  { "GPIO4", "HSPIHD", "GPIO4", "HS2_DATA1", "SD_DATA1", "EMAC_TX_ER" },
  { "GPIO5", "VSPICS0", "GPIO5", "HS1_DATA6", "4", "EMAC_RX_CLK" },
  { "SD_CLK", "SPICLK", "GPIO6", "HS1_CLK", "U1CTS", "5" },
  { "SD_DATA0", "SPIQ", "GPIO7", "HS1_DATA0", "U2RTS", "5" },
  { "SD_DATA1", "SPID", "GPIO8", "HS1_DATA1", "U2CTS", "5" },
  { "SD_DATA2", "SPIHD", "GPIO9", "HS1_DATA2", "U1RXD", "5" },
  { "SD_DATA3", "SPIWP", "GPIO10", "HS1_DATA3", "U1TXD", "5" },
  { "SD_CMD", "SPICS0", "GPIO11", "HS1_CMD", "U1RTS", "5" },
  { "MTDI", "HSPIQ", "GPIO12", "HS2_DATA2", "SD_DATA2", "EMAC_TXD3" },
  { "MTCK", "HSPID", "GPIO13", "HS2_DATA3", "SD_DATA3", "EMAC_RX_ER" },
  { "MTMS", "HSPICLK", "GPIO14", "HS2_CLK", "SD_CLK", "EMAC_TXD2" },
  { "MTDO", "HSPICS0", "GPIO15", "HS2_CMD", "SD_CMD", "EMAC_RXD3" },
  { "GPIO16", "1", "GPIO16", "HS1_DATA4", "U2RXD", "EMAC_CLK_OUT" },
  { "GPIO17", "1", "GPIO17", "HS1_DATA5", "U2TXD", "EMAC_CLK_180" },
  { "GPIO18", "VSPICLK", "GPIO18", "HS1_DATA7", "4", "5" },
  { "GPIO19", "VSPIQ", "GPIO19", "U0CTS", "4", "EMAC_TXD0" },
  { "GPIO20", "GPIO20(1)", "GPIO20(2)", "GPIO20(3)", "GPIO20(4)", "GPIO20(5)" },
  { "GPIO21", "VSPIHD", "GPIO21", "3", "4", "EMAC_TX_EN" },
  { "GPIO22", "VSPIWP", "GPIO22", "U0RTS", "4", "EMAC_TXD1" },
  { "GPIO23", "VSPID", "GPIO23", "HS1_STROBE", "4", "5" },
  { "GPIO24", "GPIO24(1)", "GPIO24(2)", "GPIO24(3)", "GPIO24(4)", "GPIO24(5)" },
  { "GPIO25", "1", "GPIO25", "3", "4", "EMAC_RXD0" },
  { "GPIO26", "1", "GPIO26", "3", "4", "EMAC_RXD1" },
  { "GPIO27", "1", "GPIO27", "3", "4", "EMAC_RX_DV" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "GPIO32", "1", "GPIO32", "3", "4", "5" },
  { "GPIO33", "1", "GPIO33", "3", "4", "5" },
  { "GPIO34", "1", "GPIO34", "3", "4", "5" },
  { "GPIO35", "1", "GPIO35", "3", "4", "5" },
  { "GPIO36", "1", "GPIO36", "3", "4", "5" },
  { "GPIO37", "1", "GPIO37", "3", "4", "5" },
  { "GPIO38", "1", "GPIO38", "3", "4", "5" },
  { "GPIO39", "1", "GPIO39", "3", "4", "5" },
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][5] = {
  // ESP32S3 MUX functions for pins
  { "GPIO0", "GPIO0", "2", "3", "4" },
  { "GPIO1", "GPIO1", "2", "3", "4" },
  { "GPIO2", "GPIO2", "2", "3", "4" },
  { "GPIO3", "GPIO3", "2", "3", "4" },
  { "GPIO4", "GPIO4", "2", "3", "4" },
  { "GPIO5", "GPIO5", "2", "3", "4" },
  { "GPIO6", "GPIO6", "2", "3", "4" },
  { "GPIO7", "GPIO7", "2", "3", "4" },
  { "GPIO8", "GPIO8", "2", "SUBSPICS1", "4" },
  { "GPIO9", "GPIO9", "2", "SUBSPIHD", "FSPIHD" },
  { "GPIO10", "GPIO10", "FSPIIO4", "SUBSPICS0", "FSPICS0" },
  { "GPIO11", "GPIO11", "FSPIIO5", "SUBSPID", "FSPID" },
  { "GPIO12", "GPIO12", "FSPIIO6", "SUBSPICLK", "FSPICLK" },
  { "GPIO13", "GPIO13", "FSPIIO7", "SUBSPIQ", "FSPIQ" },
  { "GPIO14", "GPIO14", "FSPIDQS", "SUBSPIWP", "FSPIWP" },
  { "GPIO15", "GPIO15", "U0RTS", "3", "4" },
  { "GPIO16", "GPIO16", "U0CTS", "3", "4" },
  { "GPIO17", "GPIO17", "U1TXD", "3", "4" },
  { "GPIO18", "GPIO18", "U1RXD", "CLK_OUT3", "4" },
  { "GPIO19", "GPIO19", "U1RTS", "CLK_OUT2", "4" },
  { "GPIO20", "GPIO20", "U1CTS", "CLK_OUT1", "4" },
  { "GPIO21", "GPIO21", "2", "3", "4" },
  { "1", "2", "3", "3", "4" },
  { "1", "2", "3", "3", "4" },
  { "1", "2", "3", "3", "4" },
  { "1", "2", "3", "3", "4" },
  { "SPICS1", "GPIO26", "2", "3", "4" },
  { "SPIHD", "GPIO27", "2", "3", "4" },
  { "SPIWP", "GPIO28", "2", "3", "4" },
  { "SPICS0", "GPIO29", "2", "3", "4" },
  { "SPICLK", "GPIO30", "2", "3", "4" },
  { "SPIQ", "GPIO31", "2", "3", "4" },
  { "SPID", "GPIO32", "2", "3", "4" },
  { "GPIO33", "GPIO33", "FSPIHD", "SUBSPIHD", "SPIIO4" },
  { "GPIO34", "GPIO34", "FSPICS0", "SUBSPICS0", "SPIIO5" },
  { "GPIO35", "GPIO35", "FSPID", "SUBSPID", "SPIIO6" },
  { "GPIO36", "GPIO36", "FSPICLK", "SUBSPICLK", "SPIIO7" },
  { "GPIO37", "GPIO37", "FSPIQ", "SUBSPIQ", "SPIDQS" },
  { "GPIO38", "GPIO38", "FSPIWP", "SUBSPIWP", "4" },
  { "MTCK", "GPIO39", "CLK_OUT3", "SUBSPICS1", "4" },
  { "MTDO", "GPIO40", "CLK_OUT2", "3", "4" },
  { "MTDI", "GPIO41", "CLK_OUT1", "3", "4" },
  { "MTMS", "GPIO42", "2", "3", "4" },
  { "U0TXD", "GPIO43", "CLK_OUT1", "3", "4" },
  { "U0RXD", "GPIO44", "CLK_OUT2", "3", "4" },
  { "GPIO45", "GPIO45", "2", "3", "4" },
  { "GPIO46", "GPIO46", "2", "3", "4" },
  { "SPICLK_P_DIFF", "GPIO47", "SUBSPICLK_P_DIFF", "3", "4" },
  { "SPICLK_N_DIFF", "GPIO48", "SUBSPICLK_N_DIFF", "3", "4" },
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][5] = {
  // ESP32S2 MUX functions for pins
  { "GPIO0", "GPIO0", "2", "3", "4" },
  { "GPIO1", "GPIO1", "2", "3", "4" },
  { "GPIO2", "GPIO2", "2", "3", "4" },
  { "GPIO3", "GPIO3", "2", "3", "4" },
  { "GPIO4", "GPIO4", "2", "3", "4" },
  { "GPIO5", "GPIO5", "2", "3", "4" },
  { "GPIO6", "GPIO6", "2", "3", "4" },
  { "GPIO7", "GPIO7", "2", "3", "4" },
  { "GPIO8", "GPIO8", "2", "SUBSPICS1", "4" },
  { "GPIO9", "GPIO9", "2", "SUBSPIHD", "FSPIHD" },
  { "GPIO10", "GPIO10", "FSPIIO4", "SUBSPICS0", "FSPICS0" },
  { "GPIO11", "GPIO11", "FSPIIO5", "SUBSPID", "FSPID" },
  { "GPIO12", "GPIO12", "FSPIIO6", "SUBSPICLK", "FSPICLK" },
  { "GPIO13", "GPIO13", "FSPIIO7", "SUBSPIQ", "FSPIQ", "" },
  { "GPIO14", "GPIO14", "FSPIDQS", "SUBSPIWP", "FSPIWP" },
  { "XTAL_32K_P", "GPIO15", "U0RTS", "3", "4" },
  { "XTAL_32K_N", "GPIO16", "U0CTS", "3", "4" },
  { "DAC_1", "GPIO17", "U1TXD", "3", "4" },
  { "DAC_2", "GPIO18", "U1RXD", "CLK_OUT3", "4" },
  { "GPIO19", "GPIO19", "U1RTS", "CLK_OUT2", "4" },
  { "GPIO20", "GPIO20", "U1CTS", "CLK_OUT1", "4" },
  { "GPIO21", "GPIO21", "2", "3", "4" },
  { "0", "1", "2", "3", "4" },
  { "0", "1", "2", "3", "4" },
  { "0", "1", "2", "3", "4" },
  { "0", "1", "2", "3", "4" },
  { "SPICS1", "GPIO26", "2", "3", "4" },
  { "SPIHD", "GPIO27", "2", "3", "4" },
  { "SPIWP", "GPIO28", "2", "3", "4" },
  { "SPICS0", "GPIO29", "2", "3", "4" },
  { "SPICLK", "GPIO30", "2", "3", "4" },
  { "SPIQ", "GPIO31", "2", "3", "4" },
  { "SPID", "GPIO32", "2", "3", "4" },
  { "GPIO33", "GPIO33", "FSPIHD", "SUBSPIHD", "SPIIO4" },
  { "GPIO34", "GPIO34", "FSPICS0", "SUBSPICS0", "SPIIO5" },
  { "GPIO35", "GPIO35", "FSPID", "SUBSPID", "SPIIO6" },
  { "GPIO36", "GPIO36", "FSPICLK", "SUBSPICLK", "SPIIO7" },
  { "GPIO37", "GPIO37", "FSPIQ", "SUBSPIQ", "SPIDQS" },
  { "GPIO38", "GPIO38", "FSPIWP", "SUBSPIWP", "4" },
  { "MTCK", "GPIO39", "CLK_OUT3", "SUBSPICS1", "4" },
  { "MTDO", "GPIO40", "CLK_OUT2", "3", "4" },
  { "MTDI", "GPIO41", "CLK_OUT1", "3", "4" },
  { "MTMS", "GPIO42", "2", "3", "4" },
  { "U0TXD", "GPIO43", "CLK_OUT1", "3", "4" },
  { "U0RXD", "GPIO44", "CLK_OUT2", "3", "4" },
  { "GPIO45", "GPIO45", "2", "3", "4" },
  { "GPIO46", "GPIO46", "2", "3", "4" },
#else
#warning "Unsupported target, using dummy IO_MUX function name table"
static const char *io_mux_func_name[52][6] = {
  // unknown/unsupported target. make array big enough (6 functions)
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
  { "0", "1", "2", "3", "4", "5" },
#endif  // CONFIG_IDF_TARGET...
};      //static const char *io_mux_func_name[][] = {

static int pin_show_mux_functions() {
  int pin;
  int nfunc = 5;
#ifdef CONFIG_IDF_TARGET_ESP32
  ++nfunc;
#endif
  HELP(q_printf( "%% IO MUX has <i>%s%u</> function%s for every pin. The mapping is as follows:\r\n",nfunc < 6 ? "only " : "", nfunc, nfunc == 1 ? "" : "s"));
  q_printf("%%Pin | Function<i>0</> | Function<i>1</> | Function<i>2</> | Function<i>3</> | Function<i>4</> | Function<i>5</>\r\n"
           "%%----+-----------+-----------+-----------+-----------+-----------+-----------\r\n");
  
  for (pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++) {
    q_printf( "%% %02u ",pin);
    for (int i = 0; i < nfunc; i++) 
      // 1-char long name means undefined function
      q_printf("| % 9s ",io_mux_func_name[pin][i][1] ? io_mux_func_name[pin][i] : " -undef- ");
    q_print(CRLF);
  }
  return 0;
}

static bool pin_set_iomux_function(unsigned int pin, unsigned int function) {
  
}


// same as digitalRead() but reads all pins no matter what
// exported (not static) to enable its use in user sketch
//
int digitalForceRead(int pin) {
  gpio_ll_input_enable(&GPIO, pin);
  return gpio_ll_get_level(&GPIO, pin) ? HIGH : LOW;
}

// same as digitalWrite() but bypasses periman so no init/deinit
// callbacks are called. pin bus type remain unchanged
//
void digitalForceWrite(int pin, unsigned char level) {
  gpio_ll_output_enable(&GPIO, pin);
  gpio_set_level((gpio_num_t)pin, level == HIGH ? 1 : 0);
}

// ESP32 Arduino Core as of version 3.0.5 (latest I use) defines pin OUTPUT flag as both INPUT and OUTPUT
#ifndef OUTPUT_ONLY
#define OUTPUT_ONLY ((OUTPUT) & ~(INPUT))
#endif

// same as pinMode() but calls IDF directly bypassing
// PeriMan's pin deinit/init. As a result it allows flags manipulation on
// reserved pins without crashing & rebooting
//
// exported (non-static) to allow use in a sketch (by including "extra/espshell.h" in
// user sketch .ino file)
//
void pinMode2(unsigned int pin, unsigned int flags) {

  // set ARDUINO flags to the pin using ESP-IDF functions
  if ((flags & PULLUP) == PULLUP)         gpio_ll_pullup_en(&GPIO, pin);    else gpio_ll_pullup_dis(&GPIO, pin);
  if ((flags & PULLDOWN) == PULLDOWN)     gpio_ll_pulldown_en(&GPIO, pin);  else gpio_ll_pulldown_dis(&GPIO, pin);
  if ((flags & OPEN_DRAIN) == OPEN_DRAIN) gpio_ll_od_enable(&GPIO, pin);    else gpio_ll_od_disable(&GPIO, pin);
  if ((flags & INPUT) == INPUT)           gpio_ll_input_enable(&GPIO, pin); else gpio_ll_input_disable(&GPIO, pin);

  // Deal with OUTPUT flag.
  if ((flags & OUTPUT_ONLY) != OUTPUT_ONLY)
    gpio_ll_output_disable(&GPIO, pin); 
  else
    if (!pin_is_input_only_pin(pin)) 
      gpio_ll_output_enable(&GPIO, pin);
}


// checks if pin (GPIO) number is in valid range.
// display a message if pin is out of range
static bool pin_exist(int pin) {
  // pin number is in range and is a valid GPIO number?
  if ((pin < SOC_GPIO_PIN_COUNT) && (((uint64_t)1 << pin) & SOC_GPIO_VALID_GPIO_MASK))
    return true;
#if WITH_HELP
  else {
    uint64_t mask = ~SOC_GPIO_VALID_GPIO_MASK;
    int informed = 0;
    // pin number is incorrect, display help
    q_printf("%% Available pin numbers are 0..%d", SOC_GPIO_PIN_COUNT - 1);

    if (mask)
      for (pin = 63; pin >= 0; pin--)
        if (mask & ((uint64_t)1 << pin)) {
          mask &= ~((uint64_t)1 << pin);
          if (pin < SOC_GPIO_PIN_COUNT) {
            if (!informed) {
              informed = 1;
              q_print(", except pins: ");
            } else
              q_print(", ");
            q_printf("%s<e>%d</>", mask ? "" : "and ", pin);
          }
        }
    // the function is not in .h files.
    // moreover its name has changed in recent ESP IDF
    for (pin = informed = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
      if (esp_gpio_is_pin_reserved(pin))
        informed++;

    if (informed) {
      q_print("\r\n% Reserved pins (used internally):");
      for (pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
        if (esp_gpio_is_pin_reserved(pin)) {
          informed--;
          q_printf("%s<e>%d</>", informed ? ", " : " and ", pin);
        }
    }
    q_print(CRLF);
#endif  // WITH_HELP
    return false;
  }
}


// save pin state.
// there is an array Pins[] which is used for that. Subsequent saves rewrite previous save.
// pin_load() is used to load pin state from Pins[]
//
static void pin_save(int pin) {

  bool pd, pu, ie, oe, od, slp_sel;
  uint32_t drv, fun_sel, sig_out;

  gpio_ll_get_io_config(&GPIO, pin, &pu, &pd, &ie, &oe, &od, &drv, &fun_sel, &sig_out, &slp_sel);

  //Pin peri connections:
  // fun_sel is either PIN_FUNC_GPIO and then sig_out is the signal ID
  // to route to the pin via GPIO Matrix.

  Pins[pin].sig_out = sig_out;
  Pins[pin].fun_sel = fun_sel;
  Pins[pin].bus_type = perimanGetPinBusType(pin);

  //save digital value for OUTPUT GPIO
  if (Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO && oe)
    Pins[pin].value = (digitalRead(pin) == HIGH);

  Pins[pin].flags = 0;
  if (pu) Pins[pin].flags |= PULLUP;
  if (pd) Pins[pin].flags |= PULLDOWN;
  if (ie) Pins[pin].flags |= INPUT;
  if (oe) Pins[pin].flags |= OUTPUT;
  if (od) Pins[pin].flags |= OPEN_DRAIN;
}

// Load pin state from Pins[] array
// Attempt is made to restore GPIO Matrix connections however it is not working as intended
//
static void pin_load(int pin) {

  // 1. restore pin mode
  pinMode2(pin, Pins[pin].flags);

  //2. attempt to restore peripherial connections:
  //   If pin was not configured or was simple GPIO function then restore it to simple GPIO
  //   If pin had a connection through GPIO Matrix, restore IN & OUT signals connection (use same
  //   signal number for both IN and OUT. Probably it should be fixed)
  //
  if (Pins[pin].fun_sel != PIN_FUNC_GPIO)
    q_printf("%% Pin %d IO MUX connection can not be restored\r\n", pin);
  else {
    if (Pins[pin].bus_type == ESP32_BUS_TYPE_INIT || Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO) {
      gpio_pad_select_gpio(pin);

      // restore digital value
      if ((Pins[pin].flags & OUTPUT_ONLY) && (Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO))
        digitalForceWrite(pin, Pins[pin].value ? HIGH : LOW);
    } else {
      // unfortunately this will not work with Arduino :(
      if (Pins[pin].flags & OUTPUT)
        gpio_matrix_out(pin, Pins[pin].sig_out, false, false);
      if (Pins[pin].flags & INPUT)
        gpio_matrix_in(pin, Pins[pin].sig_out, false);
    }
  }
}

// Input-Only pins as per Tech Ref. Seems like only original
// ESP32 has these while newer models have all GPIO capable
// of Input & Output
//
static bool pin_is_input_only_pin(int pin) {
  return !GPIO_IS_VALID_OUTPUT_GPIO(pin);
}


// strapping pins as per Technical Reference
//
static bool pin_is_strapping_pin(int pin) {
  switch (pin) {
#ifdef CONFIG_IDF_TARGET_ESP32
    case 0:
    case 2:
    case 5:
    case 12:
    case 15: return true;
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    case 0:
    case 45:
    case 46: return true;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    case 0:
    case 3:
    case 45:
    case 46: return true;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    case 2:
    case 8:
    case 9: return true;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    case 8:
    case 9:
    case 12:
    case 14:
    case 15: return true;
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
    case 8:
    case 9:
    case 25: return true;
#else
#warning "Unsupported target, pin_is_strapping_pin() is disabled"
#endif
    default: return false;
  }
}



// "pin X"
// Display pin information: function, direction, mode, pullup/pulldown etc
//
static int pin_show(int argc, char **argv) {

  unsigned int pin, informed = 0;

  if (argc < 2) return -1;
  if (!pin_exist((pin = q_atol(argv[1], 999)))) return 1;

  bool pu, pd, ie, oe, od, sleep_sel, res;
  uint32_t drv, fun_sel, sig_out;
  peripheral_bus_type_t type;

  res = esp_gpio_is_pin_reserved(pin);
  q_printf("%% Pin %u (GPIO%u) is ", pin, pin);

  if (res)
    q_print("<w>**RESERVED**</>, ");

  if (pin_is_strapping_pin(pin))
    q_print("strapping pin, ");

  if (pin_is_input_only_pin(pin))
    q_print("<i>**INPUT-ONLY**</>, ");

  if (!res)
    q_print("available, ");

  q_print("and is ");
  if ((type = perimanGetPinBusType(pin)) == ESP32_BUS_TYPE_INIT)
    q_print("not used by Arduino Core\r\n");
  else {

    if (type == ESP32_BUS_TYPE_GPIO)
      q_print("<i>configured as GPIO</>\r\n");
    else
      q_printf("used as \"<i>%s</>\"\r\n", perimanGetTypeName(type));
  }

  gpio_ll_get_io_config(&GPIO, pin, &pu, &pd, &ie, &oe, &od, &drv, &fun_sel, &sig_out, &sleep_sel);

  if (ie || oe || od || pu || pd || sleep_sel) {
    q_print("% Mode:<i> ");

    if (ie) q_print("INPUT, ");
    if (oe) q_print("OUTPUT, ");
    if (pu) q_print("PULL_UP, ");
    if (pd) q_print("PULL_DOWN, ");
    if (od) q_print("OPEN_DRAIN, ");
    if (sleep_sel) q_print("sleep mode selected,");
    if (!pu && !pd && ie) q_print(" input is floating");

    q_print("</>\r\n");

    if (oe && fun_sel == PIN_FUNC_GPIO) {
      q_print("% Output via GPIO matrix, ");
      if (sig_out == SIG_GPIO_OUT_IDX)
        q_print("simple GPIO output\r\n");
      else
        q_printf("provides path for signal ID: %lu\r\n", sig_out);
    } else if (oe && fun_sel != PIN_FUNC_GPIO)
      q_printf("%% Output is done via IO MUX, (function: <i>%s</>)\r\n", io_mux_func_name[pin][fun_sel]);

    if (ie && fun_sel == PIN_FUNC_GPIO) {
      q_print("% Input via GPIO matrix, ");
      for (int i = 0; i < SIG_GPIO_OUT_IDX; i++) {
        if (gpio_ll_get_in_signal_connected_io(&GPIO, i) == pin) {
          if (!informed)
            q_print("provides path for signal IDs: ");
          informed++;
          q_printf("%d, ", i);
        }
      }

      if (!informed)
        q_print("simple GPIO input");
      q_print(CRLF);

    } else if (ie)
      q_printf("%% Input is done via IO MUX, (function: <i>%s</>)\r\n", io_mux_func_name[pin][fun_sel]);
  }


  // ESP32S3 has its pin 18 and 19 drive capability of 3 but the meaning is 2 and vice-versa
  // TODO:Other versions probably have the same behaviour on some other pins. Check TechRefs
#ifdef CONFIG_IDF_TAGET_ESP32S3
  if (pin == 18 || pin == 19) {
    if (drv == 2) drv == 3 else if (drv == 3) drv == 2;
  }
#endif
  drv = !drv ? 5 : (drv == 1 ? 10 : (drv == 2 ? 20 : 40));
  q_print("% Maximum current is ");
#if WITH_COLOR
  if (drv > 20)
    q_print("<w>");
  else if (drv < 20)
    q_printf("<i>");
#endif
  q_printf("%u", (unsigned int)drv);
  q_print("</> milliamps\r\n");

  // enable INPUT if was not enabled before
  //
  // As of Arduino Core 3.0.5 digitalRead() does not work following cases:
  // 1. pin is interface pin (uart_tx as example),
  // 2. pin is not configured through PeriMan as "simple GPIO"
  // thats why IDF functions are used instead of digitalRead() and pinMode()
  if (!ie)
    gpio_ll_input_enable(&GPIO, pin);
  int val = gpio_ll_get_level(&GPIO, pin);
  if (!ie)
    gpio_ll_input_disable(&GPIO, pin);

  q_printf("%% Digital pin value is <i>%s</>\r\n", val ? "HIGH (1)" : "LOW (0)");
  return 0;
}


// "pin NUM arg1 arg2 .. argn"
// "pin NUM"
// Big fat "pin" command. Processes multiple arguments
// TODO: should I split into bunch of smaller functions?
static int cmd_pin(int argc, char **argv) {

  unsigned int flags = 0;
  unsigned int i = 2, pin;
  bool informed = false;

  // repeat whole "pin ..." command "count" times.
  // this number can be changed by "loop" keyword
  unsigned int count = 1;

  if (argc < 2) return -1;  //missing argument

  //first argument must be a decimal number: a GPIO number
  if (!pin_exist((pin = q_atol(argv[1], 999))))
    return 1;

  //"pin X" command is executed here
  if (argc == 2) return pin_show(argc, argv);

  //"pin arg1 arg2 .. argN"
  do {

    //Run through "pin NUM arg1, arg2 ... argN" arguments, looking for keywords
    // to execute.
    while (i < argc) {

      //1. "seq NUM" keyword found:
      if (!q_strcmp(argv[i], "sequence")) {
        if ((i + 1) >= argc) {
          HELP(q_printf("%% <e>Sequence number expected after \"%s\"</>\r\n", argv[i]));
          return i;
        }
        i++;

        int seq, j;

        // enable RMT sequence 'seq' on pin 'pin'
        if (seq_isready((seq = q_atol(argv[i], DEF_BAD)))) {
          HELP(q_printf("%% Sending sequence %u over GPIO %u\r\n", seq, pin));
          if ((j = seq_send(pin, seq)) < 0)
            q_printf("%% <e>Failed. Error code is: %d</>\r\n", j);

        } else
          q_printf("%% <e>Sequence %u is not configured</>\r\n", seq);
      } else
      //2. "pwm FREQ DUTY" keyword.
      // unlike global "pwm" command the duty and frequency are not an optional
      // parameter anymore. Both can be 0 which used to disable previous "pwm"
      if (!q_strcmp(argv[i], "pwm")) {

        unsigned int freq;
        float duty;
        // make sure that there are 2 extra arguments after "pwm" keyword
        if ((i + 2) >= argc) {

          HELP(q_print("% <e>Frequency and duty cycle are both expected</>\r\n"));

          return i;
        }
        i++;

        // frequency must be an integer number and duty must be a float point number
        if ((freq = q_atol(argv[i++], MAGIC_FREQ + 1)) > MAGIC_FREQ) {
          HELP(q_print("% <e>Frequency must be in range [1.." xstr(MAGIC_FREQ) "] Hz</>\r\n"));
          return i - 1;
        }

        duty = q_atof(argv[i], -1.0f);
        if (duty < 0 || duty > 1) {
          HELP(q_print("% <e>Duty cycle is a number in range [0..1] (0.01 means 1% duty)</>\r\n"));
          return i;
        }

        // enable/disable tone on given pin. if freq is 0 then tone is
        // disabled
        if (pwm_enable(pin, freq, duty) < 0) {
          HELP(q_print(Failed));
          return 0;
        }
      } else
        //3. "delay X" keyword
        //creates delay for X milliseconds.
        if (!q_strcmp(argv[i], "delay")) {
          int duration;
          if ((i + 1) >= argc) {
            HELP(q_print("% <e>Delay value expected after keyword \"delay\"</>\r\n"));
            return i;
          }
          i++;
          if ((duration = q_atol(argv[i], -1)) < 0)
            return i;

          // Display a hint for the first time when delay is longer than 5 seconds
          if (!informed && (duration > TOO_LONG)) {
            informed = true;
            if (is_foreground_task())
              HELP(q_print("% <3>Hint: Press <Enter> to interrupt the command</>\r\n"));
          }

          // Was interrupted by keypress or by "kill" command? Abort whole command.
          if (delay_interruptible(duration) != duration) {
            HELP(q_printf("%% Command \"%s\" has been interrupted\r\n", argv[0]));
            return 0;
          }
        } else
        //Now all the single-line keywords:
        // 5. "pin X save"
        if (!q_strcmp(argv[i], "save")) pin_save(pin); else
          // 9. "pin X up"
        if (!q_strcmp(argv[i], "up")) {
          flags |= PULLUP;
          pinMode2(pin, flags);
        } else  // set flags immediately as we read them
        // 10. "pin X down"
        if (!q_strcmp(argv[i], "down")) {
          flags |= PULLDOWN;
          pinMode2(pin, flags);
        } else
        // 12. "pin X in"
        if (!q_strcmp(argv[i], "in")) {
          flags |= INPUT;
          pinMode2(pin, flags);
        } else
        // 13. "pin X out"
        if (!q_strcmp(argv[i], "out")) {
          flags |= OUTPUT_ONLY;
          pinMode2(pin, flags);
        } else
        // 11. "pin X open"
        if (!q_strcmp(argv[i], "open")) {
          flags |= OPEN_DRAIN;
          pinMode2(pin, flags);
        } else
        // 14. "pin X low" keyword. only applies to I/O pins, fails for input-only pins
        if (!q_strcmp(argv[i], "low")) {
          if (pin_is_input_only_pin(pin)) {
abort_if_input_only:
            q_printf("%% <e>Pin %u is **INPUT-ONLY**, can not be set \"%s</>\"\r\n", pin, argv[i]);
            return i;
          }
          // use pinMode2/digitalForceWrite to not let the pin to be reconfigured
          // to GPIO Matrix pin. By default many GPIO pins are handled by IOMUX. However if
          // one starts to use that pin it gets configured as "GPIO Matrix simple GPIO". Code below
          // keeps the pin at IOMUX, not switching to GPIO Matrix
          flags |= OUTPUT;
          pinMode2(pin, flags);
          digitalForceWrite(pin, LOW);
        } else
        // 15. "pin X high" keyword. I/O pins only
        if (!q_strcmp(argv[i], "high")) {

          if (pin_is_input_only_pin(pin))
            goto abort_if_input_only;

          flags |= OUTPUT;
          pinMode2(pin, flags);
          digitalForceWrite(pin, HIGH);
        } else
        // 16. "pin X read"
        if (!q_strcmp(argv[i], "read")) q_printf("%% GPIO%d : logic %d\r\n", pin, digitalForceRead(pin)); else
        // 17. "pin X read"
        if (!q_strcmp(argv[i], "aread")) q_printf("%% GPIO%d : analog %d\r\n", pin, analogRead(pin)); else
        // 7. "pin X hold"
        if (!q_strcmp(argv[i], "hold")) gpio_hold_en((gpio_num_t)pin); else
        // 8. "pin X release"
        if (!q_strcmp(argv[i], "release")) gpio_hold_dis((gpio_num_t)pin); else
        // 6. "pin X load"
        if (!q_strcmp(argv[i], "load")) pin_load(pin); else
        //4. "loop" keyword
        if (!q_strcmp(argv[i], "loop")) {
          //must have an extra argument (loop count)
          if ((i + 1) >= argc) {
            HELP(q_print("% <e>Loop count expected after keyword \"loop\"</>\r\n"));
            return i;
          }
          i++;

          // loop must be the last keyword, so we can strip it later
          if ((i + 1) < argc) {
            HELP(q_print("% <e>\"loop\" must be the last keyword</>\r\n"));
            return i + 1;
          }

          //read loop count and strip two last keywords off
          if ((count = q_atol(argv[i], 0)) == 0)
            return i;
          argc -= 2;  

          if (!informed) {
            informed = true;
            HELP(q_printf("%% Repeating %u times", count));
            if (is_foreground_task())
              HELP(q_print(", press <Enter> to abort"));
            HELP(q_print(CRLF));
          }

        } else
        //"X" keyword. when we see a number we use it as a pin number
        //for subsequent keywords. must be valid GPIO number.
        if (isnum(argv[i])) {
          if (!pin_exist((pin = q_atol(argv[i], 999))))
            return i;
        } else
          // argument i was not recognized
          return i;
      i++;
    }       //big fat "while (i < argc)"
    i = 1;  // start over again

    //give a chance to cancel whole command
    // by anykey press
    if (anykey_pressed()) {
      HELP(q_print("% Key pressed, aborting..\r\n"));
      break;
    }
  } while (--count > 0);  // repeat if "loop X" command was found
  return 0;
}
#endif // #if COMPILING_ESPSHELL
