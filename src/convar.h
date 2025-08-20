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

#if COMPILING_ESPSHELL

// -- Console Variables --
//
// User sketch can **register** global or static variables to be accessible (for reading/writing)
// from ESPShell. Once registered, variables can be manipulated by "var" command: see "extra/espshell.h"
// for convar_add() definition, and example_blink.ino for example of use
//
// Sketch variables then can be accessible through "var" command:
// "var VARIABLE_NAME" - display variable (for arrays it also displays its elements)
// "var VARIABLE_NAME VALUE" - set variable to a new value
// Individual array elements (real array or a pointer) can be referred to as VARIABLE_NAME[INDEX]

// TODO: This code needs a review. There may be buffer overflows because of strcpy and sprintf. These should be changed to strlcpy & snprintf
// TODO: Verify that no variables with names longer than CONVAR_NAMELEN_MAX-1 can be registered

// "Console Variable" (convar) descriptor. These are created by convar_add() and linked into SL list
// The head of the list is "var_head". 
// Entries are created once and never deleted
//

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

// Composite variable value .
// This is to perform "unsafe" C-style casts
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
#define ARRAY_TOO_BIG       257 // don't display array content if its elements count exceeeds this number

#define CONVAR_NAMELEN_MAX  64 // max variable name length
#define CONVAR_TYPELEN_MAX  32 // should be enough even for "unsigned long long"
#define CONVAR_COUNTLEN_MAX 32 // should be enough even for "[4294967295]"

// Safe text buffer size
#define CONVAR_BUFSIZ (CONVAR_NAMELEN_MAX + CONVAR_TYPELEN_MAX + CONVAR_COUNTLEN_MAX)


// Single-linked list of all registered variables. 
// NOTE: Access to the list is not protected (no mutexes nor semaphores)
static struct convar *var_head = NULL;

// Helper constants which are used by convar_typenameX() to construct type names like "char" or "unsigned int"
static const char *__uch = "unsigned char";
static const char *__ush = "unsigned short";
static const char *__uin = "unsigned int";
static const char *__ucha = "unsigned char *";
static const char *__usha = "unsigned short *";
static const char *__uina = "unsigned int *";

// Check if variable has supported type. ESPShell supports only basic C types
// which can fit 1,2 or 4 bytes
//
static bool variable_type_is_ok(unsigned int size) {
  if (size != sizeof(char) && size != sizeof(short) && size != sizeof(int) && size != sizeof(float)) {
    q_printf("%% Variable was not registered (unsupported size: %u)\r\n",size);
    return false;
  }
  return true;
}


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

  if (variable_type_is_ok(size))
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
// The /size/ argument here is the sizeof(*ptr), i.e. size of a memory region pointed by pointer
//
void espshell_varaddp(const char *name, void *ptr, int size, bool isf, bool isp, bool isu) {

  struct convar *var;

  if (variable_type_is_ok(size))
    if ((var = (struct convar *)q_malloc(sizeof(struct convar), MEM_STATIC)) != NULL) {
   
      var->next = var_head;
      var->name = name;
      var->gpp = *(void **)ptr; // this is required if we want to set/display values of a pointer (NAME[INDEX])
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
// In this case ->ptr points to internal general purpose pointer, which points to the first array element.
// Because of this it is possible to set an array variable to a new value. It will not affect the sketch 
// (you can not change address of an array) but it will affect ESPShell.
//
//
void espshell_varadda(const char *name, void *ptr, int size,int count, bool isf, bool isp, bool isu) {

  struct convar *var;

  if (variable_type_is_ok(size))
    if ((var = (struct convar *)q_malloc(sizeof(struct convar), MEM_STATIC)) != NULL) {

      var->gpp = ptr;                   // actual pointer to the array (i.e. &array[0])
      var->next = var_head;
      var->name = name;
      var->ptr = &var->gpp;             // "address of a variable" for arrays it is always points to GPP
      var->isp = 1;                     // It is a pointer
      var->isf = 0;
      var->isu = 0;
      var->size = sizeof( void * );     // size of a pointer is a constant
      var->sizea = size;                // size of array element
      var->counta = count;              // number of elements in the array
      var->isfa = isf;                  // is?a is a twin brothers of their is? counterparts but related to the array element
      var->isua = isu;
      var->ispa = isp;
      var_head = var;
    }
}

// return asciiz string with C-style type of a variable.
// E.g. returns "float" or "unsigned int *" in case of pointers or arrays
// Never returns NULL
//
static const char *convar_typename(struct convar *var) {

  int off = var->isu ? 0 : 9; // offset to strings like "unsigned int" to make them "int" (i.e. skip first 9 bytes)

  return var ? (var->isf  ? "float"
                          : (var->isp ? (var->isfa ? "float *"
                                                   : (var->ispa ? "void **"
                                                                : (var->sizea == sizeof(int) ? __uina + off
                                                                                             :  (var->sizea == sizeof(short) ? __usha + off
                                                                                                                             : __ucha + off)))) 
                                      : (var->size == sizeof(int) ? __uin + off 
                                                                  : (var->size == sizeof(short) ? __ush + off 
                                                                                                : __uch + off))))
             : "(null)";
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
    sprintf(&out[i - 1],"%s[%u]",var->name,var->counta); // TODO: review is needed for buffer overrun
  } else {
    out[i] = ' ';
    strcpy(&out[i + 1],var->name); // TODO: review is needed for buffer overrun
  }

  return out;
}


// Find variable descriptor by variable name
// /name/ - variable name (e.g. "my_var" or "arr[10]"), can be shortened
//
// Returns a pointer to the descriptor
// WARNING: For array elements a virtual variable is created & returned, and it is STATIC variable. So convar_get() function
//          is NOT reentrant! Means, avoid running "var" command in a background.
// WARNING: convar_get() can write to the string which is passed as its argument! All convar names are expected to come from an argv's
//          so they are writeable. Dont attempt convar_get("String literal")!
static struct convar *convar_get(char *name) {

  struct convar *var;
  struct convar *candidate;
  static struct convar var0; // WARNING: NOT REENTRANT!
  char *br;
  unsigned int idx = 0;  // Index to array element if element == true

  if (name == NULL)
    return var_head;

  // Variable name is an array element? (e.g. "buff[11]")
  if ((br = (char *)q_findchar(name,'[')) != NULL) {

    static char name0[CONVAR_NAMELEN_MAX] = { 0 };
    char *index = br + 1;

    *br = '\0';
    
    br = (char *)q_findchar(index,']');
    if (!br) {
      q_print("% <e>Closing bracket \"]\" expected</>\r\n");
      return NULL;
    }
    *br = '\0';
    if ((idx = q_atol(index,DEF_BAD)) == DEF_BAD) {
      q_print("% <e>Numeric index is expected inside []</>\r\n");
      return NULL;
    }

    // Recursive call. /name/ at this point was transformed from "Text[Index]"" to "Text"
    // I.e. we are trying to get a descriptor for an array whose element was requested
    if ((var = convar_get(name)) == NULL)
      return NULL;

    // Index only applies to pointers and arrays
    if (!var->isp) {
      q_printf("%% Variable \"%s\" is neither a pointer nor an array\r\n", name);
      return NULL;
    }
    
    // Should we deny access to indicies beyound boundaries?
    // In other hand it might be useful for accessing pointers as arrays
    if (idx >= var->counta) {
      if (var->counta > 1) { // an array. defenitely we don't want to go beyound its boundaries
        q_printf("%% Requested element %u is beyond the array range 0..%u\r\n", idx, var->counta - 1);
        return NULL;
      }
      // Pointers unlike arrays always have their .counta set to 1, so we don't know the real boundaries.
      // We just issue the warning and continue. Arrays of size 1 are treated as pointers so no warning will be issued
    }

    // create a virtual variable pointing to a requested array element
    // and pass it to the code below
    memset(&var0,0,sizeof(var0));
    snprintf(name0,sizeof(name0) - 1,"%s[%u]",var->name,idx);
    var0.name = name0;
    var0.ptr = (char *)(var->gpp) + idx * var->sizea;
    var0.isf = var->isfa;
    var0.isp = var->ispa;
    var0.isu = var->isua;
    var0.size = var->sizea;
    var0.counta = 1;

    // Done
    return &var0;
  }

  // Requested variable is not an array element, it is just ordinary variable
  // Try to find exact name match...
  for (var = var_head; var; var = var->next)
    if (!strcmp(name, var->name))
      return var;

  // try partial match...
  for (var = var_head, candidate = NULL; var; var = var->next)
    if (!q_strcmp(name, var->name)) {
      if (candidate) {
        q_printf("%% <e>Ambiguity: by \"%s\" did you mean \"%s\" or \"%s\"?</>\r\n",name,var->name, candidate->name);
        return NULL;
      }
      candidate = var;
    }
  return candidate;
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
        unsigned int val = var->size == sizeof(int) ? comp.uval : (var->size == sizeof(short) ? comp.ush : comp.uchar);
        snprintf(out, olen, "%u", val);
    } else {
        signed int val = var->size == sizeof(int) ? comp.ival : (var->size == sizeof(short) ? comp.ish : comp.ichar);
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
    HELP(q_printf("%% <e>\"%s\" : No such variable registered. (type \"var\" to see the list)</>\r\n", name));
    return 1;
  }

  char out[CONVAR_BUFSIZ]; 

  if (convar_value_as_string(var,out,sizeof(out)) == 0) {
    // Print value
    q_printf("%% %s <i>%s</> = <g>%s</>;  ", convar_typename(var), var->name, out);
    // In case of a pointer or array, print its content
    if (var->isp) {
      if (var->counta == 1) // arrays of 1 element are treated as plain pointers
        q_printf("// Pointer a to %u-byte memory region", var->sizea);
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
          q_printf("%%    <g>%s</>, // %s[%u]\r\n",out,var->name,i);
        }
        q_print("% };\r\n");
      } else 
        q_print(", too many to display\r\n%% Use \"var Name[Index]\" to display individual array elements\r\n");
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
              "% Variable X name | sizeof(X) |     typeof(X)    |  Value/Address  </>\r\n"
              "%-----------------+-----------+------------------+-----------------\r\n");

    while (var) {

      if (convar_value_as_string(var, out, sizeof(out)) < 0)
        out[0] = '\0';

      
      q_printf("%%<i>%16s</> | %9u | %16s | %16s \r\n",
               var->name,
               var->size,
               convar_typename(var),
               out);

      
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
    if (p[0] == '0' && p[1] != '.') {
      unumber = q_atol(p,DEF_BAD);
      // Prepare for "unsafe" C-style typecast: these memcpy() here and below
      // just make a copy of the same value into different type variables.
      // This can be used to see a hex representation of a floating point variable and so on
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
          q_printf("%% <e>A number is expected. \"%s\" doesn't look like a number</>\r\n",p);
          return 0;
        }
    }

    // display a number in hex, octal, binary, integer or float representation
    q_printf("%% \"<i>%s</>\" is a number, which can be written as:\r\n"
             "%% <u>C-style cast of a memory content:</>\r\n"
             "%% unsigned : <g>%u</>\r\n"
             "%%   signed : <g>%i</>\r\n"
             "%% float    : <g>%f</>\r\n" 
             "%% <u>Same number in different bases:</>\r\n" 
             "%% Hex      : <g>0x%x</>\r\n"
             "%% Octal    : <g>0%o</>\r\n"
             "%% Binary   : <g>0b",
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
  //if (argc < 3)
    if (q_isnumeric(argv[1]))
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
  struct convar *var;

  if ((var = convar_get(argv[1])) == NULL)
    return 1;

  if (var->isf) {
    if (isfloat(argv[2])) {
      u.fval = q_atof(argv[2], 0);
    } else {
      HELP(q_printf("%% <e>Variable \"%s\" expects a floating point argument</>\r\n", var->name));
      return 2;
    }
  } else {
    // Integers & pointer values.
    // Warn if float argument is detected, or negative value is attempted for an unsigned variable
    //
    if (q_isnumeric(argv[2])) {
      if (q_findchar(argv[2],'.')) {
        q_printf("%% <e>Variable \"%s\" is integer: value not changed</>\r\n",var->name);
        return 0;
      }

      // New value is a negative integer?
      if (argv[2][0] == '-') {
        if (var->isu) {
          q_printf("%% <e>Variable \"%s\" is unsigned: value not changed</>\r\n",var->name);
          return 0;
        }
        signed int val = -q_atol(&(argv[2][1]), 0);
        if (var->size == sizeof(int))
          u.ival  = val; else
        if (var->size == sizeof(short))
          u.ish   = val; else
        if (var->size == sizeof(char)) 
          u.ichar = val; 
        else 
          goto report_and_exit;
      } else {
      // New value is an unsigned integer?
        unsigned int val = q_atol(argv[2], 0);
        if (var->size == sizeof(int))
          u.uval  = val; else
        if (var->size == sizeof(short))
          u.ush   = val; else
        if (var->size == sizeof(char))
          u.uchar = val;
        else {
report_and_exit:          
          q_printf("%% Bad variable size: %u\r\n",var->size); 
          return 0; 
        }
      }
    } else 
      return 2;
  }
  memcpy(var->ptr, &u, var->size);
  return 0;
}
#endif // #if COMPILING_ESPSHELL
