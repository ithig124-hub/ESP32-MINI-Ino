/**
 * ═══════════════════════════════════════════════════════════════════════════
 *  S3 MiniOS v3.0 - ESP32-S3-Touch-AMOLED-1.8 Firmware
 *  ENHANCED EDITION WITH SD WIFI, POWER CONTROLS & BATTERY ESTIMATION
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 *  NEW FEATURES:
 *  - WiFi auto-connect from SD card (/wifi/config.txt)
 *  - Stats save every 2 hours (reduced lag)
 *  - Factory reset button (data only, preserves sketch & SD)
 *  - Weather based on WiFi/IP geolocation
 *  - Instant ON/OFF button, long press for shutdown
 *  - Battery time estimate on System card
 * 
 *  Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.8
 *    • Display: SH8601 QSPI AMOLED 368x448
 *    • Touch: FT3168
 *    • IMU: QMI8658
 *    • RTC: PCF85063
 *    • PMU: AXP2101
 *    • Battery: 3.7V MX1.25 LiPo (onboard)
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <Wire.h>
#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SD_MMC.h>
#include <math.h>
#include <esp_sleep.h>

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

// SD Card (SDMMC 1-bit mode)
#define SDMMC_CLK       2
#define SDMMC_CMD       1
#define SDMMC_DATA      3

#define XCA9554_ADDR    0x20
#define AXP2101_ADDR    0x34
#define FT3168_ADDR     0x38
#define PCF85063_ADDR   0x51
#define QMI8658_ADDR    0x6B

// ═══════════════════════════════════════════════════════════════════════════
//  CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

// Save interval - 2 hours to reduce lag (was 30 seconds)
#define SAVE_INTERVAL_MS 7200000UL  // 2 hours = 7,200,000 ms

// WiFi config file path on SD card
#define WIFI_CONFIG_PATH "/wifi/config.txt"

// Button timing
#define BUTTON_LONG_PRESS_MS 3000  // 3 seconds for shutdown
#define SCREEN_OFF_TIMEOUT_MS 30000  // 30 seconds

// Battery estimation (3.7V 500mAh typical for this board)
#define BATTERY_CAPACITY_MAH 500
#define SCREEN_ON_CURRENT_MA 80   // ~80mA with screen on
#define SCREEN_OFF_CURRENT_MA 15  // ~15mA deep sleep

// ═══════════════════════════════════════════════════════════════════════════
//  WIFI CONFIGURATION (loaded from SD or defaults)
// ═══════════════════════════════════════════════════════════════════════════

char wifiSSID[64] = "";
char wifiPassword[64] = "";
char weatherCity[64] = "Perth";
char weatherCountry[8] = "AU";
long gmtOffsetSec = 8 * 3600;  // GMT+8 for Perth
bool wifiConfigFromSD = false;
bool wifiConnected = false;

// API Key (OpenWeatherMap free tier)
const char* OPENWEATHER_API = "3795c13a0d3f7e17799d638edda60e3c";

// ═══════════════════════════════════════════════════════════════════════════
//  THEME (Modern AMOLED Design)
// ═══════════════════════════════════════════════════════════════════════════

#define COL_BG          0x0000
#define COL_CARD        0x18E3
#define COL_CARD_LIGHT  0x2945
#define COL_TEXT        0xFFFF
#define COL_TEXT_DIM    0x7BEF
#define COL_TEXT_DIMMER 0x4208

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
    SUB_SYSTEM_INFO = 51,
    SUB_SYSTEM_RESET = 52,
    SUB_SYSTEM_BATTERY = 53
};

MainCard currentMainCard = CARD_CLOCK;
SubCard currentSubCard = SUB_NONE;
int subCardIndex = 0;
bool redrawNeeded = true;

// ═══════════════════════════════════════════════════════════════════════════
//  GESTURE DETECTION
// ═══════════════════════════════════════════════════════════════════════════

enum GestureType { GESTURE_NONE, GESTURE_LEFT, GESTURE_RIGHT, GESTURE_UP, GESTURE_DOWN, GESTURE_TAP };

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
uint32_t stepCount = 0;
uint32_t stepGoal = 10000;
uint32_t stepHistory[7] = {0, 0, 0, 0, 0, 0, 0};

// Music
bool musicPlaying = false;
uint8_t musicProgress = 35;
const char* musicTitle = "Night Drive";
const char* musicArtist = "Synthwave FM";
uint16_t musicDuration = 245;
uint16_t musicCurrent = 86;

// Games
uint8_t spotlightGame = 0;
unsigned long lastGameRotate = 0;
uint32_t clickerScore = 0;
uint16_t reactionTime = 0;
bool reactionWaiting = false;
unsigned long reactionStartTime = 0;

// Weather (updated from API or IP geolocation)
int8_t weatherTemp = 22;
char weatherCondition[32] = "Sunny";
uint8_t weatherHumidity = 65;
uint16_t weatherWind = 12;
int8_t forecast[5] = {22, 24, 23, 21, 20};
char weatherLocation[64] = "Perth, AU";

// System stats
uint8_t cpuUsage = 45;
uint32_t freeRAM = 234567;
float temperature = 42.5;
uint16_t fps = 60;

// Battery
uint16_t batteryVoltage = 4100;
uint8_t batteryPercent = 85;
uint32_t batteryEstimateMinutes = 0;

// Screen state
bool screenOn = true;
unsigned long lastActivityMs = 0;
unsigned long screenOffTime = 0;

// Button state for power control
unsigned long buttonPressStart = 0;
bool buttonPressed = false;

// Timing
unsigned long lastClockUpdate = 0;
unsigned long lastStepUpdate = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastStatsUpdate = 0;
unsigned long lastMusicUpdate = 0;
unsigned long lastSaveTime = 0;
unsigned long lastWeatherUpdate = 0;

// Hardware flags
bool hasIMU = false, hasRTC = false, hasPMU = false, hasSD = false;

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
    uint8_t digitalRead(uint8_t pin) {
        return (readReg(0x00) >> pin) & 0x01;
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
    void getDate(uint8_t *day, uint8_t *weekday, uint8_t *month, uint8_t *year) {
        Wire.beginTransmission(PCF85063_ADDR); Wire.write(0x07); Wire.endTransmission();
        Wire.requestFrom(PCF85063_ADDR,(uint8_t)4);
        *day = bcd(Wire.read()&0x3F);
        *weekday = Wire.read()&0x07;
        *month = bcd(Wire.read()&0x1F);
        *year = bcd(Wire.read());
    }
private:
    uint8_t bcd(uint8_t b) { return ((b>>4)*10)+(b&0x0F); }
};

class AXP2101 {
public:
    bool begin() { Wire.beginTransmission(AXP2101_ADDR); return Wire.endTransmission() == 0; }
    uint16_t getBattVoltage() { uint8_t h=readReg(0x34),l=readReg(0x35); return ((h&0x3F)<<8)|l; }
    uint8_t getBattPercent() { return readReg(0xA4)&0x7F; }
    bool isCharging() { return (readReg(0x01) & 0x04) != 0; }
    void powerOff() {
        // Set shutdown bit
        writeReg(0x10, readReg(0x10) | 0x01);
    }
private:
    uint8_t readReg(uint8_t r) { Wire.beginTransmission(AXP2101_ADDR); Wire.write(r); Wire.endTransmission(); Wire.requestFrom(AXP2101_ADDR,(uint8_t)1); return Wire.read(); }
    void writeReg(uint8_t r, uint8_t v) { Wire.beginTransmission(AXP2101_ADDR); Wire.write(r); Wire.write(v); Wire.endTransmission(); }
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
//  SD CARD WIFI CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

bool initSDCard() {
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    if (!SD_MMC.begin("/sdcard", true, true)) {  // 1-bit mode
        Serial.println("[WARN] SD Card mount failed");
        return false;
    }
    Serial.println("[OK] SD Card mounted");
    return true;
}

void createWiFiTemplate() {
    // Create /wifi folder if not exists
    if (!SD_MMC.exists("/wifi")) {
        SD_MMC.mkdir("/wifi");
    }
    
    // Create template config file
    File file = SD_MMC.open(WIFI_CONFIG_PATH, "w");
    if (file) {
        file.println("# WiFi Configuration for S3 MiniOS");
        file.println("# Edit this file with your WiFi credentials");
        file.println("# The watch will auto-connect on boot");
        file.println("#");
        file.println("# Format: KEY=VALUE (no spaces around =)");
        file.println("#");
        file.println("SSID=YourWiFiName");
        file.println("PASSWORD=YourWiFiPassword");
        file.println("#");
        file.println("# Optional: Override weather location");
        file.println("# Leave blank to auto-detect from IP");
        file.println("CITY=Perth");
        file.println("COUNTRY=AU");
        file.println("#");
        file.println("# Timezone offset in hours from GMT");
        file.println("GMT_OFFSET=8");
        file.close();
        Serial.println("[OK] Created WiFi template: " WIFI_CONFIG_PATH);
    }
}

bool loadWiFiFromSD() {
    if (!hasSD) return false;
    
    File file = SD_MMC.open(WIFI_CONFIG_PATH, "r");
    if (!file) {
        Serial.println("[INFO] No WiFi config found, creating template...");
        createWiFiTemplate();
        return false;
    }
    
    Serial.println("[INFO] Loading WiFi config from SD card...");
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        // Skip comments and empty lines
        if (line.startsWith("#") || line.length() == 0) continue;
        
        int eqPos = line.indexOf('=');
        if (eqPos < 0) continue;
        
        String key = line.substring(0, eqPos);
        String value = line.substring(eqPos + 1);
        key.trim();
        value.trim();
        
        if (key == "SSID" && value.length() > 0) {
            strncpy(wifiSSID, value.c_str(), 63);
            wifiSSID[63] = '\0';
            Serial.printf("  SSID: %s\n", wifiSSID);
        }
        else if (key == "PASSWORD") {
            strncpy(wifiPassword, value.c_str(), 63);
            wifiPassword[63] = '\0';
            Serial.println("  Password: ****");
        }
        else if (key == "CITY" && value.length() > 0) {
            strncpy(weatherCity, value.c_str(), 63);
            weatherCity[63] = '\0';
            Serial.printf("  City: %s\n", weatherCity);
        }
        else if (key == "COUNTRY" && value.length() > 0) {
            strncpy(weatherCountry, value.c_str(), 7);
            weatherCountry[7] = '\0';
        }
        else if (key == "GMT_OFFSET") {
            gmtOffsetSec = value.toInt() * 3600;
            Serial.printf("  GMT Offset: %ld\n", gmtOffsetSec / 3600);
        }
    }
    file.close();
    
    wifiConfigFromSD = (strlen(wifiSSID) > 0);
    return wifiConfigFromSD;
}

// ═══════════════════════════════════════════════════════════════════════════
//  WIFI CONNECTION & IP GEOLOCATION
// ═══════════════════════════════════════════════════════════════════════════

void connectWiFi() {
    if (strlen(wifiSSID) == 0) {
        Serial.println("[WARN] No WiFi SSID configured");
        return;
    }
    
    Serial.printf("[INFO] Connecting to WiFi: %s\n", wifiSSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPassword);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.printf("\n[OK] WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        
        // Get location from IP if city not specified
        if (strlen(weatherCity) == 0 || strcmp(weatherCity, "Perth") == 0) {
            getLocationFromIP();
        }
    } else {
        Serial.println("\n[WARN] WiFi connection failed");
    }
}

void getLocationFromIP() {
    if (!wifiConnected) return;
    
    HTTPClient http;
    http.begin("http://ip-api.com/json/?fields=city,country,countryCode,timezone");
    http.setTimeout(5000);
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
            const char* city = doc["city"];
            const char* countryCode = doc["countryCode"];
            
            if (city && strlen(city) > 0) {
                strncpy(weatherCity, city, 63);
                weatherCity[63] = '\0';
            }
            if (countryCode && strlen(countryCode) > 0) {
                strncpy(weatherCountry, countryCode, 7);
                weatherCountry[7] = '\0';
            }
            
            snprintf(weatherLocation, 63, "%s, %s", weatherCity, weatherCountry);
            Serial.printf("[OK] Location from IP: %s\n", weatherLocation);
        }
    }
    http.end();
}

// ═══════════════════════════════════════════════════════════════════════════
//  WEATHER API
// ═══════════════════════════════════════════════════════════════════════════

void fetchWeather() {
    if (!wifiConnected) return;
    
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.openweathermap.org/data/2.5/weather?q=%s,%s&appid=%s&units=metric",
        weatherCity, weatherCountry, OPENWEATHER_API);
    
    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        DynamicJsonDocument doc(1024);
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
            weatherTemp = (int8_t)doc["main"]["temp"].as<float>();
            weatherHumidity = doc["main"]["humidity"].as<uint8_t>();
            weatherWind = (uint16_t)doc["wind"]["speed"].as<float>();
            
            const char* desc = doc["weather"][0]["main"];
            if (desc) {
                strncpy(weatherCondition, desc, 31);
                weatherCondition[31] = '\0';
            }
            
            Serial.printf("[OK] Weather: %d°C, %s\n", weatherTemp, weatherCondition);
        }
    }
    http.end();
    lastWeatherUpdate = millis();
}

// ═══════════════════════════════════════════════════════════════════════════
//  DATA PERSISTENCE (every 2 hours)
// ═══════════════════════════════════════════════════════════════════════════

void saveUserData() {
    prefs.begin("minios", false);
    prefs.putUInt("steps", stepCount);
    prefs.putUInt("stepGoal", stepGoal);
    prefs.putUInt("clicker", clickerScore);
    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist%d", i);
        prefs.putUInt(key, stepHistory[i]);
    }
    prefs.end();
    Serial.println("[OK] Data saved");
    lastSaveTime = millis();
}

void loadUserData() {
    prefs.begin("minios", true);
    stepCount = prefs.getUInt("steps", 0);
    stepGoal = prefs.getUInt("stepGoal", 10000);
    clickerScore = prefs.getUInt("clicker", 0);
    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist%d", i);
        stepHistory[i] = prefs.getUInt(key, 0);
    }
    prefs.end();
    Serial.println("[OK] Data loaded");
}

// ═══════════════════════════════════════════════════════════════════════════
//  FACTORY RESET (data only, preserves sketch & SD card)
// ═══════════════════════════════════════════════════════════════════════════

void factoryReset() {
    Serial.println("[WARN] FACTORY RESET - Clearing all data...");
    
    // Clear preferences (NVS)
    prefs.begin("minios", false);
    prefs.clear();
    prefs.end();
    
    // Reset all runtime variables
    stepCount = 0;
    stepGoal = 10000;
    clickerScore = 0;
    for (int i = 0; i < 7; i++) {
        stepHistory[i] = 0;
    }
    
    // Reset music state
    musicPlaying = false;
    musicCurrent = 0;
    
    // Reset games
    spotlightGame = 0;
    reactionTime = 0;
    
    Serial.println("[OK] Factory reset complete");
    Serial.println("[INFO] SD card data preserved");
    
    // Restart to apply
    delay(1000);
    ESP.restart();
}

// ═══════════════════════════════════════════════════════════════════════════
//  BATTERY TIME ESTIMATION
// ═══════════════════════════════════════════════════════════════════════════

void calculateBatteryEstimate() {
    // Calculate remaining battery time based on current state
    float remainingCapacity = (BATTERY_CAPACITY_MAH * batteryPercent) / 100.0f;
    float currentDraw = screenOn ? SCREEN_ON_CURRENT_MA : SCREEN_OFF_CURRENT_MA;
    
    // Estimate in minutes
    batteryEstimateMinutes = (uint32_t)((remainingCapacity / currentDraw) * 60.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
//  POWER CONTROL (ON/OFF button handling)
// ═══════════════════════════════════════════════════════════════════════════

void screenOff() {
    if (!screenOn) return;
    screenOn = false;
    screenOffTime = millis();
    gfx->setBrightness(0);
    Serial.println("[INFO] Screen OFF");
}

void screenOnFunc() {
    if (screenOn) return;
    screenOn = true;
    lastActivityMs = millis();
    gfx->setBrightness(200);
    redrawNeeded = true;
    Serial.println("[INFO] Screen ON");
}

void shutdownDevice() {
    Serial.println("[INFO] Shutting down...");
    
    // Save data before shutdown
    saveUserData();
    
    // Display shutdown message
    gfx->fillScreen(COL_BG);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(100, LCD_HEIGHT/2 - 20);
    gfx->print("Shutting down...");
    delay(1000);
    
    // Turn off display
    gfx->setBrightness(0);
    
    // Power off via PMU
    if (hasPMU) {
        pmu.powerOff();
    }
    
    // If PMU doesn't work, use deep sleep
    esp_deep_sleep_start();
}

void handlePowerButton() {
    // Check if touch is in top-right corner (power button area)
    bool inPowerArea = (touchX > LCD_WIDTH - 60 && touchY < 60);
    
    if (inPowerArea && !buttonPressed) {
        buttonPressed = true;
        buttonPressStart = millis();
    }
    else if (!inPowerArea && buttonPressed) {
        unsigned long pressDuration = millis() - buttonPressStart;
        buttonPressed = false;
        
        if (pressDuration >= BUTTON_LONG_PRESS_MS) {
            // Long press - shutdown
            shutdownDevice();
        } else if (pressDuration > 50) {
            // Short press - toggle screen
            if (screenOn) {
                screenOff();
            } else {
                screenOnFunc();
            }
        }
    }
    
    // Visual feedback for long press
    if (buttonPressed && screenOn) {
        unsigned long elapsed = millis() - buttonPressStart;
        if (elapsed > 500) {
            // Show progress indicator
            int progress = min((int)((elapsed - 500) * 100 / (BUTTON_LONG_PRESS_MS - 500)), 100);
            gfx->fillRect(LCD_WIDTH - 50, 10, 40, 5, COL_CARD);
            gfx->fillRect(LCD_WIDTH - 50, 10, (40 * progress) / 100, 5, COL_DANGER);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  UI WIDGET FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void drawGradientCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c1, uint16_t c2) {
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

void drawBatteryIcon(int16_t x, int16_t y, uint8_t percent, bool charging) {
    uint16_t col = percent > 20 ? COL_SUCCESS : COL_DANGER;
    if (charging) col = COL_CLOCK;
    
    gfx->drawRect(x, y + 2, 20, 10, COL_TEXT_DIM);
    gfx->fillRect(x + 20, y + 4, 2, 6, COL_TEXT_DIM);
    int16_t fillW = (18 * percent) / 100;
    gfx->fillRect(x + 1, y + 3, fillW, 8, col);
    
    if (charging) {
        gfx->setTextSize(1);
        gfx->setCursor(x + 5, y + 3);
        gfx->setTextColor(COL_BG);
        gfx->print("*");
    }
}

void drawPowerButton(int16_t x, int16_t y) {
    // Power button indicator in top-right
    gfx->drawCircle(x, y, 12, COL_TEXT_DIMMER);
    gfx->drawLine(x, y - 8, x, y - 3, COL_TEXT_DIMMER);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN CARD SCREENS
// ═══════════════════════════════════════════════════════════════════════════

void drawMainClock() {
    gfx->fillScreen(COL_BG);
    
    // Top bar
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("CLOCK");
    
    // Power button hint
    drawPowerButton(LCD_WIDTH - 25, 25);
    
    // Battery indicator
    bool charging = hasPMU ? pmu.isCharging() : false;
    drawBatteryIcon(LCD_WIDTH - 60, 8, batteryPercent, charging);
    
    // WiFi indicator
    gfx->setTextColor(wifiConnected ? COL_SUCCESS : COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH - 85, 10);
    gfx->print("W");
    
    // Main gradient card
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, 0x4A5F, 0x001F);
    
    // App icon/branding
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("S3 MiniOS v3");
    
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
    
    // Location from WiFi
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 270);
    gfx->print(weatherLocation);
    
    // Navigation hints
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down for more");
}

void drawMainWeather() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("WEATHER");
    
    drawPowerButton(LCD_WIDTH - 25, 25);
    bool charging = hasPMU ? pmu.isCharging() : false;
    drawBatteryIcon(LCD_WIDTH - 60, 8, batteryPercent, charging);
    
    // Weather card
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, 0x07E0, 0x001F);
    
    // Location (from WiFi/IP)
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print(weatherLocation);
    
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
    gfx->printf("Humidity: %d%%  Wind: %d km/h", weatherHumidity, weatherWind);
    
    // WiFi status
    if (wifiConfigFromSD) {
        gfx->setTextColor(COL_SUCCESS);
        gfx->setCursor(MARGIN + 20, 285);
        gfx->print("WiFi from SD card");
    }
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down for forecast");
}

void drawMainSteps() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("STEPS");
    
    drawPowerButton(LCD_WIDTH - 25, 25);
    bool charging = hasPMU ? pmu.isCharging() : false;
    drawBatteryIcon(LCD_WIDTH - 60, 8, batteryPercent, charging);
    
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, 0x5A5F, 0x4011);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("Daily Steps");
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 120);
    gfx->print("Steps:");
    
    gfx->setTextSize(7);
    gfx->setTextColor(COL_TEXT);
    char stepsStr[10];
    sprintf(stepsStr, "%lu", stepCount);
    gfx->setCursor(MARGIN + 20, 155);
    gfx->print(stepsStr);
    
    char goalStr[20];
    sprintf(goalStr, "/ %lu", stepGoal);
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 220, 195);
    gfx->print(goalStr);
    
    float stepProgress = (float)stepCount / (float)stepGoal;
    if (stepProgress > 1.0f) stepProgress = 1.0f;
    drawProgressBar(MARGIN + 15, 270, LCD_WIDTH - 2*MARGIN - 30, 12, stepProgress, COL_STEPS, COL_CARD_LIGHT);
    
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
    
    drawPowerButton(LCD_WIDTH - 25, 25);
    bool charging = hasPMU ? pmu.isCharging() : false;
    drawBatteryIcon(LCD_WIDTH - 60, 8, batteryPercent, charging);
    
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 300, 0xF81F, 0x001F);
    
    gfx->fillRoundRect(MARGIN + 20, 90, 120, 120, 12, COL_CARD_LIGHT);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 45, 145);
    gfx->print("Album Art");
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN + 160, 110);
    gfx->print(musicTitle);
    
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 160, 140);
    gfx->print(musicArtist);
    
    int16_t controlY = 250;
    int16_t controlCX = LCD_WIDTH / 2;
    
    gfx->fillCircle(controlCX - 60, controlY, 18, COL_CARD_LIGHT);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(controlCX - 69, controlY - 8);
    gfx->print("|<");
    
    gfx->fillCircle(controlCX, controlY, 25, COL_TEXT);
    gfx->setTextColor(COL_BG);
    gfx->setTextSize(3);
    gfx->setCursor(controlCX - 6, controlY - 12);
    gfx->print(musicPlaying ? "||" : ">");
    
    gfx->fillCircle(controlCX + 60, controlY, 18, COL_CARD_LIGHT);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(controlCX + 51, controlY - 8);
    gfx->print(">|");
    
    drawProgressBar(MARGIN + 20, 320, LCD_WIDTH - 2*MARGIN - 40, 8, (float)musicCurrent / musicDuration, COL_MUSIC, COL_CARD_LIGHT);
}

void drawMainGames() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("GAMES");
    
    drawPowerButton(LCD_WIDTH - 25, 25);
    bool charging = hasPMU ? pmu.isCharging() : false;
    drawBatteryIcon(LCD_WIDTH - 60, 8, batteryPercent, charging);
    
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 260, 0xFD20, 0xF800);
    
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 90);
    gfx->print("GAMES HUB");
    
    const char* gameName;
    const char* gameDesc;
    
    switch(spotlightGame) {
        case 0: gameName = "Yes or No"; gameDesc = "Quick decision maker"; break;
        case 1: gameName = "Clicker"; gameDesc = "Tap as fast as you can"; break;
        case 2: gameName = "Reaction Test"; gameDesc = "Test your reflexes"; break;
        default: gameName = "Mystery Game"; gameDesc = "Coming soon..."; break;
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
    
    // Clicker score
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 20, 240);
    gfx->printf("Clicker Score: %lu", clickerScore);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down to play");
}

void drawMainSystem() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN, 10);
    gfx->print("SYSTEM");
    
    drawPowerButton(LCD_WIDTH - 25, 25);
    bool charging = hasPMU ? pmu.isCharging() : false;
    drawBatteryIcon(LCD_WIDTH - 60, 8, batteryPercent, charging);
    
    drawGradientCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 320, 0xFFE0, 0x4208);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 85);
    gfx->print("BATTERY STATUS");
    
    // Battery percentage
    gfx->setTextSize(5);
    gfx->setTextColor(batteryPercent > 20 ? COL_SUCCESS : COL_DANGER);
    gfx->setCursor(MARGIN + 40, 120);
    gfx->printf("%d%%", batteryPercent);
    
    // Charging status
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 185);
    gfx->print(charging ? "Charging..." : "On Battery");
    
    // Battery time estimate
    calculateBatteryEstimate();
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 220);
    
    if (batteryEstimateMinutes > 0) {
        uint32_t hours = batteryEstimateMinutes / 60;
        uint32_t mins = batteryEstimateMinutes % 60;
        gfx->printf("Est. Time: %luh %lum", hours, mins);
        
        gfx->setTextSize(1);
        gfx->setTextColor(COL_TEXT_DIMMER);
        gfx->setCursor(MARGIN + 20, 240);
        gfx->print(screenOn ? "(screen on)" : "(screen off)");
    }
    
    // Voltage
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 265);
    gfx->printf("Voltage: %dmV", batteryVoltage);
    
    // System info
    gfx->setCursor(MARGIN + 20, 285);
    gfx->printf("Free RAM: %lu KB", freeRAM / 1024);
    
    // WiFi status
    gfx->setCursor(MARGIN + 20, 305);
    gfx->printf("WiFi: %s", wifiConnected ? "Connected" : "Disconnected");
    if (wifiConfigFromSD) {
        gfx->print(" (SD)");
    }
    
    // SD card status
    gfx->setCursor(MARGIN + 20, 325);
    gfx->printf("SD Card: %s", hasSD ? "OK" : "Not found");
    
    // Save interval info
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(MARGIN + 20, 350);
    gfx->print("Auto-save: every 2 hours");
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 60, LCD_HEIGHT - 25);
    gfx->print("Swipe down for reset");
}

// ═══════════════════════════════════════════════════════════════════════════
//  SUB-CARD SCREENS
// ═══════════════════════════════════════════════════════════════════════════

void drawSubSystemReset() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 20);
    gfx->print("< Factory Reset");
    
    drawCard(MARGIN, 80, LCD_WIDTH - 2*MARGIN, 150, COL_CARD);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_WARNING);
    gfx->setCursor(MARGIN + 20, 100);
    gfx->print("WARNING: This will clear all data");
    
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 125);
    gfx->print("- Steps, games, settings reset");
    gfx->setCursor(MARGIN + 20, 145);
    gfx->print("- SD card data PRESERVED");
    gfx->setCursor(MARGIN + 20, 165);
    gfx->print("- Firmware NOT affected");
    gfx->setCursor(MARGIN + 20, 185);
    gfx->print("- Device will restart");
    
    // Reset button
    drawButton(MARGIN + 50, 260, LCD_WIDTH - 2*MARGIN - 100, 60, "RESET ALL", COL_DANGER);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to cancel");
}

void drawSubClockWorld() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 20);
    gfx->print("< World Clock");
    
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
    
    const char* days[] = {"M", "T", "W", "T", "F", "S", "S"};
    int16_t barW = 35;
    int16_t barSpacing = 10;
    int16_t chartY = 320;
    int16_t chartH = 180;
    
    for (int i = 0; i < 7; i++) {
        int16_t barX = MARGIN + 20 + i * (barW + barSpacing);
        float ratio = (float)stepHistory[i] / 10000.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        int16_t barH = (int16_t)(chartH * ratio);
        
        uint16_t barColor = (i == currentDay) ? COL_STEPS : COL_CARD_LIGHT;
        gfx->fillRoundRect(barX, chartY - barH, barW, barH, 4, barColor);
        
        gfx->setTextSize(1);
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setCursor(barX + 12, chartY + 10);
        gfx->print(days[i]);
    }
    
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
    
    gfx->fillCircle(LCD_WIDTH / 2, 270, 70, COL_GAMES);
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(LCD_WIDTH / 2 - 40, 255);
    gfx->print("CLICK");
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

// ═══════════════════════════════════════════════════════════════════════════
//  GESTURE DETECTION & TOUCH HANDLING
// ═══════════════════════════════════════════════════════════════════════════

GestureType detectGesture() {
    if (gestureStartX < 0 || gestureEndX < 0) return GESTURE_NONE;
    
    int16_t dx = gestureEndX - gestureStartX;
    int16_t dy = gestureEndY - gestureStartY;
    
    // Check if it's a tap (small movement)
    if (abs(dx) < 30 && abs(dy) < 30) {
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

void handleTap() {
    // Handle button taps
    
    // Check reset button on reset screen
    if (currentMainCard == CARD_SYSTEM && currentSubCard == SUB_SYSTEM_RESET) {
        if (touchX > MARGIN + 50 && touchX < LCD_WIDTH - MARGIN - 50 &&
            touchY > 260 && touchY < 320) {
            factoryReset();
            return;
        }
    }
    
    // Clicker game
    if (currentMainCard == CARD_GAMES && currentSubCard == SUB_GAMES_CLICKER) {
        if (touchX > LCD_WIDTH/2 - 70 && touchX < LCD_WIDTH/2 + 70 &&
            touchY > 200 && touchY < 340) {
            clickerScore++;
            redrawNeeded = true;
            return;
        }
    }
    
    // Music play/pause
    if (currentMainCard == CARD_MUSIC && currentSubCard == SUB_NONE) {
        int16_t controlCX = LCD_WIDTH / 2;
        if (touchX > controlCX - 25 && touchX < controlCX + 25 &&
            touchY > 225 && touchY < 275) {
            musicPlaying = !musicPlaying;
            redrawNeeded = true;
            return;
        }
    }
}

void handleGesture(GestureType gesture) {
    if (gesture == GESTURE_NONE) return;
    
    if (gesture == GESTURE_TAP) {
        handleTap();
        return;
    }
    
    if (currentSubCard == SUB_NONE) {
        // Main card navigation
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
                switch (currentMainCard) {
                    case CARD_CLOCK: currentSubCard = SUB_CLOCK_WORLD; break;
                    case CARD_STEPS: currentSubCard = SUB_STEPS_GRAPH; break;
                    case CARD_GAMES: currentSubCard = SUB_GAMES_CLICKER; break;
                    case CARD_SYSTEM: currentSubCard = SUB_SYSTEM_RESET; break;
                    default: break;
                }
                redrawNeeded = true;
                break;
            default: break;
        }
    } else {
        // Sub-card navigation
        if (gesture == GESTURE_UP) {
            currentSubCard = SUB_NONE;
            subCardIndex = 0;
            redrawNeeded = true;
        }
    }
}

void handleTouch() {
    bool t = touch.touched();
    
    if (t) {
        touch.getPoint(&touchX, &touchY);
        lastActivityMs = millis();
        
        // Wake screen on touch
        if (!screenOn) {
            screenOnFunc();
            return;
        }
    }
    
    if (t && !lastTouchState) {
        gestureStartX = touchX;
        gestureStartY = touchY;
        gestureInProgress = true;
        gestureStartTime = millis();
    } else if (!t && lastTouchState && gestureInProgress) {
        gestureEndX = touchX;
        gestureEndY = touchY;
        
        if (millis() - gestureStartTime < SWIPE_TIMEOUT) {
            GestureType gesture = detectGesture();
            handleGesture(gesture);
        }
        
        gestureInProgress = false;
        gestureStartX = gestureStartY = gestureEndX = gestureEndY = -1;
    }
    
    // Handle power button area
    if (t && screenOn) {
        handlePowerButton();
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
            uint8_t day, weekday, month, year;
            rtc.getDate(&day, &weekday, &month, &year);
            currentDay = weekday;
        } else {
            clockSecond++;
            if (clockSecond >= 60) { clockSecond = 0; clockMinute++; }
            if (clockMinute >= 60) { clockMinute = 0; clockHour++; }
            if (clockHour >= 24) clockHour = 0;
        }
        if (screenOn && currentMainCard == CARD_CLOCK && currentSubCard == SUB_NONE) {
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
            stepHistory[currentDay] = stepCount;
            lst = millis();
            if (screenOn && currentMainCard == CARD_STEPS) redrawNeeded = true;
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
        freeRAM = ESP.getFreeHeap();
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
        if (screenOn && currentMainCard == CARD_MUSIC) redrawNeeded = true;
    }
}

void serviceGames() {
    if (millis() - lastGameRotate >= 300000) {
        lastGameRotate = millis();
        spotlightGame = (spotlightGame + 1) % 3;
        if (screenOn && currentMainCard == CARD_GAMES && currentSubCard == SUB_NONE) {
            redrawNeeded = true;
        }
    }
}

void serviceWeather() {
    // Update weather every 30 minutes
    if (wifiConnected && millis() - lastWeatherUpdate >= 1800000) {
        fetchWeather();
    }
}

void serviceSave() {
    // Save every 2 hours (SAVE_INTERVAL_MS)
    if (millis() - lastSaveTime >= SAVE_INTERVAL_MS) {
        saveUserData();
    }
}

void serviceScreenTimeout() {
    // Auto screen off after SCREEN_OFF_TIMEOUT_MS of inactivity
    if (screenOn && millis() - lastActivityMs >= SCREEN_OFF_TIMEOUT_MS) {
        screenOff();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  RENDERING
// ═══════════════════════════════════════════════════════════════════════════

void renderScreen() {
    if (!redrawNeeded || !screenOn) return;
    
    if (currentSubCard == SUB_NONE) {
        switch (currentMainCard) {
            case CARD_CLOCK:   drawMainClock();   break;
            case CARD_STEPS:   drawMainSteps();   break;
            case CARD_MUSIC:   drawMainMusic();   break;
            case CARD_GAMES:   drawMainGames();   break;
            case CARD_WEATHER: drawMainWeather(); break;
            case CARD_SYSTEM:  drawMainSystem();  break;
        }
    } else {
        switch (currentSubCard) {
            case SUB_CLOCK_WORLD:    drawSubClockWorld();    break;
            case SUB_STEPS_GRAPH:    drawSubStepsGraph();    break;
            case SUB_GAMES_CLICKER:  drawSubGamesClicker();  break;
            case SUB_SYSTEM_RESET:   drawSubSystemReset();   break;
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
    Serial.println("  S3 MiniOS v3.0 - Enhanced Edition");
    Serial.println("═══════════════════════════════════════════\n");
    
    // I2C init
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    Serial.println("[OK] I2C Bus");
    
    // I/O Expander
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
    
    // Display
    gfx->begin();
    gfx->fillScreen(COL_BG);
    for (int i = 0; i <= 255; i += 5) { gfx->setBrightness(i); delay(2); }
    Serial.println("[OK] AMOLED Display");
    
    // Splash screen
    gfx->setTextSize(4);
    gfx->setTextColor(COL_CLOCK);
    gfx->setCursor(50, 140);
    gfx->print("S3 MiniOS");
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(120, 200);
    gfx->print("v3.0");
    gfx->setTextSize(1);
    gfx->setCursor(80, 240);
    gfx->print("Enhanced Edition");
    gfx->setCursor(100, 280);
    gfx->print("Loading...");
    
    // SD Card
    hasSD = initSDCard();
    if (hasSD) {
        gfx->setCursor(100, 300);
        gfx->print("SD Card: OK");
    }
    
    // Load WiFi from SD
    loadWiFiFromSD();
    
    // Touch
    if (touch.begin()) Serial.println("[OK] Touch Controller");
    
    // RTC
    hasRTC = rtc.begin(); 
    if (hasRTC) { 
        Serial.println("[OK] RTC"); 
        rtc.getTime(&clockHour, &clockMinute, &clockSecond);
    }
    
    // PMU
    hasPMU = pmu.begin(); 
    if (hasPMU) { 
        Serial.println("[OK] PMU"); 
        batteryPercent = pmu.getBattPercent(); 
        batteryVoltage = pmu.getBattVoltage(); 
    }
    
    // IMU
    hasIMU = imu.begin(); 
    if (hasIMU) Serial.println("[OK] IMU");
    
    // Load saved data
    loadUserData();
    
    // Connect WiFi
    if (strlen(wifiSSID) > 0) {
        gfx->setCursor(100, 320);
        gfx->print("Connecting WiFi...");
        connectWiFi();
        if (wifiConnected) {
            fetchWeather();
        }
    }
    
    snprintf(weatherLocation, 63, "%s, %s", weatherCity, weatherCountry);
    
    delay(1000);
    
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("  Features:");
    Serial.println("  - WiFi from SD: /wifi/config.txt");
    Serial.println("  - Auto-save: every 2 hours");
    Serial.println("  - Tap top-right: screen ON/OFF");
    Serial.println("  - Long press: shutdown");
    Serial.println("  - Battery time estimate");
    Serial.println("  - Factory reset in System menu");
    Serial.println("═══════════════════════════════════════════\n");
    
    lastActivityMs = millis();
    lastSaveTime = millis();
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
    serviceWeather();
    serviceSave();
    serviceScreenTimeout();
    handleTouch();
    renderScreen();
    
    delay(10);
}
