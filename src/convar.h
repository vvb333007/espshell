/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */

#if COMPILING_ESPSHELL

// -- Console Variables --
//
// User sketch can **register** global or static variables to be accessible (for reading/writing)
// from ESPShell. Once registered, variables can be manipulated by "var" command: see "extra/espshell.h"
// for convar_add() definition, and example_blink.ino for example of use

// "Console Variable" (convar) descriptor. These are created by convar_add() and linked into SL list
struct convar {
  struct convar *next;     // next var in list
  const char *name;        // var name
  void *ptr;               // &var
  unsigned int isf : 1;    // is it "float"?
  unsigned int isp : 1;    // is it "void *"?
  unsigned int size : 30;  // size of array pointed by /ptr/: 1,2 or 4 bytes
};

// All registered variables. Access to the list is **not thread safe**
static struct convar *var_head = NULL;

// Register new sketch variable.
// Memory allocated for variable descriptor is never free()'d. This function is not
// supposed to be called directly: instead a macro "convar_add()" (see "extra/espshell.h") must be used
//
// /name/ - variable name
// /ptr/  - pointer to the variable
// /size/ - variable size in bytes
// /isf/  - typeof(var) = "float" ?
// /isp/  - typeof(var) = "pointer" ?
//
void espshell_varadd(const char *name, void *ptr, int size, bool isf, bool isp) {

  struct convar *var;

  if (size != 1 && size != 2 && size != 4)
    return;

  if ((var = (struct convar *)q_malloc(sizeof(struct convar), MEM_STATIC)) != NULL) {
    var->next = var_head;
    var->name = name;
    var->ptr = ptr;
    var->size = size;
    var->isf = isf ? 1 : 0;
    var->isp = isp ? 1 : 0;
    var_head = var;
  }
}

// get registered variable value & length
// In:  /name/      - variable name
// Out: /value/     - variable value copied to the buffer (4 bytes min length)
//      /*fullname/ - variable canonical name
//      /*isf/      - is variable of a 'float' type?
//      /*isp*/     - is variable of a pointer type?
//
static int convar_get(const char *name, void *value, char **fullname, bool *isf, bool *isp) {

  struct convar *var = var_head;
  while (var) {
    if (!q_strcmp(name, var->name)) {
      memcpy(value, var->ptr, var->size);
      if (fullname) *fullname = (char *)var->name;
      if (isf) *isf = var->isf;
      if (isp) *isp = var->isp;
      return var->size;
    }
    var = var->next;
  }
  return 0;
}

// set registered variable value
//
static int convar_set(const char *name, void *value) {
  struct convar *var = var_head;
  while (var) {
    if (!strcmp(var->name, name)) {
      memcpy(var->ptr, value, var->size);
      return var->size;
    }
    var = var->next;
  }
  return 0;
}

// "var"     - display registered variables list
// "var X"   - display variable X value
static int cmd_var_show(int argc, char **argv) {

  int len;

  // composite variable value.
  // had to use union because of variables different sizeof()
  union {
    unsigned char uchar;  // unsigned char
    signed char ichar;    // signed --
    unsigned short ush;   // unsigned short
    signed short ish;     // signed --
    int ival;             // signed int
    unsigned int uval;    // unsigned --
    float fval;           // float
  } u;

  // "var": display variables list if no arguments were given
  if (argc < 2) {
    struct convar *var = var_head;


    if (!var)
      HELP(q_print(VarOops));
    else
      q_print("%   Sketch variables (use \"var NAME\" to see variable values)    \r\n<r>"
              "% Variable X name | sizeof(X) |     typeof(X)    |                </>\r\n"
              "%-----------------+-----------+------------------+----------------\r\n");

    while (var) {
#pragma GCC diagnostic ignored "-Wformat"
      q_printf("%%<i>% 16s</> | % 9u | % 16s |\r\n",
               var->name,
               var->size,
               var->isf ? "float" : var->isp ? "pointer / array"
                                             : (var->size == 4 ? "[un]signed int" : (var->size == 2 ? "[un]signed short" : (var->size == 1 ? "[un]signed char" : "array"))));

#pragma GCC diagnostic warning "-Wformat"
      var = var->next;
    }
    return 0;
  }

  //"var X": display variable value OR
  //"var NUMBER" display different representaton of a number
  //
  if (argc < 3) {
    unsigned int unumber;
    signed int inumber;
    float fnumber;
    // argv[n] is at least 2 bytes long (1 symbol + '\0')
    // Octal, Binary or Hex number?
    if (argv[1][0] == '0') {
      if (argv[1][1] == 'x')
        unumber = hex2uint32(&argv[1][2]);
      else if (argv[1][1] == 'b')
        unumber = binary2uint32(&argv[1][2]);
      else
        unumber = octal2uint32(&argv[1][1]);
      memcpy(&fnumber, &unumber, sizeof(fnumber));
      memcpy(&inumber, &unumber, sizeof(inumber));
    } else {
      // Integer (signed or unsigned) or floating point number?
      if (isnum(argv[1])) {
        if (argv[1][0] == '-') {
          inumber = atoi(argv[1]);
          memcpy(&unumber, &inumber, sizeof(unumber));
        } else {
          unumber = atol(argv[1]);
          memcpy(&inumber, &unumber, sizeof(inumber));
        }
        memcpy(&fnumber, &unumber, sizeof(fnumber));
      } else
        // Floating point number maybe?
        if (isfloat(argv[1])) {
          fnumber = atof(argv[1]);
          memcpy(&unumber, &fnumber, sizeof(unumber));
          memcpy(&inumber, &fnumber, sizeof(inumber));
        } else
          // No brother, this defenitely not a number.
          goto process_as_variable_name;
    }

    // display a number in hex, octal, binary, integer or float representation
    q_printf("%% \"%s\" is a number, which can be written as:\r\n"
             "%% unsigned : %u\r\n"
             "%%   signed : %i\r\n"
             "%% float    : %f\r\n" 
             "%% Hex      : 0x%x\r\n"
             "%% Octal    : 0%o\r\n"
             "%% Binary   : 0b",
             argv[1], unumber, inumber, fnumber, unumber, unumber);

    // display binary form with leading zeros omitted
    if (unumber == 0)
      q_print("00000000");
    else
      for (inumber = __builtin_clz(unumber); inumber < 32; inumber++)
        q_print((unumber & (0x80000000 >> inumber)) ? "1" : "0");

    q_print(CRLF);
    return 0;

process_as_variable_name:

    char *fullname;
    bool isf, isp;
    if ((len = convar_get(argv[1], &u, &fullname, &isf, &isp)) == 0) {
      HELP(q_printf("%% <e>\"%s\" : No such variable. (use \"var\" to display variables list)</>\r\n", argv[1]));
      return 1;
    }
    switch (len) {
      // char or unsigned char?
      case sizeof(char):
        q_printf("%% // 0x%x in hex\r\n"
                 "%% unsigned char <i>%s</> = <3>%u</>; // unsigned or ...\r\n"
                 "%%   signed char <i>%s</> = <3>%d</>; // ... signed\r\n",
                 u.uchar, fullname, u.uchar, fullname, u.ichar);
        break;
      // short or unsigned short?
      case sizeof(short):
        q_printf("%% // 0x%x in hex\r\n"
                 "%% unsigned short <i>%s</> = <3>%u</>; // unsigned or ...\r\n"
                 "%%   signed short <i>%s</> = <3>%d</>; // ... signed\r\n",
                 u.ush, fullname, u.ush, fullname, u.ish);
        break;
      case sizeof(int):
        // signed or unsigned int, float or pointer?
        if (!isp) q_printf("%% // 0x%x in hex\r\n", u.uval);
        if (isf) q_printf("%% float <i>%s</> = <3>%f</>f;\r\n", fullname, u.fval);
        else if (isp) q_printf("%% void *<i>%s</> = (void *) <3>0x%x</>; // pointer\r\n", fullname, u.uval);
        else
          q_printf("%% unsigned int <i>%s</> = <3>%u</>; // unsigned or ...\r\n%%   signed int <i>%s</> = <3>%d</>; // ... signed \r\n", fullname, u.uval, fullname, u.ival);
        break;
      // ??
      default:
        HELP(q_printf("%% <e>Variable \"%s\" has unsupported size of %d bytes</>\r\n", fullname, len));
        return 1;
    };
    return 0;
  }
  return -1;
}

// "var"           -- bypassed to cmd_var_show()
// "var X"         -- bypassed to cmd_var_show()
// "var X NUMBER"  -- set variable X to the value NUMBER
//
static int cmd_var(int argc, char **argv) {

  int len;

  union {
    unsigned char uchar;
    signed char ichar;
    unsigned short ush;
    signed short ish;
    int ival;
    unsigned int uval;
    float fval;
  } u;

  // no variables were registered but user invoked "var" command:
  // give them a hint
  if (var_head == NULL) {
    HELP(q_print(VarOops));
    return 0;
  }

  // "var"           : display variables list if no arguments were given
  // "var VAR_NAME"  : display variable value
  // "var NUMBER"    : display NUMBER in different bases
  if (argc < 3)
    return cmd_var_show(argc, argv);

  // Set variable
  // does variable exist? get its size
  char *fullname;
  bool isf, isp;
  if ((len = convar_get(argv[1], &u, &fullname, &isf, &isp)) == 0)
    return 1;

  if (isf) {
    if (isfloat(argv[2])) {
      // floating point number
      u.fval = q_atof(argv[2], 0);
    } else {
      HELP(q_printf("%% <e>Variable \"%s\" has type \"float\" and expects floating point argument</>\r\n", fullname));
      return 2;
    }
  } else
    // integers & pointer values
    if (isnum(argv[2]) || (argv[2][0] == '0' && argv[2][0] == 'x')) {
      bool sign = argv[2][0] == '-';
      if (sign) {
        u.ival = -q_atol(&(argv[2][1]), 0);
        if (len == sizeof(char)) u.ichar = u.ival;
        else if (len == sizeof(short)) u.ish = u.ival;
      } else {
        //q_print("% Unsigned integer\r\n");
        u.uval = q_atol(argv[2], 0);
        if (len == sizeof(char)) u.uchar = u.uval;
        else if (len == sizeof(short)) u.ush = u.uval;
      }
    } else
      // unknown
      return 2;

  convar_set(fullname, &u);
  return 0;
}
#endif // #if COMPILING_ESPSHELL
