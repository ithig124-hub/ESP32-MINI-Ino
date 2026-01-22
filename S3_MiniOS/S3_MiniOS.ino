/**
 * ═══════════════════════════════════════════════════════════════════════════
 *  S3 MiniOS - ESP32-S3-Touch-AMOLED-1.8 Firmware
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 *  Smartwatch-style OS with Clock, Steps, Focus Timer
 * 
 *  Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.8
 *    • Display: SH8601 QSPI AMOLED 368x448
 *    • Touch: FT3168
 *    • IMU: QMI8658
 *    • RTC: PCF85063
 *    • PMU: AXP2101
 * 
 *  Required Library: GFX Library for Arduino (by moononournation)
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <Wire.h>
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include <Preferences.h>

// ═══════════════════════════════════════════════════════════════════════════
//  HARDWARE PINS
// ═══════════════════════════════════════════════════════════════════════════

#define LCD_SDIO0       4
#define LCD_SDIO1       5
#define LCD_SDIO2       6
#define LCD_SDIO3       7
#define LCD_SCLK        11
#define LCD_CS          12
#define LCD_WIDTH       368
#define LCD_HEIGHT      448

#define IIC_SDA         15
#define IIC_SCL         14
#define TP_INT          21

#define XCA9554_ADDR    0x20
#define AXP2101_ADDR    0x34
#define FT3168_ADDR     0x38
#define PCF85063_ADDR   0x51
#define QMI8658_ADDR    0x6B

// ═══════════════════════════════════════════════════════════════════════════
//  THEME (Dark AMOLED)
// ═══════════════════════════════════════════════════════════════════════════

#define COL_BG          0x0000
#define COL_CARD        0x18E3
#define COL_TEXT        0xFFFF
#define COL_TEXT_DIM    0x7BEF
#define COL_ACCENT      0x07FF
#define COL_SUCCESS     0x07E0
#define COL_WARNING     0xFD20
#define COL_DANGER      0xF800
#define COL_STEPS       0x5E5F
#define COL_FOCUS       0xFC10
#define COL_HEADER      0x10A2

#define HEADER_HEIGHT   52
#define BTN_HEIGHT      56
#define BTN_RADIUS      12
#define CARD_RADIUS     16
#define MARGIN          12

// ═══════════════════════════════════════════════════════════════════════════
//  DISPLAY SETUP
// ═══════════════════════════════════════════════════════════════════════════

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);

// ═══════════════════════════════════════════════════════════════════════════
//  SCREEN MANAGER
// ═══════════════════════════════════════════════════════════════════════════

enum Screen { SCREEN_HOME, SCREEN_STEPS, SCREEN_FOCUS };
Screen currentScreen = SCREEN_HOME;
bool uiDirty = true;
bool forceFullRedraw = true;

// ═══════════════════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ═══════════════════════════════════════════════════════════════════════════

// Clock
uint8_t clockHour = 0, clockMinute = 0, clockSecond = 0;
uint8_t lastSecond = 255;

// Steps
uint32_t stepCount = 0;
uint32_t stepGoal = 10000;

// Focus Timer
enum FocusState { FOCUS_IDLE, FOCUS_RUNNING, FOCUS_PAUSED, FOCUS_COMPLETE };
FocusState focusState = FOCUS_IDLE;
uint16_t focusDuration = 25 * 60;
uint16_t focusRemaining = 25 * 60;
uint8_t focusSessions = 0;
unsigned long focusLastTick = 0;

// Touch
int16_t touchX = -1, touchY = -1;
bool lastTouchState = false;
unsigned long touchDebounce = 0;

// Battery
uint16_t batteryVoltage = 0;
uint8_t batteryPercent = 0;

// Timing
unsigned long lastClockUpdate = 0;
unsigned long lastStepUpdate = 0;
unsigned long lastBatteryUpdate = 0;

// Hardware flags
bool hasIMU = false, hasRTC = false, hasPMU = false;

// Preferences
Preferences prefs;

// ═══════════════════════════════════════════════════════════════════════════
//  HARDWARE DRIVERS
// ═══════════════════════════════════════════════════════════════════════════

class XCA9554 {
public:
    bool begin(uint8_t addr = XCA9554_ADDR) {
        _addr = addr;
        Wire.beginTransmission(_addr);
        return Wire.endTransmission() == 0;
    }
    void pinMode(uint8_t pin, uint8_t mode) {
        uint8_t c = readReg(0x03);
        if (mode == OUTPUT) c &= ~(1 << pin); else c |= (1 << pin);
        writeReg(0x03, c);
    }
    void digitalWrite(uint8_t pin, uint8_t val) {
        uint8_t o = readReg(0x01);
        if (val) o |= (1 << pin); else o &= ~(1 << pin);
        writeReg(0x01, o);
    }
private:
    uint8_t _addr;
    void writeReg(uint8_t r, uint8_t v) { Wire.beginTransmission(_addr); Wire.write(r); Wire.write(v); Wire.endTransmission(); }
    uint8_t readReg(uint8_t r) { Wire.beginTransmission(_addr); Wire.write(r); Wire.endTransmission(); Wire.requestFrom(_addr,(uint8_t)1); return Wire.read(); }
};

class FT3168 {
public:
    bool begin() { Wire.beginTransmission(FT3168_ADDR); return Wire.endTransmission() == 0; }
    bool touched() { Wire.beginTransmission(FT3168_ADDR); Wire.write(0x02); Wire.endTransmission(); Wire.requestFrom(FT3168_ADDR,(uint8_t)1); return (Wire.read() & 0x0F) > 0; }
    void getPoint(int16_t *x, int16_t *y) {
        Wire.beginTransmission(FT3168_ADDR); Wire.write(0x03); Wire.endTransmission();
        Wire.requestFrom(FT3168_ADDR,(uint8_t)4);
        uint8_t d[4]; for(int i=0;i<4;i++) d[i]=Wire.read();
        *x = ((d[0]&0x0F)<<8)|d[1]; *y = ((d[2]&0x0F)<<8)|d[3];
    }
};

class PCF85063 {
public:
    bool begin() { Wire.beginTransmission(PCF85063_ADDR); return Wire.endTransmission() == 0; }
    void getTime(uint8_t *h, uint8_t *m, uint8_t *s) {
        Wire.beginTransmission(PCF85063_ADDR); Wire.write(0x04); Wire.endTransmission();
        Wire.requestFrom(PCF85063_ADDR,(uint8_t)3);
        *s = bcd(Wire.read()&0x7F); *m = bcd(Wire.read()&0x7F); *h = bcd(Wire.read()&0x3F);
    }
private:
    uint8_t bcd(uint8_t b) { return ((b>>4)*10)+(b&0x0F); }
};

class AXP2101 {
public:
    bool begin() { Wire.beginTransmission(AXP2101_ADDR); return Wire.endTransmission() == 0; }
    uint16_t getBattVoltage() { uint8_t h=readReg(0x34),l=readReg(0x35); return ((h&0x3F)<<8)|l; }
    uint8_t getBattPercent() { return readReg(0xA4)&0x7F; }
private:
    uint8_t readReg(uint8_t r) { Wire.beginTransmission(AXP2101_ADDR); Wire.write(r); Wire.endTransmission(); Wire.requestFrom(AXP2101_ADDR,(uint8_t)1); return Wire.read(); }
};

class QMI8658 {
public:
    bool begin() {
        Wire.beginTransmission(QMI8658_ADDR);
        if (Wire.endTransmission() != 0) return false;
        if (readReg(0x00) != 0x05) return false;
        writeReg(0x60, 0xB0); delay(10);
        writeReg(0x03, 0x02);
        writeReg(0x08, 0x01);
        return true;
    }
    void getAccel(float *x, float *y, float *z) {
        Wire.beginTransmission(QMI8658_ADDR); Wire.write(0x35); Wire.endTransmission();
        Wire.requestFrom(QMI8658_ADDR,(uint8_t)6);
        uint8_t d[6]; for(int i=0;i<6;i++) d[i]=Wire.read();
        *x = (int16_t)((d[1]<<8)|d[0]) * (8.0f/32768.0f);
        *y = (int16_t)((d[3]<<8)|d[2]) * (8.0f/32768.0f);
        *z = (int16_t)((d[5]<<8)|d[4]) * (8.0f/32768.0f);
    }
private:
    void writeReg(uint8_t r,uint8_t v) { Wire.beginTransmission(QMI8658_ADDR); Wire.write(r); Wire.write(v); Wire.endTransmission(); }
    uint8_t readReg(uint8_t r) { Wire.beginTransmission(QMI8658_ADDR); Wire.write(r); Wire.endTransmission(); Wire.requestFrom(QMI8658_ADDR,(uint8_t)1); return Wire.read(); }
};

XCA9554 expander;
FT3168 touch;
PCF85063 rtc;
AXP2101 pmu;
QMI8658 imu;

// ═══════════════════════════════════════════════════════════════════════════
//  UI HELPERS
// ═══════════════════════════════════════════════════════════════════════════

void drawHeader(const char* title, bool showBack = false) {
    gfx->fillRect(0, 0, LCD_WIDTH, HEADER_HEIGHT, COL_HEADER);
    
    if (showBack) {
        gfx->fillRoundRect(8, 8, 50, 36, 8, COL_CARD);
        gfx->setTextColor(COL_TEXT);
        gfx->setTextSize(2);
        gfx->setCursor(22, 16);
        gfx->print("<");
    }
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(showBack ? 70 : 20, 16);
    gfx->print(title);
    
    if (hasPMU) {
        int bx = LCD_WIDTH - 55;
        gfx->setTextSize(1);
        gfx->setTextColor(batteryPercent > 20 ? COL_SUCCESS : COL_DANGER);
        gfx->setCursor(bx, 20);
        gfx->printf("%d%%", batteryPercent);
    }
}

void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint16_t color) {
    gfx->fillRoundRect(x, y, w, h, BTN_RADIUS, COL_CARD);
    gfx->drawRoundRect(x, y, w, h, BTN_RADIUS, color);
    
    gfx->setTextColor(color);
    gfx->setTextSize(2);
    int16_t tw = strlen(label) * 12;
    gfx->setCursor(x + (w - tw) / 2, y + (h - 16) / 2);
    gfx->print(label);
}

void drawCard(int16_t x, int16_t y, int16_t w, int16_t h) {
    gfx->fillRoundRect(x, y, w, h, CARD_RADIUS, COL_CARD);
}

void drawProgressRing(int16_t cx, int16_t cy, int16_t r, int16_t thick, float prog, uint16_t fg, uint16_t bg) {
    for (int i = 0; i < 360; i += 4) {
        float a = i * DEG_TO_RAD;
        gfx->fillCircle(cx + cos(a)*r, cy + sin(a)*r, thick/2, bg);
    }
    int end = (int)(prog * 360);
    for (int i = 0; i < end; i += 4) {
        float a = (i - 90) * DEG_TO_RAD;
        gfx->fillCircle(cx + cos(a)*r, cy + sin(a)*r, thick/2, fg);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  HOME SCREEN
// ═══════════════════════════════════════════════════════════════════════════

void drawHomeScreen() {
    if (forceFullRedraw) {
        gfx->fillScreen(COL_BG);
        
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setTextSize(2);
        gfx->setCursor(120, 12);
        gfx->print("S3 MiniOS");
    }
    
    if (forceFullRedraw) {
        drawCard(MARGIN, 45, LCD_WIDTH - 2*MARGIN, 110);
    }
    
    if (forceFullRedraw || clockSecond != lastSecond) {
        gfx->fillRect(MARGIN+5, 55, LCD_WIDTH-2*MARGIN-10, 90, COL_CARD);
        
        gfx->setTextSize(6);
        gfx->setTextColor(COL_TEXT);
        gfx->setCursor(60, 65);
        gfx->printf("%02d:%02d", clockHour, clockMinute);
        
        gfx->setTextSize(3);
        gfx->setTextColor(COL_ACCENT);
        gfx->setCursor(285, 85);
        gfx->printf("%02d", clockSecond);
        
        lastSecond = clockSecond;
    }
    
    if (forceFullRedraw) {
        drawCard(MARGIN, 170, (LCD_WIDTH-3*MARGIN)/2, 65);
        gfx->setTextSize(1);
        gfx->setTextColor(COL_STEPS);
        gfx->setCursor(MARGIN+12, 178);
        gfx->print("STEPS");
        
        int fx = MARGIN + (LCD_WIDTH-3*MARGIN)/2 + MARGIN;
        drawCard(fx, 170, (LCD_WIDTH-3*MARGIN)/2, 65);
        gfx->setTextSize(1);
        gfx->setTextColor(COL_FOCUS);
        gfx->setCursor(fx+12, 178);
        gfx->print("FOCUS");
    }
    
    gfx->fillRect(MARGIN+8, 198, 140, 30, COL_CARD);
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN+12, 202);
    gfx->printf("%lu", stepCount);
    
    int fx = MARGIN + (LCD_WIDTH-3*MARGIN)/2 + MARGIN;
    gfx->fillRect(fx+8, 198, 140, 30, COL_CARD);
    gfx->setTextSize(2);
    gfx->setCursor(fx+12, 202);
    if (focusState == FOCUS_RUNNING) {
        gfx->setTextColor(COL_SUCCESS);
        gfx->printf("%d:%02d", focusRemaining/60, focusRemaining%60);
    } else if (focusState == FOCUS_COMPLETE) {
        gfx->setTextColor(COL_ACCENT);
        gfx->print("Done!");
    } else {
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->print("Ready");
    }
    
    if (forceFullRedraw) {
        drawButton(MARGIN, 260, LCD_WIDTH-2*MARGIN, BTN_HEIGHT, "Steps", COL_STEPS);
        drawButton(MARGIN, 330, LCD_WIDTH-2*MARGIN, BTN_HEIGHT, "Focus Timer", COL_FOCUS);
        
        if (hasPMU) {
            gfx->setTextSize(1);
            gfx->setTextColor(COL_TEXT_DIM);
            gfx->setCursor(MARGIN, LCD_HEIGHT - 25);
            gfx->printf("Battery: %dmV  %d%%", batteryVoltage, batteryPercent);
        }
    }
    
    forceFullRedraw = false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STEPS SCREEN
// ═══════════════════════════════════════════════════════════════════════════

void drawStepsScreen() {
    if (forceFullRedraw) {
        gfx->fillScreen(COL_BG);
        drawHeader("Steps", true);
        
        float prog = (float)stepCount / stepGoal;
        drawProgressRing(LCD_WIDTH/2, 175, 75, 10, prog, COL_STEPS, COL_CARD);
        
        gfx->setTextSize(2);
        gfx->setTextColor(COL_TEXT_DIM);
        char g[20]; sprintf(g, "/ %lu", stepGoal);
        gfx->setCursor(145, 215);
        gfx->print(g);
        
        drawCard(MARGIN, 270, (LCD_WIDTH-3*MARGIN)/2, 60);
        gfx->setTextSize(1);
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setCursor(MARGIN+12, 278);
        gfx->print("DISTANCE");
        
        int cx = MARGIN + (LCD_WIDTH-3*MARGIN)/2 + MARGIN;
        drawCard(cx, 270, (LCD_WIDTH-3*MARGIN)/2, 60);
        gfx->setCursor(cx+12, 278);
        gfx->print("CALORIES");
        
        drawButton(MARGIN, 360, LCD_WIDTH-2*MARGIN, 50, "Reset Steps", COL_DANGER);
    }
    
    gfx->fillRect(LCD_WIDTH/2-60, 155, 120, 45, COL_BG);
    gfx->setTextSize(4);
    gfx->setTextColor(COL_TEXT);
    char s[10]; sprintf(s, "%lu", stepCount);
    int sw = strlen(s) * 24;
    gfx->setCursor((LCD_WIDTH-sw)/2, 160);
    gfx->print(s);
    
    float km = (stepCount * 0.7f) / 1000.0f;
    gfx->fillRect(MARGIN+8, 295, 140, 25, COL_CARD);
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN+12, 298);
    gfx->printf("%.1fkm", km);
    
    int cal = (int)(stepCount * 0.04f);
    int cx = MARGIN + (LCD_WIDTH-3*MARGIN)/2 + MARGIN;
    gfx->fillRect(cx+8, 295, 140, 25, COL_CARD);
    gfx->setCursor(cx+12, 298);
    gfx->printf("%dcal", cal);
    
    forceFullRedraw = false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  FOCUS SCREEN
// ═══════════════════════════════════════════════════════════════════════════

void drawFocusScreen() {
    uint16_t bg = (focusState == FOCUS_RUNNING) ? 0x0841 : COL_BG;
    
    if (forceFullRedraw) {
        gfx->fillScreen(bg);
        drawHeader("Focus", true);
    }
    
    float prog = (focusState == FOCUS_IDLE) ? 1.0f : (float)focusRemaining / focusDuration;
    uint16_t rc = (focusState == FOCUS_COMPLETE) ? COL_SUCCESS : COL_FOCUS;
    drawProgressRing(LCD_WIDTH/2, 180, 85, 12, prog, rc, COL_CARD);
    
    gfx->fillRect(LCD_WIDTH/2-65, 155, 130, 55, bg);
    if (focusState == FOCUS_COMPLETE) {
        gfx->setTextSize(3);
        gfx->setTextColor(COL_SUCCESS);
        gfx->setCursor(LCD_WIDTH/2-45, 170);
        gfx->print("DONE!");
    } else {
        gfx->setTextSize(5);
        gfx->setTextColor(COL_TEXT);
        gfx->setCursor(LCD_WIDTH/2-60, 158);
        gfx->printf("%02d:%02d", focusRemaining/60, focusRemaining%60);
    }
    
    if (forceFullRedraw) {
        gfx->setTextSize(2);
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setCursor(115, 285);
        gfx->printf("Sessions: %d", focusSessions);
    }
    
    if (forceFullRedraw || uiDirty) {
        gfx->fillRect(MARGIN, 320, LCD_WIDTH-2*MARGIN, 70, bg);
        
        const char* lbl; uint16_t col;
        switch (focusState) {
            case FOCUS_IDLE: lbl = "Start Focus"; col = COL_SUCCESS; break;
            case FOCUS_RUNNING: lbl = "Pause"; col = COL_WARNING; break;
            case FOCUS_PAUSED: lbl = "Resume"; col = COL_SUCCESS; break;
            case FOCUS_COMPLETE: lbl = "New Session"; col = COL_ACCENT; break;
        }
        drawButton(MARGIN, 330, LCD_WIDTH-2*MARGIN, BTN_HEIGHT, lbl, col);
        
        if (focusState != FOCUS_IDLE) {
            drawButton(MARGIN, 400, LCD_WIDTH-2*MARGIN, 40, "Reset", COL_DANGER);
        }
    }
    
    forceFullRedraw = false;
    uiDirty = false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  TOUCH HANDLING
// ═══════════════════════════════════════════════════════════════════════════

bool inRect(int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    return touchX >= rx && touchX <= rx+rw && touchY >= ry && touchY <= ry+rh;
}

void handleTouch() {
    bool t = touch.touched();
    
    if (t && !lastTouchState && millis() - touchDebounce > 150) {
        touchDebounce = millis();
        touch.getPoint(&touchX, &touchY);
        
        Serial.printf("Touch: X=%d Y=%d\n", touchX, touchY);
        
        if (currentScreen == SCREEN_HOME) {
            if (inRect(MARGIN, 260, LCD_WIDTH-2*MARGIN, BTN_HEIGHT)) {
                currentScreen = SCREEN_STEPS; forceFullRedraw = true;
            } else if (inRect(MARGIN, 330, LCD_WIDTH-2*MARGIN, BTN_HEIGHT)) {
                currentScreen = SCREEN_FOCUS; forceFullRedraw = true;
            } else if (inRect(MARGIN, 170, (LCD_WIDTH-3*MARGIN)/2, 65)) {
                currentScreen = SCREEN_STEPS; forceFullRedraw = true;
            } else if (inRect(MARGIN+(LCD_WIDTH-3*MARGIN)/2+MARGIN, 170, (LCD_WIDTH-3*MARGIN)/2, 65)) {
                currentScreen = SCREEN_FOCUS; forceFullRedraw = true;
            }
        }
        else if (currentScreen == SCREEN_STEPS) {
            if (inRect(8, 8, 50, 36)) {
                currentScreen = SCREEN_HOME; forceFullRedraw = true;
            } else if (inRect(MARGIN, 360, LCD_WIDTH-2*MARGIN, 50)) {
                stepCount = 0; saveData(); forceFullRedraw = true;
            }
        }
        else if (currentScreen == SCREEN_FOCUS) {
            if (inRect(8, 8, 50, 36)) {
                currentScreen = SCREEN_HOME; forceFullRedraw = true;
            } else if (inRect(MARGIN, 330, LCD_WIDTH-2*MARGIN, BTN_HEIGHT)) {
                switch (focusState) {
                    case FOCUS_IDLE: focusState = FOCUS_RUNNING; focusRemaining = focusDuration; focusLastTick = millis(); break;
                    case FOCUS_RUNNING: focusState = FOCUS_PAUSED; break;
                    case FOCUS_PAUSED: focusState = FOCUS_RUNNING; focusLastTick = millis(); break;
                    case FOCUS_COMPLETE: focusState = FOCUS_IDLE; focusRemaining = focusDuration; break;
                }
                uiDirty = true; forceFullRedraw = true;
            } else if (inRect(MARGIN, 400, LCD_WIDTH-2*MARGIN, 40) && focusState != FOCUS_IDLE) {
                focusState = FOCUS_IDLE; focusRemaining = focusDuration; uiDirty = true; forceFullRedraw = true;
            }
        }
    }
    lastTouchState = t;
}

// ═══════════════════════════════════════════════════════════════════════════
//  SERVICES
// ═══════════════════════════════════════════════════════════════════════════

void serviceClock() {
    if (millis() - lastClockUpdate >= 1000) {
        lastClockUpdate = millis();
        if (hasRTC) {
            rtc.getTime(&clockHour, &clockMinute, &clockSecond);
        } else {
            clockSecond++;
            if (clockSecond >= 60) { clockSecond = 0; clockMinute++; }
            if (clockMinute >= 60) { clockMinute = 0; clockHour++; }
            if (clockHour >= 24) clockHour = 0;
        }
        if (currentScreen == SCREEN_HOME) uiDirty = true;
    }
}

void serviceSteps() {
    if (!hasIMU) return;
    if (millis() - lastStepUpdate >= 50) {
        lastStepUpdate = millis();
        float ax, ay, az;
        imu.getAccel(&ax, &ay, &az);
        float mag = sqrt(ax*ax + ay*ay + az*az);
        
        static float pm = 1.0f, ppm = 1.0f;
        static unsigned long lst = 0;
        
        if (pm > ppm && pm > mag && pm > 1.3f && millis() - lst > 300) {
            stepCount++;
            lst = millis();
            if (stepCount % 100 == 0) saveData();
        }
        ppm = pm; pm = mag;
    }
}

void serviceFocus() {
    if (focusState == FOCUS_RUNNING && millis() - focusLastTick >= 1000) {
        focusLastTick = millis();
        if (focusRemaining > 0) {
            focusRemaining--;
            if (currentScreen == SCREEN_FOCUS) uiDirty = true;
        } else {
            focusState = FOCUS_COMPLETE;
            focusSessions++;
            saveData();
            uiDirty = true; forceFullRedraw = true;
        }
    }
}

void serviceBattery() {
    if (!hasPMU) return;
    if (millis() - lastBatteryUpdate >= 5000) {
        lastBatteryUpdate = millis();
        batteryVoltage = pmu.getBattVoltage();
        batteryPercent = pmu.getBattPercent();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  DATA
// ═══════════════════════════════════════════════════════════════════════════

void loadData() {
    prefs.begin("s3os", true);
    stepCount = prefs.getULong("steps", 0);
    focusSessions = prefs.getUChar("sess", 0);
    prefs.end();
}

void saveData() {
    prefs.begin("s3os", false);
    prefs.putULong("steps", stepCount);
    prefs.putUChar("sess", focusSessions);
    prefs.end();
}

// ═══════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n=== S3 MiniOS Starting ===\n");
    
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    Serial.println("[OK] I2C");
    
    if (expander.begin(XCA9554_ADDR)) {
        expander.pinMode(0, OUTPUT);
        expander.pinMode(1, OUTPUT);
        expander.pinMode(2, OUTPUT);
        expander.digitalWrite(0, LOW);
        expander.digitalWrite(1, LOW);
        expander.digitalWrite(2, LOW);
        delay(20);
        expander.digitalWrite(0, HIGH);
        expander.digitalWrite(1, HIGH);
        expander.digitalWrite(2, HIGH);
        delay(50);
        Serial.println("[OK] Expander");
    }
    
    gfx->begin();
    gfx->fillScreen(COL_BG);
    for (int i = 0; i <= 255; i += 5) { gfx->setBrightness(i); delay(2); }
    Serial.println("[OK] Display");
    
    gfx->setTextSize(4);
    gfx->setTextColor(COL_ACCENT);
    gfx->setCursor(70, 180);
    gfx->print("S3 MiniOS");
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(130, 240);
    gfx->print("Loading...");
    
    if (touch.begin()) Serial.println("[OK] Touch");
    hasRTC = rtc.begin(); if (hasRTC) { Serial.println("[OK] RTC"); rtc.getTime(&clockHour, &clockMinute, &clockSecond); }
    hasPMU = pmu.begin(); if (hasPMU) { Serial.println("[OK] PMU"); batteryPercent = pmu.getBattPercent(); batteryVoltage = pmu.getBattVoltage(); }
    hasIMU = imu.begin(); if (hasIMU) Serial.println("[OK] IMU");
    
    loadData();
    
    delay(1500);
    gfx->fillScreen(COL_BG);
    forceFullRedraw = true;
    
    Serial.println("\n=== S3 MiniOS Ready ===\n");
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
    serviceClock();
    serviceSteps();
    serviceFocus();
    serviceBattery();
    handleTouch();
    
    if (uiDirty || forceFullRedraw) {
        switch (currentScreen) {
            case SCREEN_HOME:  drawHomeScreen();  break;
            case SCREEN_STEPS: drawStepsScreen(); break;
            case SCREEN_FOCUS: drawFocusScreen(); break;
        }
        uiDirty = false;
    }
    
    delay(10);
}
