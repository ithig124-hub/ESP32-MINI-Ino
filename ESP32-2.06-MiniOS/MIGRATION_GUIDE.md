# Migration Guide: 1.8" → 2.06" Boards

## Quick Reference

### Display Hardware
| Spec | 1.8" | 2.06" |
|------|------|-------|
| Controller | SH8601 | CO5300 |
| Resolution | 368×448 px | 410×502 px |
| Size | 1.8 inches | 2.06 inches |
| Interface | QSPI | QSPI |

### Critical Pin Changes
```diff
- 1.8" Touch INT:  GPIO 21
+ 2.06" Touch INT: GPIO 38

- 1.8" LCD Reset:  Via XCA9554 I/O Expander Pin 1
+ 2.06" LCD Reset: Direct GPIO 8

- 1.8" Touch Reset: Via XCA9554 I/O Expander Pin 0  
+ 2.06" Touch Reset: Direct GPIO 9
```

### I/O Expander Status
```diff
- 1.8": Has XCA9554 at I2C address 0x20
+ 2.06": NO I/O Expander - Direct GPIO control only
```

### I2S Audio Pins (ES8311)
```diff
1.8" Board:          2.06" Board:
I2S_BCK: GPIO 9   →  I2S_BCK: GPIO 41
I2S_DI:  GPIO 8   →  I2S_DI:  GPIO 40
I2S_DO:  GPIO 10  →  I2S_DO:  GPIO 42
I2S_WS:  GPIO 45     I2S_WS:  GPIO 45 (same)
I2S_MCK: GPIO 16     I2S_MCK: GPIO 16 (same)
```

### Code Changes Required

#### 1. Display Initialization
```cpp
// OLD (1.8"):
Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus, GFX_NOT_DEFINED, 0, 368, 448);

// NEW (2.06"):
Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0, 410, 502,
    22, 0, 0, 0);
```

#### 2. Reset Control
```cpp
// OLD (1.8"): Via XCA9554 I/O Expander
if (expander.begin(XCA9554_ADDR)) {
    expander.pinMode(0, OUTPUT);  // Touch Reset
    expander.pinMode(1, OUTPUT);  // LCD Reset
    expander.digitalWrite(0, LOW);
    expander.digitalWrite(1, LOW);
    delay(20);
    expander.digitalWrite(0, HIGH);
    expander.digitalWrite(1, HIGH);
}

// NEW (2.06"): Direct GPIO Control
pinMode(LCD_RESET, OUTPUT);   // GPIO 8
pinMode(TP_RESET, OUTPUT);    // GPIO 9
digitalWrite(LCD_RESET, LOW);
digitalWrite(TP_RESET, LOW);
delay(20);
digitalWrite(LCD_RESET, HIGH);
digitalWrite(TP_RESET, HIGH);
```

#### 3. Touch Interrupt
```cpp
// OLD (1.8"):
#define TP_INT 21
pinMode(21, INPUT);

// NEW (2.06"):
#define TP_INT 38
pinMode(38, INPUT);
```

#### 4. Remove XCA9554 Code
```cpp
// DELETE these lines completely for 2.06":
#define XCA9554_ADDR 0x20
class XCA9554 { ... };  // Remove entire class
XCA9554 expander;        // Remove instance
```

## Unchanged Components

✅ **I2C Bus:** SDA=GPIO15, SCL=GPIO14  
✅ **AXP2101 PMU:** Address 0x34  
✅ **QMI8658 IMU:** Address 0x6B  
✅ **PCF85063 RTC:** Address 0x51  
✅ **FT3168 Touch:** Address 0x38  
✅ **SD Card Interface:** CLK=2, CMD=1, DATA=3  

## Testing Checklist

After porting code to 2.06":

- [ ] Display shows correctly (no offset/rotation issues)
- [ ] Touch responds (check GPIO 38 interrupt)
- [ ] LCD reset works (GPIO 8)
- [ ] Touch reset works (GPIO 9)
- [ ] Battery percentage reads correctly
- [ ] RTC time updates
- [ ] IMU detects motion/steps
- [ ] SD card mounts successfully
- [ ] WiFi connects
- [ ] Audio output (if using ES8311)

## Common Pitfalls

### ⚠️ Touch Not Working
- **Cause:** Still using GPIO 21 instead of GPIO 38
- **Fix:** Update `TP_INT` to 38 in pin definitions

### ⚠️ Display Stays Black
- **Cause:** LCD reset not initialized or still trying to use XCA9554
- **Fix:** Remove all XCA9554 code, add direct GPIO 8 reset

### ⚠️ Touch Unresponsive After Reset
- **Cause:** Touch reset (GPIO 9) not initialized
- **Fix:** Add TP_RESET GPIO initialization in setup()

### ⚠️ Compile Errors About XCA9554
- **Cause:** Leftover expander code
- **Fix:** Remove XCA9554 class definition and all references

### ⚠️ Audio Issues
- **Cause:** I2S pins changed between boards
- **Fix:** Update I2S_BCK (41), I2S_DI (40), I2S_DO (42)

## Files Modified in This Port

1. ✅ `pin_config.h` - Complete pin redefinition
2. ✅ `S3_MiniOS.ino` - Display driver, reset control, removed expander
3. ✅ `README.md` - Documentation for 2.06" board
4. ✅ `wifi/config.txt` - WiFi template (unchanged)

## Resources

- **2.06" Board Wiki:** https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06
- **2.06" Examples:** https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06
- **1.8" Board Wiki:** https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8
- **Arduino_GFX Library:** https://github.com/moononournation/Arduino_GFX

---

**Last Updated:** January 2026  
**Tested On:** ESP32-S3-Touch-AMOLED-2.06 Hardware
