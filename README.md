# Widget OS v1.0

**ESP32-S3 Touch AMOLED 1.8" Smartwatch Firmware**

A professional smartwatch OS with 24+ cards, infinite navigation, and real API integrations.

---

## ✨ Key Features

### 🧭 Navigation System
- **Infinite Horizontal Loop** - Swipe left/right cycles through categories forever
- **Vertical Category Stacks** - Swipe down for sub-cards, up to return
- **Visual Navigation Dots** - Bottom (categories) and right (sub-cards)

### 🎨 Gradient Themes (8 Options)
1. Cyber Purple
2. Ocean Teal
3. Sunset Orange
4. Neon Pink
5. Lime Green
6. Fire Red
7. Deep Space
8. Mint Fresh

### 📱 All Cards (24+)

| Category | Cards |
|----------|-------|
| **Clock** | Digital Clock, Analog Clock |
| **Compass** | Real compass with sunrise/sunset |
| **Activity** | Steps, Activity Rings, Workouts, Distance |
| **Games** | Blackjack, Dino Runner, Yes/No Spinner |
| **Weather** | Current Weather, 3-Day Forecast |
| **Stocks** | Stocks, Crypto |
| **Media** | Music Player, Gallery |
| **Timer** | Sand Timer (5min), Stopwatch, Countdown |
| **Streak** | Step Streak, Game Streak, Achievements |
| **Calendar** | Monthly Calendar (Jan 25, 2026) |
| **Settings** | Brightness, Theme, Timeout, Goal |
| **System** | Battery, System Info |

---

## 🔌 API Integration

| Service | API Key | Data |
|---------|---------|------|
| **OpenWeather** | `3795c13a...` | Temperature, conditions |
| **Alpha Vantage** | `UHLX28BF...` | Stock prices |
| **CoinAPI** | `11afad22...` | BTC, ETH prices |

When WiFi connects, the watch:
1. Syncs time via NTP (GMT+8)
2. Fetches weather data
3. Fetches crypto prices

---

## 🎮 Games

### Blackjack
- Full card game with Hit/Stand
- Streak tracking
- Win counter

### Dino Runner
- Tap "JUMP" button to jump
- Avoid obstacles
- Score counter

### Yes/No Spinner
- Spinning animation
- Random answers: Yes, No, Maybe, Ask Again, Definitely, Never

---

## ⏱️ Timers

### Sand Timer
- Fixed 5-minute duration
- Hourglass visualization
- **Notification**: Screen flashes Red/Green/Blue when complete

### Stopwatch
- Start/Pause/Reset
- MM:SS.CC display

### Countdown
- Configurable duration

---

## 📊 Streaks

### Step Streak
- Days in a row hitting step goal
- Fire icon

### Game Streak
- Blackjack wins in a row
- Win/Total games stats

### Achievements
- 10K Steps
- Win 5 Games
- 7 Day Streak

---

## ⚙️ Settings

| Setting | Options |
|---------|---------|
| **Brightness** | Slider (20-255) |
| **Theme** | 8 gradient themes |
| **Screen Timeout** | 1, 2, 3, 5, 7, 10 minutes |
| **Daily Goal** | Step target |

---

## 💾 Data Persistence

All user data saved to SPIFFS:
- Steps count
- Streaks
- Settings
- Game stats

Data saves every 30 seconds.

---

## 🔧 Hardware

| Component | Model |
|-----------|-------|
| MCU | ESP32-S3 |
| Display | 1.8" AMOLED (368×448) |
| Touch | FT3168 |
| IMU | QMI8658 |
| RTC | PCF85063 |
| PMU | AXP2101 |

---

## 📶 WiFi

```cpp
SSID: "Optus_9D2E3D"
Password: "snucktemptGLeQU"
Timezone: GMT+8
```

---

## 📁 Files

```
/app/firmware/
├── S3_MiniOS.ino    # Main firmware (~2200 lines)
├── pin_config.h     # Hardware pins
└── README.md        # This file
```

---

## 🚀 Installation

1. Open `S3_MiniOS.ino` in Arduino IDE
2. Install required libraries:
   - LVGL
   - Arduino_GFX_Library
   - Arduino_DriveBus_Library
   - ArduinoJson
   - XPowersLib
   - SensorLib
3. Select board: ESP32S3 Dev Module
4. Upload!

---

## 📱 Navigation Guide

```
┌─────────────────────────────────────┐
│     ← SWIPE LEFT/RIGHT →            │
│     (Change Category - Infinite)    │
│                                     │
│           ↓ SWIPE DOWN              │
│           (Enter Sub-cards)         │
│                                     │
│           ↑ SWIPE UP                │
│           (Return to Main)          │
│                                     │
│  [●][○][○][○][○] ← Category dots    │
└─────────────────────────────────────┘
```

---

**Widget OS v1.0** - Built for ESP32-S3 Touch AMOLED 1.8"
