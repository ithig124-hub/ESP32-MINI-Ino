/**
 * ============================================
 * ESP32-S3-Touch-AMOLED-1.8 - I2C Test
 * ============================================
 * 
 * Upload this FIRST to verify I2C is working.
 * 
 * EXPECTED OUTPUT:
 *   0x18 (ES8311 Audio)
 *   0x20 (XCA9554 I/O Expander) <-- CRITICAL
 *   0x34 (AXP2101 PMU)
 *   0x38 (FT3168 Touch)
 *   0x51 (PCF85063 RTC)
 *   0x6A (QMI8658 IMU)
 * 
 * If you see these devices, the fix is working!
 */

#include <Wire.h>

// CORRECT I2C PINS FOR THIS BOARD!
#define IIC_SDA 15
#define IIC_SCL 14

// Use USB CDC for ESP32-S3
#include "HWCDC.h"
HWCDC USBSerial;

void setup() {
    USBSerial.begin(115200);
    delay(3000);  // Wait for USB enumeration
    
    USBSerial.println("\n\n");
    USBSerial.println("================================================");
    USBSerial.println("  ESP32-S3-Touch-AMOLED-1.8 I2C Test");
    USBSerial.println("================================================");
    USBSerial.println();
    USBSerial.printf("I2C Pins: SDA=GPIO%d, SCL=GPIO%d\n\n", IIC_SDA, IIC_SCL);
    
    // Initialize I2C with CORRECT pins
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    
    USBSerial.println("Scanning I2C bus...\n");
    
    int devicesFound = 0;
    
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t error = Wire.endTransmission();
        
        if (error == 0) {
            USBSerial.printf("  [OK] 0x%02X ", addr);
            
            switch(addr) {
                case 0x18: 
                    USBSerial.println("- ES8311 Audio Codec"); 
                    break;
                case 0x20: 
                    USBSerial.println("- XCA9554 I/O Expander [CRITICAL]"); 
                    break;
                case 0x34: 
                    USBSerial.println("- AXP2101 Power Management"); 
                    break;
                case 0x38: 
                    USBSerial.println("- FT3168 Touch Controller"); 
                    break;
                case 0x51: 
                    USBSerial.println("- PCF85063 Real-Time Clock"); 
                    break;
                case 0x6A: 
                    USBSerial.println("- QMI8658 IMU/Accelerometer"); 
                    break;
                default:   
                    USBSerial.println("- Unknown device"); 
                    break;
            }
            devicesFound++;
        }
    }
    
    USBSerial.println();
    USBSerial.println("------------------------------------------------");
    USBSerial.printf("Total devices found: %d\n", devicesFound);
    USBSerial.println("------------------------------------------------");
    
    if (devicesFound >= 5) {
        USBSerial.println("\n SUCCESS! I2C is working correctly!");
        USBSerial.println("You can now upload the full MiniOS.");
    } else if (devicesFound > 0) {
        USBSerial.println("\n PARTIAL: Some devices found.");
        USBSerial.println("The XCA9554 may need to power-cycle peripherals.");
    } else {
        USBSerial.println("\n FAILED: No devices found!");
        USBSerial.println("Check your wiring or board connection.");
    }
    
    USBSerial.println("\n================================================\n");
}

void loop() {
    // Rescan every 5 seconds
    delay(5000);
    
    USBSerial.println("Rescanning...");
    int count = 0;
    
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            count++;
        }
    }
    
    USBSerial.printf("Devices: %d\n\n", count);
}
