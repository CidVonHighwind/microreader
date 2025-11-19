# SSD1677 Command Reference

Complete list of commands for the Solomon Systech SSD1677 E-Paper Display Driver.

## 1. Driver / Power / Reset Commands
- **0x01 — Driver Output Control**  
- **0x03 — Gate Driving Voltage Control**  
- **0x04 — Source Driving Voltage Control**  
- **0x08 — Initial Code Setting (OTP Program)**  
- **0x09 — Write Register for Initial Code Setting**  
- **0x0A — Read Register for Initial Code Setting**  
- **0x0C — Booster Soft-Start Control**  
- **0x10 — Deep Sleep Mode**  
- **0x12 — Software Reset**

## 2. Data Entry & Addressing
- **0x11 — Data Entry Mode Setting**  
- **0x44 — Set RAM X Address Start/End**  
- **0x45 — Set RAM Y Address Start/End**  
- **0x4E — Set RAM X Address Counter**  
- **0x4F — Set RAM Y Address Counter**

## 3. Status / Detection / Sensors
- **0x14 — HV Ready Detection**  
- **0x15 — VCI Detection**  
- **0x18 — Temperature Sensor Control**  
- **0x1A — Write Temperature Register**  
- **0x1B — Read Temperature Register**  
- **0x1C — External Temperature Sensor Command**

## 4. Display Update Process
- **0x20 — Master Activation**  
- **0x21 — Display Update Control 1**  
- **0x22 — Display Update Control 2**

## 5. RAM Read/Write
- **0x24 — Write RAM (BW)**  
- **0x25 — Write RAM (Dithering)**  
- **0x26 — Write RAM (RED)**  
- **0x27 — Read RAM**  
- **0x41 — RAM Read Select**  
- **0x46 — Auto Write BW RAM**  
- **0x47 — Auto Write RED RAM**

## 6. VCOM Control
- **0x28 — VCOM Sense**  
- **0x29 — VCOM Sense Duration**  
- **0x2A — Program VCOM OTP**  
- **0x2B — Write VCOM Control Registers**  
- **0x2C — Write VCOM Register**  
- **0x2D — OTP Register Read (Display Option)**

## 7. LUT / OTP Programming
- **0x30–0x33 — LUT / Waveform Data**  
- **0x36 — Program Waveform OTP**  
- **0x37 — Write Display Option**  
- **0x38 — Write Temperature Range**  
- **0x39 — Program Temperature Range OTP**  
- **0x3A — Program Display Options**

## 8. Border, Misc, Diagnostics
- **0x3C — Border Waveform Control**  
- **0x43 — Panel Break Detection**  
- **0x2F — Status Bit Read**  
- **0x7F — NOP**

## 9. Summary Table
(Condensed list of all command hex codes)
