/**
 * ═══════════════════════════════════════════════════════════════════════════════
 *  PIN CONFIGURATION HEADER - Widget OS
 *  ESP32-S3-Touch-AMOLED-2.06" (Waveshare)
 *  
 *  IMPORTANT: This is for the 2.06" display, NOT the 1.8" display!
 *  Display Driver: CO5300 (not SH8601)
 *  Resolution: 410×502 (not 368×448)
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
//  POWER MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════════
#define XPOWERS_CHIP_AXP2101

// ═══════════════════════════════════════════════════════════════════════════════
//  DISPLAY - CO5300 QSPI AMOLED (410×502 Rectangular)
//  NOTE: 2.06" uses CO5300 driver, NOT SH8601!
// ═══════════════════════════════════════════════════════════════════════════════
#define LCD_SDIO0       4       // QSPI_SIO0
#define LCD_SDIO1       5       // QSPI_SI1
#define LCD_SDIO2       6       // QSPI_SI2
#define LCD_SDIO3       7       // QSPI_SI3
#define LCD_SCLK        11      // QSPI_SCL
#define LCD_CS          12      // LCD_CS
#define LCD_RESET       8       // LCD_RESET

// 2.06" Display Resolution (larger than 1.8")
#define LCD_WIDTH       410
#define LCD_HEIGHT      502

// Column offset required for CO5300 driver
#define LCD_COL_OFFSET1 22
#define LCD_ROW_OFFSET1 0
#define LCD_COL_OFFSET2 0
#define LCD_ROW_OFFSET2 0

// ═══════════════════════════════════════════════════════════════════════════════
//  I2C BUS (Shared: Touch, IMU, RTC, PMU)
// ═══════════════════════════════════════════════════════════════════════════════
#define IIC_SDA         15
#define IIC_SCL         14

// ═══════════════════════════════════════════════════════════════════════════════
//  TOUCH - FT3168
// ═══════════════════════════════════════════════════════════════════════════════
#define TP_INT          38      // Touch Interrupt
#define TP_RESET        9       // Touch Reset

// I2C Address (default)
#define FT3168_DEVICE_ADDRESS   0x38

// ═══════════════════════════════════════════════════════════════════════════════
//  IMU - QMI8658 (6-Axis Accelerometer + Gyroscope)
// ═══════════════════════════════════════════════════════════════════════════════
#define IMU_INT         21      // IMU Interrupt

// I2C Addresses
#define QMI8658_L_SLAVE_ADDRESS 0x6B
#define QMI8658_H_SLAVE_ADDRESS 0x6A

// ═══════════════════════════════════════════════════════════════════════════════
//  RTC - PCF85063
// ═══════════════════════════════════════════════════════════════════════════════
#define RTC_INT         39      // RTC Interrupt

// I2C Address
#define PCF85063_SLAVE_ADDRESS  0x51

// ═══════════════════════════════════════════════════════════════════════════════
//  PMU - AXP2101 (Power Management)
// ═══════════════════════════════════════════════════════════════════════════════
// Uses shared I2C bus (IIC_SDA, IIC_SCL)
#define AXP2101_SLAVE_ADDRESS   0x34

// ═══════════════════════════════════════════════════════════════════════════════
//  SD CARD (SDMMC Mode) - Widget OS Storage
// ═══════════════════════════════════════════════════════════════════════════════
#define SDMMC_CLK       2       // SD_SCK
#define SDMMC_CMD       1       // SD_MOSI
#define SDMMC_DATA      3       // SD_MISO
#define SDMMC_CS        17      // SD_CS (if using SPI mode)

// For compatibility with SD_MMC library
constexpr int SD_CLK  = SDMMC_CLK;
constexpr int SD_MOSI = SDMMC_CMD;
constexpr int SD_MISO = SDMMC_DATA;

// ═══════════════════════════════════════════════════════════════════════════════
//  SYSTEM BUTTONS
// ═══════════════════════════════════════════════════════════════════════════════
#define BOOT_BUTTON     0       // Boot/Flash button (GPIO0)

// ═══════════════════════════════════════════════════════════════════════════════
//  LVGL BUFFER SIZE (for memory allocation)
// ═══════════════════════════════════════════════════════════════════════════════
#define LVGL_BUFFER_LINES   50
