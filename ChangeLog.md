## Изменения с версии 0.99.8  

### Документация

- Раздел "Основы" дополнен подробной инструкцией по установке.  
- В разделе "Файловая система" добавлена поддержка путей с пробелами в кавычках.  
- В разделе "GPIO" добавлено ключевое слово `toggle` для команды `pin`.  
- Незначительные правки в файлах `License.txt` и `README.md`.  
- Добавлены русские переводы: `License.ru.txt` и `README.ru.md`.

### Код

- Конец строки (CR, LF или CR+LF) пользовательского терминала определяется автоматически  
- Теперь оболочка поддерживает аргументы в кавычках — всё, что заключено в кавычки, рассматривается как один аргумент.  
- В связи с этим добавлена новая escape-последовательность `\"` (экранированная кавычка), которая теперь корректно обрабатывается.  
- Строка, начинающаяся с символа `@`, отключает эхо вывода. Нажатие `<Enter>` включает эхо обратно.  
- Исправлена работа команды `show memory ADDRESS char`:  
  - `char` и `signed char` теперь отображаются в виде десятичной таблицы.  
  - `unsigned char` отображается как обычный шестнадцатеричный дамп.  
- Команды `uptime` и `show cpuid` теперь выводят более подробную информацию о перезагрузке — как общую, так и по ядрам.  
- Команда `tty` без аргументов показывает текущее устройство ввода-вывода, используемое ESPShell.  
- Исправлена ошибка в `MUST_NOT_HAPPEN()`, которая могла приводить к некорректному удалению задачи.  
- При запуске оболочки теперь автоматически выполняется `pin save` для всех пинов.  
- В команду `pin` добавлено новое ключевое слово: `pin ... toggle`, позволяющее инвертировать состояние пина.  
- Теперь принимаются числа с заглавными `X` и `B`: например, `0XAAA1` или `0B1001011`.  
- Ввод вида `123randomgarbage` теперь считается допустимым: разбор числа прекращается на первом недопустимом символе, и используется корректная числовая часть (в данном случае `123`).  
- Улучшено поведение команды `?`: теперь корректно отображаются заголовки разделов, если команды принадлежат к разным секциям (например, `? c` покажет и `count`, и `cpu`).

### Прочее

- Постепенно улучшается качество английских сообщений ESPShell.

---

## Changes since 0.99.8

### Documentation

- Updated "Basics" section with detailed installation instructions.  
- Filesystem section: support for quoted paths (paths containing spaces).  
- GPIO section: added `"toggle"` keyword to the `pin` command.  
- Minor updates to `License.txt` and `README.md`.  
- Added Russian translations: `License.ru.txt` and `README.ru.md`.

### Code

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
