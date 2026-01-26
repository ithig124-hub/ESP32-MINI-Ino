# S3 MiniOS v4.0 - ULTIMATE PREMIUM EDITION

## 📱 ESP32-S3 Smartwatch Firmware - Premium LVGL UI + Battery Intelligence

This is a **merged firmware** combining the best features from v2.0 (Premium LVGL UI) and v3.1 (Battery Intelligence) for Waveshare ESP32-S3 Touch AMOLED smartwatch boards.

---

## 🎨 Supported Boards

| Board | Display | Resolution | Folder |
|-------|---------|------------|--------|
| ESP32-S3-Touch-AMOLED-1.8" | SH8601 QSPI AMOLED | 368×448 | `S3_MiniOS_1.8/` |
| ESP32-S3-Touch-AMOLED-2.06" | CO5300 QSPI AMOLED | 412×412 (Round) | `S3_MiniOS_2.06/` |

---

## ✨ Features Overview

### Premium LVGL UI (from v2.0)
- 🎨 **8 Apple Watch-style Gradient Themes** (Midnight, Ocean, Sunset, Aurora, Forest, Ruby, Graphite, Mint)
- 🧭 **Full Sensor Fusion Compass** with Kalman filter and smooth animation
- 🎮 **Premium Games**: Blackjack with visual cards, Dino Runner with physics, Yes/No Spinner
- ⭕ **Activity Rings** (Move, Exercise, Stand) - Apple Watch style
- 📈 **Stocks & Crypto** live prices (BTC, ETH, AAPL, TSLA)
- 🏃 **Step Tracking** with streak tracking and achievements
- 🎵 **Music Control** with progress bar
- ⏱️ **Timers**: Sand Timer, Stopwatch, Countdown, Breathe wellness
- 📅 **Calendar** with current date highlighting
- 🖼️ **SD Card Wallpaper Support**

### Battery Intelligence (from v3.1)
- 🔋 **Smart Battery Estimation** using 3 algorithms:
  - Simple (capacity-based)
  - Weighted (recent usage)
  - Learned (7-day patterns)
- 📊 **24-Hour Usage Graphs** (screen time per hour)
- 📉 **Card Usage Analytics** (% time on each feature)
- 🔌 **Charging Animation** with status display
- ⚡ **Battery Saver Mode** (auto-enable at 10%)
- ⚠️ **Low Battery Popup** at 20% and 10%
- 💾 **Persistent Data Storage** with auto-save every 2 hours

---

## 📁 File Structure

Each board folder contains **5 .ino files** that Arduino IDE will merge automatically:

```
S3_MiniOS_1.8/  (or S3_MiniOS_2.06/)
├── S3_MiniOS.ino          # Main: Config, data structures, navigation
├── S3_MiniOS_Part2.ino    # Clock, Compass, Activity cards
├── S3_MiniOS_Part3.ino    # Games, Weather, Stocks, Music cards
├── S3_MiniOS_Part4.ino    # Timers, Streaks, Calendar, System, Battery Stats
└── S3_MiniOS_Part5.ino    # Setup & Loop
```

---

## 🛠️ Installation

### Required Libraries
Install these via Arduino Library Manager:

```
- LVGL (8.3.x)
- ArduinoJson
- XPowersLib
- Adafruit_BusIO
- Arduino_GFX_Library (Waveshare fork)
- Arduino_DriveBus_Library (Waveshare)
- SensorLib (Waveshare)
```

### Board Setup
1. Install ESP32 Arduino Core (2.0.x or newer)
2. Select board: **ESP32S3 Dev Module**
3. Settings:
   - Flash Mode: QIO 80MHz
   - Flash Size: 16MB
   - Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
   - PSRAM: OPI PSRAM
   - USB CDC On Boot: Enabled

### Upload
1. Copy the appropriate folder (`S3_MiniOS_1.8` or `S3_MiniOS_2.06`) to your Arduino sketches
2. Open `S3_MiniOS.ino` in Arduino IDE
3. The IDE will automatically include all Part files
4. Upload!

---

## 📶 WiFi Configuration

Create a file on your SD card at `/wifi/config.txt`:

```ini
# WiFi Configuration
WIFI1_SSID=YourNetwork1
WIFI1_PASS=password1

WIFI2_SSID=YourNetwork2
WIFI2_PASS=password2

# Weather location
CITY=Sydney
COUNTRY=AU
GMT_OFFSET=10
```

---

## 📱 Navigation

| Gesture | Action |
|---------|--------|
| Swipe Left/Right | Switch categories |
| Swipe Up/Down | Switch sub-cards within category |
| Tap | Interact with buttons |

### Categories (12 total)
1. ⏰ **Clock** - Digital & Analog
2. 🧭 **Compass** - Heading, Level, Rotation
3. 🏃 **Activity** - Steps, Rings, Workout, Distance
4. 🎮 **Games** - Blackjack, Dino, Yes/No
5. 🌤️ **Weather** - Current & 3-Day Forecast
6. 📈 **Stocks** - Stocks & Crypto
7. 🎵 **Media** - Music, Gallery
8. ⏱️ **Timer** - Sand, Stopwatch, Countdown, Breathe
9. 🔥 **Streak** - Steps, Games, Achievements
10. 📅 **Calendar** - Monthly view
11. ⚙️ **Settings** - Theme, Brightness, Battery Saver
12. 🔋 **System** - Battery, Stats, Usage Patterns

---

## 🔋 Battery Stats Explained

The System category has 3 sub-cards:

1. **Battery** - Main view with percentage, voltage, estimates
2. **Battery Stats** - 24h screen time graph, estimate breakdown, card usage
3. **Usage Patterns** - Weekly screen time, drain analysis

### Estimation Methods
- **Simple**: Based on battery capacity and current draw
- **Weighted**: Recent drain rate (40% weight)
- **Learned**: 7-day average patterns
- **Combined**: Weighted average of all three (30/40/30)

---

## 🎨 Themes

| Theme | Primary | Accent |
|-------|---------|--------|
| Midnight | Dark Gray | Blue |
| Ocean | Deep Blue | Teal |
| Sunset | Orange | Gold |
| Aurora | Purple | Pink |
| Forest | Dark Green | Mint |
| Ruby | Dark Red | Coral |
| Graphite | Charcoal | Gray |
| Mint | Teal | Cyan |

---

## 📊 Hardware Support

### Sensors
- ✅ QMI8658 IMU (Accelerometer + Gyroscope)
- ✅ PCF85063 RTC (Real-Time Clock)
- ✅ AXP2101 PMU (Power Management)
- ✅ FT3168 Capacitive Touch

### Connectivity
- ✅ WiFi (Multi-network support via SD card)
- ✅ NTP Time Sync
- ✅ OpenWeatherMap API
- ✅ CoinGecko Crypto API

### Storage
- ✅ SD Card (WiFi config, wallpapers)
- ✅ SPIFFS (future use)
- ✅ Preferences (persistent settings)

---

## 📝 Changelog

### v4.0 (Merged Edition)
- Combined v2.0 LVGL UI + v3.1 Battery Intelligence
- Support for both 1.8" and 2.06" boards
- Fixed all card UI layouts
- Added mini status bar with battery estimate to all cards
- Enhanced low battery handling
- Improved touch gesture detection

---

## 🙏 Credits

- Original LVGL UI: S3_MiniOS v2.0
- Battery Intelligence: S3_MiniOS v3.1
- Hardware: Waveshare ESP32-S3-Touch-AMOLED Series
- Graphics Library: Arduino_GFX

---

## 📄 License

MIT License - Feel free to modify and distribute!
