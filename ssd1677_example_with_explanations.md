# SSD1677 Example Code with Explanations

This document provides example initialization and display update code for the SSD1677 e-paper driver, including detailed explanations for each command.

---

# Initialization Sequence (Minimal)

```c
void ssd1677_init() {
    // 1. Software Reset
    epd_cmd(0x12);        // SWRESET
    epd_wait_busy();      // Wait for busy to clear

    // 2. Driver Output Control
    epd_cmd(0x01);
    epd_data(0xA7);       // 680 rows -> 0x2A7, low byte
    epd_data(0x02);       // high byte
    epd_data(0x00);       // GD=0, SM=0, TB=0

    // 3. Data Entry Mode
    epd_cmd(0x11);
    epd_data(0x03);       // X+, Y+

    // 4. RAM X Start/End
    epd_cmd(0x44);
    epd_data(0x00);       // X start = 0
    epd_data(0x3B);       // X end = 959 / 8 = 0x3B

    // 5. RAM Y Start/End
    epd_cmd(0x45);
    epd_data(0x00);       // Y start (low byte)
    epd_data(0x00);       // Y start (high byte)
    epd_data(0xA7);       // Y end (low byte)
    epd_data(0x02);       // Y end (high byte)

    // 6. Border Control
    epd_cmd(0x3C);
    epd_data(0xC0);       // Border = Hi-Z

    // 7. Temperature Sensor (internal)
    epd_cmd(0x18);
    epd_data(0x80);
}
```

---

# Explanation of Each Step

### **Software Reset (0x12)**
Resets the internal registers (except deep sleep). Mandatory after power-up.

### **Driver Output Control (0x01)**
Sets display height and scan direction.  
`0x2A7 = 680 lines - 1`

### **Data Entry Mode (0x11)**
Controls RAM addressing:  
`0x03 = X increment, Y increment`.

### **Set RAM Window (0x44 & 0x45)**
Defines the region written during RAM writes.  
- For full 960×680 screen, X=0..0x3B, Y=0..0x2A7.

### **Border Waveform (0x3C)**
Controls VBD (border pixel behavior).  
`0xC0 = Hi-Z`, common default.

### **Temperature Sensor (0x18)**
`0x80 = use internal sensor`.

---

# Writing Image Data

```c
void ssd1677_write_bw(uint8_t *buffer, uint32_t size) {
    // Set RAM Address Counters
    epd_cmd(0x4E);     // X counter
    epd_data(0x00);
    epd_cmd(0x4F);     // Y counter
    epd_data(0x00);
    epd_data(0x00);

    // Write BW RAM
    epd_cmd(0x24);
    for (uint32_t i = 0; i < size; i++)
        epd_data(buffer[i]);
}
```

### Explanation
- **0x4E / 0x4F** set starting address for RAM.
- **0x24** selects the BW image buffer.

---

# Display Update Sequence

```c
void ssd1677_update() {
    epd_cmd(0x22);
    epd_data(0xC7);   // Display mode: load LUT + refresh + power off

    epd_cmd(0x20);    // Master activation
    epd_wait_busy();  // Wait for driving waves to complete
}
```

### Explanation
- **0x22 / 0xC7** tells SSD1677 which tasks to run (enable analog, load LUT, drive display).
- **0x20** starts the entire update cycle.
- **epd_wait_busy()** waits until the driver finishes waveform driving.

---

# Full Frame Example

```c
void ssd1677_display_frame(uint8_t *bw, uint8_t *red) {
    ssd1677_write_bw(bw, BW_BUFFER_SIZE);

    epd_cmd(0x26);      // Write RED RAM
    for (int i = 0; i < RED_BUFFER_SIZE; i++)
        epd_data(red[i]);

    ssd1677_update();
}
```

---

# Minimal Usage Example

```c
ssd1677_init();

ssd1677_display_frame(bw_image, red_image);
```

---

# Notes
- BUSY pin *must* be polled after reset and update.
- All RAM writes auto-increment based on data entry mode.
- SSD1677 can display BW-only or RED-only if desired.

---

# Want more?
I can also generate:
- A **C driver file (`ssd1677.c`)**
- A **header file (`ssd1677.h`)**
- Examples for Arduino, ESP32, STM32, RP2040
