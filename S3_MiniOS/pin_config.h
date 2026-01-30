/**
 * ═══════════════════════════════════════════════════════════════════════════════
 *  Widget OS - Pin Configuration
 *  ESP32-S3-Touch-AMOLED-1.8" (WOS-180A)
 * ═══════════════════════════════════════════════════════════════════════════════
 * 
 *  This file defines all hardware pin mappings for the 1.8" AMOLED board.
 *  Based on Waveshare ESP32-S3-Touch-AMOLED-1.8 reference design.
 * 
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// ═══════════════════════════════════════════════════════════════════════════════
//  DISPLAY - SH8601 QSPI AMOLED (1.8" 368x448)
// ═══════════════════════════════════════════════════════════════════════════════
#define LCD_SDIO0       4
#define LCD_SDIO1       5
#define LCD_SDIO2       6
#define LCD_SDIO3       7
#define LCD_SCLK        11
#define LCD_CS          12
#define LCD_RESET       8

// Display dimensions
#define LCD_WIDTH       368
#define LCD_HEIGHT      448

// ═══════════════════════════════════════════════════════════════════════════════
//  I2C BUS
// ═══════════════════════════════════════════════════════════════════════════════
#define IIC_SDA         15
#define IIC_SCL         14

// ═══════════════════════════════════════════════════════════════════════════════
//  TOUCH CONTROLLER - FT3168
// ═══════════════════════════════════════════════════════════════════════════════
#define TP_INT          38
#define TP_RESET        9
#define FT3168_DEVICE_ADDRESS   0x38

// ═══════════════════════════════════════════════════════════════════════════════
//  IMU - QMI8658
// ═══════════════════════════════════════════════════════════════════════════════
#define IMU_INT         21
#define QMI8658_L_SLAVE_ADDRESS 0x6B

// ═══════════════════════════════════════════════════════════════════════════════
//  RTC - PCF85063
// ═══════════════════════════════════════════════════════════════════════════════
#define RTC_INT         39

// ═══════════════════════════════════════════════════════════════════════════════
//  PMU - AXP2101
// ═══════════════════════════════════════════════════════════════════════════════
#define AXP2101_SLAVE_ADDRESS   0x34

// ═══════════════════════════════════════════════════════════════════════════════
//  SD CARD - SD_MMC 1-bit Mode
//  Reference: Waveshare ESP32-S3-Touch-AMOLED-1.8 SD Test Example
// ═══════════════════════════════════════════════════════════════════════════════
#define SDMMC_CLK       2
#define SDMMC_CMD       1
#define SDMMC_DATA      3

// Compatibility aliases for legacy code
#define SD_CLK          SDMMC_CLK
#define SD_MOSI         SDMMC_CMD
#define SD_MISO         SDMMC_DATA

// ═══════════════════════════════════════════════════════════════════════════════
//  SYSTEM BUTTONS
// ═══════════════════════════════════════════════════════════════════════════════
#define BOOT_BUTTON     0

// ═══════════════════════════════════════════════════════════════════════════════
//  DISPLAY COLOR MACROS
// ═══════════════════════════════════════════════════════════════════════════════
#define RGB565_BLACK    0x0000
#define RGB565_WHITE    0xFFFF
#define RGB565_RED      0xF800
#define RGB565_GREEN    0x07E0
#define RGB565_BLUE     0x001F

#endif // PIN_CONFIG_H
