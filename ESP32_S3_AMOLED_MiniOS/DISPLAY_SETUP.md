# ESP32-S3-Touch-AMOLED-1.8 Display Setup Guide

## WHY YOUR SCREEN IS BLACK

The SH8601 AMOLED requires:
1. **QSPI initialization** (not regular SPI)
2. **Specific GFX library** with SH8601 support
3. **Arduino_DriveBus** library for touch

Standard Arduino libraries **DO NOT** support this display.

---

## REQUIRED LIBRARIES (FROM WAVESHARE)

You MUST install these from the Waveshare repo:

```
ESP32-S3-Touch-AMOLED-1.8/examples/Arduino-v3.3.5/libraries/
├── Arduino_DriveBus/        <-- Touch + I2C drivers
├── GFX_Library_for_Arduino/ <-- Display driver with SH8601 support
├── Mylibrary/               <-- Pin config
├── XPowersLib/              <-- AXP2101 PMU
├── SensorLib/               <-- QMI8658 IMU
└── Adafruit_XCA9554/        <-- I/O Expander
```

---

## INSTALLATION STEPS

### Step 1: Download the Waveshare repo
```
https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8
```

### Step 2: Copy libraries to Arduino
Copy the entire `libraries` folder contents to:
```
Windows: C:\Users\YOUR_NAME\Documents\Arduino\libraries\
Mac:     ~/Documents/Arduino/libraries/
Linux:   ~/Arduino/libraries/
```

### Step 3: Restart Arduino IDE

### Step 4: Upload 01_HelloWorld example
```
File → Examples → (find the copied examples)
Or open: examples/Arduino-v3.3.5/examples/01_HelloWorld/01_HelloWorld.ino
```

---

## BOARD SETTINGS (Arduino IDE)

```
Board:              "ESP32S3 Dev Module"
USB CDC On Boot:    "Enabled"
Flash Size:         "16MB (128Mb)"
Partition Scheme:   "16M Flash (3MB APP/9.9MB FATFS)"
PSRAM:              "OPI PSRAM"
```

---

## WHY STANDARD LIBRARIES DON'T WORK

| Library | Problem |
|---------|---------|
| Adafruit_GFX | No SH8601 driver |
| TFT_eSPI | No QSPI support |
| LovyanGFX | Needs custom config |

The **GFX_Library_for_Arduino** from Waveshare has the `Arduino_SH8601` class built-in.

---

## QUICK TEST CODE (after installing libraries)

```cpp
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);

void setup() {
    Serial.begin(115200);
    
    if (!gfx->begin()) {
        Serial.println("Display init failed!");
        while(1);
    }
    
    gfx->fillScreen(RGB565_BLACK);
    gfx->setBrightness(255);
    
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(3);
    gfx->setCursor(50, 200);
    gfx->println("Hello World!");
}

void loop() {}
```

---

## SUMMARY

1. Download Waveshare repo
2. Copy `libraries` folder to Arduino libraries
3. Use their examples directly
4. The display WILL turn on with proper libraries
