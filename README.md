
Этот текст, но на русском, [находится тут](https://github.com/vvb333007/espshell/blob/main/README.ru.md)

Лицензионное соглашение на русском, [находится тут](https://github.com/vvb333007/espshell/blob/main/LICENSE.ru.txt)

# WHAT IS THIS

This is a debugging and development tool (a library for the Arduino framework) intended for use with **ESP32 hardware**.

It provides a command-line interface (CLI) over the serial port (UART or USB), running in parallel with your Arduino sketch. This is **not** a standalone program — the tool integrates into your sketch at compile time and enhances any sketch (even an empty one) with a shell. ESPShell also allows you to pause and resume sketch execution.

Users can enter and execute commands (including many built-in ones) in a manner similar to a Linux shell, all while their sketch continues to run. ESPShell works with the Arduino IDE Serial Monitor or any other terminal software like *PuTTY* or *Tera Term*. Linux users have a wide variety of terminal options — even the `cu` utility will do the job.

This library is useful for:

1. **Developers** working with I2C or UART devices. ESPShell includes commands to create/delete hardware interfaces, send/receive data, etc.  
   Examples:
   - Interfacing UART-based GPS chips or GSM modems  
   - Building libraries for I2C devices  
   - Testing/debugging boards that control multiple relays with a single command  
   - Viewing or modifying sketch variables  
   - Changing pin or interface parameters (e.g., UART speed or pin pull-up/down mode) on the fly — no more constant compile/upload cycles

2. **Beginners** who want to experiment with hardware without writing any code

3. **Arduino-compatible board manufacturers**. A pre-installed shell can be useful for:
   - Production testing  
   - Allowing users to interact with hardware without any programming  
   - Running demos like blinking LEDs or toggling relays via ESPShell commands

---

# HOW TO INSTALL

This library is available via the **Arduino Library Manager**:

1. Open the **Library Manager**
2. Search for `espshell`
3. Choose the latest version and click **Install**

**To install manually** (e.g., the latest source code from GitHub):

1. Create the folder: `/YourSketchBook/libraries/espshell`  
2. Copy the library contents (`/docs`, `/src`, `/examples`, etc.) into that folder  
3. Restart the Arduino IDE

---

# HOW TO USE IT IN MY PROJECT?

1. Add `#include "espshell.h"` to your sketch  
2. Compile and upload as usual  
3. Open the Serial Monitor, type `?`, and press **Enter**

> **NOTE:** The Serial Monitor is not ideal. It’s recommended to use dedicated terminal software like **Tera Term**.

---

# CAN I EXECUTE SHELL COMMANDS DIRECTLY FROM MY SKETCH?

Yes. Use the function **`espshell_exec()`**, which executes arbitrary shell command(s).  
Multiple commands can be passed at once, separated by newline characters.  
This function is asynchronous and returns immediately.

To check whether the command has finished executing, use **`espshell_exec_finished()`**.

---

# WHAT IS ESPSHELL'S MEMORY/CPU FOOTPRINT?

The latest GitHub version I checked had the following overhead:

- **Code size (.text):** +100 KB (Flash)  
- **Data size (.data + .bss):** +2.5 KB (DRAM)  
- **IRAM code (.iram):** -512 B (IRAM)  
- Some small code sections are marked with `IRAM_ATTR` (stored in IRAM permanently)

ESPShell runs on the other core (on multicore systems) to minimize interference with the main sketch.

Can we lower ESPShell's footprint even more?  [Sure!](https://vvb333007.github.io/espshell/html/Customizing.html)

---

# DOCUMENTATION

- English:  
  [ESPShell Documentation (EN)](https://vvb333007.github.io/espshell/html/index.html)

- На русском:  
  [ESPShell Documentation (RU)](https://vvb333007.github.io/espshell/html/index.ru.html)

---

# ESPSHELL DEVELOPMENT

ESPShell is written in pure C. It’s developer-friendly and well-commented.

Some areas are marked with **`TODO:`** in the source code. Additional information is available in:

- `docs/PLANS.txt`

The code is generally simple and linear. Non-obvious parts are well documented.  
Please note that the author is not a native English speaker, so some in-code comments may contain minor language mistakes.

[![GitHub Downloads (all assets, all releases)](https://img.shields.io/github/downloads/x-radio/EEBoom/total?color=66FFFF)](https://github.com/vvb333007/espshell/)
