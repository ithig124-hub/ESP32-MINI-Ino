/**
 * ═══════════════════════════════════════════════════════════════════════════════
 *  PIN CONFIGURATION HEADER
 *  ESP32-S3-Touch-AMOLED-2.06 (Waveshare)
 *  
 *  Based on official Waveshare repository:
 *  https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
//  POWER MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════════
#define XPOWERS_CHIP_AXP2101

// ═══════════════════════════════════════════════════════════════════════════════
//  DISPLAY - CO5300 QSPI AMOLED (Round 410x502)
// ═══════════════════════════════════════════════════════════════════════════════
#define LCD_SDIO0       4       // QSPI_SIO0
#define LCD_SDIO1       5       // QSPI_SI1
#define LCD_SDIO2       6       // QSPI_SI2
#define LCD_SDIO3       7       // QSPI_SI3
#define LCD_SCLK        11      // QSPI_SCL
#define LCD_CS          12      // LCD_CS
#define LCD_RESET       8       // LCD_RESET
#define LCD_TE          13      // LCD_TE (Tearing Effect)

#define LCD_WIDTH       410
#define LCD_HEIGHT      502

// ═══════════════════════════════════════════════════════════════════════════════
//  I2C BUS (Shared: Touch, IMU, RTC, PMU, Codec)
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
//  SD CARD (SPI Mode)
// ═══════════════════════════════════════════════════════════════════════════════
#define SDMMC_CLK       2       // SD_SCK
#define SDMMC_CMD       1       // SD_MOSI
#define SDMMC_DATA      3       // SD_MISO
#define SDMMC_CS        17      // SD_CS

// For compatibility with SD_MMC library
constexpr int SD_CLK  = SDMMC_CLK;
constexpr int SD_MOSI = SDMMC_CMD;
constexpr int SD_MISO = SDMMC_DATA;
constexpr int SD_CS   = SDMMC_CS;

// ═══════════════════════════════════════════════════════════════════════════════
//  AUDIO - ES8311 (Codec) + ES7210 (ADC)
// ═══════════════════════════════════════════════════════════════════════════════
#define I2S_MCLK        16      // I2S Master Clock
#define I2S_SCLK        41      // I2S Bit Clock
#define I2S_LRCK        45      // I2S Word Select (L/R Clock)
#define I2S_DOUT        42      // I2S Data Out (to codec)
#define I2S_DIN         40      // I2S Data In (from ADC)
#define PA_CTRL         46      // Audio Power Amplifier Control

// I2C Addresses for audio ICs
#define ES8311_ADDR     0x18
#define ES7210_ADDR     0x40

// ═══════════════════════════════════════════════════════════════════════════════
//  SYSTEM BUTTONS
// ═══════════════════════════════════════════════════════════════════════════════
#define BOOT_BUTTON     0       // Boot/Flash button (GPIO0)
#define PWR_BUTTON      10      // Power button

// ═══════════════════════════════════════════════════════════════════════════════
//  MEMORY CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
// 32MB Flash, 8MB PSRAM (OPI PSRAM)

// ═══════════════════════════════════════════════════════════════════════════════
//  LVGL BUFFER SIZE (for memory allocation)
// ═══════════════════════════════════════════════════════════════════════════════
#define LVGL_BUFFER_LINES   50
