/**
 * ═══════════════════════════════════════════════════════════════════════════
 *  S3 MiniOS - ESP32-S3-Touch-AMOLED-1.8 Firmware
 *  ADVANCED CARD-BASED OPERATING SYSTEM v2.0
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 *  Card-based smartwatch OS with infinite swipe navigation
 *  Multiple themes inspired by modern smartwatch UIs
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
 *    - Long press: Theme selector
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
//  THEME SYSTEM - Multiple Themes
// ═══════════════════════════════════════════════════════════════════════════

enum ThemeType {
    THEME_AMOLED = 0,
    THEME_RED_BLACK = 1,
    THEME_PURPLE_GLASS = 2,
    THEME_FITNESS = 3,
    THEME_STREAK = 4,
    THEME_COUNT = 5
};

struct Theme {
    const char* name;
    uint16_t bg;
    uint16_t card;
    uint16_t cardLight;
    uint16_t text;
    uint16_t textDim;
    uint16_t textDimmer;
    uint16_t accent1;
    uint16_t accent2;
    uint16_t accent3;
    uint16_t accent4;
    uint16_t success;
    uint16_t warning;
    uint16_t danger;
};

// Color conversion helper: RGB888 to RGB565
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

Theme themes[THEME_COUNT] = {
    // AMOLED Dark - Cyan accents
    {
        "AMOLED Dark",
        0x0000,                     // bg - pure black
        RGB565(26, 26, 26),         // card
        RGB565(45, 45, 45),         // cardLight
        0xFFFF,                     // text - white
        RGB565(136, 136, 136),      // textDim
        RGB565(85, 85, 85),         // textDimmer
        0x07FF,                     // accent1 - cyan
        0xF81F,                     // accent2 - magenta
        0x07E0,                     // accent3 - green
        0xFD20,                     // accent4 - orange
        0x07E0,                     // success - green
        0xFD20,                     // warning - orange
        0xF800                      // danger - red
    },
    // Red & Black - Minimal
    {
        "Red Black",
        RGB565(10, 10, 10),         // bg
        RGB565(26, 26, 26),         // card
        RGB565(42, 42, 42),         // cardLight
        0xFFFF,                     // text
        RGB565(170, 170, 170),      // textDim
        RGB565(102, 102, 102),      // textDimmer
        RGB565(255, 0, 51),         // accent1 - red
        RGB565(255, 51, 102),       // accent2 - pink-red
        0xFFFF,                     // accent3 - white
        RGB565(255, 0, 51),         // accent4 - red
        RGB565(0, 255, 102),        // success
        0xFD20,                     // warning
        RGB565(255, 0, 51)          // danger
    },
    // Purple Glass - Glassmorphism
    {
        "Purple Glass",
        RGB565(26, 10, 46),         // bg - deep purple
        RGB565(40, 20, 60),         // card
        RGB565(60, 30, 80),         // cardLight
        0xFFFF,                     // text
        RGB565(196, 181, 253),      // textDim - lavender
        RGB565(139, 92, 246),       // textDimmer - purple
        RGB565(192, 132, 252),      // accent1 - light purple
        RGB565(34, 211, 238),       // accent2 - cyan
        RGB565(167, 139, 250),      // accent3 - violet
        RGB565(244, 114, 182),      // accent4 - pink
        RGB565(52, 211, 153),       // success
        RGB565(251, 191, 36),       // warning
        RGB565(248, 113, 113)       // danger
    },
    // Fitness - Bright Orange
    {
        "Fitness",
        RGB565(245, 245, 245),      // bg - light gray
        0xFFFF,                     // card - white
        RGB565(240, 240, 240),      // cardLight
        RGB565(26, 26, 26),         // text - dark
        RGB565(102, 102, 102),      // textDim
        RGB565(153, 153, 153),      // textDimmer
        RGB565(255, 107, 53),       // accent1 - orange
        RGB565(124, 58, 237),       // accent2 - purple
        RGB565(6, 182, 212),        // accent3 - teal
        RGB565(16, 185, 129),       // accent4 - green
        RGB565(16, 185, 129),       // success
        RGB565(245, 158, 11),       // warning
        RGB565(239, 68, 68)         // danger
    },
    // Streak Fire - Dark with fire accents
    {
        "Streak Fire",
        RGB565(15, 15, 15),         // bg
        RGB565(26, 26, 26),         // card
        RGB565(38, 38, 38),         // cardLight
        0xFFFF,                     // text
        RGB565(156, 163, 175),      // textDim
        RGB565(75, 85, 99),         // textDimmer
        RGB565(255, 107, 53),       // accent1 - orange fire
        RGB565(59, 130, 246),       // accent2 - blue
        RGB565(16, 185, 129),       // accent3 - green
        RGB565(245, 158, 11),       // accent4 - yellow
        RGB565(16, 185, 129),       // success
        RGB565(245, 158, 11),       // warning
        RGB565(239, 68, 68)         // danger
    }
};

ThemeType currentTheme = THEME_AMOLED;
Theme* theme = &themes[THEME_AMOLED];

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
//  NAVIGATION SYSTEM - Extended Categories
// ═══════════════════════════════════════════════════════════════════════════

enum MainCard { 
    CARD_CLOCK = 0,
    CARD_STEPS = 1, 
    CARD_MUSIC = 2,
    CARD_WEATHER = 3,
    CARD_CALENDAR = 4,
    CARD_GAMES = 5,
    CARD_STREAK = 6,
    CARD_FINANCE = 7,
    CARD_SYSTEM = 8,
    CARD_TIMER = 9,
    CARD_SETTINGS = 10,
    MAIN_CARD_COUNT = 11
};

const char* cardNames[] = {
    "CLOCK", "STEPS", "MUSIC", "WEATHER", "CALENDAR",
    "GAMES", "STREAK", "FINANCE", "SYSTEM", "TIMER", "SETTINGS"
};

enum SubCard {
    SUB_NONE = 0,
    
    // Clock sub-cards (1-9)
    SUB_CLOCK_WORLD = 1,
    SUB_CLOCK_ANALOG = 2,
    SUB_CLOCK_DIGITAL = 3,
    SUB_CLOCK_ALARM = 4,
    
    // Steps sub-cards (10-19)
    SUB_STEPS_GRAPH = 10,
    SUB_STEPS_DISTANCE = 11,
    SUB_STEPS_CALORIES = 12,
    SUB_STEPS_DEBUG = 13,
    
    // Music sub-cards (20-29)
    SUB_MUSIC_PLAYLIST = 20,
    SUB_MUSIC_CONTROLS = 21,
    SUB_MUSIC_VISUALIZER = 22,
    
    // Weather sub-cards (30-39)
    SUB_WEATHER_FORECAST = 30,
    SUB_WEATHER_DETAILS = 31,
    SUB_WEATHER_HOURLY = 32,
    
    // Calendar sub-cards (40-49)
    SUB_CALENDAR_EVENTS = 40,
    SUB_CALENDAR_MONTHLY = 41,
    SUB_CALENDAR_AGENDA = 42,
    
    // Games sub-cards (50-59)
    SUB_GAMES_YESNO = 50,
    SUB_GAMES_CLICKER = 51,
    SUB_GAMES_REACTION = 52,
    SUB_GAMES_DINO = 53,
    
    // Streak sub-cards (60-69)
    SUB_STREAK_DAILY = 60,
    SUB_STREAK_WEEKLY = 61,
    SUB_STREAK_HISTORY = 62,
    
    // Finance sub-cards (70-79)
    SUB_FINANCE_BUDGET = 70,
    SUB_FINANCE_CRYPTO = 71,
    SUB_FINANCE_STOCKS = 72,
    
    // System sub-cards (80-89)
    SUB_SYSTEM_STATS = 80,
    SUB_SYSTEM_BATTERY = 81,
    SUB_SYSTEM_STORAGE = 82,
    SUB_SYSTEM_NETWORK = 83,
    
    // Timer sub-cards (90-99)
    SUB_TIMER_STOPWATCH = 90,
    SUB_TIMER_COUNTDOWN = 91,
    SUB_TIMER_POMODORO = 92,
    
    // Settings sub-cards (100-109)
    SUB_SETTINGS_THEME = 100,
    SUB_SETTINGS_DISPLAY = 101,
    SUB_SETTINGS_SOUND = 102,
    SUB_SETTINGS_ABOUT = 103
};

MainCard currentMainCard = CARD_CLOCK;
SubCard currentSubCard = SUB_NONE;
int subCardIndex = 0;
bool redrawNeeded = true;
bool inThemeSelector = false;

// Sub-card counts per category
const int subCardCounts[] = {4, 4, 3, 3, 3, 4, 3, 3, 4, 3, 4};

// ═══════════════════════════════════════════════════════════════════════════
//  GESTURE DETECTION
// ═══════════════════════════════════════════════════════════════════════════

enum GestureType { GESTURE_NONE, GESTURE_LEFT, GESTURE_RIGHT, GESTURE_UP, GESTURE_DOWN, GESTURE_TAP, GESTURE_LONG_PRESS };

int16_t gestureStartX = -1, gestureStartY = -1;
int16_t gestureEndX = -1, gestureEndY = -1;
unsigned long gestureStartTime = 0;
bool gestureInProgress = false;

#define SWIPE_THRESHOLD 80
#define SWIPE_TIMEOUT 500
#define LONG_PRESS_TIME 800

// ═══════════════════════════════════════════════════════════════════════════
//  GLOBAL STATE - Mock Data
// ═══════════════════════════════════════════════════════════════════════════

// Clock
uint8_t clockHour = 10, clockMinute = 30, clockSecond = 0;
uint8_t lastSecond = 255;
const char* daysOfWeek[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
uint8_t currentDay = 3; // Wednesday
uint8_t currentDate = 24;
uint8_t currentMonth = 1; // January
uint16_t currentYear = 2026;

// Steps
uint32_t stepCount = 6054;
uint32_t stepGoal = 10000;
uint32_t stepHistory[7] = {4200, 5800, 6100, 7300, 5200, 6054, 3200};
float caloriesBurned = 312.5;
float distanceKm = 4.2;

// Music
bool musicPlaying = false;
uint8_t musicProgress = 35;
const char* musicTitle = "Goon Gumpas";
const char* musicArtist = "Aphex Twin";
uint16_t musicDuration = 122;
uint16_t musicCurrent = 48;
uint8_t musicVolume = 54;

// Games
uint8_t spotlightGame = 0;
unsigned long lastGameRotate = 0;
uint32_t clickerScore = 0;
uint16_t reactionTime = 0;
bool reactionWaiting = false;
unsigned long reactionStartTime = 0;
bool yesNoResult = false;
bool yesNoShowing = false;
uint32_t dinoScore = 0;
bool dinoJumping = false;

// Weather (mock data)
int8_t weatherTemp = 19;
const char* weatherCondition = "Sunny";
uint8_t weatherHumidity = 65;
uint16_t weatherWind = 12;
int8_t forecast[5] = {18, 19, 18, 17, 20};
const char* forecastDays[] = {"WED", "THU", "FRI", "SAT", "SUN"};

// Calendar
uint8_t calendarEvents = 3;
const char* eventTitles[] = {"Team Meeting", "Lunch", "Code Review"};
const char* eventTimes[] = {"10:00 AM", "12:30 PM", "3:00 PM"};

// Streak
uint16_t streakDays = 12;
bool dailyGoalMet[7] = {true, true, true, false, false, false, false};
uint8_t dailyProgress = 66;

// Finance
float accountBalance = 1980.04;
float budgetGoal = 3000.0;
float btcPrice = 43465.0;
float ethPrice = 2450.0;
float btcChange = 2.4;
float ethChange = -1.2;

// System stats
uint8_t cpuUsage = 45;
uint32_t freeRAM = 234567;
float temperature = 42.5;
uint16_t fps = 60;
uint32_t storageUsed = 2048;
uint32_t storageTotal = 8192;

// Battery
uint16_t batteryVoltage = 4100;
uint8_t batteryPercent = 85;

// Timer
uint32_t stopwatchMs = 0;
bool stopwatchRunning = false;
uint32_t countdownSeconds = 300;
bool countdownRunning = false;
uint8_t pomodoroMinutes = 25;
bool pomodoroRunning = false;

// Timing
unsigned long lastClockUpdate = 0;
unsigned long lastStepUpdate = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastStatsUpdate = 0;
unsigned long lastMusicUpdate = 0;
unsigned long lastStopwatchUpdate = 0;

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

void drawGradientV(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c1, uint16_t c2) {
    for (int16_t i = 0; i < h; i++) {
        uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
        uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
        float t = (float)i / h;
        uint8_t r = r1 + (r2 - r1) * t;
        uint8_t g = g1 + (g2 - g1) * t;
        uint8_t b = b1 + (b2 - b1) * t;
        uint16_t col = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
        gfx->drawFastHLine(x, y + i, w, col);
    }
}

void drawGradientCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c1, uint16_t c2) {
    drawGradientV(x, y, w, h, c1, c2);
    gfx->drawRoundRect(x, y, w, h, CARD_RADIUS, c1);
}

void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    gfx->fillRoundRect(x, y, w, h, CARD_RADIUS, color);
}

void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint16_t color) {
    gfx->fillRoundRect(x, y, w, h, BTN_RADIUS, color);
    gfx->setTextColor(theme->text);
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
    int16_t segW = (w - 36) / 4;
    int currentSeg = (int)(progress * 4);
    
    for (int i = 0; i < 4; i++) {
        int16_t sx = x + i * (segW + 12);
        if (i < currentSeg) {
            gfx->fillRoundRect(sx, y, segW, h, 4, fg);
        } else if (i == currentSeg) {
            float partial = (progress * 4) - currentSeg;
            gfx->fillRoundRect(sx, y, segW, h, 4, bg);
            if (partial > 0) {
                int16_t pw = (int16_t)(segW * partial);
                gfx->fillRoundRect(sx, y, pw, h, 4, fg);
            }
        } else {
            gfx->fillRoundRect(sx, y, segW, h, 4, bg);
        }
    }
}

void drawProgressRing(int16_t cx, int16_t cy, int16_t r, int16_t thick, float prog, uint16_t fg, uint16_t bg) {
    for (int i = 0; i < 360; i += 6) {
        float a = i * DEG_TO_RAD;
        gfx->fillCircle(cx + cos(a)*r, cy + sin(a)*r, thick/2, bg);
    }
    int end = (int)(prog * 360);
    for (int i = 0; i < end; i += 6) {
        float a = (i - 90) * DEG_TO_RAD;
        gfx->fillCircle(cx + cos(a)*r, cy + sin(a)*r, thick/2, fg);
    }
}

void drawBatteryIcon(int16_t x, int16_t y, uint8_t percent) {
    uint16_t col = percent > 20 ? theme->success : theme->danger;
    gfx->drawRect(x, y + 2, 20, 10, theme->textDim);
    gfx->fillRect(x + 20, y + 4, 2, 6, theme->textDim);
    int16_t fillW = (18 * percent) / 100;
    gfx->fillRect(x + 1, y + 3, fillW, 8, col);
}

void drawStatusBar() {
    gfx->setTextColor(theme->textDimmer);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print(cardNames[currentMainCard]);
    drawBatteryIcon(LCD_WIDTH - 35, 8, batteryPercent);
}

void drawNavHint(const char* hint) {
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDimmer);
    int16_t tw = strlen(hint) * 6;
    gfx->setCursor((LCD_WIDTH - tw) / 2, LCD_HEIGHT - 25);
    gfx->print(hint);
}

void drawNavDots() {
    int16_t dotY = LCD_HEIGHT - 40;
    int16_t startX = (LCD_WIDTH - (MAIN_CARD_COUNT * 10)) / 2;
    
    for (int i = 0; i < MAIN_CARD_COUNT; i++) {
        uint16_t col = (i == currentMainCard) ? theme->accent1 : theme->cardLight;
        int16_t r = (i == currentMainCard) ? 4 : 3;
        gfx->fillCircle(startX + i * 10 + 5, dotY, r, col);
    }
}

void drawFireIcon(int16_t x, int16_t y, int16_t size) {
    // Simple fire emoji-like icon
    uint16_t orange = RGB565(255, 107, 53);
    uint16_t yellow = RGB565(255, 200, 50);
    uint16_t red = RGB565(255, 50, 50);
    
    // Outer flame
    gfx->fillCircle(x, y + size/4, size/2, orange);
    gfx->fillTriangle(x - size/2, y + size/4, x + size/2, y + size/4, x, y - size/2, orange);
    
    // Inner flame
    gfx->fillCircle(x, y + size/3, size/3, yellow);
    gfx->fillTriangle(x - size/3, y + size/3, x + size/3, y + size/3, x, y - size/4, yellow);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN CARD SCREENS
// ═══════════════════════════════════════════════════════════════════════════

void drawMainClock() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    // Main gradient card
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, theme->accent1, theme->accent2);
    
    // Branding
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("S3 MiniOS");
    
    // Day
    gfx->setTextSize(2);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 20, 120);
    gfx->print(daysOfWeek[currentDay]);
    
    // Time
    gfx->setTextSize(7);
    gfx->setTextColor(theme->text);
    char timeStr[10];
    sprintf(timeStr, "%02d:%02d", clockHour, clockMinute);
    gfx->setCursor(MARGIN + 20, 155);
    gfx->print(timeStr);
    
    // Seconds
    gfx->setTextSize(4);
    gfx->setTextColor(theme->accent1);
    gfx->setCursor(MARGIN + 260, 180);
    gfx->printf("%02d", clockSecond);
    
    // Progress milestones
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDimmer);
    gfx->setCursor(MARGIN + 15, 270);
    gfx->print("00:00");
    gfx->setCursor(MARGIN + 90, 270);
    gfx->print("06:00");
    gfx->setCursor(MARGIN + 165, 270);
    gfx->print("12:00");
    gfx->setCursor(MARGIN + 240, 270);
    gfx->print("18:00");
    
    float dayProgress = (clockHour * 60.0f + clockMinute) / 1440.0f;
    drawSegmentedProgress(MARGIN + 15, 285, LCD_WIDTH - 2*MARGIN - 30, 8, dayProgress, theme->text, theme->cardLight);
    
    drawNavDots();
    drawNavHint("Swipe down for more");
}

void drawMainSteps() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, theme->accent2, theme->accent1);
    
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("USBO App");
    
    gfx->setTextSize(2);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 20, 120);
    gfx->print("Steps:");
    
    gfx->setTextSize(7);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 155);
    gfx->print(stepCount);
    
    // Progress milestones
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDimmer);
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
    drawSegmentedProgress(MARGIN + 15, 285, LCD_WIDTH - 2*MARGIN - 30, 8, stepProgress, theme->text, theme->cardLight);
    
    drawNavDots();
    drawNavHint("Swipe down for more");
}

void drawMainMusic() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    drawCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 300, theme->card);
    
    // Album art placeholder
    gfx->fillRoundRect(MARGIN + 20, 90, 100, 100, 12, theme->cardLight);
    gfx->setTextColor(theme->textDim);
    gfx->setTextSize(4);
    gfx->setCursor(MARGIN + 50, 125);
    gfx->print("J");
    
    // Song info
    gfx->setTextColor(theme->text);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN + 140, 100);
    gfx->print(musicTitle);
    
    gfx->setTextColor(theme->textDim);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 140, 130);
    gfx->print(musicArtist);
    
    // Playback controls
    int16_t controlY = 230;
    int16_t cx = LCD_WIDTH / 2;
    
    // Prev
    gfx->fillCircle(cx - 70, controlY, 20, theme->cardLight);
    gfx->setTextColor(theme->text);
    gfx->setTextSize(2);
    gfx->setCursor(cx - 82, controlY - 8);
    gfx->print("|<");
    
    // Play/Pause
    gfx->fillCircle(cx, controlY, 28, theme->text);
    gfx->setTextColor(theme->bg);
    gfx->setTextSize(3);
    if (musicPlaying) {
        gfx->setCursor(cx - 12, controlY - 12);
        gfx->print("||");
    } else {
        gfx->setCursor(cx - 8, controlY - 12);
        gfx->print(">");
    }
    
    // Next
    gfx->fillCircle(cx + 70, controlY, 20, theme->cardLight);
    gfx->setTextColor(theme->text);
    gfx->setTextSize(2);
    gfx->setCursor(cx + 58, controlY - 8);
    gfx->print(">|");
    
    // Progress
    gfx->setTextColor(theme->textDim);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 20, 300);
    gfx->printf("%d:%02d", musicCurrent / 60, musicCurrent % 60);
    gfx->setCursor(LCD_WIDTH - MARGIN - 30, 300);
    gfx->printf("%d:%02d", musicDuration / 60, musicDuration % 60);
    
    drawProgressBar(MARGIN + 20, 320, LCD_WIDTH - 2*MARGIN - 40, 8, (float)musicCurrent / musicDuration, theme->accent2, theme->cardLight);
    
    drawNavDots();
    drawNavHint("Swipe down for playlist");
}

void drawMainWeather() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    // Weather gradient - sunny orange
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, RGB565(245, 175, 25), RGB565(241, 39, 17));
    
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("San Francisco");
    
    // Large temperature
    gfx->setTextSize(8);
    gfx->setCursor(MARGIN + 30, 130);
    gfx->printf("%d", weatherTemp);
    
    gfx->setTextSize(4);
    gfx->setCursor(MARGIN + 150, 135);
    gfx->print("o");
    
    // Condition
    gfx->setTextSize(2);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 20, 225);
    gfx->print(weatherCondition);
    
    // Forecast
    gfx->setTextSize(1);
    int16_t forecastY = 270;
    for (int i = 0; i < 3; i++) {
        int16_t fx = MARGIN + 30 + i * 100;
        gfx->setTextColor(theme->textDimmer);
        gfx->setCursor(fx, forecastY);
        gfx->print(forecastDays[i]);
        gfx->setTextColor(theme->text);
        gfx->setTextSize(2);
        gfx->setCursor(fx, forecastY + 15);
        gfx->printf("%do", forecast[i]);
        gfx->setTextSize(1);
    }
    
    drawNavDots();
    drawNavHint("Swipe down for details");
}

void drawMainCalendar() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    drawCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 300, theme->card);
    
    // Month/Year header
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->printf("%s %d", monthNames[currentMonth - 1], currentYear);
    
    // Day labels
    const char* dayLabels[] = {"S", "M", "T", "W", "T", "F", "S"};
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    for (int i = 0; i < 7; i++) {
        gfx->setCursor(MARGIN + 25 + i * 45, 120);
        gfx->print(dayLabels[i]);
    }
    
    // Calendar grid
    int16_t gridY = 145;
    int day = 1;
    for (int row = 0; row < 5 && day <= 31; row++) {
        for (int col = 0; col < 7 && day <= 31; col++) {
            int16_t cx = MARGIN + 25 + col * 45;
            int16_t cy = gridY + row * 35;
            
            if (day == currentDate) {
                gfx->fillCircle(cx + 8, cy + 8, 14, theme->accent1);
                gfx->setTextColor(theme->bg);
            } else {
                gfx->setTextColor(theme->text);
            }
            
            gfx->setTextSize(2);
            gfx->setCursor(cx, cy);
            gfx->printf("%d", day);
            day++;
        }
    }
    
    drawNavDots();
    drawNavHint("Swipe down for events");
}

void drawMainGames() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, theme->accent4, theme->danger);
    
    gfx->setTextSize(3);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 90);
    gfx->print("GAMES HUB");
    
    const char* gameNames[] = {"Yes or No", "Clicker", "Reaction", "Dino Run"};
    const char* gameDescs[] = {"Quick decision", "Tap fast!", "Test reflexes", "Jump obstacles"};
    
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDimmer);
    gfx->setCursor(MARGIN + 20, 140);
    gfx->print("FEATURED GAME");
    
    gfx->setTextSize(3);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 165);
    gfx->print(gameNames[spotlightGame]);
    
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 20, 205);
    gfx->print(gameDescs[spotlightGame]);
    
    // Game indicators
    int16_t iconY = 250;
    for (int i = 0; i < 4; i++) {
        uint16_t col = (i == spotlightGame) ? theme->text : theme->cardLight;
        gfx->fillCircle(MARGIN + 40 + i * 50, iconY, 12, col);
    }
    
    drawNavDots();
    drawNavHint("Swipe down to play");
}

void drawMainStreak() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    drawCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, theme->card);
    
    // Fire icon
    drawFireIcon(MARGIN + 60, 120, 50);
    
    // Streak count
    gfx->setTextSize(6);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 100, 95);
    gfx->print(streakDays);
    
    gfx->setTextSize(2);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 100, 160);
    gfx->print("Streak Days");
    
    // Motivational text
    gfx->fillRoundRect(MARGIN + 20, 200, LCD_WIDTH - 2*MARGIN - 40, 60, 12, theme->cardLight);
    gfx->setTextSize(1);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 40, 220);
    gfx->print("Keep going! You're on fire!");
    
    // Weekly dots
    int16_t dotY = 280;
    for (int i = 0; i < 7; i++) {
        uint16_t col = dailyGoalMet[i] ? theme->success : theme->cardLight;
        gfx->fillCircle(MARGIN + 40 + i * 40, dotY, 10, col);
        if (dailyGoalMet[i]) {
            gfx->setTextColor(theme->bg);
            gfx->setTextSize(1);
            gfx->setCursor(MARGIN + 36 + i * 40, dotY - 4);
            gfx->print("v");
        }
    }
    
    drawNavDots();
    drawNavHint("Swipe down for details");
}

void drawMainFinance() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, theme->accent1, theme->accent2);
    
    // Percentage badge
    gfx->fillCircle(MARGIN + 40, 90, 18, theme->cardLight);
    gfx->setTextSize(1);
    gfx->setTextColor(theme->success);
    gfx->setCursor(MARGIN + 30, 85);
    gfx->print("66%");
    
    // Balance
    gfx->setTextSize(4);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 130);
    gfx->printf("$%.2f", accountBalance);
    
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 20, 180);
    gfx->printf("/ $%.2f budget", budgetGoal);
    
    // Progress bar
    float progress = accountBalance / budgetGoal;
    drawProgressBar(MARGIN + 20, 210, LCD_WIDTH - 2*MARGIN - 40, 10, progress, theme->text, theme->cardLight);
    
    // Quick crypto stats
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDimmer);
    gfx->setCursor(MARGIN + 20, 250);
    gfx->print("BTC");
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 50, 250);
    gfx->printf("$%.0f", btcPrice);
    gfx->setTextColor(theme->success);
    gfx->setCursor(MARGIN + 150, 250);
    gfx->printf("+%.1f%%", btcChange);
    
    gfx->setTextColor(theme->textDimmer);
    gfx->setCursor(MARGIN + 20, 270);
    gfx->print("ETH");
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 50, 270);
    gfx->printf("$%.0f", ethPrice);
    gfx->setTextColor(theme->danger);
    gfx->setCursor(MARGIN + 150, 270);
    gfx->printf("%.1f%%", ethChange);
    
    drawNavDots();
    drawNavHint("Swipe down for budget");
}

void drawMainSystem() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 300, theme->accent3, theme->accent1);
    
    gfx->setTextSize(3);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 85);
    gfx->print("SYSTEM INFO");
    
    int16_t statY = 140;
    int16_t statSpacing = 50;
    
    // CPU
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDimmer);
    gfx->setCursor(MARGIN + 20, statY);
    gfx->print("CPU USAGE");
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, statY + 15);
    gfx->printf("%d%%", cpuUsage);
    drawProgressBar(MARGIN + 100, statY + 18, 200, 8, (float)cpuUsage / 100.0f, theme->text, theme->cardLight);
    
    // RAM
    statY += statSpacing;
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDimmer);
    gfx->setCursor(MARGIN + 20, statY);
    gfx->print("FREE RAM");
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, statY + 15);
    gfx->printf("%lu KB", freeRAM / 1024);
    
    // Temperature
    statY += statSpacing;
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDimmer);
    gfx->setCursor(MARGIN + 20, statY);
    gfx->print("TEMPERATURE");
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, statY + 15);
    gfx->printf("%.1fC", temperature);
    
    // FPS
    statY += statSpacing;
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDimmer);
    gfx->setCursor(MARGIN + 20, statY);
    gfx->print("FPS");
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, statY + 15);
    gfx->printf("%d", fps);
    
    drawNavDots();
    drawNavHint("Swipe down for more");
}

void drawMainTimer() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    // Dark green hourglass theme
    drawCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 280, RGB565(10, 47, 31));
    
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor((LCD_WIDTH - 80) / 2, 90);
    gfx->print("Hourglass Timer");
    
    // Hourglass visualization
    int16_t cx = LCD_WIDTH / 2;
    int16_t cy = 180;
    
    // Top triangle
    gfx->fillTriangle(cx - 40, cy - 60, cx + 40, cy - 60, cx, cy, theme->success);
    // Bottom triangle
    gfx->fillTriangle(cx - 40, cy + 60, cx + 40, cy + 60, cx, cy, theme->cardLight);
    // Sand in bottom
    gfx->fillTriangle(cx - 30, cy + 60, cx + 30, cy + 60, cx, cy + 20, theme->success);
    
    // Time display
    gfx->setTextSize(4);
    gfx->setTextColor(theme->success);
    gfx->setCursor(cx - 40, 280);
    gfx->print("24 s");
    
    drawNavDots();
    drawNavHint("Swipe down for stopwatch");
}

void drawMainSettings() {
    gfx->fillScreen(theme->bg);
    drawStatusBar();
    
    drawCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 300, theme->card);
    
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("SETTINGS");
    
    // Settings grid
    const char* settingIcons[] = {"THM", "DIS", "SND", "ABT"};
    const char* settingNames[] = {"Theme", "Display", "Sound", "About"};
    
    for (int i = 0; i < 4; i++) {
        int16_t x = MARGIN + 30 + (i % 2) * 150;
        int16_t y = 130 + (i / 2) * 100;
        
        gfx->fillRoundRect(x, y, 120, 80, 12, theme->cardLight);
        gfx->setTextSize(2);
        gfx->setTextColor(theme->accent1);
        gfx->setCursor(x + 40, y + 20);
        gfx->print(settingIcons[i]);
        gfx->setTextSize(1);
        gfx->setTextColor(theme->textDim);
        gfx->setCursor(x + 30, y + 55);
        gfx->print(settingNames[i]);
    }
    
    drawNavDots();
    drawNavHint("Swipe down for options");
}

// ═══════════════════════════════════════════════════════════════════════════
//  SUB-CARD SCREENS
// ═══════════════════════════════════════════════════════════════════════════

void drawSubHeader(const char* title) {
    gfx->setTextColor(theme->text);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 20);
    gfx->print("< ");
    gfx->print(title);
}

void drawSubClockWorld() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("World Clock");
    
    const char* cities[] = {"New York", "London", "Tokyo"};
    int8_t offsets[] = {-5, 0, 9};
    
    for (int i = 0; i < 3; i++) {
        int16_t y = 80 + i * 90;
        drawCard(MARGIN, y, LCD_WIDTH - 2*MARGIN, 75, theme->card);
        
        gfx->setTextSize(1);
        gfx->setTextColor(theme->textDim);
        gfx->setCursor(MARGIN + 15, y + 15);
        gfx->print(cities[i]);
        
        uint8_t h = (clockHour + 24 + offsets[i]) % 24;
        gfx->setTextSize(3);
        gfx->setTextColor(theme->text);
        gfx->setCursor(MARGIN + 15, y + 35);
        gfx->printf("%02d:%02d", h, clockMinute);
    }
    
    drawNavHint("Swipe up to exit");
}

void drawSubClockAnalog() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Analog Clock");
    
    int16_t cx = LCD_WIDTH / 2;
    int16_t cy = 230;
    int16_t r = 120;
    
    // Clock face
    gfx->drawCircle(cx, cy, r, theme->cardLight);
    gfx->drawCircle(cx, cy, r - 2, theme->cardLight);
    
    // Hour markers
    for (int i = 0; i < 12; i++) {
        float a = (i * 30 - 90) * DEG_TO_RAD;
        int16_t x1 = cx + cos(a) * (r - 15);
        int16_t y1 = cy + sin(a) * (r - 15);
        int16_t x2 = cx + cos(a) * (r - 5);
        int16_t y2 = cy + sin(a) * (r - 5);
        gfx->drawLine(x1, y1, x2, y2, theme->text);
    }
    
    // Hour hand
    float ha = ((clockHour % 12) * 30 + clockMinute * 0.5 - 90) * DEG_TO_RAD;
    gfx->drawLine(cx, cy, cx + cos(ha) * 60, cy + sin(ha) * 60, theme->text);
    gfx->drawLine(cx-1, cy, cx-1 + cos(ha) * 60, cy + sin(ha) * 60, theme->text);
    
    // Minute hand
    float ma = (clockMinute * 6 - 90) * DEG_TO_RAD;
    gfx->drawLine(cx, cy, cx + cos(ma) * 90, cy + sin(ma) * 90, theme->textDim);
    
    // Second hand
    float sa = (clockSecond * 6 - 90) * DEG_TO_RAD;
    gfx->drawLine(cx, cy, cx + cos(sa) * 100, cy + sin(sa) * 100, theme->accent1);
    
    // Center dot
    gfx->fillCircle(cx, cy, 5, theme->accent1);
    
    drawNavHint("Swipe up to exit");
}

void drawSubClockDigital() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Digital Clock");
    
    // Large digital time with glow effect
    gfx->setTextSize(6);
    gfx->setTextColor(theme->accent1);
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", clockHour, clockMinute, clockSecond);
    int16_t tw = strlen(timeStr) * 36;
    gfx->setCursor((LCD_WIDTH - tw) / 2, 180);
    gfx->print(timeStr);
    
    // Date
    gfx->setTextSize(2);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor((LCD_WIDTH - 200) / 2, 260);
    gfx->printf("%s, %s %d, %d", daysOfWeek[currentDay], monthNames[currentMonth - 1], currentDate, currentYear);
    
    drawNavHint("Swipe up to exit");
}

void drawSubStepsGraph() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Weekly Steps");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 280, theme->card);
    
    const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    int16_t barW = 35;
    int16_t chartY = 320;
    int16_t chartH = 180;
    
    for (int i = 0; i < 7; i++) {
        int16_t barX = MARGIN + 20 + i * (barW + 10);
        float ratio = (float)stepHistory[i] / 10000.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        int16_t barH = (int16_t)(chartH * ratio);
        
        uint16_t barColor = (i == 5) ? theme->accent1 : theme->cardLight;
        gfx->fillRoundRect(barX, chartY - barH, barW, barH, 4, barColor);
        
        gfx->setTextSize(1);
        gfx->setTextColor(theme->textDim);
        gfx->setCursor(barX + 5, chartY + 10);
        gfx->print(days[i]);
    }
    
    drawNavHint("Swipe up to exit");
}

void drawSubStepsCalories() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Calories");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 260, theme->card);
    
    // Progress ring
    int16_t cx = LCD_WIDTH / 2;
    int16_t cy = 200;
    float progress = (float)stepCount / stepGoal;
    drawProgressRing(cx, cy, 70, 12, progress, theme->accent1, theme->cardLight);
    
    // Calories in center
    gfx->setTextSize(3);
    gfx->setTextColor(theme->text);
    gfx->setCursor(cx - 40, cy - 15);
    gfx->printf("%.0f", caloriesBurned);
    gfx->setTextSize(1);
    gfx->setCursor(cx - 15, cy + 20);
    gfx->print("kcal");
    
    // Total info
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 30, 300);
    gfx->print("610.5 Total calories today");
    
    drawNavHint("Swipe up to exit");
}

void drawSubGamesYesNo() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Yes or No");
    
    if (yesNoShowing) {
        // Show result
        drawCard(MARGIN, 120, LCD_WIDTH - 2*MARGIN, 150, theme->card);
        gfx->setTextSize(5);
        gfx->setTextColor(yesNoResult ? theme->success : theme->danger);
        gfx->setCursor((LCD_WIDTH - 80) / 2, 170);
        gfx->print(yesNoResult ? "Yes" : "No");
        
        drawButton(MARGIN + 60, 310, LCD_WIDTH - 2*MARGIN - 120, 60, "Spin Again", theme->accent1);
    } else {
        drawCard(MARGIN, 100, LCD_WIDTH - 2*MARGIN, 100, theme->card);
        gfx->setTextSize(2);
        gfx->setTextColor(theme->text);
        gfx->setCursor(MARGIN + 40, 140);
        gfx->print("Should I do it?");
        
        drawButton(MARGIN + 20, 240, 140, 80, "YES", theme->success);
        drawButton(MARGIN + 180, 240, 140, 80, "NO", theme->danger);
    }
    
    drawNavHint("Swipe up to exit");
}

void drawSubGamesClicker() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Clicker");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 100, theme->card);
    gfx->setTextSize(2);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 30, 100);
    gfx->print("Score:");
    gfx->setTextSize(4);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 120, 115);
    gfx->printf("%lu", clickerScore);
    
    // Big click button
    gfx->fillCircle(LCD_WIDTH / 2, 280, 80, theme->accent4);
    gfx->setTextSize(3);
    gfx->setTextColor(theme->text);
    gfx->setCursor(LCD_WIDTH / 2 - 45, 265);
    gfx->print("CLICK");
    
    drawNavHint("Swipe up to exit");
}

void drawSubGamesReaction() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Reaction Test");
    
    if (reactionWaiting) {
        if (millis() >= reactionStartTime) {
            // GO - Green screen
            gfx->fillRoundRect(MARGIN, 100, LCD_WIDTH - 2*MARGIN, 250, CARD_RADIUS, theme->success);
            gfx->setTextSize(4);
            gfx->setTextColor(theme->text);
            gfx->setCursor(LCD_WIDTH / 2 - 80, 200);
            gfx->print("TAP NOW!");
        } else {
            // Waiting - Red screen
            gfx->fillRoundRect(MARGIN, 100, LCD_WIDTH - 2*MARGIN, 250, CARD_RADIUS, theme->danger);
            gfx->setTextSize(2);
            gfx->setTextColor(theme->text);
            gfx->setCursor(LCD_WIDTH / 2 - 80, 210);
            gfx->print("Wait for green...");
        }
    } else if (reactionTime > 0) {
        // Show result
        drawCard(MARGIN, 100, LCD_WIDTH - 2*MARGIN, 150, theme->card);
        gfx->setTextSize(2);
        gfx->setTextColor(theme->textDim);
        gfx->setCursor(MARGIN + 80, 130);
        gfx->print("Your time:");
        gfx->setTextSize(4);
        gfx->setTextColor(theme->success);
        gfx->setCursor(MARGIN + 80, 170);
        gfx->printf("%dms", reactionTime);
        
        drawButton(MARGIN + 60, 290, LCD_WIDTH - 2*MARGIN - 120, 60, "Try Again", theme->accent1);
    } else {
        // Ready to start
        drawCard(MARGIN, 100, LCD_WIDTH - 2*MARGIN, 120, theme->card);
        gfx->setTextSize(2);
        gfx->setTextColor(theme->text);
        gfx->setCursor(MARGIN + 80, 150);
        gfx->print("Tap to start");
        
        drawButton(MARGIN + 60, 260, LCD_WIDTH - 2*MARGIN - 120, 80, "START", theme->accent4);
    }
    
    drawNavHint("Swipe up to exit");
}

void drawSubGamesDino() {
    gfx->fillScreen(0x0000);
    drawSubHeader("Dino Run");
    
    // Score
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 20, 70);
    gfx->print("48 lvl");
    gfx->setCursor(LCD_WIDTH - 100, 70);
    gfx->printf("Score: %lu", dinoScore);
    
    // Ground line
    gfx->drawFastHLine(0, 320, LCD_WIDTH, theme->textDim);
    
    // Dino character (simple)
    int16_t dinoX = 80;
    int16_t dinoY = dinoJumping ? 240 : 290;
    
    // Body
    gfx->fillRect(dinoX, dinoY, 40, 30, theme->text);
    // Head
    gfx->fillRect(dinoX + 25, dinoY - 20, 25, 25, theme->text);
    // Eye
    gfx->fillRect(dinoX + 40, dinoY - 15, 5, 5, 0x0000);
    // Legs
    gfx->fillRect(dinoX + 5, dinoY + 30, 8, 15, theme->text);
    gfx->fillRect(dinoX + 25, dinoY + 30, 8, 15, theme->text);
    
    drawButton(MARGIN + 60, 360, LCD_WIDTH - 2*MARGIN - 120, 60, "JUMP", theme->cardLight);
    
    drawNavHint("Swipe up to exit");
}

void drawSubStreakDaily() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Daily Goal");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 260, theme->card);
    
    // Progress ring
    int16_t cx = LCD_WIDTH / 2;
    int16_t cy = 200;
    float progress = (float)dailyProgress / 100.0f;
    drawProgressRing(cx, cy, 70, 12, progress, theme->accent1, theme->cardLight);
    
    // Progress text
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(cx - 30, cy - 5);
    gfx->print("Progress");
    
    // Stats below
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 50, 300);
    gfx->print("1/2");
    gfx->setCursor(LCD_WIDTH - 80, 300);
    gfx->print("2/3");
    
    drawNavHint("Swipe up to exit");
}

void drawSubStreakWeekly() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Weekly Overview");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 200, theme->card);
    
    const char* days[] = {"Sat", "Sun", "Mon", "Tue", "Wed", "Thr", "Fri"};
    
    for (int i = 0; i < 7; i++) {
        int16_t x = MARGIN + 25 + i * 45;
        int16_t y = 150;
        
        uint16_t col;
        char icon;
        if (dailyGoalMet[i]) {
            col = theme->success;
            icon = 'v';
        } else if (i == 3) {
            col = theme->accent1;
            icon = '!';
        } else {
            col = theme->cardLight;
            icon = 'o';
        }
        
        gfx->fillCircle(x + 15, y, 18, col);
        gfx->setTextSize(2);
        gfx->setTextColor(theme->text);
        gfx->setCursor(x + 9, y - 8);
        gfx->print(icon);
        
        gfx->setTextSize(1);
        gfx->setTextColor(theme->textDim);
        gfx->setCursor(x + 5, y + 35);
        gfx->print(days[i]);
    }
    
    drawNavHint("Swipe up to exit");
}

void drawSubFinanceBudget() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Budget Overview");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 280, theme->card);
    
    const char* categories[] = {"Food", "Transport", "Entertainment"};
    int16_t spent[] = {450, 120, 80};
    int16_t budget[] = {600, 200, 150};
    uint16_t colors[] = {theme->accent4, theme->accent1, theme->accent3};
    
    for (int i = 0; i < 3; i++) {
        int16_t y = 110 + i * 80;
        
        gfx->setTextSize(1);
        gfx->setTextColor(theme->text);
        gfx->setCursor(MARGIN + 20, y);
        gfx->print(categories[i]);
        
        gfx->setTextColor(theme->textDim);
        gfx->setCursor(LCD_WIDTH - 100, y);
        gfx->printf("$%d/$%d", spent[i], budget[i]);
        
        drawProgressBar(MARGIN + 20, y + 20, LCD_WIDTH - 2*MARGIN - 40, 10, (float)spent[i] / budget[i], colors[i], theme->cardLight);
    }
    
    drawNavHint("Swipe up to exit");
}

void drawSubFinanceCrypto() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Crypto Watch");
    
    // Bitcoin card
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 100, theme->card);
    gfx->setTextSize(3);
    gfx->setTextColor(RGB565(247, 147, 26));
    gfx->setCursor(MARGIN + 20, 100);
    gfx->print("B");
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 60, 100);
    gfx->print("Bitcoin");
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 60, 125);
    gfx->print("BTC");
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(LCD_WIDTH - 130, 100);
    gfx->printf("$%.0f", btcPrice);
    gfx->setTextSize(1);
    gfx->setTextColor(theme->success);
    gfx->setCursor(LCD_WIDTH - 60, 130);
    gfx->printf("+%.1f%%", btcChange);
    
    // Ethereum card
    drawCard(MARGIN, 200, LCD_WIDTH - 2*MARGIN, 100, theme->card);
    gfx->setTextSize(3);
    gfx->setTextColor(RGB565(98, 126, 234));
    gfx->setCursor(MARGIN + 20, 220);
    gfx->print("E");
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(MARGIN + 60, 220);
    gfx->print("Ethereum");
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(MARGIN + 60, 245);
    gfx->print("ETH");
    gfx->setTextSize(2);
    gfx->setTextColor(theme->text);
    gfx->setCursor(LCD_WIDTH - 110, 220);
    gfx->printf("$%.0f", ethPrice);
    gfx->setTextSize(1);
    gfx->setTextColor(theme->danger);
    gfx->setCursor(LCD_WIDTH - 60, 250);
    gfx->printf("%.1f%%", ethChange);
    
    drawNavHint("Swipe up to exit");
}

void drawSubSystemBattery() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Battery");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 260, theme->card);
    
    // Large battery ring
    int16_t cx = LCD_WIDTH / 2;
    int16_t cy = 200;
    drawProgressRing(cx, cy, 70, 12, (float)batteryPercent / 100.0f, theme->success, theme->cardLight);
    
    gfx->setTextSize(4);
    gfx->setTextColor(theme->text);
    gfx->setCursor(cx - 35, cy - 15);
    gfx->printf("%d%%", batteryPercent);
    
    // Info
    gfx->setTextSize(1);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor((LCD_WIDTH - 150) / 2, 310);
    gfx->print("Estimated: 5h 30m remaining");
    
    drawNavHint("Swipe up to exit");
}

void drawSubTimerStopwatch() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Stopwatch");
    
    // Time display
    uint32_t totalSeconds = stopwatchMs / 1000;
    uint8_t minutes = totalSeconds / 60;
    uint8_t seconds = totalSeconds % 60;
    uint8_t ms = (stopwatchMs % 1000) / 10;
    
    gfx->setTextSize(5);
    gfx->setTextColor(theme->accent1);
    gfx->setCursor((LCD_WIDTH - 200) / 2, 180);
    gfx->printf("%02d:%02d", minutes, seconds);
    gfx->setTextSize(3);
    gfx->setCursor((LCD_WIDTH + 100) / 2, 190);
    gfx->printf(".%02d", ms);
    
    // Controls
    if (stopwatchRunning) {
        drawButton(MARGIN + 30, 290, 140, 60, "STOP", theme->danger);
    } else {
        drawButton(MARGIN + 30, 290, 140, 60, "START", theme->success);
    }
    drawButton(MARGIN + 190, 290, 120, 60, "RESET", theme->cardLight);
    
    drawNavHint("Swipe up to exit");
}

void drawSubTimerPomodoro() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Pomodoro");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 240, theme->card);
    
    // Time ring
    int16_t cx = LCD_WIDTH / 2;
    int16_t cy = 190;
    drawProgressRing(cx, cy, 70, 10, 1.0f, theme->danger, theme->cardLight);
    
    gfx->setTextSize(4);
    gfx->setTextColor(theme->text);
    gfx->setCursor(cx - 40, cy - 15);
    gfx->printf("%d:00", pomodoroMinutes);
    
    // Duration options
    int16_t btnY = 330;
    uint8_t durations[] = {15, 25, 45};
    for (int i = 0; i < 3; i++) {
        int16_t x = MARGIN + 30 + i * 110;
        uint16_t col = (pomodoroMinutes == durations[i]) ? theme->accent1 : theme->cardLight;
        gfx->fillRoundRect(x, btnY, 80, 40, 8, col);
        gfx->setTextSize(2);
        gfx->setTextColor(theme->text);
        gfx->setCursor(x + 20, btnY + 10);
        gfx->printf("%dm", durations[i]);
    }
    
    drawNavHint("Swipe up to exit");
}

void drawSubSettingsTheme() {
    gfx->fillScreen(theme->bg);
    drawSubHeader("Theme");
    
    for (int i = 0; i < THEME_COUNT; i++) {
        int16_t x = MARGIN + (i % 2) * 170;
        int16_t y = 80 + (i / 2) * 100;
        
        uint16_t borderCol = (i == currentTheme) ? themes[i].accent1 : themes[i].card;
        gfx->drawRoundRect(x, y, 150, 85, 12, borderCol);
        gfx->fillRoundRect(x + 2, y + 2, 146, 81, 10, themes[i].bg);
        
        gfx->setTextSize(1);
        gfx->setTextColor(themes[i].text);
        gfx->setCursor(x + 15, y + 20);
        gfx->print(themes[i].name);
        
        // Color dots
        gfx->fillCircle(x + 30, y + 55, 8, themes[i].accent1);
        gfx->fillCircle(x + 55, y + 55, 8, themes[i].accent2);
        gfx->fillCircle(x + 80, y + 55, 8, themes[i].accent3);
    }
    
    drawNavHint("Tap to select, swipe up to exit");
}

// ═══════════════════════════════════════════════════════════════════════════
//  GESTURE DETECTION
// ═══════════════════════════════════════════════════════════════════════════

GestureType detectGesture() {
    if (gestureStartX < 0 || gestureEndX < 0) return GESTURE_NONE;
    
    int16_t dx = gestureEndX - gestureStartX;
    int16_t dy = gestureEndY - gestureStartY;
    unsigned long dt = millis() - gestureStartTime;
    
    // Check for long press
    if (dt > LONG_PRESS_TIME && abs(dx) < 20 && abs(dy) < 20) {
        return GESTURE_LONG_PRESS;
    }
    
    // Check for tap
    if (dt < 200 && abs(dx) < 20 && abs(dy) < 20) {
        return GESTURE_TAP;
    }
    
    if (abs(dx) > abs(dy)) {
        if (abs(dx) > SWIPE_THRESHOLD) {
            return (dx > 0) ? GESTURE_RIGHT : GESTURE_LEFT;
        }
    } else {
        if (abs(dy) > SWIPE_THRESHOLD) {
            return (dy > 0) ? GESTURE_DOWN : GESTURE_UP;
        }
    }
    
    return GESTURE_NONE;
}

SubCard getFirstSubCard(MainCard card) {
    switch (card) {
        case CARD_CLOCK: return SUB_CLOCK_WORLD;
        case CARD_STEPS: return SUB_STEPS_GRAPH;
        case CARD_MUSIC: return SUB_MUSIC_PLAYLIST;
        case CARD_WEATHER: return SUB_WEATHER_FORECAST;
        case CARD_CALENDAR: return SUB_CALENDAR_EVENTS;
        case CARD_GAMES: return SUB_GAMES_YESNO;
        case CARD_STREAK: return SUB_STREAK_DAILY;
        case CARD_FINANCE: return SUB_FINANCE_BUDGET;
        case CARD_SYSTEM: return SUB_SYSTEM_STATS;
        case CARD_TIMER: return SUB_TIMER_STOPWATCH;
        case CARD_SETTINGS: return SUB_SETTINGS_THEME;
        default: return SUB_NONE;
    }
}

SubCard getNextSubCard(SubCard current, MainCard mainCard, bool forward) {
    int delta = forward ? 1 : -1;
    int base, count;
    
    switch (mainCard) {
        case CARD_CLOCK: base = SUB_CLOCK_WORLD; count = 4; break;
        case CARD_STEPS: base = SUB_STEPS_GRAPH; count = 4; break;
        case CARD_MUSIC: base = SUB_MUSIC_PLAYLIST; count = 3; break;
        case CARD_WEATHER: base = SUB_WEATHER_FORECAST; count = 3; break;
        case CARD_CALENDAR: base = SUB_CALENDAR_EVENTS; count = 3; break;
        case CARD_GAMES: base = SUB_GAMES_YESNO; count = 4; break;
        case CARD_STREAK: base = SUB_STREAK_DAILY; count = 3; break;
        case CARD_FINANCE: base = SUB_FINANCE_BUDGET; count = 3; break;
        case CARD_SYSTEM: base = SUB_SYSTEM_STATS; count = 4; break;
        case CARD_TIMER: base = SUB_TIMER_STOPWATCH; count = 3; break;
        case CARD_SETTINGS: base = SUB_SETTINGS_THEME; count = 4; break;
        default: return SUB_NONE;
    }
    
    int currentIdx = current - base;
    int newIdx = (currentIdx + delta + count) % count;
    return (SubCard)(base + newIdx);
}

void handleGesture(GestureType gesture) {
    if (gesture == GESTURE_NONE) return;
    
    if (gesture == GESTURE_LONG_PRESS) {
        // Open theme selector
        currentSubCard = SUB_SETTINGS_THEME;
        redrawNeeded = true;
        return;
    }
    
    if (currentSubCard == SUB_NONE) {
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
                subCardIndex = 0;
                currentSubCard = getFirstSubCard(currentMainCard);
                redrawNeeded = true;
                break;
            default:
                break;
        }
    } else {
        if (gesture == GESTURE_UP) {
            currentSubCard = SUB_NONE;
            subCardIndex = 0;
            redrawNeeded = true;
        } else if (gesture == GESTURE_LEFT) {
            currentSubCard = getNextSubCard(currentSubCard, currentMainCard, true);
            redrawNeeded = true;
        } else if (gesture == GESTURE_RIGHT) {
            currentSubCard = getNextSubCard(currentSubCard, currentMainCard, false);
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
        touch.getPoint(&gestureStartX, &gestureStartY);
        gestureInProgress = true;
        gestureStartTime = millis();
    } else if (!t && lastTouchState && gestureInProgress) {
        touch.getPoint(&gestureEndX, &gestureEndY);
        touchX = gestureEndX;
        touchY = gestureEndY;
        
        if (millis() - gestureStartTime < SWIPE_TIMEOUT) {
            GestureType gesture = detectGesture();
            
            if (gesture == GESTURE_TAP) {
                // Handle button taps
                if (currentSubCard == SUB_GAMES_CLICKER) {
                    if (inRect(LCD_WIDTH/2 - 80, 200, 160, 160)) {
                        clickerScore++;
                        redrawNeeded = true;
                    }
                } else if (currentSubCard == SUB_GAMES_REACTION) {
                    if (!reactionWaiting && reactionTime == 0) {
                        if (inRect(MARGIN + 60, 260, LCD_WIDTH - 2*MARGIN - 120, 80)) {
                            reactionWaiting = true;
                            reactionStartTime = millis() + random(1000, 3000);
                            redrawNeeded = true;
                        }
                    } else if (reactionWaiting && millis() >= reactionStartTime) {
                        reactionTime = millis() - reactionStartTime;
                        reactionWaiting = false;
                        redrawNeeded = true;
                    } else if (reactionTime > 0) {
                        if (inRect(MARGIN + 60, 290, LCD_WIDTH - 2*MARGIN - 120, 60)) {
                            reactionTime = 0;
                            redrawNeeded = true;
                        }
                    }
                } else if (currentSubCard == SUB_GAMES_YESNO) {
                    if (!yesNoShowing) {
                        if (inRect(MARGIN + 20, 240, 140, 80)) {
                            yesNoResult = true;
                            yesNoShowing = true;
                            redrawNeeded = true;
                        } else if (inRect(MARGIN + 180, 240, 140, 80)) {
                            yesNoResult = false;
                            yesNoShowing = true;
                            redrawNeeded = true;
                        }
                    } else {
                        if (inRect(MARGIN + 60, 310, LCD_WIDTH - 2*MARGIN - 120, 60)) {
                            yesNoShowing = false;
                            redrawNeeded = true;
                        }
                    }
                } else if (currentSubCard == SUB_GAMES_DINO) {
                    if (inRect(MARGIN + 60, 360, LCD_WIDTH - 2*MARGIN - 120, 60)) {
                        dinoJumping = true;
                        dinoScore++;
                        redrawNeeded = true;
                    }
                } else if (currentSubCard == SUB_TIMER_STOPWATCH) {
                    if (inRect(MARGIN + 30, 290, 140, 60)) {
                        stopwatchRunning = !stopwatchRunning;
                        redrawNeeded = true;
                    } else if (inRect(MARGIN + 190, 290, 120, 60)) {
                        stopwatchMs = 0;
                        stopwatchRunning = false;
                        redrawNeeded = true;
                    }
                } else if (currentSubCard == SUB_SETTINGS_THEME) {
                    // Theme selection
                    for (int i = 0; i < THEME_COUNT; i++) {
                        int16_t x = MARGIN + (i % 2) * 170;
                        int16_t y = 80 + (i / 2) * 100;
                        if (inRect(x, y, 150, 85)) {
                            currentTheme = (ThemeType)i;
                            theme = &themes[currentTheme];
                            prefs.putUInt("theme", currentTheme);
                            redrawNeeded = true;
                            break;
                        }
                    }
                } else if (currentMainCard == CARD_MUSIC && currentSubCard == SUB_NONE) {
                    int16_t cx = LCD_WIDTH / 2;
                    if (inRect(cx - 28, 230 - 28, 56, 56)) {
                        musicPlaying = !musicPlaying;
                        redrawNeeded = true;
                    }
                }
            } else {
                handleGesture(gesture);
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
        if ((currentMainCard == CARD_CLOCK || currentMainCard == CARD_TIMER) && currentSubCard == SUB_NONE) {
            redrawNeeded = true;
        }
        if (currentSubCard >= SUB_CLOCK_WORLD && currentSubCard <= SUB_CLOCK_ALARM) {
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
            caloriesBurned = stepCount * 0.05;
            distanceKm = stepCount * 0.0007;
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
    if (millis() - lastGameRotate >= 300000) {
        lastGameRotate = millis();
        spotlightGame = (spotlightGame + 1) % 4;
        if (currentMainCard == CARD_GAMES && currentSubCard == SUB_NONE) {
            redrawNeeded = true;
        }
    }
    
    if (reactionWaiting && millis() >= reactionStartTime && currentSubCard == SUB_GAMES_REACTION) {
        redrawNeeded = true;
    }
    
    // Reset dino jump
    if (dinoJumping) {
        static unsigned long jumpStart = 0;
        if (jumpStart == 0) jumpStart = millis();
        if (millis() - jumpStart > 300) {
            dinoJumping = false;
            jumpStart = 0;
            redrawNeeded = true;
        }
    }
}

void serviceStats() {
    if (millis() - lastStatsUpdate >= 2000) {
        lastStatsUpdate = millis();
        cpuUsage = random(30, 70);
        temperature = 40.0f + random(0, 50) / 10.0f;
        freeRAM = ESP.getFreeHeap();
        if (currentMainCard == CARD_SYSTEM) redrawNeeded = true;
    }
}

void serviceStopwatch() {
    if (stopwatchRunning) {
        unsigned long now = millis();
        if (now - lastStopwatchUpdate >= 10) {
            stopwatchMs += (now - lastStopwatchUpdate);
            lastStopwatchUpdate = now;
            if (currentSubCard == SUB_TIMER_STOPWATCH) redrawNeeded = true;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  RENDERING
// ═══════════════════════════════════════════════════════════════════════════

void renderScreen() {
    if (!redrawNeeded) return;
    
    if (currentSubCard == SUB_NONE) {
        switch (currentMainCard) {
            case CARD_CLOCK:   drawMainClock();    break;
            case CARD_STEPS:   drawMainSteps();    break;
            case CARD_MUSIC:   drawMainMusic();    break;
            case CARD_WEATHER: drawMainWeather();  break;
            case CARD_CALENDAR: drawMainCalendar(); break;
            case CARD_GAMES:   drawMainGames();    break;
            case CARD_STREAK:  drawMainStreak();   break;
            case CARD_FINANCE: drawMainFinance();  break;
            case CARD_SYSTEM:  drawMainSystem();   break;
            case CARD_TIMER:   drawMainTimer();    break;
            case CARD_SETTINGS: drawMainSettings(); break;
        }
    } else {
        switch (currentSubCard) {
            case SUB_CLOCK_WORLD:    drawSubClockWorld();    break;
            case SUB_CLOCK_ANALOG:   drawSubClockAnalog();   break;
            case SUB_CLOCK_DIGITAL:  drawSubClockDigital();  break;
            case SUB_STEPS_GRAPH:    drawSubStepsGraph();    break;
            case SUB_STEPS_CALORIES: drawSubStepsCalories(); break;
            case SUB_GAMES_YESNO:    drawSubGamesYesNo();    break;
            case SUB_GAMES_CLICKER:  drawSubGamesClicker();  break;
            case SUB_GAMES_REACTION: drawSubGamesReaction(); break;
            case SUB_GAMES_DINO:     drawSubGamesDino();     break;
            case SUB_STREAK_DAILY:   drawSubStreakDaily();   break;
            case SUB_STREAK_WEEKLY:  drawSubStreakWeekly();  break;
            case SUB_FINANCE_BUDGET: drawSubFinanceBudget(); break;
            case SUB_FINANCE_CRYPTO: drawSubFinanceCrypto(); break;
            case SUB_SYSTEM_BATTERY: drawSubSystemBattery(); break;
            case SUB_TIMER_STOPWATCH: drawSubTimerStopwatch(); break;
            case SUB_TIMER_POMODORO: drawSubTimerPomodoro(); break;
            case SUB_SETTINGS_THEME: drawSubSettingsTheme(); break;
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
    Serial.println("  S3 MiniOS v2.0 - Multi-Theme Card OS");
    Serial.println("═══════════════════════════════════════════\n");
    
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    Serial.println("[OK] I2C Bus");
    
    // Load saved theme
    prefs.begin("minios", false);
    currentTheme = (ThemeType)prefs.getUInt("theme", THEME_AMOLED);
    if (currentTheme >= THEME_COUNT) currentTheme = THEME_AMOLED;
    theme = &themes[currentTheme];
    Serial.printf("[OK] Theme: %s\n", theme->name);
    
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
    gfx->fillScreen(theme->bg);
    for (int i = 0; i <= 255; i += 5) { gfx->setBrightness(i); delay(2); }
    Serial.println("[OK] AMOLED Display");
    
    // Splash screen
    gfx->setTextSize(4);
    gfx->setTextColor(theme->accent1);
    gfx->setCursor(70, 160);
    gfx->print("S3 MiniOS");
    gfx->setTextSize(2);
    gfx->setTextColor(theme->textDim);
    gfx->setCursor(80, 220);
    gfx->print("Multi-Theme Card OS");
    gfx->setTextSize(1);
    gfx->setCursor(140, 270);
    gfx->print("v2.0 Loading...");
    
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
    
    freeRAM = ESP.getFreeHeap();
    
    delay(2000);
    
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("  System Ready - Swipe to Navigate");
    Serial.println("  Long press for Theme Selector");
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
    serviceStopwatch();
    handleTouch();
    renderScreen();
    
    delay(10);
}
