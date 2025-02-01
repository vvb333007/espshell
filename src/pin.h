/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


// -- pin (GPIO) manipulation --
// Pin is the synonim of GPIO throughout the espshell code & docs. There is no support for pin remapping simply because
// I don't have an appropriate board
//
// Main command here is "pin"
//
// Command "pin" is a tiny command processor itself: one can enter multiple arguments
// to the "pin" command to form a sort of a microcode program which gets executed.

#if COMPILING_ESPSHELL

// Structure which is used to save/load pin states by "pin X save"/"pin X load".
// These are filled by pin_save() and applied by pin_load()
static struct {

  uint8_t flags;     // INPUT,PULLUP,... Arduino pin flags (as per pinMode() )
  bool value;        // digital value
  uint16_t sig_out;  // SIG_IN & SIG_OUT for GPIO Matrix mode
  uint16_t fun_sel;  // IO_MUX function selector
  int bus_type;      // PeriMan bus type. (see ArdionoCore *periman*.c)

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


// Number of available function in IO_MUX for given pin
#define IOMUX_NFUNC 5

#ifdef CONFIG_IDF_TARGET_ESP32

// Classic ESP32 has 6 functions per pin. Function 0 selects IO_MUX GPIO mode, function 2 selects GPIO_MATRIX GPIO mode
// All other ESP32 variants have only 5 functions with function 1 being GPIO_MATRIX selector
# undef IOMUX_NFUNC
# define IOMUX_NFUNC 6
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][IOMUX_NFUNC] = {
  { "0", "CLK_OUT1", "0", 0, 0, "EMAC_TX_CLK" },
  { "U0TXD", "CLK_OUT3", "1", 0, 0, "EMAC_RXD2" },
  { "2", "HSPIWP", "2", "HS2_DATA0", "SD_DATA0", 0 },
  { "U0RXD", "CLK_OUT2", "3", 0, 0, 0 },
  { "4", "HSPIHD", "4", "HS2_DATA1", "SD_DATA1", "EMAC_TX_ER" },
  { "5", "VSPICS0", "5", "HS1_DATA6", 0, "EMAC_RX_CLK" },
  { "SD_CLK", "SPICLK", "6", "HS1_CLK", "U1CTS", 0 },
  { "SD_DATA0", "SPIQ", "7", "HS1_DATA0", "U2RTS", 0 },
  { "SD_DATA1", "SPID", "8", "HS1_DATA1", "U2CTS", 0 },
  { "SD_DATA2", "SPIHD", "9", "HS1_DATA2", "U1RXD", 0 },
  { "SD_DATA3", "SPIWP", "10", "HS1_DATA3", "U1TXD", 0 },
  { "SD_CMD", "SPICS0", "11", "HS1_CMD", "U1RTS", 0 },
  { "MTDI", "HSPIQ", "12", "HS2_DATA2", "SD_DATA2", "EMAC_TXD3" },
  { "MTCK", "HSPID", "13", "HS2_DATA3", "SD_DATA3", "EMAC_RX_ER" },
  { "MTMS", "HSPICLK", "14", "HS2_CLK", "SD_CLK", "EMAC_TXD2" },
  { "MTDO", "HSPICS0", "15", "HS2_CMD", "SD_CMD", "EMAC_RXD3" },
  { "16", 0, "16", "HS1_DATA4", "U2RXD", "EMAC_CLK_OUT" },
  { "17", 0, "17", "HS1_DATA5", "U2TXD", "EMAC_CLK_180" },
  { "18", "VSPICLK", "18", "HS1_DATA7", 0, 0 },
  { "19", "VSPIQ", "19", "U0CTS", 0, "EMAC_TXD0" },
  { "20", "20", "20", "20", "20", "20" },
  { "21", "VSPIHD", "21", 0, 0, "EMAC_TX_EN" },
  { "22", "VSPIWP", "22", "U0RTS", 0, "EMAC_TXD1" },
  { "23", "VSPID", "23", "HS1_STROBE", 0, 0 },
  { "24", "24(1)", "24(2)", "24(3)", "24(4)", "24(5)" },
  { "25", 0, "25", 0, 0, "EMAC_RXD0" },
  { "26", 0, "26", 0, 0, "EMAC_RXD1" },
  { "27", 0, "27", 0, 0, "EMAC_RX_DV" },
  { 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0 },
  { "32", 0, "32", 0, 0, 0 },
  { "33", 0, "33", 0, 0, 0 },
  { "34", 0, "34", 0, 0, 0 },
  { "35", 0, "35", 0, 0, 0 },
  { "36", 0, "36", 0, 0, 0 },
  { "37", 0, "37", 0, 0, 0 },
  { "38", 0, "38", 0, 0, 0 },
  { "39", 0, "39", 0, 0, 0 },
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][IOMUX_NFUNC] = {
  // ESP32S3 MUX functions for pins. S3 has only 5 functions per pin 
  { "0", "0", 0, 0, 0 },
  { "1", "1", 0, 0, 0 },
  { "2", "2", 0, 0, 0 },
  { "3", "3", 0, 0, 0 },
  { "4", "4", 0, 0, 0 },
  { "5", "5", 0, 0, 0 },
  { "6", "6", 0, 0, 0 },
  { "7", "7", 0, 0, 0 },
  { "8", "8", 0, "SUBSPICS1", 0 },
  { "9", "9", 0, "SUBSPIHD", "FSPIHD" },
  { "10", "10", "FSPIIO4", "SUBSPICS0", "FSPICS0" },
  { "11", "11", "FSPIIO5", "SUBSPID", "FSPID" },
  { "12", "12", "FSPIIO6", "SUBSPICLK", "FSPICLK" },
  { "13", "13", "FSPIIO7", "SUBSPIQ", "FSPIQ" },
  { "14", "14", "FSPIDQS", "SUBSPIWP", "FSPIWP" },
  { "15", "15", "U0RTS", 0, 0 },
  { "16", "16", "U0CTS", 0, 0 },
  { "17", "17", "U1TXD", 0, 0 },
  { "18", "18", "U1RXD", "CLK_OUT3", 0 },
  { "19", "19", "U1RTS", "CLK_OUT2", 0 },
  { "20", "20", "U1CTS", "CLK_OUT1", 0 },
  { "21", "21", 0, 0, 0 },
  { 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0 },
  { "SPICS1", "26", 0, 0, 0 },
  { "SPIHD", "27", 0, 0, 0 },
  { "SPIWP", "28", 0, 0, 0 },
  { "SPICS0", "29", 0, 0, 0 },
  { "SPICLK", "30", 0, 0, 0 },
  { "SPIQ", "31", 0, 0, 0 },
  { "SPID", "32", 0, 0, 0 },
  { "33", "33", "FSPIHD", "SUBSPIHD", "SPIIO4" },
  { "34", "34", "FSPICS0", "SUBSPICS0", "SPIIO5" },
  { "35", "35", "FSPID", "SUBSPID", "SPIIO6" },
  { "36", "36", "FSPICLK", "SUBSPICLK", "SPIIO7" },
  { "37", "37", "FSPIQ", "SUBSPIQ", "SPIDQS" },
  { "38", "38", "FSPIWP", "SUBSPIWP", 0 },
  { "MTCK", "39", "CLK_OUT3", "SUBSPICS1", 0 },
  { "MTDO", "40", "CLK_OUT2", 0, 0 },
  { "MTDI", "41", "CLK_OUT1", 0, 0 },
  { "MTMS", "42", 0, 0, 0 },
  { "U0TXD", "43", "CLK_OUT1", 0, 0 },
  { "U0RXD", "44", "CLK_OUT2", 0, 0 },
  { "45", "45", 0, 0, 0 },
  { "46", "46", 0, 0, 0 },
  { "SPIC_PDIF", "47", "SSPICPDIF", 0, 0 },
  { "SPIC_NDIF", "48", "SSPICNDIF", 0, 0 },
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][IOMUX_NFUNC] = {
  // ESP32S2 MUX functions for pins
  { "0", "0", 0, 0, 0 },
  { "1", "1", 0, 0, 0 },
  { "2", "2", 0, 0, 0 },
  { "3", "3", 0, 0, 0 },
  { "4", "4", 0, 0, 0 },
  { "5", "5", 0, 0, 0 },
  { "6", "6", 0, 0, 0 },
  { "7", "7", 0, 0, 0 },
  { "8", "8", 0, "SUBSPICS1", 0 },
  { "9", "9", 0, "SUBSPIHD", "FSPIHD" },
  { "10", "10", "FSPIIO4", "SUBSPICS0", "FSPICS0" },
  { "11", "11", "FSPIIO5", "SUBSPID", "FSPID" },
  { "12", "12", "FSPIIO6", "SUBSPICLK", "FSPICLK" },
  { "13", "13", "FSPIIO7", "SUBSPIQ", "FSPIQ", "" },
  { "14", "14", "FSPIDQS", "SUBSPIWP", "FSPIWP" },
  { "XTAL_32K_P", "15", "U0RTS", 0, 0 },
  { "XTAL_32K_N", "16", "U0CTS", 0, 0 },
  { "DAC_1", "17", "U1TXD", 0, 0 },
  { "DAC_2", "18", "U1RXD", "CLK_OUT3", 0 },
  { "19", "19", "U1RTS", "CLK_OUT2", 0 },
  { "20", "20", "U1CTS", "CLK_OUT1", 0 },
  { "21", "21", 0, 0, 0 },
  { 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0 },
  { "SPICS1", "26", 0, 0, 0 },
  { "SPIHD", "27", 0, 0, 0 },
  { "SPIWP", "28", 0, 0, 0 },
  { "SPICS0", "29", 0, 0, 0 },
  { "SPICLK", "30", 0, 0, 0 },
  { "SPIQ", "31", 0, 0, 0 },
  { "SPID", "32", 0, 0, 0 },
  { "33", "33", "FSPIHD", "SUBSPIHD", "SPIIO4" },
  { "34", "34", "FSPICS0", "SUBSPICS0", "SPIIO5" },
  { "35", "35", "FSPID", "SUBSPID", "SPIIO6" },
  { "36", "36", "FSPICLK", "SUBSPICLK", "SPIIO7" },
  { "37", "37", "FSPIQ", "SUBSPIQ", "SPIDQS" },
  { "38", "38", "FSPIWP", "SUBSPIWP", 0 },
  { "MTCK", "39", "CLK_OUT3", "SUBSPICS1", 0 },
  { "MTDO", "40", "CLK_OUT2", 0, 0 },
  { "MTDI", "41", "CLK_OUT1", 0, 0 },
  { "MTMS", "42", 0, 0, 0 },
  { "U0TXD", "43", "CLK_OUT1", 0, 0 },
  { "U0RXD", "44", "CLK_OUT2", 0, 0 },
  { "45", "45", 0, 0, 0 },
  { "46", "46", 0, 0, 0 },
#else
#  warning "Unsupported target, using dummy IO_MUX function name table"
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][IOMUX_NFUNC] = { 0 
#endif  // CONFIG_IDF_TARGET...
};

// WARNING! Not reentrat, use with caution
static const char *iomux_funame(unsigned char pin, unsigned char func) {
  static char gpio[8] = {'G','P','I','O',0};
  // lisp style on
  return  func < IOMUX_NFUNC && 
          pin < SOC_GPIO_PIN_COUNT && 
          io_mux_func_name[pin][func] ? ( io_mux_func_name[pin][func][0] >= '0' && 
                                          io_mux_func_name[pin][func][0] <= '9' ? (strcpy(&gpio[4], io_mux_func_name[pin][func]), gpio) 
                                                                                : io_mux_func_name[pin][func])
                                      : " -undef- ";
}

// Display full table: all pins and every function available for every pin.
// Function which is currently selected for the pin is displayed in reverse colors plus "*" symbol is
// displayed after function name
//
static int pin_show_mux_functions() {
  unsigned char pin,i;

  bool pd, pu, ie, oe, od, slp_sel;
  uint32_t drv, fun_sel, sig_out;

  HELP(q_print( "% IO MUX has <i>" xstr(IOMUX_NFUNC) "</> functions for every pin. The mapping is as follows:\r\n"));

  // Table header Save space.
  q_print("% Pin ");
  for (i = 0; i < IOMUX_NFUNC; i++)
     q_printf("| Function<i>%d</> ",i);
  q_print("\r\n%-----");
  for (i = 0; i < IOMUX_NFUNC; i++)
     q_print("+-----------");
  q_print(CRLF);

  // run through all the pins
  for (pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++) {
    
    if (io_mux_func_name[pin][0]) { // can't use pin_exist() here : it is not silent 
      q_printf( "%%  %02u ",pin);

      // get pin IO_MUX function currently selected
      gpio_ll_get_io_config(&GPIO, pin, &pu, &pd, &ie, &oe, &od, &drv, &fun_sel, &sig_out, &slp_sel);

      // For each pin, run through all its functions. 
      // Highligh function that is currently assigned to the pin (via per/post tags)
      for (int i = 0; i < IOMUX_NFUNC; i++) {
        const char *pre = (i == fun_sel) ? "<r>" : "";    // gcc must fold two comparisions into one
        const char *post = (i == fun_sel) ? "*</>" : " ";
#pragma GCC diagnostic ignored "-Wformat"          
        q_printf("|%s % 9s%s", pre, iomux_funame(pin, i), post);
#pragma GCC diagnostic warning "-Wformat"                
      }
      q_print(CRLF);
    }
  }
  HELP(q_print( "\r\n"
                "% NOTE 1: To select GPIO matrix use function #" xstr(PIN_FUNC_GPIO) " or\r\n"
                "%         use \"pin X matrix\" command where \"X\" is the pin number\r\n"
                "% NOTE 2: Function, that is currently assigned to the pin is marked with \"*\"\r\n"));
  return 0;
}


// Virtual IO_MUX function #255, which has nothing to do with IO_MUX. Instead it calls gpio_pad_select_gpio()
#define PIN_FUNC_PAD_SELECT_GPIO (unsigned char)(-1) 

// Set IO_MUX / GPIO Matrix function for the pin
// /pin/       - pin number
// /function/  - IO_MUX function code (in range [0..IOMUX_NFUNC) ). Code 0 is usually "GPIO via IO_MUX"
//               while function #1 is the "GPIO via GPIO_Matrix" (with one exception: original ESP32 uses function#2
//               for that)
static bool pin_set_iomux_function(unsigned char pin, unsigned char function) {

  // Special case for a non-existent function 0xff: execute IDF's gpio_pad_select_gpio and return
  if (function == PIN_FUNC_PAD_SELECT_GPIO) {
    gpio_pad_select_gpio(pin);
    return true;
  }
  
  // Sanity check for arguments
  if (function >= IOMUX_NFUNC) {
    HELP(q_printf("%% <e>Valid function numbers are [0 .. %d]</>\r\n",IOMUX_NFUNC - 1));
    return false;
  }

  // Set new IO_MUX function for the pin
  gpio_ll_func_sel(&GPIO, pin, function);
  return true;
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
void pinForceMode(unsigned int pin, unsigned int flags) {

  // Set Arduino flags to the pin using ESP-IDF functions
  // NOTE: do not replace "(flags & MACRO) == MACRO" with "flags & MACRO": Arduino flags can have more than
  //       1 bit set

  if ((flags & PULLUP) == PULLUP)         gpio_ll_pullup_en(&GPIO, pin);    else gpio_ll_pullup_dis(&GPIO, pin);
  if ((flags & PULLDOWN) == PULLDOWN)     gpio_ll_pulldown_en(&GPIO, pin);  else gpio_ll_pulldown_dis(&GPIO, pin);
  if ((flags & OPEN_DRAIN) == OPEN_DRAIN) gpio_ll_od_enable(&GPIO, pin);    else gpio_ll_od_disable(&GPIO, pin);
  if ((flags & INPUT) == INPUT)           gpio_ll_input_enable(&GPIO, pin); else gpio_ll_input_disable(&GPIO, pin);

  // OUTPUT_ONLY is a "true" OUTPUT flag (see macro definition above)
  // workaround for Arduino Core's workaround :)

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
  if (oe) Pins[pin].flags |= OUTPUT; // TODO: OUTPUT_ONLY ??
  if (od) Pins[pin].flags |= OPEN_DRAIN;
}

// Load pin state from Pins[] array
// Attempt is made to restore GPIO Matrix connections however it is not working as intended
//
static void pin_load(int pin) {

  // 1. restore pin mode
  pinForceMode(pin, Pins[pin].flags);

  // TODO: rewrite code below

  //2. attempt to restore peripherial connections:
  //   If pin was not configured or was simple GPIO function then restore it to simple GPIO
  //   If pin had a connection through GPIO Matrix, restore IN & OUT signals connection
  if (Pins[pin].fun_sel != PIN_FUNC_GPIO) {
    pin_set_iomux_function(pin,Pins[pin].fun_sel);
  }
  else {
    if (Pins[pin].bus_type == ESP32_BUS_TYPE_INIT || Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO) {
      gpio_pad_select_gpio(pin);

      // restore digital value
      if ((Pins[pin].flags & OUTPUT_ONLY) && (Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO))
        digitalForceWrite(pin, Pins[pin].value ? HIGH : LOW);
    } else {
      // unfortunately this will not work with Arduino :(
      // TODO: remove. Once lost, matrix connections can not be properly restored because of Periman's deinit, which uninstall drivers
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

    // Output
    if (oe) {
      q_print("% Output is done via <*>");
      if (fun_sel == PIN_FUNC_GPIO) {
        q_print("GPIO Matrix</>, ");
        if (sig_out == SIG_GPIO_OUT_IDX)
          q_print("acts as simple GPIO output (SIG_GPIO_OUT_IDX)\r\n");
        else
          q_printf("provides path for signal ID: %lu\r\n", sig_out);
      } else
        q_printf("IO MUX</>, (function: <i>%s</>)\r\n", iomux_funame(pin,fun_sel));
    } else
      q_print("% Output is disabled\r\n");

    // Input
    if (ie) {
      q_print("% Input is done via <*>");
      if (fun_sel == PIN_FUNC_GPIO) {
        q_print("GPIO Matrix</>, ");
        for (int i = 0; i < SIG_GPIO_OUT_IDX; i++)
          if (gpio_ll_get_in_signal_connected_io(&GPIO, i) == pin) {
            if (!informed)
              q_print("connected signal IDs: ");
            informed++;
            q_printf("%d, ", i);
          }
        if (!informed)
          q_print("acts as simple GPIO input");
        q_print(CRLF);
      } else
        q_printf("IO MUX</>, (function: <i>%s</>)\r\n", iomux_funame(pin,fun_sel));
    } else
      q_print("% Input is disabled\r\n");
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
          pinForceMode(pin, flags);
        } else  // set flags immediately as we read them
        // 10. "pin X down"
        if (!q_strcmp(argv[i], "down")) {
          flags |= PULLDOWN;
          pinForceMode(pin, flags);
        } else
        // 12. "pin X in"
        if (!q_strcmp(argv[i], "in")) {
          flags |= INPUT;
          pinForceMode(pin, flags);
        } else
        // 13. "pin X out"
        if (!q_strcmp(argv[i], "out")) {
          flags |= OUTPUT_ONLY;
          pinForceMode(pin, flags);
        } else
        // 11. "pin X open"
        if (!q_strcmp(argv[i], "open")) {
          flags |= OPEN_DRAIN;
          pinForceMode(pin, flags);
        } else
        // 14. "pin X low" keyword. only applies to I/O pins, fails for input-only pins
        if (!q_strcmp(argv[i], "low")) {
          if (pin_is_input_only_pin(pin)) {
abort_if_input_only:
            q_printf("%% <e>Pin %u is **INPUT-ONLY**, can not be set \"%s</>\"\r\n", pin, argv[i]);
            return i;
          }
          // use pinForceMode/digitalForceWrite to not let the pin to be reconfigured
          // to GPIO Matrix pin. By default many GPIO pins are handled by IOMUX. However if
          // one starts to use that pin it gets configured as "GPIO Matrix simple GPIO". Code below
          // keeps the pin at IOMUX, not switching to GPIO Matrix
          flags |= OUTPUT;
          pinForceMode(pin, flags);
          digitalForceWrite(pin, LOW);
        } else
        // 15. "pin X high" keyword. I/O pins only
        if (!q_strcmp(argv[i], "high")) {

          if (pin_is_input_only_pin(pin))
            goto abort_if_input_only;

          flags |= OUTPUT;
          pinForceMode(pin, flags);
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
        // 6.1 "pin X iomux [NUMBER | gpio]"
        if (!q_strcmp(argv[i], "iomux")) {
          unsigned char function = 0; // default is IO_MUX function 0 which is, in most cases, a GPIO function via IO_MUX
          if ((i+1) < argc)
            // if we have extra arguments, then treat number as IO_MUX function, treat text as special case. 
            function = q_atol(argv[++i],PIN_FUNC_PAD_SELECT_GPIO); 
          pin_set_iomux_function(pin, function);
        } else
        // 6.2 "pin X matrix [in|out NUMBER]"
        if (!q_strcmp(argv[i], "matrix")) {
          // set pin function to "Simple GPIO via GPIO Matrix"
          pin_set_iomux_function(pin, PIN_FUNC_GPIO);

          // Is there any signal IDs provided? 
          if (i + 2 < argc) {
//            if (!isnum(argv[i + 2])
//              return i + 2;
            unsigned int sig_id = q_atol(argv[i + 2],SIG_GPIO_OUT_IDX);
            if (argv[i + 1][0] == 'i')
              gpio_matrix_in(pin, sig_id, false);
            else
              gpio_matrix_out(pin, sig_id, false, false);
            i += 2;
          }
        } else
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
            HELP(q_printf("%% Repeating whole command %u times", count));
            if (is_foreground_task())
              HELP(q_print(", press <Enter> to abort"));
            HELP(q_print(CRLF));
          }

        } else
        //A keyword which is a number. when we see a number we use it as a pin number
        //for subsequent keywords. Must be valid GPIO number.
        if (isnum(argv[i])) {
          if (!pin_exist((pin = q_atol(argv[i], DEF_BAD))))
            return i;
        } else
          // argument i was not recognized
          return i;
      // go to the next keyword
      i++;
    }       //big fat "while (i < argc)"
    i = 1;  // prepare to start over again

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

