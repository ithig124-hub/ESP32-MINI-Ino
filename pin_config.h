/**
 * ESP32-S3-Touch-AMOLED-1.8 Pin Configuration
 * Based on official Waveshare pinout
 * Compatible with pocketClock and Waveshare examples
 */

#pragma once

// Power chip define (required for XPowersLib)
#define XPOWERS_CHIP_AXP2101

// ============================================
// DISPLAY (QSPI - SH8601 AMOLED)
// ============================================
#define LCD_SDIO0       4
#define LCD_SDIO1       5
#define LCD_SDIO2       6
#define LCD_SDIO3       7
#define LCD_SCLK        11
#define LCD_CS          12
#define LCD_WIDTH       368
#define LCD_HEIGHT      448

// ============================================
// I2C BUS (ALL PERIPHERALS)
// ============================================
#define IIC_SDA         15
#define IIC_SCL         14

// ============================================
// TOUCH CONTROLLER (FT3168)
// ============================================
#define TP_INT          21
#define FT3168_ADDR     0x38

// ============================================
// I/O EXPANDER (XCA9554) - CRITICAL!
// Controls power to peripherals
// ============================================
#define XCA9554_ADDR    0x20

// XCA9554 Pin Assignments:
// P0 = Touch Reset
// P1 = Display Reset  
// P2 = Peripheral Power Enable
// P4 = Backlight Control (input)
// P5 = PMU IRQ (input)

// ============================================
// POWER MANAGEMENT (AXP2101)
// ============================================
#define AXP2101_ADDR    0x34

// ============================================
// RTC (PCF85063)
// ============================================
#define PCF85063_ADDR   0x51

// ============================================
// IMU / ACCELEROMETER (QMI8658)
// ============================================
#define QMI8658_ADDR    0x6B

// ============================================
// AUDIO CODEC (ES8311)
// ============================================
#define ES8311_ADDR     0x18

// I2S Pins (ES8311 Audio)
#define I2S_MCK_IO      16
#define I2S_BCK_IO      9
#define I2S_WS_IO       45
#define I2S_DO_IO       10
#define I2S_DI_IO       8

// Alternative I2S naming (for pocketClock compatibility)
#define MCLKPIN         16
#define BCLKPIN         9
#define WSPIN           45
#define DOPIN           10
#define DIPIN           8

// Power Amplifier
#define PA_PIN          46
#define PA              46

// ============================================
// SD CARD (SDMMC - 1-bit mode)
// ============================================
#define SDMMC_CLK       2
#define SDMMC_CMD       1
#define SDMMC_DATA      3
