/**
 * ============================================
 * ESP32-S3-Touch-AMOLED-1.8 - FULL MiniOS
 * With Display, Touch, PMU, RTC, IMU
 * ============================================
 * 
 * REQUIRED LIBRARIES (copy from Waveshare repo):
 * - GFX_Library_for_Arduino (with SH8601 support)
 * - Arduino_DriveBus
 * - XPowersLib
 * - Adafruit_XCA9554
 * 
 * Download from: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8
 * Copy: examples/Arduino-v3.3.5/libraries/* to Arduino/libraries/
 */

#include <Wire.h>
#include <Arduino.h>
#include "Arduino_GFX_Library.h"

// ============================================
// PIN DEFINITIONS
// ============================================
// Display (QSPI)
#define LCD_SDIO0       4
#define LCD_SDIO1       5
#define LCD_SDIO2       6
#define LCD_SDIO3       7
#define LCD_SCLK        11
#define LCD_CS          12
#define LCD_WIDTH       368
#define LCD_HEIGHT      448

// I2C
#define IIC_SDA         15
#define IIC_SCL         14

// Touch
#define TP_INT          21
#define FT3168_ADDR     0x38

// I/O Expander
#define XCA9554_ADDR    0x20

// PMU
#define AXP2101_ADDR    0x34

// RTC
#define PCF85063_ADDR   0x51

// IMU
#define QMI8658_ADDR    0x6B

// Colors (RGB565)
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define GRAY    0x7BEF

// ============================================
// DISPLAY SETUP (QSPI SH8601 AMOLED)
// ============================================
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS,    // CS
    LCD_SCLK,  // SCK
    LCD_SDIO0, // SDIO0
    LCD_SDIO1, // SDIO1
    LCD_SDIO2, // SDIO2
    LCD_SDIO3  // SDIO3
);

Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus,
    GFX_NOT_DEFINED, // RST
    0,               // rotation
    false,           // IPS
    LCD_WIDTH,
    LCD_HEIGHT
);

// ============================================
// SIMPLE XCA9554 I/O EXPANDER DRIVER
// ============================================
class XCA9554 {
public:
    bool begin(uint8_t addr = 0x20) {
        _addr = addr;
        Wire.beginTransmission(_addr);
        return Wire.endTransmission() == 0;
    }
    
    void pinMode(uint8_t pin, uint8_t mode) {
        uint8_t config = readReg(0x03);
        if (mode == OUTPUT) {
            config &= ~(1 << pin);
        } else {
            config |= (1 << pin);
        }
        writeReg(0x03, config);
    }
    
    void digitalWrite(uint8_t pin, uint8_t val) {
        uint8_t output = readReg(0x01);
        if (val) {
            output |= (1 << pin);
        } else {
            output &= ~(1 << pin);
        }
        writeReg(0x01, output);
    }
    
private:
    uint8_t _addr;
    
    void writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(_addr);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }
    
    uint8_t readReg(uint8_t reg) {
        Wire.beginTransmission(_addr);
        Wire.write(reg);
        Wire.endTransmission();
        Wire.requestFrom(_addr, (uint8_t)1);
        return Wire.read();
    }
};

// I/O Expander
XCA9554 expander;

// ============================================
// SIMPLE TOUCH DRIVER
// ============================================
class TouchDriver {
public:
    bool begin() {
        Wire.beginTransmission(FT3168_ADDR);
        return Wire.endTransmission() == 0;
    }
    
    bool touched() {
        Wire.beginTransmission(FT3168_ADDR);
        Wire.write(0x02);
        Wire.endTransmission();
        Wire.requestFrom(FT3168_ADDR, (uint8_t)1);
        return (Wire.read() & 0x0F) > 0;
    }
    
    void getPoint(int16_t *x, int16_t *y) {
        Wire.beginTransmission(FT3168_ADDR);
        Wire.write(0x03);
        Wire.endTransmission();
        Wire.requestFrom(FT3168_ADDR, (uint8_t)4);
        uint8_t d[4];
        for(int i=0; i<4; i++) d[i] = Wire.read();
        *x = ((d[0] & 0x0F) << 8) | d[1];
        *y = ((d[2] & 0x0F) << 8) | d[3];
    }
};

// ============================================
// SIMPLE PMU DRIVER
// ============================================
class PMUDriver {
public:
    bool begin() {
        Wire.beginTransmission(AXP2101_ADDR);
        return Wire.endTransmission() == 0;
    }
    
    uint16_t getBattVoltage() {
        uint8_t h = readReg(0x34);
        uint8_t l = readReg(0x35);
        return ((h & 0x3F) << 8) | l;
    }
    
    uint8_t getBattPercent() {
        return readReg(0xA4) & 0x7F;
    }
    
private:
    uint8_t readReg(uint8_t reg) {
        Wire.beginTransmission(AXP2101_ADDR);
        Wire.write(reg);
        Wire.endTransmission();
        Wire.requestFrom(AXP2101_ADDR, (uint8_t)1);
        return Wire.read();
    }
};

// ============================================
// SIMPLE RTC DRIVER
// ============================================
class RTCDriver {
public:
    bool begin() {
        Wire.beginTransmission(PCF85063_ADDR);
        return Wire.endTransmission() == 0;
    }
    
    void getTime(uint8_t *h, uint8_t *m, uint8_t *s) {
        Wire.beginTransmission(PCF85063_ADDR);
        Wire.write(0x04);
        Wire.endTransmission();
        Wire.requestFrom(PCF85063_ADDR, (uint8_t)3);
        *s = bcd2dec(Wire.read() & 0x7F);
        *m = bcd2dec(Wire.read() & 0x7F);
        *h = bcd2dec(Wire.read() & 0x3F);
    }
    
private:
    uint8_t bcd2dec(uint8_t b) { return ((b >> 4) * 10) + (b & 0x0F); }
};

// Instances
TouchDriver touch;
PMUDriver pmu;
RTCDriver rtc;

// ============================================
// SETUP
// ============================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n========================================");
    Serial.println("  ESP32-S3-Touch-AMOLED-1.8 MiniOS");
    Serial.println("========================================\n");
    
    // ----------------------------------------
    // 1. Initialize I2C
    // ----------------------------------------
    Serial.println("[1] Init I2C...");
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    Serial.println("    I2C OK\n");
    
    // ----------------------------------------
    // 2. Initialize I/O Expander & Power Sequence
    // ----------------------------------------
    Serial.println("[2] Init I/O Expander...");
    if (!expander.begin(XCA9554_ADDR)) {
        Serial.println("    ERROR: XCA9554 not found!");
    } else {
        Serial.println("    XCA9554 OK");
        
        // Configure pins
        expander.pinMode(0, OUTPUT);  // Touch RST
        expander.pinMode(1, OUTPUT);  // Display RST
        expander.pinMode(2, OUTPUT);  // Peripheral power
        
        // Power cycle
        Serial.println("    Power cycling...");
        expander.digitalWrite(0, LOW);
        expander.digitalWrite(1, LOW);
        expander.digitalWrite(2, LOW);
        delay(50);
        expander.digitalWrite(0, HIGH);
        expander.digitalWrite(1, HIGH);
        expander.digitalWrite(2, HIGH);
        delay(100);
        Serial.println("    Power sequence OK\n");
    }
    
    // ----------------------------------------
    // 3. Initialize DISPLAY (CRITICAL!)
    // ----------------------------------------
    Serial.println("[3] Init Display...");
    if (!gfx->begin()) {
        Serial.println("    ERROR: Display init failed!");
    } else {
        Serial.println("    Display OK");
        
        // Set brightness (0-255)
        gfx->setBrightness(255);
        
        // Clear screen
        gfx->fillScreen(BLACK);
        
        // Show startup message
        gfx->setTextColor(WHITE);
        gfx->setTextSize(3);
        gfx->setCursor(50, 180);
        gfx->println("MiniOS");
        gfx->setTextSize(2);
        gfx->setCursor(80, 230);
        gfx->println("Starting...");
        
        Serial.println("    Brightness set to 255\n");
    }
    
    // ----------------------------------------
    // 4. Initialize Touch
    // ----------------------------------------
    Serial.println("[4] Init Touch...");
    if (touch.begin()) {
        Serial.println("    Touch OK\n");
    } else {
        Serial.println("    Touch NOT FOUND\n");
    }
    
    // ----------------------------------------
    // 5. Initialize PMU
    // ----------------------------------------
    Serial.println("[5] Init PMU...");
    if (pmu.begin()) {
        Serial.println("    PMU OK\n");
    } else {
        Serial.println("    PMU NOT FOUND\n");
    }
    
    // ----------------------------------------
    // 6. Initialize RTC
    // ----------------------------------------
    Serial.println("[6] Init RTC...");
    if (rtc.begin()) {
        Serial.println("    RTC OK\n");
    } else {
        Serial.println("    RTC NOT FOUND\n");
    }
    
    // ----------------------------------------
    // Show main screen
    // ----------------------------------------
    delay(1000);
    drawMainScreen();
    
    Serial.println("========================================");
    Serial.println("  MiniOS Ready!");
    Serial.println("========================================\n");
}

// ============================================
// DRAW MAIN SCREEN
// ============================================
void drawMainScreen() {
    gfx->fillScreen(BLACK);
    
    // Header
    gfx->fillRect(0, 0, LCD_WIDTH, 50, 0x1082);  // Dark blue
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(100, 15);
    gfx->println("MiniOS v1.0");
    
    // Status area
    gfx->drawRect(10, 60, LCD_WIDTH-20, 100, WHITE);
    gfx->setCursor(20, 75);
    gfx->setTextSize(2);
    gfx->println("System Status");
    
    // Touch area
    gfx->drawRect(10, 170, LCD_WIDTH-20, 200, 0x07E0);  // Green border
    gfx->setCursor(20, 185);
    gfx->setTextColor(0x07E0);
    gfx->println("Touch Area");
    gfx->setTextColor(WHITE);
    gfx->setCursor(20, 210);
    gfx->setTextSize(1);
    gfx->println("Draw here!");
    
    // Footer
    gfx->setCursor(50, LCD_HEIGHT - 30);
    gfx->setTextSize(1);
    gfx->setTextColor(0x7BEF);  // Gray
    gfx->println("ESP32-S3-Touch-AMOLED-1.8");
}

// ============================================
// UPDATE STATUS
// ============================================
void updateStatus() {
    // Battery
    uint16_t battV = pmu.getBattVoltage();
    uint8_t battP = pmu.getBattPercent();
    
    // Time
    uint8_t h, m, s;
    rtc.getTime(&h, &m, &s);
    
    // Clear status area
    gfx->fillRect(15, 95, LCD_WIDTH-30, 55, BLACK);
    
    // Draw battery
    gfx->setTextColor(WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(20, 100);
    gfx->printf("Batt: %dmV %d%%", battV, battP);
    
    // Draw time
    gfx->setCursor(20, 125);
    gfx->printf("Time: %02d:%02d:%02d", h, m, s);
}

// ============================================
// MAIN LOOP
// ============================================
static unsigned long lastUpdate = 0;
static int16_t lastX = -1, lastY = -1;

void loop() {
    // Update status every 2 seconds
    if (millis() - lastUpdate > 2000) {
        lastUpdate = millis();
        updateStatus();
    }
    
    // Handle touch
    if (touch.touched()) {
        int16_t x, y;
        touch.getPoint(&x, &y);
        
        // Draw on touch area (170 to 370)
        if (y >= 170 && y <= 370) {
            if (lastX >= 0 && lastY >= 0) {
                gfx->drawLine(lastX, lastY, x, y, 0xF800);  // Red
            }
            gfx->fillCircle(x, y, 3, 0xF800);  // Red dot
            lastX = x;
            lastY = y;
        }
        
        Serial.printf("Touch: X=%d Y=%d\n", x, y);
    } else {
        lastX = -1;
        lastY = -1;
    }
    
    delay(10);
}
