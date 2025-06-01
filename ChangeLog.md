## ��������� � ������ 0.99.8  

### ������������

- ������ "������" �������� ��������� ����������� �� ���������.  
- � ������� "�������� �������" ��������� ��������� ����� � ��������� � ��������.  
- � ������� "GPIO" ��������� �������� ����� `toggle` ��� ������� `pin`.  
- �������������� ������ � ������ `License.txt` � `README.md`.  
- ��������� ������� ��������: `License.ru.txt` � `README.ru.md`.

### ���

- ������ �������� ������������ ��������� � �������� � ��, ��� ��������� � �������, ��������������� ��� ���� ��������.  
- � ����� � ���� ��������� ����� escape-������������������ `\"` (�������������� �������), ������� ������ ��������� ��������������.  
- ������, ������������ � ������� `@`, ��������� ��� ������. ������� `<Enter>` �������� ��� �������.  
- ���������� ������ ������� `show memory ADDRESS char`:  
  - `char` � `signed char` ������ ������������ � ���� ���������� �������.  
  - `unsigned char` ������������ ��� ������� ����������������� ����.  
- ������� `uptime` � `show cpuid` ������ ������� ����� ��������� ���������� � ������������ � ��� �����, ��� � �� �����.  
- ������� `tty` ��� ���������� ���������� ������� ���������� �����-������, ������������ ESPShell.  
- ���������� ������ � `MUST_NOT_HAPPEN()`, ������� ����� ��������� � ������������� �������� ������.  
- ��� ������� �������� ������ ������������� ����������� `pin save` ��� ���� �����.  
- � ������� `pin` ��������� ����� �������� �����: `pin ... toggle`, ����������� ������������� ��������� ����.  
- ������ ����������� ����� � ���������� `X` � `B`: ��������, `0XAAA1` ��� `0B1001011`.  
- ���� ���� `123randomgarbage` ������ ��������� ����������: ������ ����� ������������ �� ������ ������������ �������, � ������������ ���������� �������� ����� (� ������ ������ `123`).  
- �������� ��������� ������� `?`: ������ ��������� ������������ ��������� ��������, ���� ������� ����������� � ������ ������� (��������, `? c` ������� � `count`, � `cpu`).

### ������

- ���������� ���������� �������� ���������� ��������� ESPShell.

---

## Changes since 0.99.8

### Documentation

- Updated "Basics" section with detailed installation instructions.  
- Filesystem section: support for quoted paths (paths containing spaces).  
- GPIO section: added `"toggle"` keyword to the `pin` command.  
- Minor updates to `License.txt` and `README.md`.  
- Added Russian translations: `License.ru.txt` and `README.ru.md`.

### Code

- The shell now supports quoted arguments � everything enclosed in quotes is treated as a single argument.  
- As a result, a new escape sequence `\"` (escaped quote) has been introduced and is properly recognized.  
- Lines starting with `@` disable shell echo; pressing `<Enter>` re-enables it.  
- Fixed behavior of `show memory ADDRESS char`:  
  - `char` and `signed char` now produce a decimal table.  
  - `unsigned char` results in a regular hex dump.  
- `uptime` and `show cpuid` now display more detailed reboot information, both overall and per core.  
- `tty` without arguments now shows the current I/O device used by espshell.  
- Fixed a bug in `MUST_NOT_HAPPEN()` which previously led to incorrect task deletion.  
- On shell startup, `pin save` is now automatically executed for all pins.  
- New `pin` command keyword: `pin ... toggle` � inverts the pin's state.  
- Numbers with uppercase `X` or `B` are now valid: e.g., `0XAAA1` or `0B1001011`.  
- Inputs like `123randomgarbage` are now accepted: parsing stops at the first invalid character and uses the valid numeric prefix (`123` in this case).  
- Improved behavior of the `?` command: it now displays section headers correctly when commands belong to multiple sections (e.g., `? c` lists both `count`, `cpu`, etc.).

### Other

- Gradually improving the quality of English in ESPShell messages.
