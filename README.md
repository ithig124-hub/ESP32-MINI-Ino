# Widget OS v2.0 - Premium Edition

**ESP32-S3 Touch AMOLED 1.8" Smartwatch Firmware**

A professional smartwatch OS with 26+ premium cards, smooth navigation, full sensor fusion compass, and Apple Watch-inspired design.

---

## ✨ Major Improvements in v2.0

### 🧭 Full Sensor Fusion Compass
- **Complementary Filter** - Fuses gyroscope + accelerometer for accurate orientation
- **Relative Orientation Tracking** - Tracks rotation from initial position
- **Tilt-Based Level Display** - Bubble level like a carpenter's tool
- **Gyroscope Rotation Visualization** - Shows roll, pitch, yaw in real-time
- **Smooth Needle Animation** - No more jerky 2-line display!

### 🎨 Premium Apple Watch-Inspired Design
- **8 New Gradient Themes** - Midnight, Ocean, Sunset, Aurora, Forest, Ruby, Graphite, Mint
- **Glassmorphism Cards** - Frosted glass effect with shadows
- **Pill-Shaped Navigation Dots** - Active indicator stretches like Apple Watch
- **Color-Coded UI Elements** - Semantic colors for status (green=good, red=warning)

### 🎮 Enhanced Games
- **Blackjack** - Visual playing cards with suits and ranks
- **Dino Run** - Pixel-style dinosaur with particle ground effects
- **Yes/No Spinner** - Glowing result ring with color feedback

### 🔄 Smooth Navigation
- **Gesture Velocity Detection** - Faster swipes = quicker transitions
- **Infinite Horizontal Loop** - Seamless category cycling
- **Visual Transition States** - Direction indicators during swipes

---

## 🧭 Navigation System

### Core Structure
The UI is built from **main cards (categories)** arranged in a horizontal, infinite loop.

```
Clock → Compass → Activity → Games → Weather → Stocks → Media → Timer → Streak → Calendar → Settings → System → Clock → ...
```

### Gesture Controls
| Gesture | Action |
|---------|--------|
| **Swipe Left/Right** | Change category (infinite loop) |
| **Swipe Down** | Enter sub-cards within category |
| **Swipe Up** | Return to main card of category |

### Navigation Rules
- Horizontal swipes ALWAYS move between main cards
- Entering a category always shows its main card first
- Leaving a category resets to main card
- Sub-cards are only accessible via vertical swipes

---

## 📱 All Cards (26+)

| Category | Cards |
|----------|-------|
| **Clock** | Digital Clock (with status bar), Analog Clock |
| **Compass** | Sensor Fusion Compass, Bubble Level, Gyro Rotation |
| **Activity** | Steps (gradient), Activity Rings, Workouts, Distance |
| **Games** | Blackjack (visual cards), Dino Runner, Yes/No Spinner |
| **Weather** | Current Weather, 3-Day Forecast |
| **Stocks** | Stock Prices, Cryptocurrency |
| **Media** | Music Player, Gallery |
| **Timer** | Sand Timer (5min), Stopwatch, Countdown |
| **Streak** | Step Streak (fire), Game Streak, Achievements |
| **Calendar** | Monthly Calendar (Jan 2026) |
| **Settings** | Brightness, Theme, Timeout, Compass Calibration |
| **System** | Battery, System Info |

---

## 🧭 Compass Modes

### 1. Sensor Fusion Compass (Main)
- **Algorithm**: Complementary filter (98% gyro, 2% accel)
- **Features**:
  - Rotating compass rose with cardinal directions
  - Red/Blue needle (N/S)
  - Degree heading display (0-360°)
  - Direction label (N, NE, E, SE, S, SW, W, NW)
  - Auto-calibration when device is level

### 2. Bubble Level / Tilt Display
- **Purpose**: Shows if device is level
- **Features**:
  - Circular bubble that moves with tilt
  - Crosshairs for reference
  - Green = Level, Red = Tilted
  - X/Y tilt values in degrees

### 3. Gyroscope Rotation Tracking
- **Purpose**: Shows 3D orientation changes
- **Features**:
  - Three concentric arcs (Roll, Pitch, Yaw)
  - Color-coded: Red=Pitch, Green=Roll, Blue=Yaw
  - Real-time degree values

---

## 🎨 Gradient Themes (8 Options)

| Theme | Colors |
|-------|--------|
| **Midnight** | Dark grays with blue accents |
| **Ocean** | Deep blues and teals |
| **Sunset** | Orange to gold gradient |
| **Aurora** | Purple to pink |
| **Forest** | Deep greens |
| **Ruby** | Red tones |
| **Graphite** | Neutral grays |
| **Mint** | Fresh green and white |

---

## 🎮 Games

### Blackjack
- **Visual Cards**: Shows rank (A, 2-10, J, Q, K) and suit color
- **Animations**: Card shadows, golden buttons
- **Features**: Hit/Stand buttons, win streak tracking
- **Casino Feel**: Green felt background, gold accents

### Dino Runner
- **Character**: Pixel-style green dinosaur with eye
- **Environment**: Particle ground texture, red obstacles
- **Controls**: Large "JUMP!" button with glow
- **Game Over**: Overlay with final score

### Yes/No Spinner
- **Display**: Large result in center circle
- **Feedback**: Glowing ring (green=positive, red=negative)
- **Answers**: Yes, No, Maybe, Ask Again, Definitely, Never

---

## ⚙️ Sensor Fusion Details

### Complementary Filter Algorithm
```cpp
// Fuse accelerometer (long-term accuracy) with gyroscope (short-term accuracy)
roll = 0.98 * (roll + gyro_roll * dt) + 0.02 * accel_roll;
pitch = 0.98 * (pitch + gyro_pitch * dt) + 0.02 * accel_pitch;
yaw += gyro_yaw * dt;  // Integrated with drift compensation
```

### Calibration
- **Auto-Calibrate**: When device is held level for 1+ second
- **Manual Calibrate**: Via Settings → Calibrate Compass
- **Reference Point**: Initial orientation becomes "North"

### Why No True North?
The QMI8658 IMU has accelerometer and gyroscope, but **no magnetometer**. True north requires a magnetometer to sense Earth's magnetic field. Our pseudo-compass shows **relative orientation** from where you started.

---

## 🔌 API Integration

| Service | Usage |
|---------|-------|
| **OpenWeather** | Temperature, conditions for Sydney |
| **CoinAPI** | BTC, ETH prices |
| **Alpha Vantage** | Stock prices |
| **NTP** | Time sync (GMT+8) |

---

## 💾 Data Persistence

All user data saved to SPIFFS every 30 seconds:
- Steps count & streaks
- Game statistics
- Settings (theme, brightness, timeout)
- Compass calibration state

---

## 🔧 Hardware

| Component | Model |
|-----------|-------|
| MCU | ESP32-S3 |
| Display | 1.8" AMOLED (368×448) |
| Touch | FT3168 |
| IMU | QMI8658 (Accel + Gyro) |
| RTC | PCF85063 |
| PMU | AXP2101 |

---

## 📁 Files

```
/app/esp32_project/
├── S3_MiniOS.ino    # Main firmware (~3400 lines)
├── pin_config.h     # Hardware pin definitions
└── README.md        # This documentation
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
3. Select board: **ESP32S3 Dev Module**
4. Flash size: 16MB
5. PSRAM: OPI PSRAM
6. Upload!

---

## 📱 Navigation Guide

```
┌────────────────────────────────────────┐
│     ← SWIPE LEFT/RIGHT →               │
│     (Change Category - Infinite Loop)  │
│                                        │
│           ↓ SWIPE DOWN                 │
│           (Enter Sub-cards)            │
│                                        │
│           ↑ SWIPE UP                   │
│           (Return to Main)             │
│                                        │
│  [━━][●][●][●][●] ← Category dots      │
│                            ●           │
│                            ●  Sub-card │
│                            ●  dots     │
└────────────────────────────────────────┘
```

---

**Widget OS v2.0** - Premium Edition
Built for ESP32-S3 Touch AMOLED 1.8"

---

## v2.1 Updates

### Bug Fixes
- **Dino Jump Fixed**: Proper physics-based jumping with gravity and velocity
- **Theme Switching**: Instant theme changes (removed save delay)
- **Reduced Lag**: Compass/sensor updates optimized to 150ms intervals

### New Features
- **Breathe Card**: Apple Watch-style breathing exercise with animated expanding circle
  - 4 second inhale → 2 second hold → 6 second exhale cycle
  - Calming teal gradient design
  - Visual breathing guide

### Improved
- **Settings Card**: Theme preview strip showing current gradient + accent colors
- **Dino Physics**: Realistic parabolic jump arc using velocity + gravity

### Card Count: 27+ cards
