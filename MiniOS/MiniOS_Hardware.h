/**
 * ESP32-S3-Touch-AMOLED-1.8 MiniOS Hardware Abstraction
 * Complete hardware initialization with proper power sequencing
 */

#pragma once

#include <Wire.h>
#include "pin_config.h"

// ============================================
// HARDWARE STATUS
// ============================================
struct HardwareStatus {
    bool display;
    bool i2c;
    bool expander;
    bool touch;
    bool pmu;
    bool rtc;
    bool imu;
    bool audio;
};

// ============================================
// XCA9554 I/O EXPANDER CLASS
// ============================================
class XCA9554 {
public:
    bool begin(TwoWire *wire = &Wire, uint8_t addr = XCA9554_ADDR) {
        _wire = wire;
        _addr = addr;
        
        _wire->beginTransmission(_addr);
        if (_wire->endTransmission() != 0) {
            return false;
        }
        
        // Set P0, P1, P2 as outputs, P4, P5 as inputs
        // Config register: 1 = input, 0 = output
        writeRegister(0x03, 0b11111000);  // P0-P2 output, P3-P7 input
        
        return true;
    }
    
    void pinMode(uint8_t pin, uint8_t mode) {
        uint8_t config = readRegister(0x03);
        if (mode == OUTPUT) {
            config &= ~(1 << pin);
        } else {
            config |= (1 << pin);
        }
        writeRegister(0x03, config);
    }
    
    void digitalWrite(uint8_t pin, uint8_t val) {
        uint8_t output = readRegister(0x01);
        if (val) {
            output |= (1 << pin);
        } else {
            output &= ~(1 << pin);
        }
        writeRegister(0x01, output);
    }
    
    uint8_t digitalRead(uint8_t pin) {
        uint8_t input = readRegister(0x00);
        return (input >> pin) & 0x01;
    }
    
private:
    TwoWire *_wire;
    uint8_t _addr;
    
    void writeRegister(uint8_t reg, uint8_t val) {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        _wire->write(val);
        _wire->endTransmission();
    }
    
    uint8_t readRegister(uint8_t reg) {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        _wire->endTransmission();
        _wire->requestFrom(_addr, (uint8_t)1);
        return _wire->read();
    }
};

// ============================================
// SIMPLE FT3168 TOUCH DRIVER
// ============================================
class FT3168_Touch {
public:
    bool begin(TwoWire *wire = &Wire) {
        _wire = wire;
        
        _wire->beginTransmission(FT3168_ADDR);
        if (_wire->endTransmission() != 0) {
            return false;
        }
        
        // Read chip ID
        uint8_t id = readRegister(0xA3);
        Serial.printf("Touch IC ID: 0x%02X\n", id);
        
        return true;
    }
    
    bool isTouched() {
        uint8_t touches = readRegister(0x02);
        return (touches & 0x0F) > 0;
    }
    
    void getPoint(int16_t *x, int16_t *y) {
        uint8_t data[4];
        readRegisters(0x03, data, 4);
        
        *x = ((data[0] & 0x0F) << 8) | data[1];
        *y = ((data[2] & 0x0F) << 8) | data[3];
    }
    
    uint8_t getFingers() {
        return readRegister(0x02) & 0x0F;
    }
    
private:
    TwoWire *_wire;
    
    uint8_t readRegister(uint8_t reg) {
        _wire->beginTransmission(FT3168_ADDR);
        _wire->write(reg);
        _wire->endTransmission();
        _wire->requestFrom(FT3168_ADDR, (uint8_t)1);
        return _wire->read();
    }
    
    void readRegisters(uint8_t reg, uint8_t *buf, uint8_t len) {
        _wire->beginTransmission(FT3168_ADDR);
        _wire->write(reg);
        _wire->endTransmission();
        _wire->requestFrom(FT3168_ADDR, len);
        for (uint8_t i = 0; i < len; i++) {
            buf[i] = _wire->read();
        }
    }
};

// ============================================
// SIMPLE AXP2101 PMU DRIVER
// ============================================
class AXP2101_PMU {
public:
    bool begin(TwoWire *wire = &Wire) {
        _wire = wire;
        
        _wire->beginTransmission(AXP2101_ADDR);
        if (_wire->endTransmission() != 0) {
            return false;
        }
        
        // Read chip ID
        uint8_t id = readRegister(0x03);
        Serial.printf("PMU Chip ID: 0x%02X\n", id);
        
        // Enable battery voltage ADC
        enableADC();
        
        return true;
    }
    
    void enableADC() {
        // Enable VBAT, VBUS, VSYS measurement
        writeRegister(0x30, 0x07);
    }
    
    float getBatteryVoltage() {
        uint8_t high = readRegister(0x34);
        uint8_t low = readRegister(0x35);
        uint16_t raw = ((high & 0x3F) << 8) | low;
        return raw * 1.0f;  // Already in mV
    }
    
    float getVbusVoltage() {
        uint8_t high = readRegister(0x38);
        uint8_t low = readRegister(0x39);
        uint16_t raw = ((high & 0x3F) << 8) | low;
        return raw * 1.0f;  // mV
    }
    
    uint8_t getBatteryPercent() {
        return readRegister(0xA4) & 0x7F;
    }
    
    bool isCharging() {
        uint8_t status = readRegister(0x01);
        return ((status >> 5) & 0x03) == 0x01;
    }
    
    bool isVbusPresent() {
        uint8_t status = readRegister(0x00);
        return (status & 0x20) != 0;
    }
    
    float getTemperature() {
        // AXP2101 internal temp sensor
        uint8_t high = readRegister(0x3C);
        uint8_t low = readRegister(0x3D);
        uint16_t raw = ((high & 0x3F) << 8) | low;
        return 22.0f + (raw - 2825) * 0.1f;  // Approximate conversion
    }
    
private:
    TwoWire *_wire;
    
    uint8_t readRegister(uint8_t reg) {
        _wire->beginTransmission(AXP2101_ADDR);
        _wire->write(reg);
        _wire->endTransmission();
        _wire->requestFrom(AXP2101_ADDR, (uint8_t)1);
        return _wire->read();
    }
    
    void writeRegister(uint8_t reg, uint8_t val) {
        _wire->beginTransmission(AXP2101_ADDR);
        _wire->write(reg);
        _wire->write(val);
        _wire->endTransmission();
    }
};

// ============================================
// SIMPLE PCF85063 RTC DRIVER
// ============================================
class PCF85063_RTC {
public:
    bool begin(TwoWire *wire = &Wire) {
        _wire = wire;
        
        _wire->beginTransmission(PCF85063_ADDR);
        if (_wire->endTransmission() != 0) {
            return false;
        }
        
        // Start oscillator
        writeRegister(0x00, 0x00);
        
        return true;
    }
    
    void getTime(uint8_t *hour, uint8_t *minute, uint8_t *second) {
        uint8_t data[3];
        readRegisters(0x04, data, 3);
        
        *second = bcdToDec(data[0] & 0x7F);
        *minute = bcdToDec(data[1] & 0x7F);
        *hour = bcdToDec(data[2] & 0x3F);
    }
    
    void setTime(uint8_t hour, uint8_t minute, uint8_t second) {
        writeRegister(0x04, decToBcd(second));
        writeRegister(0x05, decToBcd(minute));
        writeRegister(0x06, decToBcd(hour));
    }
    
    void getDate(uint8_t *day, uint8_t *month, uint16_t *year) {
        uint8_t data[4];
        readRegisters(0x07, data, 4);
        
        *day = bcdToDec(data[0] & 0x3F);
        *month = bcdToDec(data[2] & 0x1F);
        *year = 2000 + bcdToDec(data[3]);
    }
    
private:
    TwoWire *_wire;
    
    uint8_t bcdToDec(uint8_t bcd) {
        return ((bcd >> 4) * 10) + (bcd & 0x0F);
    }
    
    uint8_t decToBcd(uint8_t dec) {
        return ((dec / 10) << 4) | (dec % 10);
    }
    
    uint8_t readRegister(uint8_t reg) {
        _wire->beginTransmission(PCF85063_ADDR);
        _wire->write(reg);
        _wire->endTransmission();
        _wire->requestFrom(PCF85063_ADDR, (uint8_t)1);
        return _wire->read();
    }
    
    void readRegisters(uint8_t reg, uint8_t *buf, uint8_t len) {
        _wire->beginTransmission(PCF85063_ADDR);
        _wire->write(reg);
        _wire->endTransmission();
        _wire->requestFrom(PCF85063_ADDR, len);
        for (uint8_t i = 0; i < len; i++) {
            buf[i] = _wire->read();
        }
    }
    
    void writeRegister(uint8_t reg, uint8_t val) {
        _wire->beginTransmission(PCF85063_ADDR);
        _wire->write(reg);
        _wire->write(val);
        _wire->endTransmission();
    }
};

// ============================================
// SIMPLE QMI8658 IMU DRIVER
// ============================================
class QMI8658_IMU {
public:
    bool begin(TwoWire *wire = &Wire) {
        _wire = wire;
        
        _wire->beginTransmission(QMI8658_ADDR);
        if (_wire->endTransmission() != 0) {
            return false;
        }
        
        // Read WHO_AM_I
        uint8_t id = readRegister(0x00);
        Serial.printf("IMU WHO_AM_I: 0x%02X\n", id);
        
        if (id != 0x05) {
            return false;
        }
        
        // Reset
        writeRegister(0x60, 0xB0);
        delay(10);
        
        // Configure accelerometer: +/- 8g, 500Hz
        writeRegister(0x03, 0x02);  // ACC config
        
        // Configure gyroscope: +/- 512 dps, 500Hz
        writeRegister(0x04, 0x52);  // GYRO config
        
        // Enable sensors
        writeRegister(0x08, 0x03);  // Enable ACC + GYRO
        
        return true;
    }
    
    void getAccel(float *x, float *y, float *z) {
        uint8_t data[6];
        readRegisters(0x35, data, 6);
        
        int16_t rawX = (data[1] << 8) | data[0];
        int16_t rawY = (data[3] << 8) | data[2];
        int16_t rawZ = (data[5] << 8) | data[4];
        
        // Scale for +/- 8g
        *x = rawX * (8.0f / 32768.0f);
        *y = rawY * (8.0f / 32768.0f);
        *z = rawZ * (8.0f / 32768.0f);
    }
    
    void getGyro(float *x, float *y, float *z) {
        uint8_t data[6];
        readRegisters(0x3B, data, 6);
        
        int16_t rawX = (data[1] << 8) | data[0];
        int16_t rawY = (data[3] << 8) | data[2];
        int16_t rawZ = (data[5] << 8) | data[4];
        
        // Scale for +/- 512 dps
        *x = rawX * (512.0f / 32768.0f);
        *y = rawY * (512.0f / 32768.0f);
        *z = rawZ * (512.0f / 32768.0f);
    }
    
private:
    TwoWire *_wire;
    
    uint8_t readRegister(uint8_t reg) {
        _wire->beginTransmission(QMI8658_ADDR);
        _wire->write(reg);
        _wire->endTransmission();
        _wire->requestFrom(QMI8658_ADDR, (uint8_t)1);
        return _wire->read();
    }
    
    void readRegisters(uint8_t reg, uint8_t *buf, uint8_t len) {
        _wire->beginTransmission(QMI8658_ADDR);
        _wire->write(reg);
        _wire->endTransmission();
        _wire->requestFrom(QMI8658_ADDR, len);
        for (uint8_t i = 0; i < len; i++) {
            buf[i] = _wire->read();
        }
    }
    
    void writeRegister(uint8_t reg, uint8_t val) {
        _wire->beginTransmission(QMI8658_ADDR);
        _wire->write(reg);
        _wire->write(val);
        _wire->endTransmission();
    }
};
