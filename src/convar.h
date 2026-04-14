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
// A user sketch can **register** global or static variables to make them accessible 
// for reading and writing from ESPShell. Once registered, variables can be manipulated 
// using the "var" command. See "espshell.h" for the convar_add() definition, 
// and led_blink_var_example.ino for usage examples.
//
// Registered variables are accessible through the "var" command:
//   "var VARIABLE_NAME"           - display the variable (or an array element)
//   "var VARIABLE_NAME VALUE"     - set the variable to a new value
// Individual array elements (whether real arrays or pointers) can be accessed as VARIABLE_NAME[INDEX].
//
// TODO: Review this code. Possible buffer overflows may occur due to use of strcpy and sprintf. 
// TODO: These should be replaced with strlcpy and snprintf.
// TODO: Verify that variables with names longer than CONVAR_NAMELEN_MAX - 1 cannot be registered.
// TODO: Add support "long long", 64bit type
// TODO: Parse array sizes "[NUM]"

//
// "Console Variable" (convar) descriptors are created by convar_add() 
// and linked into a singly linked list (head is "var_head"). 
// Entries are created once and never deleted.

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
                            // if, however, it is 0, then this means a generic pointer (memory size is unknown)
                            // this happens when accessing array elements, due to implementation. Fixing it requires too much effort.
  unsigned int counta;      // sizeof(array)/sizeof(array_element, i.e. nnumber of elements in the array)
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
// NOTE: Access to the list is not protected (no mutexes nor semaphores). All convar_addX() must be made from the same
//       task context
//       
static struct convar *var_head = NULL;


// Check if variable has supported type. ESPShell supports only basic C types
// which can fit 1,2 or 4 bytes
static bool convar_is_size_ok(unsigned int size) {
  if (size != sizeof(char) && size != sizeof(short) && size != sizeof(int) && size != sizeof(float)) {
    q_printf("%% Variable was not registered (unsupported size: %u)\r\n",size);
    return false;
  }
  return true;
}

// For strings "[123]" return 123, for strings "[]" return def
//
static unsigned int  read_array_size(const char *name0, unsigned int def) {

  char name[CONVAR_NAMELEN_MAX], *index, *br;

  if (!name0)
    return 42;

  strlcpy(name, name0, sizeof(name));

  if (NULL != (br = (char *)q_findchar(name,'['))) {
    
    *br = '\0';
    index = br + 1;
    if (NULL != (br = (char *)q_findchar(index,']'))) {
      *br = '\0';
      return q_atol(index, def);
    }
  }
  return def;
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

  if (convar_is_size_ok(size))
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

  if ((isp && (size == 0)) || convar_is_size_ok(size))
    if ((var = (struct convar *)q_malloc(sizeof(struct convar), MEM_STATIC)) != NULL) {
   
      var->next = var_head;
      var->name = name;
      var->gpp = *(void **)ptr; // this is required if we want to set/display values of a pointer (NAME[INDEX])
      var->ptr = ptr;
      var->isp = 1;
      var->isf = 0;
      var->isu = isu;
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

  if (convar_is_size_ok(size))
    if ((var = (struct convar *)q_malloc(sizeof(struct convar), MEM_STATIC)) != NULL) {

      var->gpp = ptr;                   // actual pointer to the array (i.e. &array[0])
      var->next = var_head;
      var->name = name;
      var->ptr = &var->gpp;             // "address of a variable" for arrays it is always points to GPP
      var->isp = 1;                     // It is a pointer
      var->isf = 0;
      var->isu = isu;                   // array element is signed or not?
      var->size = sizeof( void * );     // size of a pointer is a constant
      var->sizea = size;                // size of array element
      var->counta = count;              // number of elements in the array
      var->isfa = isf;                  // isXa is a twin brothers of their isX counterparts but related to the array element
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

  const char *__uch  = "unsigned char";
  const char *__ush  = "unsigned short";
  const char *__uin  = "unsigned int";
  //const char *__ull  = "unsigned long long";
  const char *__ucha = "unsigned char *";
  const char *__usha = "unsigned short *";
  const char *__uina = "unsigned int *";
  //const char *__ulla = "unsigned long long *";

  int off = var->isu ? 0 : 9; // offset to strings like "unsigned int" to make them "int" (i.e. skip first 9 bytes)

  return var ? (var->isf  ? "float"
                          : (var->isp ? (var->isfa ? "float *" :
                                        (var->ispa ? "void **" :
                                        (var->sizea == sizeof(int)   ? __uina + off :
                                        (var->sizea == sizeof(short) ? __usha + off : __ucha + off))))
                          : (var->size == sizeof(int) ? __uin + off
                          : (var->size == sizeof(short) ? __ush + off : __uch + off))))
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
  // TODO: refactor to use read_array_size()
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

    // Dynamically created variables may cause headache :(
    if (!is_valid_address(var->ptr,var->size)) {
      snprintf(out,olen,"<w>?? <address %p is not readable></>", var->ptr);
      return -1;
    }

    memcpy(&comp, var->ptr, var->size);
    if (var->isf)
      snprintf(out, olen, "%f", comp.fval);
    else if (var->isp)
      snprintf(out, olen, "0x%x", comp.uval);
    else if (var->isu) {
        unsigned int val = var->size == sizeof(int) ? comp.uval : (var->size == sizeof(short) ? comp.ush : comp.uchar);
        if (val > 1024)
          snprintf(out, olen, "%u //*%x*/", val, val);
        else
          snprintf(out, olen, "%u", val);
    } else {
        signed int val = var->size == sizeof(int) ? comp.ival : (var->size == sizeof(short) ? comp.ish : comp.ichar);
          snprintf(out, olen, "%i", val);
    }
    return 0;
  }
  return -1;
}


#if 0

static void inline __attribute__((always_inline))
_memcpy(char *dst, char *src, uint8_t count) {
  switch(count) {
    case 4: *dst++ = *src++; // FALLTHROUGH
    case 3: *dst++ = *src++; // FALLTHROUGH
    case 2: *dst++ = *src++; // FALLTHROUGH
    case 1: *dst = *src;     // FALLTHROUGH
    default:
  };
}

// Compare two variables
// We need this for our ifcond.h module, which performs logic operations on variables
//
// Returns <0 if var < var2, >0 if var > var2 or 0 if var == var2
//
//convar_map_variable() ?
static int convar_compare(struct convar *var, struct convar *var2) {

  if (var && var2) {
    composite_t comp = { 0 }, comp2 = { 0 };

    _memcpy((char *)&comp, (char *)var->ptr, var->size);
    _memcpy((char *)&comp2, (char *)var2->ptr, var2->size);

    if (var->isf) return comp.fval - comp2.fval; else
    if (var->isp) return comp.uval - comp2.uval; else
    if (var->isu) return (int)(var->size == sizeof(int) ? comp.uval - comp2.uval
                                                        : (var->size == sizeof(short) ? comp.ush - comp2.ush
                                                                                      : comp.uchar - comp2.uchar)); else
    return (int)(var->size == sizeof(int) ? comp.ival - comp2.ival
                                          : (var->size == sizeof(short) ? comp.ish - comp2.ish
                                                                        : comp.ichar - comp2.ichar));
  }
  return -1;
}
#endif
// Show variable value by variable name
//
static int convar_show_var(char *name) {

  struct convar *var;
  char out[CONVAR_BUFSIZ]; 

  if ((var = convar_get(name)) == NULL) {
    HELP(q_printf("%% <e>\"%s\" : No such variable registered. (type \"var\" to see the list)</>\r\n", name));
    return 1;
  }

  if (convar_value_as_string(var,out,sizeof(out)) == 0) {

    // For arrays and pointers display array base address
    if (var->isp) {

      if (var->counta > 1)
        q_printf("%% Array <i>%s[%u]</> at address <g>%s</>, %u byte%s per element\r\n", var->name, var->counta, out, PPA(var->sizea));
      else {
        if (var->sizea < 1) {
          // TODO: what is this???
          q_print("%% void *<i>%s</> = <g>%s</>;\r\n");
          return 0;
        } else
          q_printf("%% Pointer <i>&%s</> == %p, <i>%s</> == <g>%s</>, sizeof(*%s) == %u\r\n", var->name, var->ptr, var->name, out, var->name, var->sizea);
      }

      // In case of a pointer or array, print its content

      if (var->counta < ARRAY_TOO_BIG) {

        struct convar var0 = { 0 };

        q_printf("\r\n%% %s = {\r\n", convar_typename2(var));

        // Create dummy variable. It will be used to display array elements
        // Fill common fielfd, but leave .ptr not set
        var0.name = var->name;
        var0.isf = var->isfa;
        var0.isp = var->ispa;
        var0.isu = var->isua;
        var0.size = var->sizea ? var->sizea : 1; // void pointers have sizea==0 and counta==1

        // Go through all elements, calculate real pointer for everyt array element
        for (int i = 0; i < var->counta; i++) {
          int err;
          var0.ptr = (void *)((char *)(*(void **)var->ptr) + var->sizea * i); // love pointer arithmetic :)
          err = convar_value_as_string(&var0,out,sizeof(out));
          q_printf("%%    [%u] = <%c>%s</>,\r\n",
                    i,
                    !err ? 'g' : 'w', // color tag. red on failure, green on success
                    out);
        }
        q_print("% };\r\n");
      } else 
        q_print(", too many to display\r\n%% Use \"var Name[Index]\" to display individual array elements\r\n");
    } else
      q_printf("%% %s <i>%s</> = <g>%s</>;\r\n", convar_typename(var), var->name, out);

    return 0;
  }
  return CMD_FAILED;
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
              "% Variable X name | sizeof(X) |     typeof(X)    |  Value or Address // Hex view</>\r\n"
              "%-----------------+-----------+------------------+------------------------------\r\n");

    while (var) {

      if (convar_value_as_string(var, out, sizeof(out)) < 0)
        out[0] = '\0';

      
      q_printf("%%<i>%16s</> | %9u | %16s | %24s \r\n",
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



// "var ADDRESS VAR_NAME C-TYPE"
// Register new variables in runtime. This one is used to gain write access to an arbitrary memory location
//
static int cmd_var_address(int argc, char **argv) {

  uint8_t *address;
  size_t length;
  char *name;
  int end = 3;
  bool is_float, is_ptr, is_array, is_signed;

  // Min 4 args: "var 0x0 name char"  
  if (argc < 4)
    return CMD_MISSING_ARG;

  // Read association address and check if it is sane
  address = (uint8_t *)hex2uintptr(argv[1]);
  if (!is_valid_address(address, 4)) {
    HELP(q_print("% <e>The address is out of range. Valid range is 0x3f000000..0x7fffffff</>\r\n"));
    return 1;
  }

  // Read CTYPE
  if (end == userinput_read_ctype(argc, argv, end, &length, &is_ptr, &is_array, &is_signed, &is_float, NULL)) {
    HELP(q_print("% A variable type is expected (e.g. \"<i>void * []</>\",  \"<i>char</>\" or \"<i>int []</>\")\r\n"));
    return end;
  }

  // Allocated once but never freed
  name = q_strdup(argv[2], MEM_STATIC); //TODO:
  if (name == NULL)
    return CMD_FAILED;

  

  // Array of pointers is always saved as "void *[]" no matter what real pointer type was:
  // int * [] is the same as void * [] - both are arrays of pointers
  if (is_ptr && is_array) {
    espshell_varadda( name, address, sizeof(void *), read_array_size(argv[argc - 1], 64), 0, 1, 0);
  } else 
  // Simple scalar type
  if (!is_ptr && !is_array) {
    espshell_varadd( name, address, length , is_float, 0, !is_signed);
  } else
  // A pointer
  if (is_ptr) {
    espshell_varaddp( name, address, length, is_float, 1, !is_signed);
  } else {
    // An array. The "[]" part is always the last argv element
    espshell_varadda( name, 
                      address,
                      length,                              // element size
                      read_array_size(argv[argc - 1], 64), // array element count
                      is_float,
                      0,
                      !is_signed);
  }

  return 0;
}



// "var"           -- bypassed to cmd_var_show()
// "var X"         -- bypassed to cmd_var_show()
// "var ADDRESS X CTYPE" -- bypassed to cmd_var_address()
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

  // Associate address with the variable argv[2]
  // e.g "var 0x3fced08c g_ic unsigned int []"
  //
  if (q_isnumeric(argv[1]))
    return cmd_var_address(argc, argv);

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
