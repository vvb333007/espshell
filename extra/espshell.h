#ifndef espshell_h
#define espshell_h

//Register global or static variable to be accessible from espshell:
//
// int my_var;
//
// void setup() {
//   ...
//   espshell_var(my_var);
// }
//
// Registered variables can be accessed by "var" command:
//
// esp32#>var my_var 666       <--- set variable
// esp32#>var my_var           <--- display variable
#if 1
#  define convar_add( VAR ) espshell_varadd( #VAR, &VAR, sizeof(VAR))
#else
#  define convar_add( VAR )
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Execute an arbitrary shell command (\n are allowed for multiline).
// it is an async call: it returns immediately. one can use espshell_exec_finished()
// to check if actual shell command has finished its execution. Data pointed by "p"
// must remain allocated until command execution finishes
void espshell_exec(const char *p);

// check if last espshell_exec() call has completed its
// execution
bool espshell_exec_finished();


// DONT USE THIS! use convar_add() instead
void espshell_varadd(const char *name, void *ptr, int size);

// By default ESPShell occupies UART0. It could be changed
// at compile time by setting #define USE_UART in espshell.c
// to required value OR at runtime by calling console_attach2port()
// 
// Special value 99 means USB console port for boards with USB-OTG
// support
int console_attach2port(int i);


// classic digitalRead() undergoes PeriMan checks which do not allow to
// read data for pins which are NOT configured as GPIO: for example uart pins
// or i2c pins.
//
// digitalForceRead() is able to read any pin no matter what. 
// for OUTPUT pins it enables INPUT automatically.
int digitalForceRead(int pin);

// same as pinMode() but calls IDF directly bypassing
// PeriMan's pin deinit/init. As a result it allows flags manipulation on
// reserved pins without crashing & rebooting. And it is much faster than 
// classic pinMode()
void pinMode2(unsigned int pin, unsigned int flags);

#ifdef __cplusplus
};
#endif
                      
#endif //espshell_h
