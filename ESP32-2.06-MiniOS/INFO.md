# S3 MiniOS v4.0 - Ultimate Premium Edition
## ESP32-S3-Touch-AMOLED-2.06" Smartwatch Firmware

**Official Waveshare Board Support**

---

## 📋 PROJECT OVERVIEW

**Version:** 4.0 Ultimate Premium Edition  
**Target Hardware:** [Waveshare ESP32-S3-Touch-AMOLED-2.06](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06)  
**Official Repository:** [waveshareteam/ESP32-S3-Touch-AMOLED-2.06](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06)  
**Display:** CO5300 QSPI AMOLED 410x502 (Round/Rounded Rectangle)  
**Framework:** Arduino + LVGL GUI  

---

## 🛠 HARDWARE SPECIFICATIONS

| Component | Model | Interface | GPIO Pins |
|-----------|-------|-----------|-----------|
| **MCU** | ESP32-S3 | - | 32MB Flash, 8MB PSRAM |
| **Display** | CO5300 | QSPI | SIO0:4, SIO1:5, SIO2:6, SIO3:7, SCL:11, CS:12, RST:8, TE:13 |
| **Touch** | FT3168 | I2C (0x38) | SDA:15, SCL:14, INT:38, RST:9 |
| **IMU** | QMI8658 | I2C (0x6B) | SDA:15, SCL:14, INT:21 |
| **RTC** | PCF85063 | I2C (0x51) | SDA:15, SCL:14, INT:39 |
| **PMU** | AXP2101 | I2C (0x34) | SDA:15, SCL:14 |
| **Codec** | ES8311 | I2C/I2S | MCLK:16, SCLK:41, LRCK:45, DOUT:42 |
| **ADC** | ES7210 | I2C/I2S | DIN:40, PA_CTRL:46 |
| **SD Card** | MicroSD | SPI | MOSI:1, SCK:2, MISO:3, CS:17 |

---

## 📌 COMPLETE GPIO PINOUT

### Display (QSPI CO5300)
| Function | GPIO | Description |
|----------|------|-------------|
| QSPI_SIO0 | GPIO4 | Data line 0 |
| QSPI_SI1 | GPIO5 | Data line 1 |
| QSPI_SI2 | GPIO6 | Data line 2 |
| QSPI_SI3 | GPIO7 | Data line 3 |
| QSPI_SCL | GPIO11 | Clock |
| LCD_CS | GPIO12 | Chip Select |
| LCD_RESET | GPIO8 | Display Reset |
| LCD_TE | GPIO13 | Tearing Effect |

### Touch (FT3168)
| Function | GPIO |
|----------|------|
| I2C_SDA | GPIO15 |
| I2C_SCL | GPIO14 |
| Interrupt | GPIO38 |
| RESET | GPIO9 |

### IMU (QMI8658)
| Function | GPIO |
|----------|------|
| I2C_SDA | GPIO15 |
| I2C_SCL | GPIO14 |
| Interrupt | GPIO21 |

### RTC (PCF85063)
| Function | GPIO |
|----------|------|
| I2C_SDA | GPIO15 |
| I2C_SCL | GPIO14 |
| Interrupt | GPIO39 |

### Audio (ES8311 + ES7210)
| Function | GPIO | Description |
|----------|------|-------------|
| I2S_MCLK | GPIO16 | Master Clock |
| I2S_SCLK | GPIO41 | Bit Clock |
| I2S_LRCK | GPIO45 | L/R Clock |
| I2S_ASDOUT | GPIO42 | Audio Data Out |
| I2S_DSDIN | GPIO40 | Audio Data In |
| PA_CTRL | GPIO46 | Amplifier Control |

### SD Card
| Function | GPIO |
|----------|------|
| MOSI | GPIO1 |
| SCK | GPIO2 |
| MISO | GPIO3 |
| SDCS | GPIO17 |

### System
| Function | GPIO |
|----------|------|
| BOOT | GPIO0 |
| PWR | GPIO10 |

---

## 📁 FILE STRUCTURE

```
S3_MiniOS_2.06/
├── S3_MiniOS.ino         # Main file: Config, hardware init, navigation
├── S3_MiniOS_Part2.ino   # Clock, compass, steps, activity UI cards
├── S3_MiniOS_Part3.ino   # Games, weather, stocks, media cards
├── S3_MiniOS_Part4.ino   # Timer, streaks, calendar, settings, battery
├── S3_MiniOS_Part5.ino   # Setup() and loop() functions
├── pin_config.h          # GPIO pin definitions (OFFICIAL)
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
- **Optimized for 410x502 display** (rounded rectangle viewport)

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

## 📐 DISPLAY DIFFERENCES (vs 1.8" Version)

| Aspect | 1.8" Version | 2.06" Version |
|--------|--------------|---------------|
| **Resolution** | 368x448 | 410x502 |
| **Shape** | Rectangular | Rounded Rectangle |
| **Driver** | SH8601 | CO5300 |
| **Aspect Ratio** | Portrait | Taller Portrait |
| **Pixel Area** | 164,864 px | 205,820 px |

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

## 🔧 COMPILATION REQUIREMENTS

### Arduino IDE Setup
1. **Board:** ESP32S3 Dev Module
2. **USB CDC On Boot:** Enabled ⚠️ IMPORTANT
3. **USB Mode:** Hardware CDC and JTAG
4. **PSRAM:** OPI PSRAM
5. **Flash Size:** 32MB (240MHz)
6. **Partition Scheme:** Huge APP (3MB No OTA)

### Required Libraries
From Waveshare official repo (`examples/Arduino-v3.2.0/libraries/`):
```
- Arduino_DriveBus    (Waveshare custom)
- Arduino_GFX         (Waveshare modified)
- Mylibrary           (Contains pin_config.h)
- SensorLib           (QMI8658, PCF85063)
- XPowersLib          (AXP2101)
- lvgl                (v8.x)
```

Additional libraries:
```
- ArduinoJson
- WiFi (built-in)
- HTTPClient (built-in)
- FS / SD_MMC / SPIFFS (built-in)
```

---

## 🐛 KNOWN ISSUES & FIXES

### USBSerial Not Declared Error
**Error:** `'USBSerial' was not declared in this scope`

**Fix Applied:** Compatibility block added at top of S3_MiniOS.ino:
```cpp
#if ARDUINO_USB_CDC_ON_BOOT
  #define USBSerial Serial
#else
  HWCDC USBSerial;
#endif
```

### Display Not Initializing
Ensure `pin_config.h` is present with correct GPIO definitions.
Verify I/O expander (XCA9554) at address 0x20 is working.

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

## 🔗 OFFICIAL RESOURCES

- **Wiki:** https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06
- **GitHub:** https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06
- **Schematic:** Available in GitHub repo `/Schematic` folder
- **Factory Firmware:** Available in GitHub repo `/FirmWare` folder

---

## 🔄 UPDATE HISTORY

| Version | Changes |
|---------|---------|
| v4.0 | Merged v2.0 UI + v3.1 Battery Intelligence |
| v4.0.1 | Fixed USBSerial, updated to official 410x502 resolution |

---

## 📝 LICENSE

Open source firmware for Waveshare ESP32-S3 smartwatch development.
Based on Apache-2.0 licensed official Waveshare examples.

---

*Generated for S3 MiniOS v4.0 - 2.06" Display Version*
*Calibrated for official Waveshare ESP32-S3-Touch-AMOLED-2.06 board*
