# S3 MiniOS

Smartwatch-style firmware for **ESP32-S3-Touch-AMOLED-1.8**

## Features
- Real-time clock display
- Step counter (with IMU)
- Focus/Pomodoro timer (25 min sessions)
- Touch navigation
- Data persistence (survives reboot)
- Dark AMOLED theme

## Required Library
Install via Arduino Library Manager:
- **GFX Library for Arduino** by moononournation (v1.6+)

## Board Settings (Arduino IDE)
```
Board:              ESP32S3 Dev Module
USB CDC On Boot:    Enabled
Flash Size:         16MB
PSRAM:              OPI PSRAM
```

## Hardware
- Display: SH8601 QSPI AMOLED 368x448
- Touch: FT3168 (I2C 0x38)
- IMU: QMI8658 (I2C 0x6B)
- RTC: PCF85063 (I2C 0x51)
- PMU: AXP2101 (I2C 0x34)
- I/O Expander: XCA9554 (I2C 0x20)

## Screens
1. **Home** - Clock, steps preview, focus status, nav buttons
2. **Steps** - Progress ring, step count, distance, calories
3. **Focus** - 25min timer, start/pause, session counter

## Upload
Just flash `S3_MiniOS.ino` - single file, no dependencies except GFX library.
