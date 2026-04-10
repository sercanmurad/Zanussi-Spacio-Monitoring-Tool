# Zanussi-Spacio-Monitoring-Tool

============================================================
  Zanussi Spacio C6 — IoT Monitor with Telegram Bot
  Hardware : ESP32-S3 DevKit + YP-05 (FT232RL) + HW-221 (TXS0108E) + MP1584EN
  RS232     : Port 15 on SM1 board — 9600 8N1, no handshaking
  Version   : 1.0
 ============================================================
 Hardware Architecture
1  MP1584EN  IN+  ←  +24V DC from machine (through 1A fuse)
2  MP1584EN  OUT+ →  ESP32 VIN  (set to exactly 5.0V first!)
3  YP-05     VCC  ←  ESP32 3V3  (jumper on YP-05 must be at 3.3V!)
4  YP-05     TXD  →  ESP32 GPIO16  (UART2 RX)
5  YP-05     RXD  ←  ESP32 GPIO17  (UART2 TX)
6  YP-05 DB9 Pin2 ←  Machine Port-15 DB9 Pin3 (TXD)
7  YP-05 DB9 Pin3 →  Machine Port-15 DB9 Pin2 (RXD)
8  YP-05 DB9 Pin5 ↔  Machine Port-15 DB9 Pin5 (GND)
9  HW-221    VCCA ←  ESP32 3V3
10  HW-221    VCCB ←  MP1584EN 5V
11  HW-221    OE   ←  ESP32 3V3  (always enabled)
12  HW-221    B1   ←  LED1 anode on SM1  (5V presence)
13  HW-221    B2   ←  LED2 anode on SM1  (24V presence)
14  HW-221    B3   ←  LED3 anode on SM1  (CPU blink)
15  HW-221    B4   ←  LED5 anode on SM1  (motor active)
16  HW-221    B5   ←  LED6 anode on SM1  (motor overcurrent ALARM)
17  HW-221    A1   →  ESP32 GPIO32
18  HW-221    A2   →  ESP32 GPIO33
19  HW-221    A3   →  ESP32 GPIO4   + 10kΩ pull-down to GND
20  HW-221    A4   →  ESP32 GPIO5   + 10kΩ pull-down to GND
21  HW-221    A5   →  ESP32 GPIO6   + 10kΩ pull-down to GND

  HOW RS232 DATA WORKS
   The machine sends printer-format ASCII text over RS232 whenever:
   • A drink is dispensed (sale record line)
   • An error or alarm occurs (error line)
   • Statistics are printed (open door → press key 0 → print)

