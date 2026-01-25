# S3 MiniOS v3.1 - Battery Intelligence Edition

**ESP32-S3-Touch-AMOLED-1.8 Smartwatch Firmware**

## 🔋 New Battery Intelligence Features

### 1. Sleep Mode Tracking
- **Screen on/off time history** - Tracks every minute of usage
- **24-hour hourly buckets** - See your screen time pattern
- **Card usage tracking** - Know which features you use most
- **Step activity correlation** - Links activity to battery usage

### 2. Battery Estimation Methods
| Method | Description | Weight |
|--------|-------------|--------|
| **Simple** | Based on current screen state | 30% |
| **Weighted** | Recent usage matters more | 40% |
| **Learned** | 7-day pattern analysis | 30% |
| **Combined** | Weighted average of all three | Display |

### 3. Battery Stats Sub-Card
- **24-hour screen time graph** - Bar chart by hour
- **Estimate breakdown** - See all estimation methods
- **Card usage pie chart** - Visual usage distribution
- Swipe down from System, then left/right

### 4. Mini Estimate on All Cards
- Status bar shows: WiFi, Battery Saver, **~Xh Xm**, Battery %
- Always visible at the top of every screen

### 5. Low Battery Warning Popup
- **20%** - Warning popup, auto-dismisses in 5 seconds
- **10%** - CRITICAL popup, auto-enables Battery Saver
- Tap anywhere to dismiss early

### 6. Battery Saver Mode
- **Dimmed screen** (100 vs 200 brightness)
- **Shorter timeout** (10s vs 30s screen off)
- **Tap "Saver" on System card** to toggle
- Auto-enables at 10% battery

### 7. Charging Animation
- Animated battery icon when plugged in
- "Charging..." text with animated dots
- Status bar shows "Charging" instead of estimate

---

## 📱 Navigation

### Main Cards (Swipe Left/Right)
| Card | Features |
|------|----------|
| Clock | Time, day, location, status bar |
| Steps | Count, goal, weekly graph |
| Music | Player controls |
| Games | Clicker, most-used card stat |
| Weather | Temperature, conditions |
| System | Battery %, estimate, saver toggle |

### System Sub-Cards (Swipe Down, then Left/Right)
1. **Battery Stats** - 24h graph, estimates breakdown, card usage
2. **Usage Patterns** - Weekly screen time, drain analysis
3. **Factory Reset** - Clear all data (preserves SD + firmware)

---

## 📊 Battery Stats Details

### Screen Time Graph
```
 │▇▇    ▇▇▇▇      ▇▇▇▇▇▇▇▇
 │▇▇    ▇▇▇▇      ▇▇▇▇▇▇▇▇
 │▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇
 └────────────────────────
  00  06  12  18  24 (hours)
```
Shows screen-on minutes per hour. Current hour highlighted in cyan.

### Card Usage
```
Clock:    ████████████████░░░░ 45%
Steps:    ████████░░░░░░░░░░░░ 20%
System:   ██████░░░░░░░░░░░░░░ 15%
Weather:  ████░░░░░░░░░░░░░░░░ 10%
Games:    ██░░░░░░░░░░░░░░░░░░  5%
Music:    █░░░░░░░░░░░░░░░░░░░  5%
```

### Drain Analysis
- **Avg drain/hour**: Based on all recorded hours
- **Recent drain rate**: More weight on last few hours
- **Est. full battery**: How long 100% would last

---

## ⚡ Power Controls

| Action | How | Result |
|--------|-----|--------|
| Screen ON/OFF | Tap top-right corner | Instant toggle |
| Shutdown | Hold top-right 3 seconds | Safe shutdown + save |
| Battery Saver | Tap "Saver: OFF" on System | Toggle dim mode |

---

## 📶 WiFi from SD Card

Create `/wifi/config.txt`:
```
SSID=YourNetwork
PASSWORD=YourPassword
CITY=Perth
COUNTRY=AU
GMT_OFFSET=8
```

---

## 🔧 Technical Details

### Battery Constants
```cpp
BATTERY_CAPACITY_MAH = 500      // Typical 3.7V LiPo
SCREEN_ON_CURRENT_MA = 80       // Display active
SCREEN_OFF_CURRENT_MA = 15      // Sleep mode
SAVER_MODE_CURRENT_MA = 40      // Dimmed screen

LOW_BATTERY_WARNING = 20%       // Yellow warning
CRITICAL_BATTERY_WARNING = 10%  // Red + auto saver
```

### Data Storage
- **Save interval**: Every 2 hours (reduces lag)
- **Stored data**: Steps, scores, settings, battery stats
- **Learning data**: 7 days of usage patterns
- **Card usage**: Time spent on each card

---

## 📁 Files

```
/app/esp32-project/
├── S3_MiniOS/
│   └── S3_MiniOS.ino          # Main firmware v3.1
├── sd_card_template/
│   └── wifi/
│       └── config.txt          # WiFi template
├── pin_config.h
└── README.md                   # This file
```

---

## 📋 Changelog

### v3.1 (Current)
- Sleep mode tracking (screen time, card usage)
- ML-style battery estimation (simple/weighted/learned)
- Battery Stats sub-card with graphs
- Usage Patterns sub-card
- Mini battery estimate on all cards
- Low battery popup at 20% and 10%
- Battery Saver mode (auto + manual)
- Charging animation
- Step activity correlation

### v3.0
- WiFi auto-connect from SD card
- 2-hour save interval
- Factory reset button
- IP geolocation for weather
- Quick tap ON/OFF, long press shutdown
- Battery time estimation

---

**S3 MiniOS v3.1** - Battery Intelligence Edition  
Built for ESP32-S3 Touch AMOLED 1.8"
