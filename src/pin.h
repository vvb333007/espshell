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


// -- pin (GPIO) manipulation --
// Pin is the synonym of GPIO throughout the espshell code & docs. There is no support for pin remapping simply because
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
// Values like "0", "15" means GPIO function. E.g. "7" means "GPIO7"
// Values of zero mean this function is undefined/unused
//
//TODO: add support for other Espressif ESP32 variants (namely C3, C6, C61, H2, and P4)
//



// Number of available function in IO_MUX for given pin
// Classic ESP32 has 6 functions per pin. Function 0 selects IO_MUX GPIO mode, function 2 selects GPIO_MATRIX GPIO mode
// All other ESP32 variants have only 5 functions with function 1 being GPIO_MATRIX selector
//
#ifdef CONFIG_IDF_TARGET_ESP32
#  define IOMUX_NFUNC 6
#else
#  define IOMUX_NFUNC 5
#endif

// Each pin can be switched to one of 5 or 6 functions numbered from 0 to 4 (or 5)
// Array entries consist of names of functions available.
// Non-existent pins are those with all zeros {0,0,0,0,0}
//
static const char *io_mux_func_name[SOC_GPIO_PIN_COUNT][IOMUX_NFUNC] = {

#ifdef CONFIG_IDF_TARGET_ESP32

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
  { 0, 0, 0, 0, 0, 0 },
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
  0 // all zeros
#endif  // CONFIG_IDF_TARGET...
}; //iomux function table

// WARNING! Not reentrat, use with caution
// WARNING! Undefined behaviour IF io_mux_func_name[][] is a long number, as a text: "12345". These numbers
//          are pin numbers and are not expected to go beyound 255
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

// Display iomux table: all pins and every function available for every pin.
// Function which is currently selected for the pin is displayed in reverse colors plus "*" symbol is
// displayed after function name
//

static int cmd_show_iomux(UNUSED int argc, UNUSED char **argv) {
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
    
    if (pin_exist_silent(pin)) {
      // add "!" before pin number for RESERVED pins. 
      // Input-only pins are painted green. Didn't checked it touroughly but it looks like only original ESP32
      // has input-only pins; other models (both xtensa and risc-v CPUs) have no such restriction
      char color = 'n', mark = ' ';
      if (pin_is_input_only_pin(pin))
        color = 'g';
      if (esp_gpio_is_pin_reserved(pin)) {
        mark = '!';
        if (color == 'n')
          color = 'w';
      }
      q_printf( "%% %c<%c>%02u</> ",mark,color,pin);

      // get pin IO_MUX function currently selected
      gpio_ll_get_io_config(&GPIO, pin, &pu, &pd, &ie, &oe, &od, &drv, &fun_sel, &sig_out, &slp_sel);

      // For each pin, run through all its functions. 
      // Highligh function that is currently assigned to the pin (via pre/post tags)
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
                "% Legend:\r\n"
                "%   Function, that is currently assigned to the pin is <r>marked with \"*\"</>\r\n"
                "%   Input-only pins are green (ESP32 only)\r\n"
                "%   Pins that are <w>RESERVED</> all marked with \"<b>!</>\", avoid them!\r\n"));
  return 0;
}


// Virtual IO_MUX function #255, which has nothing to do with IO_MUX. Instead it calls gpio_pad_select_gpio()
#define PIN_FUNC_PAD_SELECT_GPIO (unsigned char)(-1) 

// Set IO_MUX / GPIO Matrix function for the pin
// /pin/       - pin number
// /function/  - IO_MUX function code (in range [0..IOMUX_NFUNC) ). Code 0 is usually "GPIO via IO_MUX"
//               while function #1 is the "GPIO via GPIO_Matrix" (with one exception: original ESP32 uses function#2
//               for that); can be PIN_FUNC_PAD_SELECT_GPIO
//               
static bool pin_set_iomux_function(unsigned char pin, unsigned char function) {

  // Special case for a non-existent function 0xff: reset pin, execute IDF's gpio_pad_select_gpio and return
  if (function == PIN_FUNC_PAD_SELECT_GPIO) {
    gpio_reset_pin(pin);
    gpio_pad_select_gpio(pin);
    VERBOSE(q_print("GPIO pad reset, select_gpio\r\n"));
    return true;
  }
  
  // Sanity check for arguments
  if (function >= IOMUX_NFUNC) {
    HELP(q_printf("%% <e>Invalid function number! Good ones are these: [0 .. %d]</>\r\n",IOMUX_NFUNC - 1));
    return false;
  }

  // Set new IO_MUX function for the pin
  gpio_ll_func_sel(&GPIO, pin, function);
  return true;
}


// Same as digitalRead() but reads all pins no matter what.
// Public API. It is much faster than digitalRead()
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

// ESP32 Arduino Core as of version 3.0.5 (at the moment of writing) defines pin OUTPUT flag as both INPUT and OUTPUT
// "_ONLY" here a bit misleading here: pin can be INPUT and OUTPUT_ONLY at the same time.
#ifndef OUTPUT_ONLY
#define OUTPUT_ONLY ((OUTPUT) & ~(INPUT))
#endif

// same as pinMode() but calls IDF directly, bypassing
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


// When user attempts to use non-existent pin (i.e. a pin number which is out of range or 
// a pin number which does not exist by design) then this notice is displayed
// TODO: should we precache these q_printf() output at startup?
//
static bool pin_not_exist_notice(unsigned char pin) {
#if WITH_HELP
  unsigned char pin0 = pin;
  
  if (pin >= SOC_GPIO_PIN_COUNT)
    q_printf("%% Valid pin numbers are from <i>0</> to <i>%u</> and \r\n%% ", SOC_GPIO_PIN_COUNT - 1);
  else
    q_print("% Unfortunately ");
  q_printf("following pin(s) do not exist: <i>%u  ", pin);
  
  // TODO: workaround the case where all pins are valid in the mask
  for (pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++)
      if (pin != pin0 && (SOC_GPIO_VALID_GPIO_MASK & ((uint64_t)1 << pin)) == 0)
        q_printf("%u  ", pin);

  // Dump RESERVED pins. ESP32's reserved pins are those used for SPIFLASH and SPIRAM.
  // Defenitely must not be used; However on ESP32-S3 reserved pin is any pin which is used by any driver,
  // for example GPIO 43 is an UART0 pin and it is reserved.
  q_print("</>\r\n"
          "% Reserved by SoC / drivers:<i> ");
  int res = 0;
  for (pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++) {
      if (pin_exist_silent(pin) && esp_gpio_is_pin_reserved(pin)) {
        q_printf("%u  ", pin);
        res++;
      }
  }
  if (!res)
    q_print("none");
  q_print("</>\r\n");

#endif  // WITH_HELP
  return false;
}

// pin number is in range and is a valid GPIO number?
//
static inline bool pin_exist(unsigned char pin) {
  return ((pin < SOC_GPIO_PIN_COUNT) && (((uint64_t)1 << pin) & SOC_GPIO_VALID_GPIO_MASK))  ? true
                                                                                            : pin_not_exist_notice(pin);
}

// Same as above but does not print anything to terminal
static inline bool pin_exist_silent(unsigned char pin) {
  return ((pin < SOC_GPIO_PIN_COUNT) && (((uint64_t)1 << pin) & SOC_GPIO_VALID_GPIO_MASK));
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
  // If fun_sel == PIN_FUNC_GPIO then sig_out is the signal ID
  // to route to the pin via GPIO Matrix.

  Pins[pin].sig_out = sig_out;
  Pins[pin].fun_sel = fun_sel;
  Pins[pin].bus_type = perimanGetPinBusType(pin);

  //save digital value for OUTPUT GPIO
  if (Pins[pin].bus_type == ESP32_BUS_TYPE_GPIO && oe)
    Pins[pin].value = (digitalForceRead(pin) == HIGH);

  Pins[pin].flags = 0;
  if (pu) Pins[pin].flags |= PULLUP;
  if (pd) Pins[pin].flags |= PULLDOWN;
  if (ie) Pins[pin].flags |= INPUT;
  if (oe) Pins[pin].flags |= OUTPUT_ONLY;
  if (od) Pins[pin].flags |= OPEN_DRAIN;
}  

// Load pin state from Pins[] array
// Attempt is made to restore GPIO Matrix connections however it is not working as intended
//
static void pin_load(int pin) {

  // 1. restore pin mode
  pinForceMode(pin, Pins[pin].flags);

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
      // Once lost, matrix connections can not be properly restored because of Periman's deinit, which uninstall drivers
#if 0      
      if (Pins[pin].flags & OUTPUT_ONLY)
        gpio_matrix_out(pin, Pins[pin].sig_out, false, false);
      if (Pins[pin].flags & INPUT)
        gpio_matrix_in(pin, Pins[pin].sig_out, false);
#endif        
    }
  }
}

// Input-Only pins as per Tech Ref. Seems like only original
// ESP32 has these while newer models have all GPIO capable
// of Input & Output
//
static inline bool pin_is_input_only_pin(int pin) {
  return !GPIO_IS_VALID_OUTPUT_GPIO(pin);
}


// strapping pins as per Technical Reference (a 64bit bitmask)
// TODO: add other ESP32 variants
#ifdef CONFIG_IDF_TARGET_ESP32
#  define STRAPPING_PINS 1 | (1 << 2) | (1 << 5) | (1 << 12) | (1 << 15)
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#  define STRAPPING_PINS 1ULL | (1ULL << 45) | (1ULL << 46)
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#  define STRAPPING_PINS 1ULL | (1ULL << 3) | (1ULL << 45) | (1ULL << 46)
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#  define STRAPPING_PINS (1 << 2) | (1 << 8) | (1 << 9)
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#  define STRAPPING_PINS (1 << 8) | (1 << 9) | (1 << 12) | (1 << 14) | (1 << 15)
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
#  define STRAPPING_PINS (1 << 8) | (1 << 9) | (1 << 25)
#else
#  define STRAPPING_PINS 0
#  warning "Unsupported (yet) target, pin_is_strapping_pin() is disabled. Dont hesitate to add support by yourself!"
#endif

// Check if pin is a strapping pin
static inline bool pin_is_strapping_pin(int pin) {
  return ((unsigned long long)( STRAPPING_PINS ) & ((unsigned long long)1 << pin)) != 0;
}



// "pin X"
// Display pin information: function, direction, mode, pullup/pulldown etc
//
static int cmd_show_pin(int argc, char **argv) {

  unsigned int pin, informed = 0;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!pin_exist((pin = q_atol(argv[1], 999))))
    return 1;

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

  q_printf("%savailable, and is ", res ? "un" : "");

  const char *usage = "<o>not used";
  if ((type = perimanGetPinBusType(pin)) != ESP32_BUS_TYPE_INIT) {
    if (type == ESP32_BUS_TYPE_GPIO)
      usage = "<g>configured as GPIO";
    else
      usage = "<o>used as <i>%s";
  }

  q_printf(usage, perimanGetTypeName(type));
  q_print("</> by Arduino Core\r\n");

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
      q_print("% Output is done via <b>");
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
      q_print("% Input is done via <b>");
      // if pin function is set to GPIO_Matrix, fetch and display all the peri ID's that are connected to this pin
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
  
#ifdef CONFIG_IDF_TARGET_ESP32S3
  if (pin == 18 || pin == 19) {
    if (drv == 2)
      drv = 3;
    else if (drv == 3) 
      drv = 2;
  }
#endif
  q_printf("%% Maximum drive current is %u mA\r\n", (unsigned int)(5 * (1 << drv)));
  

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

// -- Mutant command handlers --
// These are never called by a command processor. Instead, they are called from cmd_pin() handler and their purpose 
// is to offload the cmd_pin() which became too big to read/debug and too heavy for the CPU cache so it was split into 
// smaller handlers. These handlers accept two or three extra parameters as compared to "normal" handlers.
//
// Current argument index is advanced by these micro-handlers so main cmd_pin() can process all arguments 
//


// handles "pin X pwm FREQ DUTY"
// Since pin is multiple-argument command we also pass the /start/ index into argv[] array
// 
static int cmd_pin_pwm(int argc, char **argv,unsigned int pin, unsigned int *start) {

  unsigned int i = *start;
  unsigned int freq;
  float duty;
  // make sure that there are 2 extra arguments after "pwm" keyword
  if ((i + 2) >= argc) {
    HELP(q_print("% <e>Frequency and duty cycle: both are expected</>\r\n"));
    return CMD_MISSING_ARG;
  }
  i++;
  freq = q_atol(argv[i++], PWM_MAX_FREQUENCY + 1);
  *start = i;

  // frequency must be an integer number and duty must be a float point number
  if (freq > PWM_MAX_FREQUENCY) {
    HELP(q_print("% <e>Maximum frequency is " xstr(PWM_MAX_FREQUENCY) " Hz</>\r\n"));
    return i - 1;
  }

  duty = q_atof(argv[i], -1.0f);
  if ( duty < 0.0f || duty > 1.0f ) {
    HELP(q_print("% <e>Duty cycle is a number in range [0..1] (0.01 means 1% duty)</>\r\n"));
    return i;
  }

  // enable/disable tone on given pin. if freq is 0 then tone is
  // disabled
  if (pwm_enable(pin, freq, duty) < 0) {
    HELP(q_print(Failed));
    return CMD_FAILED;
  }

  return 0;
}

// handles "sequence SEQ" keyword
//
static int cmd_pin_sequence(int argc, char **argv,unsigned int pin, unsigned int *start) {

  unsigned int i = *start;
  int seq, j;

  // do we have at least 1 extra arg after ith?
  if ((i + 1) >= argc) {
    HELP(q_printf("%% <e>Sequence number expected after \"%s\"</>\r\n", argv[i]));
    return CMD_MISSING_ARG;
  }
  *start = ++i;

  // Enable selected RMT sequence 'seq' on pin 'pin'
  if (seq_isready((seq = q_atol(argv[i], DEF_BAD)))) {
    //HELP(q_printf("%% Sending sequence %u over GPIO %u\r\n", seq, pin));
    if ((j = seq_send(pin, seq)) == 0)
      return 0;
    q_printf("%% <e>RMT failed with code %d</>\r\n", j);
  } else
    q_printf("%% <e>Sequence %u is not configured</>\r\n", seq);
  return CMD_FAILED;
}

// handles "pin X matrix [in|out SIGNAL_ID]"
// Since pin is multiple-argument command we also pass the /start/ index into argv[] array
// 
static int cmd_pin_matrix(int argc, char **argv,unsigned int pin, unsigned int *start) {

  unsigned int i = *start;

  // set pin function to "Simple GPIO via GPIO Matrix"
  pin_set_iomux_function(pin, PIN_FUNC_GPIO);

  // Is there any signal IDs provided? 
  if (i + 2 < argc) {

    // must be a number or a keyword "gpio"
    if (q_strcmp(argv[i + 2],"gpio") && !isnum(argv[i + 2]))
      return i + 2;

    // read the signal id. defaults to "simple GPIO" on fail
    unsigned int sig_id = q_atol(argv[i + 2],SIG_GPIO_OUT_IDX);
    // TODO: handle "invert" flag/keyword
    if (argv[i + 1][0] == 'i')                     // was it "in" signal?
      gpio_matrix_in(pin, sig_id, false);
    else
      gpio_matrix_out(pin, sig_id, false, false);  // ..no. then it is "out"

    // advance to next keyword (skip [in|out] and SIGNAL_ID)
    *start += 2;
  } else {
    VERBOSE("% matrix keyword but no signals: defaulting to SIG_GPIO_OUT_IDX");
    //gpio_matrix_in(pin, SIG_GPIO_OUT_IDX, false);
    gpio_matrix_out(pin, SIG_GPIO_OUT_IDX, false, false);
  }

  return 0;
}

// handles "pin X ... loop COUNT"
// Since pin is multiple-argument command we also pass the /start/ index into argv[] array
// TODO: make COUNT arg to "loop" optional. Omitted count means "loop forever"
//
static int cmd_pin_loop(int argc, char **argv,unsigned int pin, unsigned int *start, unsigned int *count) {

  unsigned int i = *start;
  
  if ((i + 1) >= argc) {
    HELP(q_print("% <e>Loop count expected after keyword \"loop\"</>\r\n"));
    return CMD_MISSING_ARG;
  }
  
  *start = ++i;

  // loop must be the last keyword, so we can strip it later
  if ((i + 1) < argc) {
    HELP(q_print("% <e>\"loop\" must be the last keyword</>\r\n"));
    return i + 1;
  }
  //read loop count or "infinite"
  *count = q_atol(argv[i], (unsigned int)(-1));

  HELP(q_printf("%% Repeating whole command %u times%s\r\n", *count - 1,is_foreground_task() ? ", press <Enter> to abort" : ""));

  return 0;
}


// "pin NUM arg1 arg2 .. argn"
//
// Big fat "pin" command. Processes multiple arguments
// TODO: do some sort of caching in case of a looped commands: we don't need all these q_atol() and q_atrcmp() for the second, third whatever pass
//
static int cmd_pin(int argc, char **argv) {

  unsigned int  flags = 0, // Flags to set (i.e. OUTPUT ,INPUT, OPEN_DRAIN, PULL_UP, PULL_DOWN etc)
                i = 2,     // Argument, we start our processing from (0 is the command itself, 1 is a pin number)
                pin;       // Pin number, currently processed by the command

  bool  informed = false,               // Did we inform user on how to interrupt this command?
        is_fore = is_foreground_task(); // Foreground or background task? 

  unsigned int count = 1; // Command loop count

  if (argc < 2)
    return CMD_MISSING_ARG;  //missing argument

  //first argument must be a decimal number: a GPIO number
  if (!pin_exist((pin = q_atol(argv[1], DEF_BAD)))) 
    return 1;

  do { // Do the same command /count/ times.

    // Run through "pin NUM arg1, arg2 ... argN" arguments, looking for keywords to execute.
    // Abort if there were errors during next keywords processing. 

    while (i < argc) {

      int ret;

      //2. "pwm FREQ DUTY" keyword. Shortened "p"
      // unlike global "pwm" command the duty and frequency are not an optional parameter anymore
      // and PWM channel is autoselected
      if (!q_strcmp(argv[i], "pwm")) {
        if ((ret = cmd_pin_pwm(argc,argv,pin,&i)) != 0)
          return ret;
      } else
      //3. "delay X" keyword. Shortened "d"
      // Creates *interruptible* delay for X milliseconds.
      if (!q_strcmp(argv[i], "delay")) {
        int duration;
        if ((i + 1) >= argc) {
          HELP(q_print("% <e>Delay value expected after keyword \"delay\"</>\r\n"));
          return i;
        }
        i++;
        //if ((duration = (int)q_atol(argv[i], -1)) < 0)
        if ((duration = atoi(argv[i])) == 0)
          return i;
        // Display a hint for the first time when delay is longer than 5 seconds.
        // Any key works instead of <Enter> but Enter works in Arduino Serial Monitor
        if (!informed && is_fore && (duration > TOO_LONG)) {
          informed = true;
          HELP(q_print("% <g>Hint: Press [Enter] to interrupt the command</>\r\n"));
        }
        // Was interrupted by a keypress or by the "kill" command? Abort whole command then.
        if (delay_interruptible(duration) != duration) {
          HELP(q_printf("%% Command \"%s\" has been interrupted\r\n", argv[0]));
          // TODO: return CMD_FAILED ?
          return 0;
        }
      } else
      // 4. "pin X save". Shortened "s"
      if (!q_strcmp(argv[i], "save")) pin_save(pin); else
      // 5. "pin X up". Shortened "u"
      if (!q_strcmp(argv[i], "up")) pinForceMode(pin, (flags |= PULLUP));  else
      // 6. "pin X down". Shortened "do"
      if (!q_strcmp(argv[i], "down")) pinForceMode(pin, (flags |= PULLDOWN)); else
      // 7. "pin X in". Shortened "i"
      if (!q_strcmp(argv[i], "in")) pinForceMode(pin, (flags |= INPUT)); else
      // 8. "pin X out". Shortened "o"
      if (!q_strcmp(argv[i], "out")) pinForceMode(pin, (flags |= OUTPUT_ONLY)); else
      // 9. "pin X open". Shortened "op"
      if (!q_strcmp(argv[i], "open")) pinForceMode(pin, (flags |= OPEN_DRAIN)); else
      // 10. "pin X low | high" keyword. only applies to I/O pins, fails for input-only pins. Shortened "l" and "h"
      if (!q_strcmp(argv[i], "low") || !q_strcmp(argv[i],"high")) {
        if (pin_is_input_only_pin(pin)) {
          q_printf("%% <e>Pin %u is **INPUT-ONLY**, can not be set \"%s</>\"\r\n", pin, argv[i]);
          return i;
        }
        // use pinForceMode/digitalForceWrite to not let the pin to be reconfigured (bypass PeriMan)
        // pinForceMode is not needed because ForceWrite does it
        if ((flags & OUTPUT_ONLY) == 0)
       /*   pinForceMode(pin, (*/flags |= OUTPUT_ONLY/*))*/;
          
        digitalForceWrite(pin, argv[i][0] == 'l' ? LOW : HIGH);
      } else
      // 11. "pin X read". Shortened "r"
      if (!q_strcmp(argv[i], "read")) q_printf("%% GPIO%d : logic %d\r\n", pin, digitalForceRead(pin)); else
      // 12. "pin X aread". Shortened "a"
      if (!q_strcmp(argv[i], "aread")) q_printf("%% GPIO%d : analog %d\r\n", pin, analogRead(pin)); else
      //1. "seq NUM" keyword.. Shortened "se"
      if (!q_strcmp(argv[i], "sequence")) {
        if ((ret = cmd_pin_sequence(argc,argv,pin,&i)) != 0)
          return ret;
      } else
      // 13. "pin X hold". Shortened "ho"
      if (!q_strcmp(argv[i], "hold")) gpio_hold_en((gpio_num_t)pin); else
      // 14. "pin X release". Shortened "rel" (interferes with "read")
      if (!q_strcmp(argv[i], "release")) gpio_hold_dis((gpio_num_t)pin); else
      // 15. "pin X load". Shortened "loa" (interferes with "low")
      if (!q_strcmp(argv[i], "load")) pin_load(pin); else
      // 16. "pin X iomux [NUMBER | gpio]" . Shortened "io"
      if (!q_strcmp(argv[i], "iomux")) {
        // default is IO_MUX function 0 which is, in most cases, a GPIO function via IO_MUX
        unsigned char function = 0; 

        // if we have extra arguments, then treat number as IO_MUX function, treat text as special case. 
        if ((i+1) < argc)
            function = q_atol(argv[++i],PIN_FUNC_PAD_SELECT_GPIO); 
          pin_set_iomux_function(pin, function);
      } else
      // 17. "pin X matrix [in|out NUMBER]" . Shortened "m"
      if (!q_strcmp(argv[i], "matrix")) {
        if ((ret = cmd_pin_matrix(argc,argv,pin,&i)) != 0)
          return ret;
      } else
      //18. "loop" keyword. Shortened "loo"
      if (!q_strcmp(argv[i], "loop")) { 
        if ((ret = cmd_pin_loop(argc,argv,pin,&i,&count)) != 0)
          return ret;
        // Strip "loop COUNT" arguments. We read them only once and do not want to read same number on the next pass
        argc -= 2;
      } else
      //A keyword which is a decimal number. When we see a number we use it as a pin number
      //for subsequent keywords. Must be a valid GPIO number.
      if (isnum2(argv[i])) {
        if (!pin_exist_silent((pin = /*q_atol(argv[i], DEF_BAD)*/atoi2(argv[i]))))
          return i;
      } else
      // argument i was not recognized
        return i;

      // Keyword was executed successfully. Proceed to the next keyword.
      i++;
    }       //big fat "while (i < argc)"
    i = 1;  // prepare to start over again

    // give a chance to cancel whole command
    // by anykey press, but only if it is a foreground process
    if (is_fore)
      if (anykey_pressed()) {
        HELP(q_print("% Key pressed, aborting..\r\n"));
        break;
      }
  } while (--count > 0);  // repeat if "loop X"
  return 0;
}
#endif // #if COMPILING_ESPSHELL

#if 0
// TODO: refactor that monster /if else/ statement in cmd_pin()
//       Template for cmd_pin() refactoring: too many q_strcmps
  switch (argv[i][0]) {
    case 'p' : // pwm FREQ DUTY
              if ((ret = cmd_pin_pwm(argc,argv,pin,&i)) != 0)
                return ret;
              break;
    case 'u' : // up
              pinForceMode(pin, (flags |= PULLUP));
              break;
    case 'a' : // aread
              break;
    case 'm' : // matrix in|out NUMBER
              break;

    case 'd' : // down 
              if (argv[i][1] == 'o') {
                pinForceMode(pin, (flags |= PULLDOWN));
              } else {
              // delay TIME_MS

              }
              break;
    case 's' : // seq NUMBER
              if (argv[i][1] == 'e') {

              } else
              // save
                pin_save(pin);

              
              break;
    case 'i' :// in 
              if (likely(argv[i][1] != 'o')) {
                pinForceMode(pin, (flags |= INPUT));
              } else {
              // iomux [FUNCTION | gpio]
              }
              break;
    case 'o' : // out
              if (likely(argv[i][1] != 'p'))
                pinForceMode(pin, (flags |= OUTPUT));
              else
              // open
                pinForceMode(pin, (flags |= OPEN_DRAIN));
              
              
              break;
    case 'l' : // loop
              if (argv[i][1] && argv[i][2] == 'o') {

              } else if (argv[i][1] && argv[i][2] == 'a') {
              // load
                pin_load(pin);
              } else {
                // low
              
              }
              break;
    case 'h' : // hold
              if (unlikely(argv[i][1] == 'o')) {

              } else {
              // high
                if (pin_is_input_only_pin(pin)) {
                  q_printf("%% <e>Pin %u is **INPUT-ONLY**, can not be set \"%s</>\"\r\n", pin, argv[i]);
                  return i;
                }
                // use pinForceMode/digitalForceWrite to not let the pin to be reconfigured (bypass PeriMan)
                // pinForceMode is not needed because ForceWrite does it
                flags |= OUTPUT_ONLY;
                digitalForceWrite(pin, HIGH);
              }
              break;
    case 'r' : // release
              if (unlikely(argv[i][1] && argv[i][2] == 'l')) {

              } else {
                // read
              }
              break;
  };
#endif    

