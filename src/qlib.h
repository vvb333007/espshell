/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Burdzhanadze <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */


// -- Q-Lib: helpful routines: ascii to number conversions, etc --
// Probably should be merged with misc.h
//
#if COMPILING_ESPSHELL

// strdup() + extra 256 bytes
static char *q_strdup256(const char *ptr, int type) {
  char *p = NULL;
  if (ptr != NULL) {
    int len = strlen(ptr);
    if ((p = (char *)q_malloc(len + 256 + 1, type)) != NULL)
      strcpy(p, ptr);
  }
  return p;
}


//check if given ascii string is a decimal number. Ex.: "12345", "-12"
// "minus" sign is only accepted if first in the string
//
static bool isnum(const char *p) {
  if (p && *p) {
    if (*p == '-') p++;
    while (*p >= '0' && *p <= '9') p++;
    return *p == '\0';  //if *p is 0 then all the chars were digits (end of line reached).
  }
  return false;
}

// check if ascii string is a float number
// NOTE: "0.5" and ".5" are both valid inputs
static bool isfloat(const char *p) {
  if (p && *p) {
    bool dot = false;
    if (*p == '-') p++;
    while ((*p >= '0' && *p <= '9') || (*p == '.' && !dot)) {
      if (*p == '.')
        dot = true;
      p++;
    }
    return !(*p);  //if *p is 0 then all the chars were ok. (end of line reached).
  }
  return false;
}



// Check if given ascii string is a hex BYTE.
// String may or may not start with "0x"
// Strings "a" , "5a", "0x5" and "0x5A" are valid input
//
static bool ishex(const char *p) {
  if (p && *p) {
    if (p[0] == '0' && p[1] == 'x')
      p += 2;
    while (*p != '\0') {
      if ((*p < '0' || *p > '9') && (*p < 'a' || *p > 'f') && (*p < 'A' || *p > 'F'))
        break;
      p++;
    }
    return *p == '\0';
  }
  return false;
}

// check only first 1-2 bytes (not counting "0x" if present)
static bool ishex2(const char *p) {

  if (p && *p) {
    if (p[0] == '0' && p[1] == 'x')
      p += 2;

    if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
      p++;
      if ((*p == 0) || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
        return true;
    }
  }
  return false;
}

//convert hex ascii byte.
//strings "A", "5a" "0x5a" are valid input
//
static unsigned char hex2uint8(const char *p) {

  unsigned char f, l;  //first and last

  if (p[0] == '0' && p[1] == 'x')
    p += 2;

  f = *p++;

  //single character HEX?
  if (!(*p)) {
    l = f;
    f = '0';
  } else l = *p;

  // make it lowercase
  if (f >= 'A' && f <= 'F') f = f + 'a' - 'A';
  if (l >= 'A' && l <= 'F') l = l + 'a' - 'A';

  //convert first hex character to decimal
  if (f >= '0' && f <= '9') f = f - '0';
  else if (f >= 'a' && f <= 'f') f = f - 'a' + 10;
  else return 0;

  //convert second hex character to decimal
  if (l >= '0' && l <= '9') l = l - '0';
  else if (l >= 'a' && l <= 'f') l = l - 'a' + 10;
  else return 0;

  return (f << 4) | l;
}

// convert a hex string to uint32_t
// if string is too long then number converted will be equal
// to last 4 bytes of the string
static unsigned int hex2uint32(const char *p) {

  unsigned int value = 0;
  unsigned int four = 0;

  if (p[0] == '0' && p[1] == 'x')
    p += 2;

  while (*p) {
    if (*p >= '0' && *p <= '9') four = *p - '0';
    else if (*p >= 'a' && *p <= 'f') four = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F') four = *p - 'A' + 10;
    else return 0;
    value <<= 4;
    value |= four;
    p++;
  }
  return value;
}

static unsigned int octal2uint32(const char *p) {
  unsigned int value = 0;
  unsigned int three = 0;
  while (*p) {
    if (*p >= '0' && *p <= '7') three = *p - '0';
    else return 0;
    value <<= 3;
    value |= three;
    p++;
  }
  return value;
}

// convert strings
// 0b10010101 and 10100101 (with or without leading "0b") to
// unsigned int values
//
static unsigned int binary2uint32(const char *p) {
  unsigned int value = 0;
  unsigned int one = 0;

  if (p[0] == '0' && p[1] == 'b')
    p += 2;

  while (*p) {
    if (*p == '0' || *p == '1') one = *p - '0';
    else return 0;
    value <<= 1;
    value |= one;
    p++;
  }
  return value;
}

// q_atol() : extended version of atol()
// 1. Accepts decimal, hex,octal or binary numbers (0x for hex, 0 for octal, 0b for binary)
// 2. If conversion fails (bad symbols in string, empty string etc) the
//    "def" value is returned
//
// TAG:atol
#define DEF_BAD ((unsigned int)(-1))

static unsigned int q_atol(const char *p, unsigned int def) {
  if (p && *p) {
    if (isnum(p))  // decimal number?
      def = atol(p);
    else if (p[0] == '0') {  // leading "0" : either hexadecimal, binary or octal number
      if (p[1] == 'x') {     // hexadecimal
        if (ishex(p))
          def = hex2uint32(p);
      } else if (p[1] == 'b')  // binary (TODO: isbin())
        def = binary2uint32(p);
      else
        def = octal2uint32(p);  // octal  (TODO: isoct())
    }
  }
  return def;
}

// same for the atof():
static inline float q_atof(const char *p, float def) {
  if (p && *p)
    if (isfloat(p))
      def = atof(p);
  return def;
}



// Loose strcmp() which deoes partial match. It is used to match commands and parameters which are shortened:
// e.g. user typed "seq" instead of "sequence" or "m w" instead of "mount wwwroot"
//
// /partial/ - string which expected to be incomplete/shortened
// /full/    - full string to compare against
//
// q_strcmp("seq","sequence") == 0
// q_strcmp("sequence","seq") == 1
//
static int q_strcmp(const char *partial, const char *full) {
  int plen;
  if (partial && full && (*partial == *full))      // quick reject
    if ((plen = strlen(partial)) <= strlen(full))  //     OR
      return strncmp(partial, full, plen);         //  full test
  return 1;
}

static inline const char *q_findchar(const char *str, char sym) {
  if (str) {
    while (*str && sym != *str)
      str++;
    if (sym == *str)
      return str;
  }
  return NULL;
}


// adopted from esp32-hal-uart.c Arduino Core
// TODO: remake to either use non-static buffer OR use sync objects because we have async tasks which dp q_printf
//       thus reusing internal 128 bytes buffer
static int __printfv(const char *format, va_list arg) {

  static char buf[128 + 1];  // TODO: Mystery#2. Crashes without /static/.
  char *temp = buf;
  uint32_t len;
  int ret;
  va_list copy;

  // make fake vsnprintf to find out required buffer length
  va_copy(copy, arg);
  len = vsnprintf(NULL, 0, format, copy);
  va_end(copy);

  // if required buffer is larger than built-in one then allocate
  // a new buffer
  if (len >= sizeof(buf))
    if ((temp = (char *)q_malloc(len + 1, MEM_TMP)) == NULL)
      return 0;

  // actual printf()
  vsnprintf(temp, len + 1, format, arg);
  ret = q_print(temp);
  if (temp != buf)
    q_free(temp);
  return ret;
}

// same as printf() but uses global var 'uart' to direct
// its output to different uarts
// NOTE: add -Wall or at least -Wformat to Arduino's c_flags for __attribute__ to have
//       effect.
static int PRINTF_LIKE q_printf(const char *format, ...) {
  int len;
  va_list arg;
  va_start(arg, format);
  len = __printfv(format, arg);
  va_end(arg);
  return len;
}


//Faster than q_printf() but only does non-formatted output
static int q_print(const char *str) {

  size_t len = 0;

  if (Echo < 0)  //"echo silent"
    return 0;

  if (str && *str) {

    const char *p, *pp = str;
    const char *ins;

    while (*pp) {
      if ((p = q_findchar(pp, '<')) == NULL)
        return console_write_bytes(pp, strlen(pp));
      if (p[1] && p[2] == '>') {
        ins = NULL;
        if (Color)
          switch (p[1]) {
            case 'i': ins = esc_i; break;
            case 'w': ins = esc_w; break;
            case 'e': ins = esc_e; break;
            case '/': ins = esc_n; break;
            case 'r': ins = esc_r; break;
            case '2': ins = esc_2; break;
            case '1': ins = esc_1; break;
            case '3': ins = esc_3; break;
            case 'b': ins = esc_b; break;
            case '*': ins = esc_ast; break;
            case '_': ins = esc__; break;
          }
        len += console_write_bytes(pp, p - pp);
        if (ins)
          len += console_write_bytes(ins, strlen(ins));
        pp = p + 3;
      } else {
        len += console_write_bytes(pp, p - pp + 1);
        pp = p + 1;
      }
    }
  }
  return len;
}

// make fancy hex data output: mixed hex values
// and ASCII. Useful to examine I2C EEPROM contents.
//
// data printed 16 bytes per line, a space between hex values, 2 spaces
// after each 4th byte. then separator and ascii representation are printed
//
static unsigned short tbl_min_len = 16;

static void q_printhex(const unsigned char *p, unsigned int len) {

  if (!p || !len)
    return;

  if (len < tbl_min_len) {
    // data array is too small. just do simple output
    do {
      q_printf("%02x ", *p++);
    } while (--len);
    q_print(CRLF);
    return;
  }


  char ascii[16 + 1];
  unsigned int space = 1;

  q_print("<r>       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F  |0123456789ABCDEF</>\r\n");
  q_print("<r>-----</>-----------------------------------------------------+----------------\r\n");

  for (unsigned int i = 0, j = 0; i < len; i++) {
    // add offset at the beginning of every line. it doesnt hurt to have it.
    // and it is useful when exploring eeprom contens
    if (!j)
      q_printf("<r>%04x:</> ", i);

    //print hex byte value
    q_printf("%02x ", p[i]);

    // add an extra space after each 4 bytes printed
    if ((space++ & 3) == 0)
      q_print(" ");

    //add printed byte to ascii representation
    //dont print anything with codes less than 32 and higher than 127
    ascii[j++] = (p[i] < ' ' || p[i] > '~') ? '.' : p[i];

    // one complete line could be printed:
    // we had 16 bytes or we reached end of the buffer
    if ((j > 15) || (i + 1) >= len) {

      // end of buffer but less than 16 bytes:
      // pad line with spaces
      // TODO: one space is lost somewhere when dumping sizes like 251
      if (j < 16) {
        unsigned char spaces = (16 - j) * 3 + (j <= 4 ? 3 : (j <= 8 ? 2 : (j <= 12 ? 1 : 0))) + 1;  // empirical :)
        char tmp[spaces + 1];
        memset(tmp, ' ', spaces);
        tmp[spaces] = '\0';
        q_print(tmp);
      }

      // print a separator and the same line but in ascii form
      q_print("|");
      ascii[j] = '\0';
      q_print(ascii);
      q_print(CRLF);
      j = 0;
    }
  }
}



// convert argument TEXT for uart/write and files/write commands (and others)
// to a buffer.
//
// /argc/
// /argv/
// /i/    - first argv to start collecting text from
// /out/  - allocated buffer
// Returns number of bytes in buffer /*out/
//
static int text2buf(int argc, char **argv, int i /* START */, char **out) {

  int size = 0;
  char *b;

  if (i >= argc)
    return -1;

  //instead of estimating buffer size just allocate 512 bytes buffer: espshell
  // input strings are limited to 500 bytes.
  if ((*out = b = (char *)q_malloc(ESPSHELL_MAX_INPUT_LENGTH + 12, MEM_TEXT2BUF)) != NULL) {
    // go thru all the arguments and send them. the space is inserted between arguments
    do {
      char *p = argv[i];
      while (*p) {
        char c = *p;
        p++;
        if (c == '\\') {
          switch (*p) {
            case '\\':             p++;              c = '\\';              break;
            case 'n':              p++;              c = '\n';              break;
            case 'r':              p++;              c = '\r';              break;
            case 't':              p++;              c = '\t';              break;
            case 'e':              p++;              c = '\e';              break;
            case 'v':              p++;              c = '\v';              break;
            case 'b':              p++;              c = '\b';              break;
            default:
              if (ishex2(p)) {
                c = hex2uint8(p);
                if (p[0] == '0' && p[1] == 'x')
                  p += 2;
                p++;
                if (*p) 
                  p++;
              } else {
                // unknown escape sequence: fallthrough to get "\" printed
              }
          }
        }
        *b++ = c;
        size++;
      }
      i++;
      //if there are more arguments - insert a space
      if (i < argc) {
        *b++ = ' ';
        size++;
      }
      // input line length limiting. just in case. normally editline() must not accept lines which are too long
      if (size > ESPSHELL_MAX_INPUT_LENGTH)
        break;
    } while (i < argc);
  }
  return size;
}


// version of delay() which can be interrupted by user input (terminal
// keypress) for delays longer than 5 seconds.
//
// for delays shorter than 5 seconds fallbacks to normal(delay)
//
// `duration` - delay time in milliseconds
//  returns duration if ok, <duration if was interrupted
#define TOO_LONG 4999
#define DELAY_POLL 250

static unsigned int delay_interruptible(unsigned int duration) {

  // if duration is longer than 4999ms split it in 250ms
  // intervals and check for a keypress or task "kill" ntofication from the "kill" command
  // in between
  unsigned int delayed = 0;
  if (duration > TOO_LONG) {
    while (duration >= DELAY_POLL) {
      duration -= DELAY_POLL;
      delayed += DELAY_POLL;

      // wait for the notify sent by "kill TASK_ID" command.
      if (xTaskNotifyWait(0, 0xffffffff, NULL, pdMS_TO_TICKS(DELAY_POLL)) == pdPASS)
        return delayed;  // interrupted by "kill" command

      if (anykey_pressed())
        return delayed;
    }
  }
  if (duration) {
    unsigned int now = millis();
    if (xTaskNotifyWait(0, 0xffffffff, NULL, pdMS_TO_TICKS(duration)) == pdPASS)
      duration = millis() - now;  // calculate how long we were in waiting state before notification was received
    delayed += duration;
  }
  return delayed;
}

// return "st", "nd", "rd" ot "th" depending on number
static __attribute__((const)) const char *number_english_ending(unsigned int n) {
  return n == 1 ? "st" : (n == 2 ? "nd" : (n == 3 ? "rd" : "th"));
}




#endif //#if COMPILING_ESPSHELL
