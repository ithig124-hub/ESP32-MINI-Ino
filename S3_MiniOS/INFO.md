# S3 MiniOS v4.0 - Ultimate Premium Edition
## ESP32-S3-Touch-AMOLED-1.8" Smartwatch Firmware

---

## 📋 PROJECT OVERVIEW

**Version:** 4.0 Ultimate Premium Edition  
**Target Hardware:** Waveshare ESP32-S3-Touch-AMOLED-1.8"  
**Display:** SH8601 QSPI AMOLED 368x448 (Rectangular)  
**Framework:** Arduino + LVGL GUI  

---

## 🛠 HARDWARE SPECIFICATIONS

| Component | Model | Interface | Details |
|-----------|-------|-----------|---------|
| **MCU** | ESP32-S3 | - | Dual-core, PSRAM support |
| **Display** | SH8601 | QSPI | 368x448 AMOLED |
| **Touch** | FT3168 | I2C | Capacitive touch |
| **IMU** | QMI8658 | I2C | 6-axis accel/gyro |
| **RTC** | PCF85063 | I2C | Real-time clock |
| **PMU** | AXP2101 | I2C | Power management |
| **I/O Expander** | XCA9554 | I2C (0x20) | GPIO expansion |
| **Storage** | SD_MMC | SDIO | SD card slot |

---

## 📁 FILE STRUCTURE

```
S3_MiniOS_1.8/
├── S3_MiniOS.ino         # Main file: Config, hardware init, navigation
├── S3_MiniOS_Part2.ino   # Clock, compass, steps, activity UI cards
├── S3_MiniOS_Part3.ino   # Games, weather, stocks, media cards
├── S3_MiniOS_Part4.ino   # Timer, streaks, calendar, settings, battery
├── S3_MiniOS_Part5.ino   # Setup() and loop() functions
└── INFO.md               # This documentation file
```

**Note:** All `.ino` files must be in the same folder. Arduino IDE merges them during compilation.

---

## ✨ FEATURES

### UI & Navigation
- **Apple Watch-style gradient themes** (8 premium themes)
- **12 category cards** with swipe navigation
- **Smooth animated transitions** (200ms)
- **Navigation dots** showing position
- **Mini status bar** on all cards

### Categories (12 Total)
| # | Category | Sub-Cards | Description |
|---|----------|-----------|-------------|
| 0 | Clock | 2 | Digital + Analog clock |
| 1 | Compass | 3 | Sensor fusion, tilt/level, gyro |
| 2 | Activity | 4 | Steps, rings, workout, distance |
| 3 | Games | 3 | Blackjack, Dino runner, Yes/No spinner |
| 4 | Weather | 2 | Current + 3-day forecast |
| 5 | Stocks | 2 | Stocks + Crypto prices |
| 6 | Media | 2 | Music player + Gallery |
| 7 | Timer | 4 | Sand timer, stopwatch, countdown, breathe |
| 8 | Streak | 3 | Step streak, game streak, achievements |
| 9 | Calendar | 1 | Monthly calendar view |
| 10 | Settings | 1 | Theme, brightness, battery saver |
| 11 | System | 3 | Battery info, stats, factory reset |

### Battery Intelligence
- **ML-style battery estimation** (simple/weighted/learned/combined)
- **24-hour usage pattern analysis**
- **Card usage tracking** with graphs
- **Low battery popup** at 20% and 10%
- **Battery Saver mode** (auto + manual)
- **Charging animation**
- **Mini battery estimate** on all cards

### Sensor Fusion
- **Compass with Kalman filter**
- **Complementary filter** (α=0.98, β=0.02)
- **Tilt-compensated heading**
- **Step detection** with peak detection algorithm

---

## 🎨 THEMES

| # | Name | Primary | Secondary | Style |
|---|------|---------|-----------|-------|
| 0 | Midnight | #1C1C1E | #0A84FF | Dark blue accent |
| 1 | Ocean | #0D3B66 | #52B2CF | Teal ocean tones |
| 2 | Sunset | #FF6B35 | #FFD166 | Warm orange |
| 3 | Aurora | #7B2CBF | #C77DFF | Purple aurora |
| 4 | Forest | #1B4332 | #52B788 | Green nature |
| 5 | Ruby | #9B2335 | #FF6B6B | Red ruby |
| 6 | Graphite | #1C1C1E | #8E8E93 | Neutral gray |
| 7 | Mint | #00A896 | #00F5D4 | Fresh mint |

---

## 🔌 PIN CONFIGURATION

**Requires:** `pin_config.h` header file with:
- `LCD_CS`, `LCD_SCLK`, `LCD_SDIO0-3` - Display QSPI
- `IIC_SDA`, `IIC_SCL` - I2C bus
- `TP_INT` - Touch interrupt
- `SDMMC_CLK`, `SDMMC_CMD`, `SDMMC_DATA` - SD card

---

## 📡 WIFI & APIs

### SD Card WiFi Configuration
Create `/wifi/config.txt` on SD card:
```ini
# Single network
SSID=YourNetwork
PASSWORD=YourPassword
CITY=Perth
COUNTRY=AU
GMT_OFFSET=8

# Or multiple networks
WIFI1_SSID=Network1
WIFI1_PASS=Password1
WIFI2_SSID=Network2
WIFI2_PASS=Password2
```

### API Services
| Service | API | Purpose |
|---------|-----|---------|
| OpenWeather | Included | Weather data |
| CoinGecko | Free tier | BTC/ETH prices |
| AlphaVantage | Included | Stock prices |

---

## 📊 DATA PERSISTENCE

Data saved to NVS (Preferences) every 2 hours:
- Steps, distance, calories
- Step history (7 days)
- Game stats (streak, wins, played)
- Settings (theme, brightness, timeout)
- Battery learning data (7 days)
- Card usage times

---

## 🔧 COMPILATION REQUIREMENTS

### Arduino IDE Setup
1. **Board:** ESP32S3 Dev Module
2. **USB CDC On Boot:** Enabled
3. **USB Mode:** Hardware CDC and JTAG
4. **PSRAM:** OPI PSRAM

### Required Libraries
```
- lvgl
- ArduinoJson
- Wire
- WiFi
- HTTPClient
- FS / SD_MMC / SPIFFS
- Arduino_GFX_Library
- Arduino_DriveBus_Library
- Adafruit_XCA9554
- XPowersLib
- SensorLib (for QMI8658, PCF85063)
```

---

## 🐛 KNOWN ISSUES & FIXES

### USBSerial Not Declared Error
**Error:** `'USBSerial' was not declared in this scope`

**Solution:** Add at the top of `S3_MiniOS.ino` after includes:
```cpp
#if !defined(USBSerial)
  #if ARDUINO_USB_CDC_ON_BOOT
    #define USBSerial Serial
  #else
    HWCDC USBSerial;
  #endif
#endif
```

Or ensure in Arduino IDE:
- **USB CDC On Boot:** Enabled
- **USB Mode:** Hardware CDC and JTAG

---

## 📱 TOUCH GESTURES

| Gesture | Action |
|---------|--------|
| Swipe Left | Next category |
| Swipe Right | Previous category |
| Swipe Down | Next sub-card |
| Swipe Up | Previous sub-card |
| Tap (screen off) | Wake screen |
| Tap (popup) | Dismiss low battery warning |

---

## ⚡ POWER MODES

| Mode | Screen Timeout | Brightness | Current Draw |
|------|----------------|------------|--------------|
| Normal | 30 seconds | User setting | ~80mA |
| Battery Saver | 10 seconds | 100 (fixed) | ~40mA |
| Screen Off | - | 0 | ~15mA |

---

## 🔄 UPDATE HISTORY

| Version | Changes |
|---------|---------|
| v4.0 | Merged v2.0 UI + v3.1 Battery Intelligence |
| v3.1 | Added battery stats, usage tracking, ML estimation |
| v2.0 | Premium LVGL UI, themes, games, sensor fusion |

---

## 📝 LICENSE

Open source firmware for Waveshare ESP32-S3 smartwatch development.

---

*Generated for S3 MiniOS v4.0 - 1.8" Display Version*
