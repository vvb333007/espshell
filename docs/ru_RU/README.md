ESP32Shell for the Arduino Framework by vvb333007 <vvb@nym.hush.com>


!!!! ������������ ��������. ���������� ������ �����-����� ��������� � �����������. !!!

��� ��� �����:
-------------

 ��� ���������� ��� Arduino IDE, ����������, ���������� ���������� � ������� �
 ����������\������������� ���������� ��� ���������� ��������� ����� ��������� 
 ��� ���� �� ������ ESP32. 

 ������������ ����� ��������������� ����, ��������, ������� �� ��������� �������� 
 linux, ������� ���������� ��������� ���������� �������. ������� �������� ������ 
 � uart, i2c, ������, � ������� ����������� ������������ esp32

 ������������ ������ � ����� �������, ����������� ����������� ������, � 
 ������������� ��������� ��������� ������ �� Serial (uart0). ESPShell �� 
 ������������ OSB-OTG �����, � ������� Serial ������ ����� USB-CDC.

 ESPShell ����������� �������������. ������������ �������, ������� ����� ���������, 
 ��� �������� ���� espshell.h ���-������ � ���� ������. ��� ���������� ������ 
 ���������� Arduino IDE ���� ������������� ����������� � ������ ������� � ������� 
 ���������� ����� ������� ������ ������.

 ������ ArduinoIDE Serial Monitor ��� �������� TeraTerm (PyTTY) ����� �������� � 
 ����� ������� ���������� �����. �� ���������� � �������� � ������������� ��������
 ���������������� ��������� ArduinoIDE Serial Monitor � TeraTerm. ������� ��������, 
 ��� ������������� TeraTerm ����������������, �.�. ��� � ��� ����� �������� ������ 
 CTRL+C � CTRL+Z, ��� ������ �������, ����� �� ����������� ������ � GSM ��������


 
� ���� ���� �����, � ���� �������� ���� ESPshell:
-------------------------------------------------

���������� �������� ������� 

#include "espshell.h"

����-������ � ������ ������ ������. ���������� ���� �������������� � � ������
������ ���������� ����������, ����� �����������. �������� � ������ ����� � �
Arduino Serial Monitor, �� �����, ���-��, ������������ ����������� �����������
����� TeraTerm ��� PuTTY
 

� ������� ��� �����?
--------------------

** ���������� �������� **

 ������ RAM, ������� ���������� ESPShell ��� ���� ����� �������: ��� ����� 10�����, ��
 ������� �������� - ��� ���� ������ ESPShell, � ��������� - ���������� ����������. 
 ������ �����, �������, ����� ������������ (��. ����� STACKSIZE � espshell.c). ������
 ������������ RAM ��� �� ����� ��� ��������� �������� ������� ������ (��. ����� 
 WITH_HISTORY), �������� ����� SEQUENCES_NUM, MOUNTPOINTS_NUM ��� ����� �������� ���������
 �������� ������ (WITH_FS, WITH_FAT, WITH_SPIFFS, WITH_LITTLEFS)

 ������� � ������ ���� ����� 60 �������� (�.�. ������ �������� ���������� �� ��� ��������)
 ��������� ��������� ��� �������� �����, �������� ������� ���������� ������ (��. �����
 WITH_HELP � espshell.c). � ����������� �������� ������, ������ ���� ����������� ��, ��������,
 2������, � ������ ������ (������ ������� ������) - �������� �� 18�����, ����� ��������
 �������� ����� 20�����.


� � ������ ������� ����� ��������?
----------------------------------

��. 
����������, � ����������� ����� ������ example_standalone_shell.ino - ��� ��� ��� � ���� ������
�����, ������������ �������������� �������� - ��������� ����

