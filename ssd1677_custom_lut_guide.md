# SSD1677 Custom LUT Guide
Complete reference for **creating**, **loading** and **applying** a custom LUT on the SSD1677 E‑Paper Display Driver.

---

# 📘 1. What a LUT Is

The SSD1677 uses a **Look-Up Table (LUT)** to control **pixel waveform driving** during updates.  
Each pixel (BW/RED) needs a sequence of voltage phases to switch states correctly.

A LUT controls:
- Voltage level per phase (VSH1, VSH2, VSL, Hi‑Z)
- VCOM toggling pattern
- Duration of each phase (TP0–TP7)
- Phase repetitions
- Additional red-pixel handling

The LUT is **34 bytes long**, labeled **WS0–WS33**.

---

# 📐 2. LUT Structure Overview

| WS Range | Purpose |
|----------|---------|
| WS0–WS7 | Source voltage phase control |
| WS8–WS14 | VCOM waveform control |
| WS15–WS23 | Phase timing (durations) |
| WS24–WS33 | Repeat counts, misc red/clean phases |

Each entry is exactly **1 byte**.

---

# 🧱 3. How to Build a Custom LUT

## Step 1 — Define Source Voltage Waveform (WS0–WS7)
You choose for each phase:
- VSH1 (medium positive)
- VSH2 (strong positive — drives white)
- VSL (strong negative — drives black)
- Hi‑Z (float)

These define **pixel movement direction** and strength.

---

## Step 2 — Define VCOM Waveform (WS8–WS14)
VCOM biases the entire display.  
These bytes define:
- On/off toggling per phase  
- Matching with source driver phases  
- Ghost reduction  

---

## Step 3 — Phase Timing TP0–TP7 (WS15–WS23)
Each TPx sets duration of a phase.
Longer = cleaner image, slower refresh.  
Shorter = faster, but potential ghosting.

---

## Step 4 — Repeat Counts & Finalization (WS24–WS33)
These adjust:
- How many times each phase repeats  
- Red pigment extra driving  
- Cleanup phases  

---

# 🔧 4. How to Load a Custom LUT into the SSD1677

A custom LUT is written using **Command 0x32**:

```
CMD 0x32
DATA WS0
DATA WS1
...
DATA WS33
```

All **34 bytes** must be written sequentially.

---

# 🚀 5. How to Apply (Use) the Custom LUT

After loading the LUT, tell the display to **use it**.

### 1. Configure Display Update Mode (0x22)
Typical value enabling LUT usage:
```
CMD 0x22
DATA 0xF7
```

### 2. Start Master Activation (0x20)
```
CMD 0x20
WAIT BUSY = LOW
```

While BUSY is high, the LUT waveform is driving the display.

---

# 🧪 7. Example Code

```c
// Load LUT
void ssd1677_load_lut(const uint8_t* lut) {
    epd_cmd(0x32);
    for (int i = 0; i < 34; i++)
        epd_data(lut[i]);
}

// Apply LUT
void ssd1677_apply_lut() {
    epd_cmd(0x22);
    epd_data(0xF7);   // Use LUT
    epd_cmd(0x20);    // Master Activation
    epd_wait_busy();
}
```

---

# 📝 8. Recommended Workflow for Designing LUTs

1. Begin with a working reference LUT from your panel vendor  
2. Tweak:
   - voltage steps (WS0–WS7)
   - VCOM phases (WS8–WS14)
   - timing (WS15–WS23)
   - repeat cycles (WS24–WS33)
3. Test transitions:
   - White ↔ Black
   - Red ↔ White
4. Test ghosting levels  
5. Optimize speed vs quality  
6. Burn to OTP only once fully validated  

---

# ✔️ 9. Summary

**Build a custom LUT**
- Create 34 bytes defining waveform phases, timing, and VCOM

**Use a custom LUT**
1. Write with **0x32**
2. Enable with **0x22**
3. Trigger with **0x20**

**Optional**
- Burn to OTP with **0x36**

This file contains everything you need to design, load, apply, and optionally store custom LUTs on the SSD1677.

