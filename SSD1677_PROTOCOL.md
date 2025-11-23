# SSD1677 E-Ink Display Communication Protocol
## GDEQ0426T82 (4.26" 800×480) on Xteink X4

Based on the GxEPD2_426_GDEQ0426T82 driver implementation.

## Hardware Configuration
- **Controller**: SSD1677
- **Display**: 800×480 pixels (100×60 bytes)
- **SPI Pins**: SCLK=8, MOSI=10, CS=21, DC=4, RST=5, BUSY=6
- **SPI Settings**: 10MHz, MSB First, SPI Mode 0

## Low-Level SPI Communication

### Command Format
```
DC=LOW, CS=LOW → Transfer(command_byte) → CS=HIGH, DC=HIGH
```

### Data Format
```
DC=HIGH, CS=LOW → Transfer(data_byte) → CS=HIGH
```

### Bulk Transfer Format
```
CS=LOW → Transfer(byte1) → Transfer(byte2) → ... → CS=HIGH
```

## Reset Sequence

```
RST=HIGH → delay(10ms) → RST=LOW → delay(10ms) → RST=HIGH → delay(10ms)
```

## Initialization Sequence (_InitDisplay)

After reset, wait 10ms, then:

| Step | Command | Data | Description |
|------|---------|------|-------------|
| 1 | `0x12` | - | Software Reset (SWRESET) |
| - | delay | 10ms | Wait for reset |
| 2 | `0x18` | `0x80` | Temperature sensor control |
| 3 | `0x0C` | `0xAE`, `0xC7`, `0xC3`, `0xC0`, `0x80` | Booster soft start configuration |
| 4 | `0x01` | `0xDF`, `0x01`, `0x02` | Driver output control (479 gates = HEIGHT-1) |
| 5 | `0x3C` | `0x01` | Border waveform control |
| 6 | Set RAM area | See below | Configure full screen area |

**Driver Output Control Detail** (Command `0x01`):
- Byte 1: `(HEIGHT - 1) % 256` = `0xDF` (479 & 0xFF)
- Byte 2: `(HEIGHT - 1) / 256` = `0x01` (479 >> 8)
- Byte 3: `0x02` (interlaced/SM mode)

## RAM Area Configuration (_setPartialRamArea)

Sets the window for subsequent RAM writes. Y-coordinates are reversed due to hardware gates orientation.

**For coordinates (x, y, w, h):**

1. Calculate reversed Y: `y_rev = HEIGHT - y - h`

2. **Set RAM Entry Mode** (Command `0x11`):
   - Data: `0x01` (X increment, Y decrement - Y reversed)

3. **Set RAM X Address** (Command `0x44`):
   - Data[0]: `x % 256` (X start LSB)
   - Data[1]: `x / 256` (X start MSB)
   - Data[2]: `(x + w - 1) % 256` (X end LSB)
   - Data[3]: `(x + w - 1) / 256` (X end MSB)

4. **Set RAM Y Address** (Command `0x45`):
   - Data[0]: `(y_rev + h - 1) % 256` (Y start LSB)
   - Data[1]: `(y_rev + h - 1) / 256` (Y start MSB)
   - Data[2]: `y_rev % 256` (Y end LSB)
   - Data[3]: `y_rev / 256` (Y end MSB)

5. **Set RAM X Counter** (Command `0x4E`):
   - Data[0]: `x % 256` (Initial X LSB)
   - Data[1]: `x / 256` (Initial X MSB)

6. **Set RAM Y Counter** (Command `0x4F`):
   - Data[0]: `(y_rev + h - 1) % 256` (Initial Y LSB)
   - Data[1]: `(y_rev + h - 1) / 256` (Initial Y MSB)

## Writing Image Data

### Write to Current Buffer (Command `0x24`)

1. Configure RAM area with `_setPartialRamArea(x, y, w, h)`
2. Send command `0x24`
3. Start bulk transfer (CS=LOW)
4. Transfer image data bytes (one bit per pixel, MSB first)
   - Total bytes = `(w * h) / 8`
   - `0xFF` = white, `0x00` = black
5. End transfer (CS=HIGH)

### Write to Previous Buffer (Command `0x26`)

Same as above but use command `0x26` instead of `0x24`. Used for differential updates.

### Full Screen Clear

1. Write to previous buffer: `_setPartialRamArea(0, 0, 800, 480)` → Command `0x26` → 48000 bytes of `0xFF`
2. Write to current buffer: `_setPartialRamArea(0, 0, 800, 480)` → Command `0x24` → 48000 bytes of `0xFF`
3. Perform full refresh

## Display Update (Refresh)

### Power On (_PowerOn)

| Command | Data | Description |
|---------|------|-------------|
| `0x22` | `0xE0` | Display update control sequence |
| `0x20` | - | Master activation (trigger update) |
| Wait | ~100ms | Wait while BUSY pin is HIGH |

### Full Refresh (_Update_Full)

| Step | Command | Data | Description |
|------|---------|------|-------------|
| 1 | `0x21` | `0x40`, `0x00` | Display update control (bypass RED as 0, single chip) |
| 2a | `0x1A` | `0x5A` | Temperature register (fast mode only) |
| 2b | `0x22` | `0xD7` | Update sequence (fast mode) |
| **OR** | | | |
| 2b | `0x22` | `0xF7` | Update sequence (normal mode, extended temp) |
| 3 | `0x20` | - | Master activation |
| Wait | ~1600ms | Wait while BUSY pin is HIGH |

**Fast vs Normal Mode**: `useFastFullUpdate=true` uses faster refresh but limited temperature range.

### Partial Refresh (_Update_Part)

| Command | Data | Description |
|---------|------|-------------|
| `0x21` | `0x00`, `0x00` | Display update control (RED normal, single chip) |
| `0x22` | `0xFC` | Partial update sequence |
| `0x20` | - | Master activation |
| Wait | ~600ms | Wait while BUSY pin is HIGH |

## Power Management

### Power Off (_PowerOff)

| Command | Data | Description |
|---------|------|-------------|
| `0x22` | `0x83` | Power off sequence |
| `0x20` | - | Master activation |
| Wait | ~200ms | Wait while BUSY pin is HIGH |

### Hibernate (Deep Sleep)

1. Execute Power Off sequence
2. Send command `0x10` (Deep Sleep Mode)
3. Send data `0x01` (Enter deep sleep)

**Wake from Hibernate**: Requires hardware reset via RST pin.

## Complete Write & Display Workflow

### Full Screen Update (Initial or Complete Refresh)

```
1. _InitDisplay() [if not initialized]
2. _setPartialRamArea(0, 0, 800, 480)
3. Write to previous buffer: Command 0x26 + 48000 bytes
4. Write to current buffer: Command 0x24 + 48000 bytes
5. _Update_Full()
```

### Partial Update (Fast Refresh)

```
1. _InitDisplay() [if not initialized]
2. [First time only] Clear screen buffers with full refresh
3. _setPartialRamArea(x, y, w, h)
4. Write image: Command 0x24 + image bytes
5. _Update_Part()
6. Write image again: Command 0x24 + image bytes
7. Write to previous: Command 0x26 + same image bytes
```

**Why write twice?** Partial updates compare current vs previous buffer. Writing to both buffers after refresh prevents ghosting on next update.

## Timing Specifications

| Operation | Duration | Notes |
|-----------|----------|-------|
| Reset pulse | 10ms | Low duration |
| Power on | ~100ms | BUSY signal duration |
| Power off | ~200ms | BUSY signal duration |
| Full refresh | ~1600ms | Normal mode, wait for BUSY |
| Partial refresh | ~600ms | Wait for BUSY |
| Software reset delay | 10ms | After command 0x12 |

## BUSY Signal Monitoring

- **Pin**: GPIO6 (INPUT)
- **Active level**: HIGH
- **Polling**: Read pin until LOW, with timeout protection
- **Timeout**: 10000ms (10 seconds)
- **Usage**: Wait after commands `0x20` (master activation)

## Complete Example: Draw Image at Position

```cpp
// 1. Initialize (once)
_reset();
delay(10);
_InitDisplay();

// 2. Set RAM area for image at (x=100, y=50, w=200, h=100)
_setPartialRamArea(100, 50, 200, 100);

// 3. Write image data to current buffer
_writeCommand(0x24);
_startTransfer();
for (uint32_t i = 0; i < (200 * 100) / 8; i++) {
    _transfer(imageData[i]);
}
_endTransfer();

// 4. Perform partial update
_writeCommand(0x21);
_writeData(0x00);
_writeData(0x00);
_writeCommand(0x22);
_writeData(0xFC);
_writeCommand(0x20);
_waitWhileBusy("Update", 600);

// 5. Write image again to sync buffers
_setPartialRamArea(100, 50, 200, 100);
_writeCommand(0x26);
_startTransfer();
for (uint32_t i = 0; i < (200 * 100) / 8; i++) {
    _transfer(imageData[i]);
}
_endTransfer();
_writeCommand(0x24);
_startTransfer();
for (uint32_t i = 0; i < (200 * 100) / 8; i++) {
    _transfer(imageData[i]);
}
_endTransfer();
```

## Command Reference

| Command | Name | Purpose |
|---------|------|---------|
| `0x01` | Driver Output Control | Set gate scanning (HEIGHT) |
| `0x0C` | Booster Soft Start | Configure boost converter |
| `0x10` | Deep Sleep Mode | Enter low power mode |
| `0x11` | Data Entry Mode | Set X/Y increment direction |
| `0x12` | Software Reset | Reset controller |
| `0x18` | Temperature Sensor | Control temp sensor |
| `0x1A` | Temperature Register | Set temp value (fast mode) |
| `0x20` | Master Activation | Trigger display update |
| `0x21` | Display Update Control | Configure update mode |
| `0x22` | Display Update Sequence | Set update waveform |
| `0x24` | Write RAM (BW) | Write to current buffer |
| `0x26` | Write RAM (RED/OLD) | Write to previous buffer |
| `0x3C` | Border Waveform | Configure border behavior |
| `0x44` | Set RAM X Address | Define X window |
| `0x45` | Set RAM Y Address | Define Y window |
| `0x4E` | Set RAM X Counter | Set initial X position |
| `0x4F` | Set RAM Y Counter | Set initial Y position |

## Notes

- All X coordinates and widths must be multiples of 8 (byte boundaries)
- Y coordinates are reversed in hardware (gates bottom-to-top)
- RAM auto-increments after each byte transfer
- Total RAM size: 48,000 bytes (800×480 ÷ 8)
- Dual-buffer system enables differential partial updates
- First write after init should be full refresh to clear ghost images
