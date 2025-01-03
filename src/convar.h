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
  unsigned int isf :  1;    // is it "float"?
  unsigned int isp :  1;    // is it "void *"?
  unsigned int isu :  1;    // is it "unsigned" ?
  unsigned int size:  3;    // variable size (1,2 or 4 bytes)
  unsigned int esize: 25;   // if variable is a pointer (or an array) then this field contains sizeof(array_element)
};

// composite variable value.
// had to use union because of variables different sizeof()
typedef union composite_u {
  unsigned char  uchar;  // unsigned char
  signed char    ichar;  // signed --
  unsigned short ush;    // unsigned short
  signed short   ish;    // signed --
  int            ival;   // signed int
  unsigned int   uval;   // unsigned --
  float          fval;   // float
} composite_t;


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
// /isu/  - unsigned value (ignored for "pointer" and "float" variables)
//
void espshell_varadd(const char *name, void *ptr, int size, bool isf, bool isp, bool isu) {

  struct convar *var;

  if (size != 1 && size != 2 && size != 4) {
    q_printf("%% Variable \"%s\" was not registered (unsupported size %u)\r\n",name,size);
    return;
  }

  if ((var = (struct convar *)q_malloc(sizeof(struct convar), MEM_STATIC)) != NULL) {
    var->next = var_head;
    var->name = name;
    var->ptr = ptr;
    var->size = size;
    var->isf = isf ? 1 : 0;
    var->isp = isp ? 1 : 0;
    var->isu = isu ? 1 : 0;
    var_head = var;
  }
}

#if NOT_YET
void espshell_varaddp(const char *name, void *ptr, int size, bool isf, bool isp, bool isu) {

  struct convar *var;

  if ((var = (struct convar *)q_malloc(sizeof(struct convar), MEM_STATIC)) != NULL) {
    var->next = var_head;
    var->name = name;
    var->ptr = ptr;
    var->size = sizeof( void * );
    var->esize = size;
    var->isf = 0;
    var->isp = 1;
    var->isu = 0;
    var_head = var;
  }
}
#endif

//
static struct convar *convar_get(const char *name) {

  struct convar *var = var_head;
  while (var) {
    if (!q_strcmp(name, var->name))
      return var;
    var = var->next;
  }
  return NULL;
}

static const char *__uch = "unsigned char";
static const char *__ush = "unsigned short";
static const char *__uin = "unsigned int";



//
static int convar_show_var(char *name) {

    struct convar *var;
    composite_t *comp;

    if ((var = convar_get(name)) == NULL) {
      HELP(q_printf("%% <e>\"%s\" : No such variable. (use \"var\" to display variables list)</>\r\n", name));
      return 1;
    }

    comp = (composite_t *)var->ptr;

    if (comp == NULL) // must not happen
      abort();


    q_print(CRLF);


    switch (var->size) {

      case 1:
          if (var->isu)
            q_printf("%% %s <i>%s</> = <3>%u</>; // 0x%x in hex\r\n", __uch, var->name, comp->uchar, comp->uchar); 
          else
            q_printf("%% %s <i>%s</> = <3>%i</>; // 0x%x in hex\r\n", __uch + 9, var->name, comp->ichar, comp->ichar); 
      break;

      case 2:
          if (var->isu)
            q_printf("%% %s <i>%s</> = <3>%u</>; // 0x%x in hex\r\n", __ush, var->name, comp->ush, comp->ush); 
          else
            q_printf("%% %s <i>%s</> = <3>%i</>; // 0x%x in hex\r\n", __ush + 9, var->name, comp->ish, comp->ish); 
      break;

      case 4:
          if (var->isf)
            q_printf("%% float <i>%s</> = <3>%f</>; // 0x%x in hex\r\n", var->name, comp->fval, comp->uval);
          else 
          if (var->isp)
            q_printf("%% void *<i>%s</> = <3>0x%x</>;\r\n", var->name, comp->uval);
          else {
            if (var->isu)
              q_printf("%% %s <i>%s</> = <3>%u</>; // 0x%x in hex\r\n", __uin, var->name, comp->uval, comp->uval); 
            else
              q_printf("%% %s <i>%s</> = <3>%d</>; // 0x%x in hex\r\n", __uin + 9, var->name, comp->ival, comp->ival); 
          }
      break;

      default:
        HELP(q_printf("%% <e>Variable \"%s\" has unsupported size of %d bytes</>\r\n", var->name, var->size));
        return 1;
    }

    return 0;
}

static int convar_value_as_string(struct convar *var, char *out, int olen) {

  if (!var || !out)
    return -1;

  char tmp[256];
  composite_t comp;

  memcpy(&comp, var->ptr, var->size);
  if (var->isf)
    snprintf(tmp, 255, "%f", comp.fval);
  else if (var->isp)
    snprintf(tmp, 255, "0x%x", comp.uval);
  else if (var->isu) {
      unsigned int val = var->size == 4 ? comp.uval : (var->size == 2 ? comp.ush : comp.uchar);
      snprintf(tmp, 255, "%u", val);
  } else {
      signed int val = var->size == 4 ? comp.ival : (var->size == 2 ? comp.ish : comp.ichar);
      snprintf(tmp, 255, "%i", val);
  }

  if (strlen(tmp) >= olen)
    return -1;

  strcpy(out,tmp);
  return 0;
}


static const char *convar_typename(struct convar *var) {
  int off = var->isu ? 0 : 9;
  return var ? (var->isf ? "float" : 
                          (var->isp ? "pointer / array" : 
                                      (var->size == 4 ? __uin + off : (var->size == 2 ? __ush + off : __uch + off)))) : "???";
}

//
static int convar_show_list() {

    struct convar *var = var_head;
    char out[256] = { 0 };

    if (!var)
      HELP(q_print(VarOops));
    else
      q_print("% Sketch variables:\r\n<r>"
              "% Variable X name | sizeof(X) |     typeof(X)    |     Value      </>\r\n"
              "%-----------------+-----------+------------------+----------------\r\n");

    while (var) {

      if (convar_value_as_string(var, out, sizeof(out)) < 0)
        out[0] = '\0';

#pragma GCC diagnostic ignored "-Wformat"
      q_printf("%%<i>% 16s</> | % 9u | % 16s | % 16s \r\n",
               var->name,
               var->size,
               convar_typename(var),
               out);

#pragma GCC diagnostic warning "-Wformat"
      var = var->next;
    }
    return 0;
}

//
static int convar_show_number(const char *p) {


    unsigned int unumber;
    signed int   inumber;
    float        fnumber;

    
    
    // argv[n] is at least 2 bytes long (1 symbol + '\0')
    // Octal, Binary or Hex number?
    if (p[0] == '0') {
      unumber = q_atol(p,DEF_BAD);
      memcpy(&fnumber, &unumber, sizeof(fnumber));
      memcpy(&inumber, &unumber, sizeof(inumber));
    } else {
      if (isnum(p)) {
        if (p[0] == '-') {
          inumber = atoi(p);
          memcpy(&unumber, &inumber, sizeof(unumber));
        } else {
          unumber = atol(p);
          memcpy(&inumber, &unumber, sizeof(inumber));
        }
        memcpy(&fnumber, &unumber, sizeof(fnumber));
      } else
        // Floating point number maybe?
        if (isfloat(p)) {
          fnumber = atof(p);
          memcpy(&unumber, &fnumber, sizeof(unumber));
          memcpy(&inumber, &fnumber, sizeof(inumber));
        } else {
          // No brother, this defenitely not a number.
          q_printf("%% \"%s\" doesn't look like number\r\n",p);
          return 0;
        }
    }

    // display a number in hex, octal, binary, integer or float representation
    q_printf("%% \"%s\" is a number, which can be written as:\r\n"
             "%% unsigned : %u\r\n"
             "%%   signed : %i\r\n"
             "%% float    : %f\r\n" 
             "%% Hex      : 0x%x\r\n"
             "%% Octal    : 0%o\r\n"
             "%% Binary   : 0b",
             p, unumber, inumber, fnumber, unumber, unumber);

    // display binary form with leading zeros omitted
    if (unumber == 0)
      q_print("00000000");
    else
      for (inumber = __builtin_clz(unumber); inumber < 32; inumber++)
        q_print((unumber & (0x80000000 >> inumber)) ? "1" : "0");

    q_print(CRLF);
    return 0;
}


// "var"           - display registered variables list
// "var VAR_NAME"  - display variable VAR_NAME value
// "var NUMBER"    - display NUMBER in different bases / types
static int cmd_var_show(int argc, char **argv) {

  // "var": display variables list if no arguments were given
  if (argc < 2)
    return convar_show_list();


  // "var NUMBER" : displays different representation of a constant
  if (argc < 3)
    if (q_numeric(argv[1]))
      return convar_show_number(argv[1]);

  return convar_show_var(argv[1]);
}

// "var"           -- bypassed to cmd_var_show()
// "var X"         -- bypassed to cmd_var_show()
// "var X NUMBER"  -- set variable X to the value NUMBER
//
static int cmd_var(int argc, char **argv) {

  composite_t u;  

  // no variables were registered but user invoked "var" command:
  // give them a hint
  if (var_head == NULL) {
    HELP(q_print(VarOops));
    return 0;
  }

  if (argc < 3)
    return cmd_var_show(argc, argv);

  // Set variable
  // does variable exist? get its size
  struct convar *var;
  if ((var = convar_get(argv[1])) == NULL)
    return 1;

  if (var->isf) {
    if (isfloat(argv[2])) {
      u.fval = q_atof(argv[2], 0);
    } else {
      HELP(q_printf("%% <e>Variable \"%s\" is \"float\" and expects floating point argument</>\r\n", var->name));
      return 2;
    }
  } else {
    // integers & pointer values
    if (q_numeric(argv[2])) {
      if (argv[2][0] == '-') {
        if (var->isu) {
          q_printf("%% Variable \"%s\" is unsigned\r\n",var->name);
          return 0;
        }
        signed int val = -q_atol(&(argv[2][1]), 0);
        if (var->size == 4) u.ival  = val; else
        if (var->size == 2) u.ish   = val; else
        if (var->size == 1) u.ichar = val; else { q_printf("%% Bad variable size %u\r\n",var->size); return 0; }
      } else {
        unsigned int val = q_atol(argv[2], 0);
        if (var->size == 4) u.uval  = val; else
        if (var->size == 2) u.ush   = val; else
        if (var->size == 1) u.uchar = val; else { q_printf("%% Bad variable size %u\r\n",var->size); return 0; }
      }

      memcpy(var->ptr, &u, var->size);
    } else 
      return 2;
  }
  return 0;
}
#endif // #if COMPILING_ESPSHELL
