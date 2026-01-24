/**
 * ═══════════════════════════════════════════════════════════════════════════
 *  S3 MiniOS - ESP32-S3-Touch-AMOLED-1.8 Firmware
 *  ADVANCED CARD-BASED OPERATING SYSTEM
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 *  Card-based smartwatch OS with infinite swipe navigation
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
 *  NAVIGATION:
 *    - Swipe LEFT/RIGHT: Navigate between main category cards (infinite loop)
 *    - Swipe DOWN from main card: Enter category stack
 *    - In stack: LEFT/RIGHT navigates sub-cards, UP returns to main card
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <Wire.h>
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include <Preferences.h>
#include <math.h>

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
//  THEME (Modern AMOLED Design)
// ═══════════════════════════════════════════════════════════════════════════

#define COL_BG          0x0000
#define COL_CARD        0x18E3
#define COL_CARD_LIGHT  0x2945
#define COL_TEXT        0xFFFF
#define COL_TEXT_DIM    0x7BEF
#define COL_TEXT_DIMMER 0x4208

// Vibrant accent colors for different categories
#define COL_CLOCK       0x07FF   // Cyan
#define COL_STEPS       0x5E5F   // Purple-blue
#define COL_MUSIC       0xF81F   // Magenta
#define COL_GAMES       0xFD20   // Orange
#define COL_WEATHER     0x07E0   // Green
#define COL_SYSTEM      0xFFE0   // Yellow
#define COL_SUCCESS     0x07E0
#define COL_WARNING     0xFD20
#define COL_DANGER      0xF800

#define MARGIN          12
#define CARD_RADIUS     16
#define BTN_RADIUS      12

// ═══════════════════════════════════════════════════════════════════════════
//  DISPLAY SETUP
// ═══════════════════════════════════════════════════════════════════════════

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);

// ═══════════════════════════════════════════════════════════════════════════
//  NAVIGATION SYSTEM
// ═══════════════════════════════════════════════════════════════════════════

enum MainCard { 
    CARD_CLOCK = 0,
    CARD_STEPS = 1, 
    CARD_MUSIC = 2,
    CARD_GAMES = 3,
    CARD_WEATHER = 4,
    CARD_SYSTEM = 5,
    MAIN_CARD_COUNT = 6
};

enum SubCard {
    SUB_NONE = 0,
    
    // Clock sub-cards (1-9)
    SUB_CLOCK_WORLD = 1,
    SUB_CLOCK_DATE = 2,
    SUB_CLOCK_ALARM = 3,
    
    // Steps sub-cards (10-19)
    SUB_STEPS_GRAPH = 10,
    SUB_STEPS_DISTANCE = 11,
    SUB_STEPS_DEBUG = 12,
    
    // Music sub-cards (20-29)
    SUB_MUSIC_PLAYLIST = 20,
    SUB_MUSIC_PLAYER = 21,
    
    // Games sub-cards (30-39)
    SUB_GAMES_YESNO = 30,
    SUB_GAMES_CLICKER = 31,
    SUB_GAMES_REACTION = 32,
    
    // Weather sub-cards (40-49)
    SUB_WEATHER_FORECAST = 40,
    SUB_WEATHER_DETAILS = 41,
    
    // System sub-cards (50-59)
    SUB_SYSTEM_STATS = 50,
    SUB_SYSTEM_INFO = 51
};

MainCard currentMainCard = CARD_CLOCK;
SubCard currentSubCard = SUB_NONE;
int subCardIndex = 0;
bool redrawNeeded = true;

// ═══════════════════════════════════════════════════════════════════════════
//  GESTURE DETECTION
// ═══════════════════════════════════════════════════════════════════════════

enum GestureType { GESTURE_NONE, GESTURE_LEFT, GESTURE_RIGHT, GESTURE_UP, GESTURE_DOWN };

int16_t gestureStartX = -1, gestureStartY = -1;
int16_t gestureEndX = -1, gestureEndY = -1;
unsigned long gestureStartTime = 0;
bool gestureInProgress = false;

#define SWIPE_THRESHOLD 80
#define SWIPE_TIMEOUT 500

// ═══════════════════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ═══════════════════════════════════════════════════════════════════════════

// Clock
uint8_t clockHour = 10, clockMinute = 30, clockSecond = 0;
uint8_t lastSecond = 255;
const char* daysOfWeek[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
uint8_t currentDay = 3; // Wednesday

// Steps
uint32_t stepCount = 6054;
uint32_t stepGoal = 10000;
uint32_t stepHistory[7] = {4200, 5800, 6100, 7300, 5200, 6054, 0};

// Music
bool musicPlaying = false;
uint8_t musicProgress = 35; // 0-100
const char* musicTitle = "Night Drive";
const char* musicArtist = "Synthwave FM";
uint16_t musicDuration = 245; // seconds
uint16_t musicCurrent = 86;

// Games
uint8_t spotlightGame = 0; // 0=YesNo, 1=Clicker, 2=Reaction
unsigned long lastGameRotate = 0;
uint32_t clickerScore = 0;
uint16_t reactionTime = 0;
bool reactionWaiting = false;
unsigned long reactionStartTime = 0;

// Weather (mock data)
int8_t weatherTemp = 22;
const char* weatherCondition = "Partly Cloudy";
uint8_t weatherHumidity = 65;
uint16_t weatherWind = 12; // km/h
int8_t forecast[5] = {22, 24, 23, 21, 20};

// System stats
uint8_t cpuUsage = 45;
uint32_t freeRAM = 234567;
float temperature = 42.5;
uint16_t fps = 60;

// Battery
uint16_t batteryVoltage = 4100;
uint8_t batteryPercent = 85;

// Timing
unsigned long lastClockUpdate = 0;
unsigned long lastStepUpdate = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastStatsUpdate = 0;
unsigned long lastMusicUpdate = 0;

// Hardware flags
bool hasIMU = false, hasRTC = false, hasPMU = false;

// Preferences
Preferences prefs;

// Touch
int16_t touchX = -1, touchY = -1;
bool lastTouchState = false;
unsigned long touchDebounce = 0;

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
//  UI WIDGET FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void drawGradientCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c1, uint16_t c2) {
    // Vertical gradient
    for (int16_t i = 0; i < h; i++) {
        uint8_t r1 = (c1 >> 11) & 0x1F;
        uint8_t g1 = (c1 >> 5) & 0x3F;
        uint8_t b1 = c1 & 0x1F;
        
        uint8_t r2 = (c2 >> 11) & 0x1F;
        uint8_t g2 = (c2 >> 5) & 0x3F;
        uint8_t b2 = c2 & 0x1F;
        
        float t = (float)i / h;
        uint8_t r = r1 + (r2 - r1) * t;
        uint8_t g = g1 + (g2 - g1) * t;
        uint8_t b = b1 + (b2 - b1) * t;
        
        uint16_t col = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
        gfx->drawFastHLine(x, y + i, w, col);
    }
    
    // Rounded corners overlay
    gfx->drawRoundRect(x, y, w, h, CARD_RADIUS, c1);
}

void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color = COL_CARD) {
    gfx->fillRoundRect(x, y, w, h, CARD_RADIUS, color);
}

void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint16_t color) {
    gfx->fillRoundRect(x, y, w, h, BTN_RADIUS, color);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    int16_t tw = strlen(label) * 12;
    gfx->setCursor(x + (w - tw) / 2, y + (h - 16) / 2);
    gfx->print(label);
}

void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, float progress, uint16_t fg, uint16_t bg) {
    gfx->fillRoundRect(x, y, w, h, h/2, bg);
    int16_t fw = (int16_t)(w * progress);
    if (fw > 0) {
        gfx->fillRoundRect(x, y, fw, h, h/2, fg);
    }
}

void drawSegmentedProgress(int16_t x, int16_t y, int16_t w, int16_t h, float progress, uint16_t fg, uint16_t bg) {
    // 4 segments like reference image
    int16_t segW = (w - 36) / 4;
    int16_t segH = h;
    int currentSeg = (int)(progress * 4);
    
    for (int i = 0; i < 4; i++) {
        int16_t sx = x + i * (segW + 12);
        if (i < currentSeg) {
            gfx->fillRoundRect(sx, y, segW, segH, 4, fg);
        } else if (i == currentSeg) {
            float partial = (progress * 4) - currentSeg;
            gfx->fillRoundRect(sx, y, segW, segH, 4, bg);
            if (partial > 0) {
                int16_t pw = (int16_t)(segW * partial);
                gfx->fillRoundRect(sx, y, pw, segH, 4, fg);
            }
        } else {
            gfx->fillRoundRect(sx, y, segW, segH, 4, bg);
        }
    }
}

void drawProgressRing(int16_t cx, int16_t cy, int16_t r, int16_t thick, float prog, uint16_t fg, uint16_t bg) {
    // Background circle
    for (int i = 0; i < 360; i += 6) {
        float a = i * DEG_TO_RAD;
        gfx->fillCircle(cx + cos(a)*r, cy + sin(a)*r, thick/2, bg);
    }
    // Foreground arc
    int end = (int)(prog * 360);
    for (int i = 0; i < end; i += 6) {
        float a = (i - 90) * DEG_TO_RAD;
        gfx->fillCircle(cx + cos(a)*r, cy + sin(a)*r, thick/2, fg);
    }
}

void drawBatteryIcon(int16_t x, int16_t y, uint8_t percent) {
    uint16_t col = percent > 20 ? COL_SUCCESS : COL_DANGER;
    gfx->drawRect(x, y + 2, 20, 10, COL_TEXT_DIM);
    gfx->fillRect(x + 20, y + 4, 2, 6, COL_TEXT_DIM);
    int16_t fillW = (18 * percent) / 100;
    gfx->fillRect(x + 1, y + 3, fillW, 8, col);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN CARD SCREENS
// ═══════════════════════════════════════════════════════════════════════════

void drawMainClock() {
    gfx->fillScreen(COL_BG);
    
    // Top indicator
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("CLOCK");
    
    // Battery indicator
    drawBatteryIcon(LCD_WIDTH - 35, 8, batteryPercent);
    
    // Main gradient card
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, 0x4A5F, 0x001F);
    
    // App icon/branding
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("S3 MiniOS");
    
    // Day
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 120);
    gfx->print(daysOfWeek[currentDay]);
    
    // Time - large display
    gfx->setTextSize(7);
    gfx->setTextColor(COL_TEXT);
    char timeStr[10];
    sprintf(timeStr, "%02d:%02d", clockHour, clockMinute);
    gfx->setCursor(MARGIN + 20, 155);
    gfx->print(timeStr);
    
    // Seconds
    gfx->setTextSize(4);
    gfx->setTextColor(COL_CLOCK);
    gfx->setCursor(MARGIN + 260, 180);
    gfx->printf("%02d", clockSecond);
    
    // Progress milestones (hours of day)
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    int16_t progY = 270;
    gfx->setCursor(MARGIN + 15, progY);
    gfx->print("00:00");
    gfx->setCursor(MARGIN + 90, progY);
    gfx->print("06:00");
    gfx->setCursor(MARGIN + 165, progY);
    gfx->print("12:00");
    gfx->setCursor(MARGIN + 240, progY);
    gfx->print("18:00");
    
    // Time progress bar (through the day)
    float dayProgress = (clockHour * 60.0f + clockMinute) / 1440.0f;
    drawSegmentedProgress(MARGIN + 15, 285, LCD_WIDTH - 2*MARGIN - 30, 8, dayProgress, COL_CLOCK, COL_CARD_LIGHT);
    
    // Navigation hints
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down for more");
}

void drawMainSteps() {
    gfx->fillScreen(COL_BG);
    
    // Top indicator
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("STEPS");
    
    drawBatteryIcon(LCD_WIDTH - 35, 8, batteryPercent);
    
    // Main gradient card
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, 0x5A5F, 0x4011);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("S3 MiniOS");
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 120);
    gfx->print("Steps:");
    
    // Large step count
    gfx->setTextSize(7);
    gfx->setTextColor(COL_TEXT);
    char stepsStr[10];
    sprintf(stepsStr, "%lu", stepCount);
    gfx->setCursor(MARGIN + 20, 155);
    gfx->print(stepsStr);
    
    // Goal indicator
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    char goalStr[20];
    sprintf(goalStr, "/ %lu", stepGoal);
    gfx->setCursor(MARGIN + 220, 195);
    gfx->print(goalStr);
    
    // Progress milestones
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(MARGIN + 15, 270);
    gfx->print("2000");
    gfx->setCursor(MARGIN + 100, 270);
    gfx->print("4000");
    gfx->setCursor(MARGIN + 185, 270);
    gfx->print("6000");
    gfx->setCursor(MARGIN + 270, 270);
    gfx->print("8000");
    
    float stepProgress = (float)stepCount / 8000.0f;
    if (stepProgress > 1.0f) stepProgress = 1.0f;
    drawSegmentedProgress(MARGIN + 15, 285, LCD_WIDTH - 2*MARGIN - 30, 8, stepProgress, COL_STEPS, COL_CARD_LIGHT);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down for more");
}

void drawMainMusic() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("MUSIC");
    
    drawBatteryIcon(LCD_WIDTH - 35, 8, batteryPercent);
    
    // Music card with gradient
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 300, 0xF81F, 0x001F);
    
    // Album art placeholder
    gfx->fillRoundRect(MARGIN + 20, 90, 120, 120, 12, COL_CARD_LIGHT);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 45, 145);
    gfx->print("Album Art");
    
    // Song info
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN + 160, 110);
    gfx->print("Night");
    gfx->setCursor(MARGIN + 160, 135);
    gfx->print("Drive");
    
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 160, 165);
    gfx->print("Synthwave FM");
    
    // Playback controls
    int16_t controlY = 240;
    int16_t controlCX = LCD_WIDTH / 2;
    
    // Prev button
    gfx->fillCircle(controlCX - 60, controlY, 18, COL_CARD_LIGHT);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(controlCX - 69, controlY - 8);
    gfx->print("|<");
    
    // Play/Pause button
    gfx->fillCircle(controlCX, controlY, 25, COL_TEXT);
    gfx->setTextColor(COL_BG);
    gfx->setTextSize(3);
    if (musicPlaying) {
        gfx->setCursor(controlCX - 9, controlY - 12);
        gfx->print("||");
    } else {
        gfx->setCursor(controlCX - 6, controlY - 12);
        gfx->print(">");
    }
    
    // Next button
    gfx->fillCircle(controlCX + 60, controlY, 18, COL_CARD_LIGHT);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(controlCX + 51, controlY - 8);
    gfx->print(">|");
    
    // Progress bar
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 20, 300);
    gfx->printf("%d:%02d", musicCurrent / 60, musicCurrent % 60);
    gfx->setCursor(LCD_WIDTH - MARGIN - 30, 300);
    gfx->printf("%d:%02d", musicDuration / 60, musicDuration % 60);
    
    drawProgressBar(MARGIN + 20, 320, LCD_WIDTH - 2*MARGIN - 40, 8, (float)musicCurrent / musicDuration, COL_MUSIC, COL_CARD_LIGHT);
}

void drawMainGames() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("GAMES");
    
    drawBatteryIcon(LCD_WIDTH - 35, 8, batteryPercent);
    
    // Games hub card
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, 0xFD20, 0xF800);
    
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 90);
    gfx->print("GAMES HUB");
    
    // Spotlight game changes every 5 minutes
    const char* gameName;
    const char* gameDesc;
    
    switch(spotlightGame) {
        case 0:
            gameName = "Yes or No";
            gameDesc = "Quick decision maker";
            break;
        case 1:
            gameName = "Clicker";
            gameDesc = "Tap as fast as you can";
            break;
        case 2:
            gameName = "Reaction Test";
            gameDesc = "Test your reflexes";
            break;
        default:
            gameName = "Mystery Game";
            gameDesc = "Coming soon...";
    }
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(MARGIN + 20, 140);
    gfx->print("FEATURED GAME");
    
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 165);
    gfx->print(gameName);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 205);
    gfx->print(gameDesc);
    
    // Game icons/indicators
    int16_t iconY = 245;
    gfx->fillCircle(MARGIN + 40, iconY, 15, spotlightGame == 0 ? COL_TEXT : COL_CARD_LIGHT);
    gfx->fillCircle(MARGIN + 100, iconY, 15, spotlightGame == 1 ? COL_TEXT : COL_CARD_LIGHT);
    gfx->fillCircle(MARGIN + 160, iconY, 15, spotlightGame == 2 ? COL_TEXT : COL_CARD_LIGHT);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down to play");
}

void drawMainWeather() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("WEATHER");
    
    drawBatteryIcon(LCD_WIDTH - 35, 8, batteryPercent);
    
    // Weather card
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, 0x07E0, 0x001F);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("San Francisco");
    
    // Large temperature
    gfx->setTextSize(8);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 40, 130);
    gfx->printf("%d", weatherTemp);
    
    gfx->setTextSize(4);
    gfx->setCursor(MARGIN + 145, 135);
    gfx->print("o");
    
    // Condition
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 225);
    gfx->print(weatherCondition);
    
    // Additional info
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 20, 265);
    gfx->printf("Humidity: %d%%", weatherHumidity);
    gfx->setCursor(MARGIN + 20, 285);
    gfx->printf("Wind: %d km/h", weatherWind);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down for forecast");
}

void drawMainSystem() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("SYSTEM");
    
    drawBatteryIcon(LCD_WIDTH - 35, 8, batteryPercent);
    
    // System stats card
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 300, 0xFFE0, 0x4208);
    
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 85);
    gfx->print("SYSTEM INFO");
    
    // Stats grid
    int16_t statY = 140;
    int16_t statSpacing = 45;
    
    // CPU
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(MARGIN + 20, statY);
    gfx->print("CPU USAGE");
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, statY + 15);
    gfx->printf("%d%%", cpuUsage);
    drawProgressBar(MARGIN + 100, statY + 22, 200, 8, (float)cpuUsage / 100.0f, COL_SYSTEM, COL_CARD_LIGHT);
    
    // RAM
    statY += statSpacing;
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(MARGIN + 20, statY);
    gfx->print("FREE RAM");
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, statY + 15);
    gfx->printf("%lu KB", freeRAM / 1024);
    
    // Temperature
    statY += statSpacing;
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(MARGIN + 20, statY);
    gfx->print("TEMPERATURE");
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, statY + 15);
    gfx->printf("%.1fC", temperature);
    
    // FPS
    statY += statSpacing;
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(MARGIN + 20, statY);
    gfx->print("FPS");
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, statY + 15);
    gfx->printf("%d", fps);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down for more");
}

// ═══════════════════════════════════════════════════════════════════════════
//  SUB-CARD SCREENS
// ═══════════════════════════════════════════════════════════════════════════

void drawSubClockWorld() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 20);
    gfx->print("< World Clock");
    
    // Time zones
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 70, COL_CARD);
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 90);
    gfx->print("NEW YORK");
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 15, 110);
    gfx->printf("%02d:%02d", (clockHour + 20) % 24, clockMinute);
    
    drawCard(MARGIN, 165, LCD_WIDTH - 2*MARGIN, 70, COL_CARD);
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 175);
    gfx->print("LONDON");
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 15, 195);
    gfx->printf("%02d:%02d", (clockHour + 5) % 24, clockMinute);
    
    drawCard(MARGIN, 250, LCD_WIDTH - 2*MARGIN, 70, COL_CARD);
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 260);
    gfx->print("TOKYO");
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 15, 280);
    gfx->printf("%02d:%02d", (clockHour + 14) % 24, clockMinute);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

void drawSubStepsGraph() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 20);
    gfx->print("< Weekly Steps");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 280, COL_CARD);
    
    // Simple bar chart
    const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    int16_t barW = 35;
    int16_t barSpacing = 10;
    int16_t chartY = 320;
    int16_t chartH = 180;
    
    for (int i = 0; i < 7; i++) {
        int16_t barX = MARGIN + 20 + i * (barW + barSpacing);
        float ratio = (float)stepHistory[i] / 10000.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        int16_t barH = (int16_t)(chartH * ratio);
        
        uint16_t barColor = (i == 5) ? COL_STEPS : COL_CARD_LIGHT;
        gfx->fillRoundRect(barX, chartY - barH, barW, barH, 4, barColor);
        
        gfx->setTextSize(1);
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setCursor(barX + 5, chartY + 10);
        gfx->print(days[i]);
    }
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

void drawSubGamesYesNo() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 20);
    gfx->print("< Yes or No");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 120, COL_CARD);
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 30, 110);
    gfx->print("Should I do it?");
    
    // Yes button
    drawButton(MARGIN + 20, 230, 140, 80, "YES", COL_SUCCESS);
    
    // No button
    drawButton(MARGIN + 180, 230, 140, 80, "NO", COL_DANGER);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

void drawSubGamesClicker() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 20);
    gfx->print("< Clicker");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 100, COL_CARD);
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 30, 95);
    gfx->print("Score:");
    gfx->setTextSize(4);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 100, 125);
    gfx->printf("%lu", clickerScore);
    
    // Big click button
    gfx->fillCircle(LCD_WIDTH / 2, 270, 70, COL_GAMES);
    gfx->setTextSize(4);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(LCD_WIDTH / 2 - 40, 255);
    gfx->print("CLICK");
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

void drawSubGamesReaction() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 20);
    gfx->print("< Reaction Test");
    
    if (reactionWaiting) {
        // Green screen - tap now!
        gfx->fillRoundRect(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 280, CARD_RADIUS, COL_SUCCESS);
        gfx->setTextSize(4);
        gfx->setTextColor(COL_TEXT);
        gfx->setCursor(LCD_WIDTH / 2 - 70, 200);
        gfx->print("TAP NOW!");
    } else if (reactionTime > 0) {
        // Show result
        drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 120, COL_CARD);
        gfx->setTextSize(2);
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setCursor(MARGIN + 30, 110);
        gfx->print("Your time:");
        gfx->setTextSize(4);
        gfx->setTextColor(COL_TEXT);
        gfx->setCursor(MARGIN + 80, 145);
        gfx->printf("%dms", reactionTime);
        
        drawButton(MARGIN + 50, 240, LCD_WIDTH - 2*MARGIN - 100, 60, "Try Again", COL_GAMES);
    } else {
        // Ready state
        drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 120, COL_CARD);
        gfx->setTextSize(2);
        gfx->setTextColor(COL_TEXT);
        gfx->setCursor(MARGIN + 40, 120);
        gfx->print("Tap to start");
        
        drawButton(MARGIN + 50, 240, LCD_WIDTH - 2*MARGIN - 100, 80, "START", COL_GAMES);
    }
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

// ═══════════════════════════════════════════════════════════════════════════
//  GESTURE DETECTION
// ═══════════════════════════════════════════════════════════════════════════

GestureType detectGesture() {
    if (gestureStartX < 0 || gestureEndX < 0) return GESTURE_NONE;
    
    int16_t dx = gestureEndX - gestureStartX;
    int16_t dy = gestureEndY - gestureStartY;
    
    if (abs(dx) > abs(dy)) {
        // Horizontal swipe
        if (abs(dx) > SWIPE_THRESHOLD) {
            return (dx > 0) ? GESTURE_RIGHT : GESTURE_LEFT;
        }
    } else {
        // Vertical swipe
        if (abs(dy) > SWIPE_THRESHOLD) {
            return (dy > 0) ? GESTURE_DOWN : GESTURE_UP;
        }
    }
    
    return GESTURE_NONE;
}

void handleGesture(GestureType gesture) {
    if (gesture == GESTURE_NONE) return;
    
    if (currentSubCard == SUB_NONE) {
        // We're on a main card
        switch (gesture) {
            case GESTURE_LEFT:
                currentMainCard = (MainCard)((currentMainCard + 1) % MAIN_CARD_COUNT);
                redrawNeeded = true;
                break;
            case GESTURE_RIGHT:
                currentMainCard = (MainCard)((currentMainCard + MAIN_CARD_COUNT - 1) % MAIN_CARD_COUNT);
                redrawNeeded = true;
                break;
            case GESTURE_DOWN:
                // Enter sub-card stack
                subCardIndex = 0;
                switch (currentMainCard) {
                    case CARD_CLOCK:
                        currentSubCard = SUB_CLOCK_WORLD;
                        break;
                    case CARD_STEPS:
                        currentSubCard = SUB_STEPS_GRAPH;
                        break;
                    case CARD_MUSIC:
                        currentSubCard = SUB_MUSIC_PLAYER;
                        break;
                    case CARD_GAMES:
                        currentSubCard = SUB_GAMES_YESNO;
                        break;
                    case CARD_WEATHER:
                        currentSubCard = SUB_WEATHER_FORECAST;
                        break;
                    case CARD_SYSTEM:
                        currentSubCard = SUB_SYSTEM_STATS;
                        break;
                }
                redrawNeeded = true;
                break;
            default:
                break;
        }
    } else {
        // We're in a sub-card
        if (gesture == GESTURE_UP) {
            // Return to main card
            currentSubCard = SUB_NONE;
            subCardIndex = 0;
            redrawNeeded = true;
        } else if (gesture == GESTURE_LEFT || gesture == GESTURE_RIGHT) {
            // Navigate within sub-cards
            int direction = (gesture == GESTURE_LEFT) ? 1 : -1;
            
            switch (currentMainCard) {
                case CARD_CLOCK:
                    if (gesture == GESTURE_LEFT) {
                        if (currentSubCard == SUB_CLOCK_WORLD) currentSubCard = SUB_CLOCK_DATE;
                        else if (currentSubCard == SUB_CLOCK_DATE) currentSubCard = SUB_CLOCK_ALARM;
                        else currentSubCard = SUB_CLOCK_WORLD;
                    } else {
                        if (currentSubCard == SUB_CLOCK_ALARM) currentSubCard = SUB_CLOCK_DATE;
                        else if (currentSubCard == SUB_CLOCK_DATE) currentSubCard = SUB_CLOCK_WORLD;
                        else currentSubCard = SUB_CLOCK_ALARM;
                    }
                    break;
                case CARD_STEPS:
                    if (gesture == GESTURE_LEFT) {
                        if (currentSubCard == SUB_STEPS_GRAPH) currentSubCard = SUB_STEPS_DISTANCE;
                        else if (currentSubCard == SUB_STEPS_DISTANCE) currentSubCard = SUB_STEPS_DEBUG;
                        else currentSubCard = SUB_STEPS_GRAPH;
                    } else {
                        if (currentSubCard == SUB_STEPS_DEBUG) currentSubCard = SUB_STEPS_DISTANCE;
                        else if (currentSubCard == SUB_STEPS_DISTANCE) currentSubCard = SUB_STEPS_GRAPH;
                        else currentSubCard = SUB_STEPS_DEBUG;
                    }
                    break;
                case CARD_GAMES:
                    if (gesture == GESTURE_LEFT) {
                        if (currentSubCard == SUB_GAMES_YESNO) currentSubCard = SUB_GAMES_CLICKER;
                        else if (currentSubCard == SUB_GAMES_CLICKER) currentSubCard = SUB_GAMES_REACTION;
                        else currentSubCard = SUB_GAMES_YESNO;
                    } else {
                        if (currentSubCard == SUB_GAMES_REACTION) currentSubCard = SUB_GAMES_CLICKER;
                        else if (currentSubCard == SUB_GAMES_CLICKER) currentSubCard = SUB_GAMES_YESNO;
                        else currentSubCard = SUB_GAMES_REACTION;
                    }
                    break;
                default:
                    break;
            }
            redrawNeeded = true;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  TOUCH HANDLING
// ═══════════════════════════════════════════════════════════════════════════

bool inRect(int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    return touchX >= rx && touchX <= rx+rw && touchY >= ry && touchY <= ry+rh;
}

void handleTouch() {
    bool t = touch.touched();
    
    if (t && !lastTouchState) {
        // Touch started
        touch.getPoint(&gestureStartX, &gestureStartY);
        gestureInProgress = true;
        gestureStartTime = millis();
    } else if (!t && lastTouchState && gestureInProgress) {
        // Touch ended
        touch.getPoint(&gestureEndX, &gestureEndY);
        
        if (millis() - gestureStartTime < SWIPE_TIMEOUT) {
            GestureType gesture = detectGesture();
            
            if (gesture != GESTURE_NONE) {
                handleGesture(gesture);
            } else {
                // Tap detected - handle button presses
                touch.getPoint(&touchX, &touchY);
                
                if (currentSubCard == SUB_GAMES_CLICKER) {
                    // Clicker game - anywhere tap
                    if (inRect(LCD_WIDTH/2 - 70, 200, 140, 140)) {
                        clickerScore++;
                        redrawNeeded = true;
                    }
                } else if (currentSubCard == SUB_GAMES_REACTION) {
                    // Reaction test
                    if (!reactionWaiting && reactionTime == 0) {
                        // Start button
                        if (inRect(MARGIN + 50, 240, LCD_WIDTH - 2*MARGIN - 100, 80)) {
                            reactionWaiting = true;
                            reactionStartTime = millis() + random(1000, 3000);
                            redrawNeeded = true;
                        }
                    } else if (reactionWaiting) {
                        // User tapped during green screen
                        if (millis() >= reactionStartTime) {
                            reactionTime = millis() - reactionStartTime;
                            reactionWaiting = false;
                            redrawNeeded = true;
                        }
                    } else if (reactionTime > 0) {
                        // Try again button
                        if (inRect(MARGIN + 50, 240, LCD_WIDTH - 2*MARGIN - 100, 60)) {
                            reactionTime = 0;
                            redrawNeeded = true;
                        }
                    }
                } else if (currentMainCard == CARD_MUSIC && currentSubCard == SUB_NONE) {
                    // Music player controls
                    int16_t controlY = 240;
                    int16_t controlCX = LCD_WIDTH / 2;
                    
                    // Play/Pause button
                    if (inRect(controlCX - 25, controlY - 25, 50, 50)) {
                        musicPlaying = !musicPlaying;
                        redrawNeeded = true;
                    }
                }
            }
        }
        
        gestureInProgress = false;
        gestureStartX = gestureStartY = gestureEndX = gestureEndY = -1;
    }
    
    lastTouchState = t;
}

// ═══════════════════════════════════════════════════════════════════════════
//  BACKGROUND SERVICES
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
        if (currentMainCard == CARD_CLOCK && currentSubCard == SUB_NONE) {
            redrawNeeded = true;
        }
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
            stepHistory[5] = stepCount;
            lst = millis();
            if (currentMainCard == CARD_STEPS) redrawNeeded = true;
        }
        ppm = pm; pm = mag;
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

void serviceMusic() {
    if (musicPlaying && millis() - lastMusicUpdate >= 1000) {
        lastMusicUpdate = millis();
        musicCurrent++;
        if (musicCurrent >= musicDuration) {
            musicCurrent = 0;
            musicPlaying = false;
        }
        if (currentMainCard == CARD_MUSIC) redrawNeeded = true;
    }
}

void serviceGames() {
    // Rotate spotlight game every 5 minutes
    if (millis() - lastGameRotate >= 300000) {
        lastGameRotate = millis();
        spotlightGame = (spotlightGame + 1) % 3;
        if (currentMainCard == CARD_GAMES && currentSubCard == SUB_NONE) {
            redrawNeeded = true;
        }
    }
    
    // Reaction test timing
    if (reactionWaiting && millis() >= reactionStartTime && currentSubCard == SUB_GAMES_REACTION) {
        redrawNeeded = true;
    }
}

void serviceStats() {
    if (millis() - lastStatsUpdate >= 2000) {
        lastStatsUpdate = millis();
        // Mock stats update
        cpuUsage = random(30, 70);
        temperature = 40.0f + random(0, 50) / 10.0f;
        if (currentMainCard == CARD_SYSTEM) redrawNeeded = true;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  RENDERING
// ═══════════════════════════════════════════════════════════════════════════

void renderScreen() {
    if (!redrawNeeded) return;
    
    if (currentSubCard == SUB_NONE) {
        // Main card
        switch (currentMainCard) {
            case CARD_CLOCK:   drawMainClock();   break;
            case CARD_STEPS:   drawMainSteps();   break;
            case CARD_MUSIC:   drawMainMusic();   break;
            case CARD_GAMES:   drawMainGames();   break;
            case CARD_WEATHER: drawMainWeather(); break;
            case CARD_SYSTEM:  drawMainSystem();  break;
        }
    } else {
        // Sub-card
        switch (currentSubCard) {
            case SUB_CLOCK_WORLD:     drawSubClockWorld();    break;
            case SUB_STEPS_GRAPH:     drawSubStepsGraph();    break;
            case SUB_GAMES_YESNO:     drawSubGamesYesNo();    break;
            case SUB_GAMES_CLICKER:   drawSubGamesClicker();  break;
            case SUB_GAMES_REACTION:  drawSubGamesReaction(); break;
            default: break;
        }
    }
    
    redrawNeeded = false;
}

// ═══════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("  S3 MiniOS - Card-Based Operating System");
    Serial.println("═══════════════════════════════════════════\n");
    
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    Serial.println("[OK] I2C Bus");
    
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
        Serial.println("[OK] I/O Expander");
    }
    
    gfx->begin();
    gfx->fillScreen(COL_BG);
    for (int i = 0; i <= 255; i += 5) { gfx->setBrightness(i); delay(2); }
    Serial.println("[OK] AMOLED Display");
    
    // Splash screen
    gfx->setTextSize(5);
    gfx->setTextColor(COL_CLOCK);
    gfx->setCursor(50, 160);
    gfx->print("S3 MiniOS");
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(90, 230);
    gfx->print("Card-Based OS");
    gfx->setTextSize(1);
    gfx->setCursor(140, 270);
    gfx->print("Loading...");
    
    if (touch.begin()) Serial.println("[OK] Touch Controller");
    hasRTC = rtc.begin(); 
    if (hasRTC) { 
        Serial.println("[OK] RTC"); 
        rtc.getTime(&clockHour, &clockMinute, &clockSecond); 
    }
    hasPMU = pmu.begin(); 
    if (hasPMU) { 
        Serial.println("[OK] PMU"); 
        batteryPercent = pmu.getBattPercent(); 
        batteryVoltage = pmu.getBattVoltage(); 
    }
    hasIMU = imu.begin(); 
    if (hasIMU) Serial.println("[OK] IMU");
    
    delay(2000);
    
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("  System Ready - Swipe to Navigate");
    Serial.println("═══════════════════════════════════════════\n");
    
    redrawNeeded = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
    serviceClock();
    serviceSteps();
    serviceBattery();
    serviceMusic();
    serviceGames();
    serviceStats();
    handleTouch();
    renderScreen();
    
    delay(10);
}
