## Changes since 0.99.10 (latest is 0.99.12)

### New features
- HostID has been added (autosaved),  new command "hostid"
- NVS editor: Access NVS, read/create/delete/set key-value pairs, export and import, new command directory "nvs"
- WiFi support: Access Point, Station, Acesspoint+Station modes. DHCP client and server. NAT/P, NTP.
  - new command directory "wifi"
  - new command "wifi log enable|disable"
  - new command "wifi storage flash | ram"
- Time: commands to set/view current time added; timezone support; NTP can update local time
  - new command "time zone TIME_OFFSET"
  - new command "time" (and "show time")
  - new command "time set TEXT"
  - new command "show time"
- Sleep: new command added to set up alarm(s). Deep sleep support added
  - new command "nap alarm"
  - new command "nap deep"
  - new command "show nap"
- Pulse generator: pulse sequences can be saved to a text file and later loaded with "exec"
  - "tail" and "head" were added to the sequence configuration
  - new command "head"
  - new command "tail"
  - new command "save"
  - new command "loop"

### Changes/Fixes/Updates
- CLI: the "&" keyword (background execution) now accept CPU Core specifier along with Priority:
  - "pin 2 low high loop inf &0.1" --> run command with Priority=0 on CPU#1
- Internals: few mutexes were eliminated by using "lockless" approach (_Atomic)
  - Context is now uintptr_t
  - On SoCs with USB-CDC console espshell runs on the same core Arduino runs on (workaround for buggy USB-CDC)
- UART: better error checking on input parameters
  - new command added "show uart NUM" to display UART configuration
  - new command added to the "uart" directory : "show" to display UART configuration
- Tasks: Command "show tasks" now shows the CPU core
- Bugfixes: looped background "pin" command without delays failed to stop via "kill" (had to use "kill -9")
- Help system: "?" searches current AND main command directories


## Changes since 0.99.8

### Documentation
- big change: added /alias/, /exec/, /if/ and /every/ commands
- Updated "Basics" section with detailed installation instructions.  
- Filesystem section: support for quoted paths (paths containing spaces).  
- GPIO section: added `"toggle"` keyword to the `pin` command.  
- Minor updates to `License.txt` and `README.md`.  
- Added Russian translations: `License.ru.txt` and `README.ru.md`.
-
### Code
- new commands: show if, show alias
- crash fixed: entering <esc>0<esc> caused access violation exception
- "delay 0" is allowed in the "pin" command 
- task manipulation commands now accept task names, not only task ID's
- command "show tasks" now displays all tasks, including system ones
- command /priority/ to change tasks priority. command priority is inherited now
- command /echo/ now accept TEXT and -n option to stay on the same line
- & symbol now accepts the task priority: &10
- big change: added /alias/, /exec/, /if/ and /every/ commands
- The end-of-line sequence (CR, LF, or CR+LF) of the user terminal is detected automatically.  
- The shell now supports quoted arguments — everything enclosed in quotes is treated as a single argument.  
- As a result, a new escape sequence `\"` (escaped quote) has been introduced and is properly recognized.  
- Lines starting with `@` disable shell echo; pressing `<Enter>` re-enables it.  
- Fixed behavior of `show memory ADDRESS char`:  
  - `char` and `signed char` now produce a decimal table.  
  - `unsigned char` results in a regular hex dump.  
- `uptime` and `show cpuid` now display more detailed reboot information, both overall and per core.  
- `tty` without arguments now shows the current I/O device used by espshell.  
- Fixed a bug in `MUST_NOT_HAPPEN()` which previously led to incorrect task deletion.  
- On shell startup, `pin save` is now automatically executed for all pins.  
- New `pin` command keyword: `pin ... toggle` — inverts the pin's state.  
- Numbers with uppercase `X` or `B` are now valid: e.g., `0XAAA1` or `0B1001011`.  
- Inputs like `123randomgarbage` are now accepted: parsing stops at the first invalid character and uses the valid numeric prefix (`123` in this case).  
- Improved behavior of the `?` command: it now displays section headers correctly when commands belong to multiple sections (e.g., `? c` lists both `count`, `cpu`, etc.).


### Other

- Gradually improving the quality of English in ESPShell messages.
