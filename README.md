# S3 MiniOS v3.0 - Enhanced Edition

**ESP32-S3-Touch-AMOLED-1.8 Smartwatch Firmware**

## ✨ New Features in v3.0

### 📶 WiFi Auto-Connect from SD Card
- Create `/wifi/config.txt` on your SD card with WiFi credentials
- Watch automatically connects on boot
- Template file provided in `sd_card_template/wifi/config.txt`

```
# Example /wifi/config.txt
SSID=YourWiFiName
PASSWORD=YourPassword
CITY=Perth
COUNTRY=AU
GMT_OFFSET=8
```

### 🔋 Battery Time Estimation
- Shows estimated remaining battery time on System card
- Accounts for screen-on vs screen-off power consumption
- Based on 500mAh battery capacity

### ⚡ Reduced Lag - 2 Hour Save Interval
- Stats now save every **2 hours** instead of 30 seconds
- Dramatically reduces lag and write operations
- Data still safe - saved on shutdown

### 🔄 Factory Reset Button
- Found in System menu (swipe down from System card)
- Resets ALL data (steps, scores, settings)
- **Preserves:** SD card data, firmware/sketch
- Device restarts after reset

### 🌍 Weather by WiFi Location
- Auto-detects your city from IP address
- Or manually set city in SD card config
- Default: Perth, AU

### 🔘 Power Button Controls
- **Tap top-right corner**: Toggle screen ON/OFF
- **Long press (3 sec)**: Shutdown device
- Visual progress bar during long press

---

## 📱 Card Layout

| Card | Description |
|------|-------------|
| **Clock** | Time, date, location, WiFi status |
| **Steps** | Daily steps with progress bar |
| **Music** | Media controls (mock) |
| **Games** | Clicker game, more coming |
| **Weather** | Temperature from API + location |
| **System** | Battery %, estimate, reset option |

---

## 🎮 Navigation

| Gesture | Action |
|---------|--------|
| Swipe Left/Right | Change main card |
| Swipe Down | Enter sub-cards |
| Swipe Up | Return to main card |
| Tap | Interact with buttons |

---

## 📁 SD Card Structure

```
SD_CARD/
└── wifi/
    └── config.txt    ← WiFi credentials here
```

### Config File Format

```
# Comment lines start with #
SSID=MyWiFiNetwork
PASSWORD=MySecretPassword
CITY=Perth           # Optional: weather location
COUNTRY=AU           # Optional: 2-letter country code
GMT_OFFSET=8         # Optional: timezone offset
```

---

## 🔋 Battery Information

- **Battery Type**: 3.7V LiPo (MX1.25 connector)
- **Capacity**: ~500mAh (typical)
- **Screen On**: ~80mA draw → ~6 hours runtime
- **Screen Off**: ~15mA draw → ~33 hours standby

---

## ⚡ Power Controls

### Screen Toggle (Quick Tap)
- Tap the top-right corner to turn screen ON/OFF
- Saves battery when not in use

### Shutdown (Long Press)
- Hold top-right corner for 3 seconds
- Progress bar appears during hold
- Saves data before shutdown
- Uses AXP2101 power-off feature

---

## 🔧 Hardware Compatibility

| Component | Model |
|-----------|-------|
| Board | Waveshare ESP32-S3-Touch-AMOLED-1.8 |
| Display | SH8601 QSPI AMOLED 368×448 |
| Touch | FT3168 Capacitive |
| IMU | QMI8658 (Accelerometer/Gyro) |
| RTC | PCF85063 |
| PMU | AXP2101 |
| Battery | 3.7V LiPo via MX1.25 header |

---

## 📂 Files

```
/app/esp32-project/
├── S3_MiniOS/
│   └── S3_MiniOS.ino     # Main firmware (v3.0)
├── sd_card_template/
│   └── wifi/
│       └── config.txt     # WiFi template - copy to SD card
├── pin_config.h           # Hardware pin definitions
└── README.md              # This file
```

---

## 🚀 Installation

1. Open `S3_MiniOS/S3_MiniOS.ino` in Arduino IDE
2. Install required libraries:
   - Arduino_GFX_Library
   - ArduinoJson
   - Preferences (built-in)
3. Board: **ESP32S3 Dev Module**
4. Flash: 16MB, PSRAM: OPI
5. Upload!

### Setting up WiFi

1. Copy `sd_card_template/wifi/config.txt` to SD card
2. Edit with your WiFi credentials
3. Insert SD card into watch
4. Reboot - WiFi connects automatically!

---

## 📋 Changelog

### v3.0 (Current)
- WiFi auto-connect from SD card `/wifi/config.txt`
- Save interval changed to 2 hours (reduced lag)
- Factory reset button in System menu
- Weather location from IP geolocation
- Battery time estimation display
- Quick tap ON/OFF, long press shutdown
- Power button visual feedback

### v2.x
- Premium UI design
- Activity tracking
- Games hub
- Weather API integration

---

**S3 MiniOS v3.0** - Enhanced Edition  
Built for ESP32-S3 Touch AMOLED 1.8"
