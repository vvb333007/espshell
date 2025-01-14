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
  void *ptr;               // &var or &gpp
  void *gpp;               // helper pointer to handle arrays 
  unsigned int isf :  1;    // is it "float"?
  unsigned int isp :  1;    // is it array or pointer (non-void type)?
  unsigned int isu :  1;    // is it "unsigned" ?

  unsigned int isfa :  1;    // is array element of "float" type?
  unsigned int ispa :  1;    // -- pointer ?
  unsigned int isua :  1;    // -- unsigned ?
    
  unsigned int size:  3;    // variable size (1,2 or 4 bytes)
  unsigned int sizea: 22;   // if variable is a pointer (or an array) then this field contains sizeof(array_element)
  unsigned int counta;      //              --                                                 sizeof(array)/sizeof(array_element, i.e. nnumber of elements in the array)
};

// composite variable value.
// had to use union because of variables different sizeof()
//
typedef union composite_u {
  unsigned char  uchar;  // unsigned char
  signed char    ichar;  // signed --
  unsigned short ush;    // unsigned short
  signed short   ish;    // signed --
  int            ival;   // signed int
  unsigned int   uval;   // unsigned --
  float          fval;   // float
} composite_t;

// Limits
#define ARRAY_TOO_BIG       257 // don't display array content if its element count exceeeds this number. TODO: accept "var NAME[123] and var[12 16]"

#define CONVAR_NAMELEN_MAX  64 // max variable name length
#define CONVAR_TYPELEN_MAX  32 // should be enough even for "unsigned long long"
#define CONVAR_COUNTLEN_MAX 32 // should be enough even for "[4294967295]"

#define CONVAR_BUFSIZ (CONVAR_NAMELEN_MAX + CONVAR_TYPELEN_MAX + CONVAR_COUNTLEN_MAX)



// All registered variables. Access to the list is **not thread safe**
static struct convar *var_head = NULL;

static const char *__uch = "unsigned char";
static const char *__ush = "unsigned short";
static const char *__uin = "unsigned int";
static const char *__ucha = "unsigned char *";
static const char *__usha = "unsigned short *";
static const char *__uina = "unsigned int *";


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

// Code above fails when trying to register a pointer, because of integer comparision code
// Code below is used to register pointers
//
void espshell_varaddp(const char *name, void *ptr, int size, bool isf, bool isp, bool isu) {

  struct convar *var;

  if ((var = (struct convar *)q_malloc(sizeof(struct convar), MEM_STATIC)) != NULL) {
   
    var->next = var_head;
    var->name = name;
    var->ptr = ptr;
    var->isp = 1;
    var->isf = 0;
    var->isu = 0;
    var->size = sizeof( void * );
    var->sizea = size;
    var->counta = 1;
    var->isfa = isf;
    var->isua = isu;
    var->ispa = isp;
    var_head = var;
  }
}

// Code above fails when trying to register an array, because of & operator logic. (&array and &pointer are different things)
// Code below is used to register arrays
//
void espshell_varadda(const char *name, void *ptr, int size,int count, bool isf, bool isp, bool isu) {

  struct convar *var;

  if ((var = (struct convar *)q_malloc(sizeof(struct convar), MEM_STATIC)) != NULL) {

    var->gpp = ptr;
    var->next = var_head;
    var->name = name;
    var->ptr = &var->gpp;
    var->isp = 1;
    var->isf = 0;
    var->isu = 0;
    var->size = sizeof( void * );
    var->sizea = size;
    var->counta = count;
    var->isfa = isf;
    var->isua = isu;
    var->ispa = isp;
    var_head = var;
  }
}

// return asciiz string with C-style variable type
// e.g. "float" or "unsigned int *" in case of pointers or arrays
//
static const char *convar_typename(struct convar *var) {
  int off = var->isu ? 0 : 9; // offset to strings like "unsigned int" to make them "int" (i.e. skip first /off/ bytes)
  return var ? (var->isf ? "float" : 
                          (var->isp ? (var->isfa ? "float *" : 
                                                    (var->ispa ? "void **" : 
                                                                 (var->sizea == 4 ? __uina + off : 
                                                                                    (var->sizea == 2 ? __usha + off : 
                                                                                                       __ucha + off)))) :
                                      (var->size == 4 ? __uin + off : (var->size == 2 ? __ush + off : __uch + off)))) : 
                "(null)";
}


// NOTE: NOT REENTRANT!!
// For array-type variables only.
//
static const char *convar_typename2(struct convar *var) {

  static char out[CONVAR_BUFSIZ];
  const char *tn;
  int i;

  MUST_NOT_HAPPEN((i = strlen((tn = convar_typename(var)))) >= sizeof(out));

  strcpy(out,tn);
  
  // For arrays (with number of elements > 1) we replace terminating "*" of typename
  // with VAR_NAME[ARRAY_COUNT]. E.g. typename "float *" for variable "test" will be something like: "float test[]"
  //
  if (var->isp) {

    MUST_NOT_HAPPEN(out[i - 1] != '*'); // must not happen

    out[i - 1] = ' ';
    sprintf(&out[i],"%s[%u]",var->name,var->counta); // TODO: this is unsafe! Limit variable name length to something real like 64 characters
  } else {
    out[i] = ' ';
    strcpy(&out[i + 1],var->name); // TODO: this is unsafe! Limit variable name length to something real like 64 characters
  }

  return out;
}


// Find variable descriptor by variable name
//
// TODO: if there are more than 1 match on shortened name, don't pick up the first matched. Instead, display a warning and list all the
//       matched variables.
static struct convar *convar_get(const char *name) {

  struct convar *var = var_head;
  while (var) {
    if (!q_strcmp(name, var->name))
      return var;
    var = var->next;
  }
  return NULL;
}

// Print the value of a variable.
// Printing is done to internal buffer and then it is copied to /out/ if /olen/ permits
//
static int convar_value_as_string(struct convar *var, char *out, int olen) {

  if (var && out && olen) {
    composite_t comp;
    memcpy(&comp, var->ptr, var->size);
    if (var->isf)
      snprintf(out, olen, "%f", comp.fval);
    else if (var->isp)
      snprintf(out, olen, "0x%x", comp.uval);
    else if (var->isu) {
        unsigned int val = var->size == 4 ? comp.uval : (var->size == 2 ? comp.ush : comp.uchar);
        snprintf(out, olen, "%u", val);
    } else {
        signed int val = var->size == 4 ? comp.ival : (var->size == 2 ? comp.ish : comp.ichar);
        snprintf(out, olen, "%i", val);
    }
    return 0;
  }
  return -1;
}


// Show variable value by variable name
//
static int convar_show_var(char *name) {

    struct convar *var;
    if ((var = convar_get(name)) == NULL) {
      HELP(q_printf("%% <e>\"%s\" : No such variable. (use \"var\" to display variables list)</>\r\n", name));
      return 1;
    }

  char out[CONVAR_BUFSIZ]; 
  if (convar_value_as_string(var,out,sizeof(out)) == 0) {
    // Print value
    q_printf("%% %s <i>%s</> = <3>%s</>;  ", convar_typename(var), var->name, out);
    // In case of a pointer or array, print its content
    if (var->isp) {
      if (var->counta == 1) // arrays of 1 element are treated as plain pointers
        q_printf("// Pointer to %u-byte memory region", var->sizea);
      else
        q_printf("// Array of %u elements, (%u bytes per element)", var->counta, var->sizea);
      if (var->counta < ARRAY_TOO_BIG) {
        q_printf("\r\n%% %s = {\r\n", convar_typename2(var));

        struct convar var0 = { 0 };

        
        var0.name = var->name;
        var0.isf = var->isfa;
        var0.isp = var->ispa;
        var0.isu = var->isua;
        var0.size = var->sizea;

        for (int i = 0; i < var->counta; i++) {
          var0.ptr = (void *)((char *)(*(void **)var->ptr) + var->sizea * i); // love pointer arithmetic :)
          convar_value_as_string(&var0,out,sizeof(out));
          q_printf("%%    <3>%s</>, // %s[%u]\r\n",out,var->name,i);
        }
        q_print("% };\r\n");
      } else 
        q_printf(", too many to display\r\n");
    } else
      q_print(CRLF);
  }
    return 0;
}


// Show all variables in table form. Arrays and pointers are displayed as addresses, not as *(address)
// To display memory content one have to use "var NAME" not just "var"
static int convar_show_list() {

    struct convar *var = var_head;
    char out[CONVAR_BUFSIZ] = { 0 };

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

// Utility function to display a number in different bases and different casts:
// i.e. display "float" as "hex", or an arbitrary number in octal, binary or hexadecimal form
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
    q_printf("%% \"<i>%s</>\" is a number, which can be written as:\r\n"
             "%% <_>C-style cast of a memory content:</>\r\n"
             "%% unsigned : <3>%u</>\r\n"
             "%%   signed : <3>%i</>\r\n"
             "%% float    : <3>%f</>\r\n" 
             "%% <_>Same number in different bases:</>\r\n" 
             "%% Hex      : <3>0x%x</>\r\n"
             "%% Octal    : <3>0%o</>\r\n"
             "%% Binary   : <3>0b",
             p, unumber, inumber, fnumber, unumber, unumber);

    // display binary form with leading zeros omitted
    if (unumber == 0)
      q_print("00000000");
    else
      for (inumber = __builtin_clz(unumber); inumber < 32; inumber++)
        q_print((unumber & (0x80000000 >> inumber)) ? "1" : "0");

    q_print("</>\r\n");
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
    // TODO: warn if float argument is detected
    if (q_numeric(argv[2])) {
      if (argv[2][0] == '-') {
        if (var->isu) {
          q_printf("%% Variable \"%s\" is unsigned, new value is not set\r\n",var->name);
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
    } else 
      return 2;
  }
  memcpy(var->ptr, &u, var->size);
  return 0;
}
#endif // #if COMPILING_ESPSHELL
