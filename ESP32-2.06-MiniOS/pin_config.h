/**
 * ESP32-S3-Touch-AMOLED-2.06 Pin Configuration
 * Adapted from 1.8" version for 2.06" board
 * Based on official Waveshare pinout
 */

#pragma once

// Power chip define (required for XPowersLib)
#define XPOWERS_CHIP_AXP2101

// ============================================
// DISPLAY (QSPI - CO5300 AMOLED)
// ============================================
#define LCD_SDIO0       4
#define LCD_SDIO1       5
#define LCD_SDIO2       6
#define LCD_SDIO3       7
#define LCD_SCLK        11
#define LCD_CS          12
#define LCD_RESET       8       // Direct GPIO (not via expander)
#define LCD_WIDTH       410     // 2.06" display width
#define LCD_HEIGHT      502     // 2.06" display height

// ============================================
// I2C BUS (ALL PERIPHERALS)
// ============================================
#define IIC_SDA         15
#define IIC_SCL         14

// ============================================
// TOUCH CONTROLLER (FT3168)
// ============================================
#define TP_INT          38      // Changed from GPIO 21 (1.8") to GPIO 38 (2.06")
#define TP_RESET        9       // Direct GPIO (not via expander)
#define FT3168_ADDR     0x38

// ============================================
// NOTE: XCA9554 I/O EXPANDER NOT PRESENT
// The 2.06" board does NOT have XCA9554 expander
// LCD and Touch resets are direct GPIO pins
// ============================================

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

// I2S Pins (ES8311 Audio) - 2.06" board configuration
#define I2S_MCK_IO      16
#define I2S_BCK_IO      41
#define I2S_WS_IO       45
#define I2S_DO_IO       42
#define I2S_DI_IO       40

// Alternative I2S naming (for compatibility)
#define MCLKPIN         16
#define BCLKPIN         41
#define WSPIN           45
#define DOPIN           42
#define DIPIN           40

// Power Amplifier
#define PA_PIN          46
#define PA              46

// ============================================
// SD CARD (SDMMC - 1-bit mode)
// ============================================
#define SDMMC_CLK       2
#define SDMMC_CMD       1
#define SDMMC_DATA      3
#define SDMMC_CS        17

// ============================================
// BUTTONS
// ============================================
#define BOOT_PIN        0
#define PWR_PIN         10
