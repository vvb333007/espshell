
1. Installation (Preinstalled Arduino IDE with esp32 board support package from Espressif is expected):

    1. Copy library folder ("espshell") as is to your <SketchDirectory>/libraries/
    2. Restart Arduino IDE

2. Usage: 

    1. Add #include "espshell.h" to your sketch
    2. Compile, upload
    3. Open terminal monitor, type "?" and press <Enter>

3. Documentation (a bit outdated here and there):

    1. English (most up to date) is in "espshell/docs/"
    2. Russian (is up to date when I have time) is in "espshell/docs/ru_RU/"

4. Development:

    ESPShell code is written in pure C, developer friendly and well-commented. There are few areas which calls for
    attention and they are can be found as TODO: throughout the code. There are docs/PROBLEMS.txt and docs/PLANS.txt
    for further reading

