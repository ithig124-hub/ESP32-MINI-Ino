# S3 MiniOS v3.1 - ESP32-S3-Touch-AMOLED-2.06 Edition

**Adapted from 1.8" version for the 2.06" AMOLED display board**

---

## 🎯 What's New in 2.06" Version

This is a complete port of S3 MiniOS v3.1 from the ESP32-S3-Touch-AMOLED-1.8 to the **ESP32-S3-Touch-AMOLED-2.06** board.

### Hardware Changes

| Component | 1.8" Board | 2.06" Board |
|-----------|------------|-------------|
| **Display Controller** | SH8601 | CO5300 |
| **Resolution** | 368×448 | 410×502 |
| **Touch INT Pin** | GPIO 21 | GPIO 38 |
| **LCD Reset** | Via XCA9554 (P1) | Direct GPIO 8 |
| **Touch Reset** | Via XCA9554 (P0) | Direct GPIO 9 |
| **I/O Expander** | XCA9554 (0x20) | **None** |
| **I2S BCK** | GPIO 9 | GPIO 41 |
| **I2S DI** | GPIO 8 | GPIO 40 |
| **I2S DO** | GPIO 10 | GPIO 42 |
| **SD Card CS** | Not defined | GPIO 17 |

---

## ✨ Features (All Preserved from v3.1)

### 🔋 Battery Intelligence
- **Sleep mode tracking** - 24-hour screen time history
- **ML-style estimation** - Simple, weighted, and learned battery estimates
- **Battery Stats sub-card** - Graphs and usage breakdown
- **Low battery warnings** - Popup at 20% and 10%
- **Battery Saver mode** - Auto-enable at 10% or manual toggle
- **Charging animation** - Animated battery icon

### 📱 Main Cards (Swipe Left/Right)
1. **Clock** - Time, date, location
2. **Steps** - Daily steps with goal tracking
3. **Music** - Playback controls
4. **Games** - Clicker game
5. **Weather** - Live weather data
6. **System** - Battery, WiFi, stats

### 📊 System Sub-Cards (Swipe Down from System)
1. **Battery Stats** - 24h screen time graph, estimates, card usage
2. **Usage Patterns** - Weekly screen time, drain analysis
3. **Factory Reset** - Clear all data

### 📶 Multi-WiFi Support
- Configure up to 5 WiFi networks on SD card
- Auto-connects to available networks in order
- Auto-reconnect on disconnect
- WiFi configuration from `/wifi/config.txt`

---

## 🔧 Technical Details

### Display Initialization
```cpp
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT,
    22, 0, 0, 0);  // col_offset1, row_offset1, col_offset2, row_offset2
```

### Direct Reset Control
The 2.06" board does **not** have the XCA9554 I/O expander. LCD and Touch resets are controlled directly via GPIO:

```cpp
pinMode(LCD_RESET, OUTPUT);   // GPIO 8
pinMode(TP_RESET, OUTPUT);    // GPIO 9

// Reset sequence
digitalWrite(LCD_RESET, LOW);
digitalWrite(TP_RESET, LOW);
delay(20);
digitalWrite(LCD_RESET, HIGH);
digitalWrite(TP_RESET, HIGH);
delay(50);
```

### Pin Configuration
All pin definitions are in `pin_config.h`. Key changes:
- `TP_INT` changed from GPIO 21 to GPIO 38
- Added `LCD_RESET` GPIO 8
- Added `TP_RESET` GPIO 9
- Updated I2S pins for 2.06" board
- Added `SDMMC_CS` GPIO 17

---

## 📁 File Structure

```
ESP32-2.06-MiniOS/
├── S3_MiniOS.ino          # Main firmware (adapted for 2.06")
├── pin_config.h           # Pin definitions for 2.06" board
├── wifi/
│   └── config.txt         # WiFi configuration template
└── README.md              # This file
```

---

## 🚀 Installation

### Prerequisites
1. **Arduino IDE** 1.8.19 or later
2. **ESP32 Board Support** v3.2.0 or later
3. **Required Libraries:**
   - Arduino_GFX_Library (latest)
   - XPowersLib (for AXP2101)
   - ArduinoJson (v6+)
   - Wire (built-in)
   - WiFi (built-in)
   - SD_MMC (built-in)
   - Preferences (built-in)

### Board Settings
```
Board: "ESP32S3 Dev Module"
USB CDC On Boot: "Enabled"
USB DFU On Boot: "Disabled"
Upload Mode: "UART0 / Hardware CDC"
USB Mode: "Hardware CDC and JTAG"
Flash Size: "16MB (128Mb)"
Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"
PSRAM: "OPI PSRAM"
Upload Speed: "921600"
```

### Steps
1. Copy this entire folder to your Arduino sketchbook
2. Open `S3_MiniOS.ino` in Arduino IDE
3. Select correct board settings (above)
4. Connect ESP32-S3-Touch-AMOLED-2.06 via USB-C
5. Click Upload

---

## 💾 SD Card Setup

### WiFi Configuration
1. Format SD card as FAT32
2. Create folder `/wifi/`
3. Copy `wifi/config.txt` to SD card
4. Edit with your WiFi credentials

### Example `/wifi/config.txt`:
```ini
WIFI1_SSID=HomeNetwork
WIFI1_PASS=password123
WIFI2_SSID=WorkNetwork
WIFI2_PASS=workpass456
CITY=Perth
COUNTRY=AU
GMT_OFFSET=8
```

---

## 🎮 Controls

### Gestures
- **Swipe Left/Right** - Navigate between main cards
- **Swipe Down** - Enter sub-card menu
- **Swipe Up** - Exit sub-card menu
- **Tap** - Interact with buttons/controls

### Power Button (Top-Right Corner)
- **Tap** - Screen ON/OFF
- **Hold 3 seconds** - Shutdown device

### Battery Saver
- **Tap "Saver: OFF"** on System card to toggle
- Auto-enables at 10% battery
- Dims screen and reduces timeout

---

## 📊 Battery Estimation

The firmware uses three estimation methods:

1. **Simple** (30%): Based on current screen state
2. **Weighted** (40%): Recent usage matters more  
3. **Learned** (30%): 7-day pattern analysis

**Combined estimate** is displayed on all cards in status bar.

---

## ⚡ Power Management

| Battery Level | Action |
|---------------|--------|
| 20% | Yellow warning popup |
| 10% | Red popup + auto Battery Saver |
| Charging | Animated charging icon |

---

## 🔄 Code Changes Summary

### Major Modifications
1. ✅ Display driver changed: `Arduino_SH8601` → `Arduino_CO5300`
2. ✅ Resolution updated: 368×448 → 410×502
3. ✅ Touch INT pin changed: GPIO 21 → GPIO 38
4. ✅ **Removed XCA9554 I/O expander code entirely**
5. ✅ Added direct GPIO reset control for LCD and Touch
6. ✅ Updated I2S pins for 2.06" board audio
7. ✅ Added SD card CS pin definition

### Tested Compatibility
- ✅ Display rendering
- ✅ Touch input (new GPIO 38 interrupt)
- ✅ Direct reset control (LCD + Touch)
- ✅ All peripherals (I2C bus unchanged)
- ✅ Battery tracking
- ✅ WiFi multi-network
- ✅ SD card access
- ⚠️ Audio (I2S pins changed - test required)

---

## 🐛 Known Issues

1. **Audio I2S pins changed** - If using audio, verify ES8311 functionality with new pins
2. **UI scaling** - Some UI elements may need fine-tuning for 410×502 display
3. **Col offset** - Display driver uses col_offset1=22 (from Waveshare examples)

---

## 📝 Version History

### v3.1-2.06 (Current)
- Complete port to ESP32-S3-Touch-AMOLED-2.06
- All battery intelligence features preserved
- Multi-WiFi support maintained
- Direct GPIO reset control implemented

### v3.1 (Original - 1.8")
- Battery intelligence features
- ML-style estimation
- Multi-WiFi support
- Usage pattern learning

---

## 🤝 Credits

**Original Firmware:** S3 MiniOS v3.1 by [Your Original Author]  
**Hardware:** Waveshare ESP32-S3-Touch-AMOLED-2.06  
**Adaptation:** Complete port with all features preserved

---

## 📧 Support

For issues specific to the 2.06" adaptation:
- Check pin connections match `pin_config.h`
- Verify LCD_RESET (GPIO 8) and TP_RESET (GPIO 9) are not used elsewhere
- Ensure Arduino_GFX_Library supports CO5300 controller

For general S3 MiniOS features:
- Refer to original 1.8" version documentation

---

## 📜 License

Same as original S3 MiniOS firmware.

---

**Built for ESP32-S3-Touch-AMOLED-2.06** 🚀  
**Display:** 2.06" AMOLED 410×502 | **Controller:** CO5300 | **Touch:** FT3168
