# ESP32-S3 Touch AMOLED Mini OS

A sophisticated smartwatch-style firmware for the ESP32-S3 with a 1.8" Touch AMOLED display. Built with **LVGL** for a professional, modern UI experience.

![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue)
![Display](https://img.shields.io/badge/Display-1.8%22%20AMOLED-green)
![UI](https://img.shields.io/badge/UI-LVGL-orange)
![Cards](https://img.shields.io/badge/Cards-24+-purple)

---

## ✨ Features

- **24+ Interactive Cards** - Swipe through a rich collection of apps and utilities
- **LVGL Graphics** - Smooth, professional UI with animations
- **Real Sensor Integration** - IMU for steps, RTC for time, PMU for battery
- **4 Themes** - Dark, Blue, Green, Purple (AMOLED optimized)
- **WiFi Connected** - Auto-connects on startup
- **SD Card Support** - Load music and photos from SD card
- **Touch Navigation** - Intuitive swipe gestures

---

## 📱 Card Overview

### ⏰ Time & Navigation
| Card | Description |
|------|-------------|
| **Digital Clock** | Large time, date, day of week, WiFi status |
| **Analog Clock** | Classic clock face with Roman numerals |
| **World Clock** | 4 timezones (New York, London, Tokyo, Sydney) |
| **Compass** | N/S/E/W directions with sunrise/sunset times |
| **Calendar** | Monthly view with day selection |

### 🏃 Health & Fitness
| Card | Description |
|------|-------------|
| **Activity Rings** | Apple-style Move/Exercise/Stand rings |
| **Steps** | Real-time step counter with goal progress |
| **Workout** | Bike, Run, Basketball tracking modes |
| **Sleep Tracker** | Timer-based sleep monitoring |
| **Sleep Graph** | Movement visualization for sleep quality |
| **Streak** | Daily activity streak counter |
| **Daily Goal** | Circular progress toward step goal |

### 🎵 Media & Entertainment
| Card | Description |
|------|-------------|
| **Music Player** | Play music from SD card `/Music` folder |
| **Gallery** | View photos from SD card `/Photos` folder |
| **Dino Game** | Tap-to-jump endless runner |
| **Yes/No Spinner** | Random answer generator |
| **Quotes** | Motivational quote carousel |

### 📊 Information
| Card | Description |
|------|-------------|
| **Weather** | Temperature and conditions (mock data) |
| **Stocks** | AAPL, GOOGL, MSFT, AMZN prices |
| **Crypto** | BTC, ETH, SOL, ADA prices |

### ⚙️ Utilities & System
| Card | Description |
|------|-------------|
| **Timer** | Countdown timer with controls |
| **Volume** | Audio volume slider |
| **Torch** | Full-screen flashlight |
| **Notes** | 5 quick note slots |
| **Battery** | Detailed battery status with charging indicator |
| **Themes** | 4 color theme options |
| **System** | CPU, RAM, Temperature, WiFi status |

---

## 🎮 Navigation

```
┌─────────────────────────────────────┐
│                                     │
│     ← SWIPE LEFT    SWIPE RIGHT →   │
│        Next Card    Previous Card   │
│                                     │
│              ↓ SWIPE DOWN           │
│              Sub-card / Details     │
│                                     │
│              ↑ SWIPE UP             │
│              Back to Main           │
│                                     │
└─────────────────────────────────────┘

Navigation Dots:
• Bottom: Main card position (horizontal)
• Right side: Sub-card position (vertical)
```

---

## 🔧 Hardware Requirements

### Supported Board
- **Waveshare ESP32-S3-Touch-AMOLED-1.8**

### Components
| Component | Model | I2C Address |
|-----------|-------|-------------|
| Display | SH8601 AMOLED (368×448) | QSPI |
| Touch | FT3168 | 0x38 |
| IMU | QMI8658 | 0x6B |
| RTC | PCF85063 | 0x51 |
| PMU | AXP2101 | 0x34 |
| I/O Expander | XCA9554 | 0x20 |
| SD Card | SDMMC 1-bit | - |

### Pin Configuration
```
Display (QSPI):
  SDIO0: 4, SDIO1: 5, SDIO2: 6, SDIO3: 7
  SCLK: 11, CS: 12

I2C Bus:
  SDA: 15, SCL: 14

Touch:
  INT: 21

SD Card (SDMMC):
  CLK: 2, CMD: 1, DATA: 3
```

---

## 📦 Installation

### Prerequisites
1. **Arduino IDE** (2.0+ recommended)
2. **ESP32 Board Package** (3.x)
3. **Required Libraries:**
   - LVGL
   - Arduino_GFX_Library
   - Arduino_DriveBus_Library
   - Adafruit_XCA9554
   - XPowersLib
   - SensorLib (for QMI8658, PCF85063)

### Steps

1. **Clone or download** this repository

2. **Install libraries** via Arduino Library Manager or manually

3. **Open** `S3_MiniOS.ino` in Arduino IDE

4. **Select Board:**
   ```
   Tools → Board → ESP32S3 Dev Module
   ```

5. **Configure Board Settings:**
   ```
   USB Mode: USB-OTG (TinyUSB)
   USB CDC On Boot: Enabled
   Flash Size: 16MB
   Partition Scheme: 16M Flash (3MB APP)
   PSRAM: OPI PSRAM
   ```

6. **Upload** to your device

---

## 📂 SD Card Setup

Create these folders on your SD card:

```
/sdcard/
├── Music/          # Audio files (.mp3, .wav)
│   ├── song1.mp3
│   ├── song2.mp3
│   └── ...
└── Photos/         # Image files (.jpg, .png, .bmp)
    ├── photo1.jpg
    ├── photo2.jpg
    └── ...
```

> **Note:** Images should be resized to fit the display (368×448 max) for best performance.

---

## 🌐 WiFi Configuration

Edit these lines in `S3_MiniOS.ino` to set your WiFi credentials:

```cpp
const char* WIFI_SSID = "Your_SSID";
const char* WIFI_PASSWORD = "Your_Password";
```

---

## 🎨 Themes

| Theme | Background | Accent | Best For |
|-------|------------|--------|----------|
| **Dark** | Pure Black (#000000) | Cyan (#00D4FF) | Battery saving, AMOLED |
| **Blue** | Navy (#0A1929) | Light Blue (#5090D3) | Professional look |
| **Green** | Forest (#0D1F0D) | Green (#4CAF50) | Nature, health focus |
| **Purple** | Deep Purple (#1A0A29) | Violet (#BB86FC) | Creative, modern |

---

## 📊 Sensor Integration

### Step Counting Algorithm
```
1. Read accelerometer (X, Y, Z) at 50ms intervals
2. Calculate magnitude: √(x² + y² + z²)
3. Detect step when: 0.5 < delta < 3.0
4. Update metrics:
   - Distance = steps × 0.7m
   - Calories = steps × 0.04 kcal
```

### Sleep Tracking
```
1. User starts sleep tracking
2. Movement sampled every 60 seconds
3. Data stored in 24-point array
4. Visualized as line graph
5. Low movement = Deep sleep
   High movement = Light sleep
```

### Battery Monitoring
```
- Real-time percentage from AXP2101
- Charging detection
- Color-coded indicator:
  Green: >60%
  Orange: 20-60%
  Red: <20%
```

---

## 🎮 Games

### Dino Runner
- **Controls:** Tap anywhere to jump
- **Objective:** Avoid obstacles
- **Scoring:** +1 for each obstacle passed
- **Game Over:** Tap to restart

### Yes/No Spinner
- **Tap REVEAL** for random answer
- **Answers:** YES!, NO!, MAYBE..., ASK AGAIN, DEFINITELY!, NEVER!, 100%, NOPE

---

## 📁 File Structure

```
firmware/
├── S3_MiniOS.ino     # Main firmware (1987 lines)
├── pin_config.h      # Hardware pin definitions
└── README.md         # This file
```

---

## 🔄 Main Loop Processes

| Interval | Process |
|----------|---------|
| 5ms | LVGL timer handler |
| 50ms | IMU reading, step detection |
| 50ms | Dino game update (when active) |
| 60s | Sleep movement sampling (when tracking) |
| 1000ms | Clock update, timer countdown |

---

## 🛠️ Customization

### Adding a New Card

1. Add card enum in `MainCard`:
```cpp
enum MainCard {
  // ... existing cards
  CARD_MY_NEW_CARD,
  MAIN_CARD_COUNT
};
```

2. Update `subCardCounts` array

3. Create card function:
```cpp
void createMyNewCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("MY CARD");
  
  // Add your UI elements here
}
```

4. Add case in `navigateToCard()`:
```cpp
case CARD_MY_NEW_CARD: createMyNewCard(); break;
```

### Changing Step Goal
```cpp
int dailyGoal = 10000;  // Change this value
```

### Adding World Clock Cities
```cpp
const char* worldCities[] = {"New York", "London", "Tokyo", "Sydney"};
const int worldOffsets[] = {-5, 0, 9, 11};  // UTC offsets
```

---

## 📝 License

This project is open source. Feel free to use, modify, and distribute.

---

## 🙏 Credits

- **LVGL** - Light and Versatile Graphics Library
- **Waveshare** - Hardware reference and examples
- **VolosR/pocketClock** - LVGL implementation reference

---

## 📞 Support

For issues or questions:
1. Check the pin configuration matches your hardware
2. Ensure all libraries are installed
3. Verify board settings in Arduino IDE

---

**Enjoy your Mini OS! 🚀**
