/**
 * ═══════════════════════════════════════════════════════════════════════════
 *  S3 MiniOS v3.1 - ESP32-S3-Touch-AMOLED-1.8 Firmware
 *  FULL BATTERY INTELLIGENCE EDITION
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 *  NEW IN v3.1:
 *  - Advanced sleep mode tracking
 *  - Usage pattern learning (ML-style)
 *  - Battery Stats sub-card with graphs
 *  - Mini battery estimate on all cards
 *  - Low battery warning popup
 *  - Battery saver mode
 *  - Charging animation
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

#define SAVE_INTERVAL_MS 7200000UL  // 2 hours
#define WIFI_CONFIG_PATH "/wifi/config.txt"
#define BUTTON_LONG_PRESS_MS 3000
#define SCREEN_OFF_TIMEOUT_MS 30000
#define SCREEN_OFF_TIMEOUT_SAVER_MS 10000  // Battery saver mode

// Battery specs
#define BATTERY_CAPACITY_MAH 500
#define SCREEN_ON_CURRENT_MA 80
#define SCREEN_OFF_CURRENT_MA 15
#define SAVER_MODE_CURRENT_MA 40

// Low battery threshold
#define LOW_BATTERY_WARNING 20
#define CRITICAL_BATTERY_WARNING 10

// Usage tracking
#define USAGE_HISTORY_SIZE 24  // 24 hours of hourly data
#define CARD_USAGE_SLOTS 6     // Number of main cards

// ═══════════════════════════════════════════════════════════════════════════
//  MULTI-WIFI CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define MAX_WIFI_NETWORKS 5

struct WiFiNetwork {
    char ssid[64];
    char password[64];
    bool valid;
};

WiFiNetwork wifiNetworks[MAX_WIFI_NETWORKS];
int numWifiNetworks = 0;
int connectedNetworkIndex = -1;

char weatherCity[64] = "Perth";
char weatherCountry[8] = "AU";
long gmtOffsetSec = 8 * 3600;
bool wifiConfigFromSD = false;
bool wifiConnected = false;

const char* OPENWEATHER_API = "3795c13a0d3f7e17799d638edda60e3c";

// ═══════════════════════════════════════════════════════════════════════════
//  THEME COLORS
// ═══════════════════════════════════════════════════════════════════════════

#define COL_BG          0x0000
#define COL_CARD        0x18E3
#define COL_CARD_LIGHT  0x2945
#define COL_TEXT        0xFFFF
#define COL_TEXT_DIM    0x7BEF
#define COL_TEXT_DIMMER 0x4208

#define COL_CLOCK       0x07FF
#define COL_STEPS       0x5E5F
#define COL_MUSIC       0xF81F
#define COL_GAMES       0xFD20
#define COL_WEATHER     0x07E0
#define COL_SYSTEM      0xFFE0
#define COL_SUCCESS     0x07E0
#define COL_WARNING     0xFD20
#define COL_DANGER      0xF800
#define COL_CHARGING    0x07FF

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
    SUB_CLOCK_WORLD = 1,
    SUB_STEPS_GRAPH = 10,
    SUB_GAMES_CLICKER = 30,
    SUB_SYSTEM_RESET = 52,
    SUB_SYSTEM_BATTERY_STATS = 53,
    SUB_SYSTEM_USAGE = 54
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
//  BATTERY INTELLIGENCE SYSTEM
// ═══════════════════════════════════════════════════════════════════════════

struct BatteryStats {
    // Current session tracking
    uint32_t screenOnTimeMs;
    uint32_t screenOffTimeMs;
    uint32_t sessionStartMs;
    
    // Historical data (24 hours, hourly buckets)
    uint16_t hourlyScreenOnMins[USAGE_HISTORY_SIZE];
    uint16_t hourlyScreenOffMins[USAGE_HISTORY_SIZE];
    uint16_t hourlySteps[USAGE_HISTORY_SIZE];
    uint8_t currentHourIndex;
    
    // Card usage tracking
    uint32_t cardUsageTime[CARD_USAGE_SLOTS];  // ms spent on each card
    
    // Battery drain tracking
    uint8_t batteryAtHourStart;
    float avgDrainPerHour;
    float weightedDrainRate;  // More weight on recent data
    
    // Learning data (7 days)
    float dailyAvgScreenOnHours[7];
    float dailyAvgDrainRate[7];
    uint8_t currentDayIndex;
    
    // Computed estimates
    uint32_t simpleEstimateMins;
    uint32_t weightedEstimateMins;
    uint32_t learnedEstimateMins;
    uint32_t combinedEstimateMins;
};

BatteryStats batteryStats = {0};

// Battery saver mode
bool batterySaverMode = false;
bool batterySaverAutoEnabled = false;

// Low battery warning
bool lowBatteryWarningShown = false;
bool criticalBatteryWarningShown = false;
unsigned long lowBatteryPopupTime = 0;
bool showingLowBatteryPopup = false;

// Charging animation
uint8_t chargingAnimFrame = 0;
unsigned long lastChargingAnimMs = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ═══════════════════════════════════════════════════════════════════════════

// Clock
uint8_t clockHour = 10, clockMinute = 30, clockSecond = 0;
uint8_t lastSecond = 255;
const char* daysOfWeek[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
uint8_t currentDay = 3;

// Steps
uint32_t stepCount = 0;
uint32_t stepGoal = 10000;
uint32_t stepHistory[7] = {0};

// Music
bool musicPlaying = false;
uint16_t musicDuration = 245;
uint16_t musicCurrent = 86;
const char* musicTitle = "Night Drive";
const char* musicArtist = "Synthwave FM";

// Games
uint8_t spotlightGame = 0;
unsigned long lastGameRotate = 0;
uint32_t clickerScore = 0;

// Weather
int8_t weatherTemp = 22;
char weatherCondition[32] = "Sunny";
uint8_t weatherHumidity = 65;
uint16_t weatherWind = 12;
char weatherLocation[64] = "Perth, AU";

// System
uint32_t freeRAM = 234567;

// Battery
uint16_t batteryVoltage = 4100;
uint8_t batteryPercent = 85;
bool isCharging = false;

// Screen state
bool screenOn = true;
unsigned long lastActivityMs = 0;
unsigned long screenOnStartMs = 0;
unsigned long screenOffStartMs = 0;

// Button
unsigned long buttonPressStart = 0;
bool buttonPressed = false;

// Timing
unsigned long lastClockUpdate = 0;
unsigned long lastStepUpdate = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastMusicUpdate = 0;
unsigned long lastSaveTime = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastUsageUpdate = 0;
unsigned long lastHourlyUpdate = 0;

// Hardware flags
bool hasIMU = false, hasRTC = false, hasPMU = false, hasSD = false;

// Preferences
Preferences prefs;

// Touch
int16_t touchX = -1, touchY = -1;
bool lastTouchState = false;

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
    void getDate(uint8_t *day, uint8_t *weekday, uint8_t *month, uint8_t *year) {
        Wire.beginTransmission(PCF85063_ADDR); Wire.write(0x07); Wire.endTransmission();
        Wire.requestFrom(PCF85063_ADDR,(uint8_t)4);
        *day = bcd(Wire.read()&0x3F); *weekday = Wire.read()&0x07;
        *month = bcd(Wire.read()&0x1F); *year = bcd(Wire.read());
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
    void powerOff() { writeReg(0x10, readReg(0x10) | 0x01); }
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
        writeReg(0x03, 0x02); writeReg(0x08, 0x01);
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
//  SD CARD & WIFI
// ═══════════════════════════════════════════════════════════════════════════

bool initSDCard() {
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    if (!SD_MMC.begin("/sdcard", true, true)) return false;
    Serial.println("[OK] SD Card");
    return true;
}

void createWiFiTemplate() {
    if (!SD_MMC.exists("/wifi")) SD_MMC.mkdir("/wifi");
    File file = SD_MMC.open(WIFI_CONFIG_PATH, "w");
    if (file) {
        file.println("# ═══════════════════════════════════════════════════════");
        file.println("# S3 MiniOS WiFi Configuration - MULTI-NETWORK SUPPORT");
        file.println("# ═══════════════════════════════════════════════════════");
        file.println("#");
        file.println("# Add up to 5 WiFi networks. Watch will try each in order");
        file.println("# until one connects successfully.");
        file.println("#");
        file.println("# FORMAT: Use WIFI1, WIFI2, WIFI3, etc. for multiple networks");
        file.println("#");
        file.println("# ─────────────────────────────────────────────────────────");
        file.println("# NETWORK 1 (Primary - Home)");
        file.println("# ─────────────────────────────────────────────────────────");
        file.println("WIFI1_SSID=YourHomeNetwork");
        file.println("WIFI1_PASS=YourHomePassword");
        file.println("#");
        file.println("# ─────────────────────────────────────────────────────────");
        file.println("# NETWORK 2 (Secondary - Work)");
        file.println("# ─────────────────────────────────────────────────────────");
        file.println("WIFI2_SSID=YourWorkNetwork");
        file.println("WIFI2_PASS=YourWorkPassword");
        file.println("#");
        file.println("# ─────────────────────────────────────────────────────────");
        file.println("# NETWORK 3 (Optional - Mobile Hotspot)");
        file.println("# ─────────────────────────────────────────────────────────");
        file.println("#WIFI3_SSID=MyPhoneHotspot");
        file.println("#WIFI3_PASS=HotspotPassword");
        file.println("#");
        file.println("# ─────────────────────────────────────────────────────────");
        file.println("# GLOBAL SETTINGS");
        file.println("# ─────────────────────────────────────────────────────────");
        file.println("# Weather location (leave blank to auto-detect from IP)");
        file.println("CITY=Perth");
        file.println("COUNTRY=AU");
        file.println("#");
        file.println("# Timezone offset from GMT in hours");
        file.println("GMT_OFFSET=8");
        file.close();
        Serial.println("[OK] Created multi-WiFi template");
    }
}

bool loadWiFiFromSD() {
    if (!hasSD) return false;
    File file = SD_MMC.open(WIFI_CONFIG_PATH, "r");
    if (!file) { createWiFiTemplate(); return false; }
    
    Serial.println("[INFO] Loading WiFi networks from SD...");
    
    // Reset network list
    numWifiNetworks = 0;
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
        wifiNetworks[i].ssid[0] = '\0';
        wifiNetworks[i].password[0] = '\0';
        wifiNetworks[i].valid = false;
    }
    
    // Temporary storage for parsing
    char tempSSID[5][64] = {""};
    char tempPass[5][64] = {""};
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.startsWith("#") || line.length() == 0) continue;
        int eqPos = line.indexOf('=');
        if (eqPos < 0) continue;
        String key = line.substring(0, eqPos);
        String value = line.substring(eqPos + 1);
        key.trim(); value.trim();
        
        // Parse WIFI1_SSID through WIFI5_SSID
        for (int i = 1; i <= MAX_WIFI_NETWORKS; i++) {
            char ssidKey[16], passKey[16];
            snprintf(ssidKey, sizeof(ssidKey), "WIFI%d_SSID", i);
            snprintf(passKey, sizeof(passKey), "WIFI%d_PASS", i);
            
            if (key == ssidKey) {
                strncpy(tempSSID[i-1], value.c_str(), 63);
                tempSSID[i-1][63] = '\0';
            }
            else if (key == passKey) {
                strncpy(tempPass[i-1], value.c_str(), 63);
                tempPass[i-1][63] = '\0';
            }
        }
        
        // Legacy single-network support (SSID= and PASSWORD=)
        if (key == "SSID" && strlen(tempSSID[0]) == 0) {
            strncpy(tempSSID[0], value.c_str(), 63);
        }
        else if (key == "PASSWORD" && strlen(tempPass[0]) == 0) {
            strncpy(tempPass[0], value.c_str(), 63);
        }
        
        // Global settings
        else if (key == "CITY") strncpy(weatherCity, value.c_str(), 63);
        else if (key == "COUNTRY") strncpy(weatherCountry, value.c_str(), 7);
        else if (key == "GMT_OFFSET") gmtOffsetSec = value.toInt() * 3600;
    }
    file.close();
    
    // Build valid network list
    for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
        if (strlen(tempSSID[i]) > 0) {
            strncpy(wifiNetworks[numWifiNetworks].ssid, tempSSID[i], 63);
            strncpy(wifiNetworks[numWifiNetworks].password, tempPass[i], 63);
            wifiNetworks[numWifiNetworks].valid = true;
            Serial.printf("  Network %d: %s\n", numWifiNetworks + 1, tempSSID[i]);
            numWifiNetworks++;
        }
    }
    
    wifiConfigFromSD = (numWifiNetworks > 0);
    Serial.printf("[INFO] Loaded %d WiFi networks\n", numWifiNetworks);
    return wifiConfigFromSD;
}

void connectWiFi() {
    if (numWifiNetworks == 0) {
        Serial.println("[WARN] No WiFi networks configured");
        return;
    }
    
    WiFi.mode(WIFI_STA);
    
    // Try each network in order
    for (int i = 0; i < numWifiNetworks; i++) {
        if (!wifiNetworks[i].valid) continue;
        
        Serial.printf("[INFO] Trying WiFi %d/%d: %s\n", i + 1, numWifiNetworks, wifiNetworks[i].ssid);
        
        WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 15) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            connectedNetworkIndex = i;
            Serial.printf("[OK] Connected to: %s\n", wifiNetworks[i].ssid);
            Serial.printf("[OK] IP: %s\n", WiFi.localIP().toString().c_str());
            return;
        } else {
            Serial.printf("[WARN] Failed to connect to: %s\n", wifiNetworks[i].ssid);
            WiFi.disconnect();
            delay(100);
        }
    }
    
    Serial.println("[WARN] Could not connect to any WiFi network");
    wifiConnected = false;
    connectedNetworkIndex = -1;
}

// Auto-reconnect to next available network if disconnected
void checkWiFiReconnect() {
    if (wifiConnected && WiFi.status() != WL_CONNECTED) {
        Serial.println("[WARN] WiFi disconnected, attempting reconnect...");
        wifiConnected = false;
        connectedNetworkIndex = -1;
        connectWiFi();
    }
}

void getLocationFromIP() {
    if (!wifiConnected) return;
    HTTPClient http;
    http.begin("http://ip-api.com/json/?fields=city,countryCode");
    http.setTimeout(5000);
    if (http.GET() == HTTP_CODE_OK) {
        DynamicJsonDocument doc(256);
        if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
            if (doc["city"]) strncpy(weatherCity, doc["city"], 63);
            if (doc["countryCode"]) strncpy(weatherCountry, doc["countryCode"], 7);
        }
    }
    http.end();
}

void fetchWeather() {
    if (!wifiConnected) return;
    char url[256];
    snprintf(url, sizeof(url), "http://api.openweathermap.org/data/2.5/weather?q=%s,%s&appid=%s&units=metric",
        weatherCity, weatherCountry, OPENWEATHER_API);
    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    if (http.GET() == HTTP_CODE_OK) {
        DynamicJsonDocument doc(1024);
        if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
            weatherTemp = (int8_t)doc["main"]["temp"].as<float>();
            weatherHumidity = doc["main"]["humidity"].as<uint8_t>();
            weatherWind = (uint16_t)doc["wind"]["speed"].as<float>();
            if (doc["weather"][0]["main"]) strncpy(weatherCondition, doc["weather"][0]["main"], 31);
        }
    }
    http.end();
    lastWeatherUpdate = millis();
}

// ═══════════════════════════════════════════════════════════════════════════
//  BATTERY INTELLIGENCE FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void updateUsageTracking() {
    unsigned long now = millis();
    
    // Update screen time counters
    if (screenOn) {
        batteryStats.screenOnTimeMs += (now - lastUsageUpdate);
        batteryStats.cardUsageTime[currentMainCard] += (now - lastUsageUpdate);
    } else {
        batteryStats.screenOffTimeMs += (now - lastUsageUpdate);
    }
    
    lastUsageUpdate = now;
}

void updateHourlyStats() {
    // Called every hour or when hour changes
    uint8_t hourIndex = clockHour % USAGE_HISTORY_SIZE;
    
    // Store current hour data
    batteryStats.hourlyScreenOnMins[hourIndex] = batteryStats.screenOnTimeMs / 60000;
    batteryStats.hourlyScreenOffMins[hourIndex] = batteryStats.screenOffTimeMs / 60000;
    batteryStats.hourlySteps[hourIndex] = stepCount;
    
    // Calculate drain rate for this hour
    uint8_t currentBattery = batteryPercent;
    if (batteryStats.batteryAtHourStart > currentBattery) {
        float drainThisHour = batteryStats.batteryAtHourStart - currentBattery;
        
        // Update average drain rate
        batteryStats.avgDrainPerHour = (batteryStats.avgDrainPerHour * 0.8f) + (drainThisHour * 0.2f);
        
        // Weighted drain rate (more recent = more weight)
        batteryStats.weightedDrainRate = (batteryStats.weightedDrainRate * 0.6f) + (drainThisHour * 0.4f);
    }
    
    batteryStats.batteryAtHourStart = currentBattery;
    batteryStats.currentHourIndex = hourIndex;
    
    // Reset session counters for new hour
    batteryStats.screenOnTimeMs = 0;
    batteryStats.screenOffTimeMs = 0;
    
    lastHourlyUpdate = millis();
}

void updateDailyStats() {
    // Called at midnight or day change
    uint8_t dayIndex = currentDay % 7;
    
    // Calculate total screen on time for the day
    float totalOnHours = 0;
    for (int i = 0; i < USAGE_HISTORY_SIZE; i++) {
        totalOnHours += batteryStats.hourlyScreenOnMins[i] / 60.0f;
    }
    
    batteryStats.dailyAvgScreenOnHours[dayIndex] = totalOnHours;
    batteryStats.dailyAvgDrainRate[dayIndex] = batteryStats.avgDrainPerHour;
    batteryStats.currentDayIndex = dayIndex;
}

void calculateBatteryEstimates() {
    float remainingCapacity = (BATTERY_CAPACITY_MAH * batteryPercent) / 100.0f;
    
    // 1. Simple estimate (based on current mode)
    float currentDraw = batterySaverMode ? SAVER_MODE_CURRENT_MA : 
                        (screenOn ? SCREEN_ON_CURRENT_MA : SCREEN_OFF_CURRENT_MA);
    batteryStats.simpleEstimateMins = (uint32_t)((remainingCapacity / currentDraw) * 60.0f);
    
    // 2. Weighted estimate (based on recent usage pattern)
    if (batteryStats.weightedDrainRate > 0.1f) {
        batteryStats.weightedEstimateMins = (uint32_t)((batteryPercent / batteryStats.weightedDrainRate) * 60.0f);
    } else {
        batteryStats.weightedEstimateMins = batteryStats.simpleEstimateMins;
    }
    
    // 3. Learned estimate (based on historical patterns)
    float avgDailyOnHours = 0;
    float avgDailyDrain = 0;
    int validDays = 0;
    
    for (int i = 0; i < 7; i++) {
        if (batteryStats.dailyAvgScreenOnHours[i] > 0) {
            avgDailyOnHours += batteryStats.dailyAvgScreenOnHours[i];
            avgDailyDrain += batteryStats.dailyAvgDrainRate[i];
            validDays++;
        }
    }
    
    if (validDays > 0 && avgDailyDrain > 0) {
        avgDailyOnHours /= validDays;
        avgDailyDrain /= validDays;
        batteryStats.learnedEstimateMins = (uint32_t)((batteryPercent / avgDailyDrain) * 60.0f);
    } else {
        batteryStats.learnedEstimateMins = batteryStats.simpleEstimateMins;
    }
    
    // 4. Combined estimate (weighted average of all three)
    batteryStats.combinedEstimateMins = (
        batteryStats.simpleEstimateMins * 30 +
        batteryStats.weightedEstimateMins * 40 +
        batteryStats.learnedEstimateMins * 30
    ) / 100;
}

void checkLowBattery() {
    if (isCharging) {
        lowBatteryWarningShown = false;
        criticalBatteryWarningShown = false;
        showingLowBatteryPopup = false;
        return;
    }
    
    if (batteryPercent <= CRITICAL_BATTERY_WARNING && !criticalBatteryWarningShown) {
        criticalBatteryWarningShown = true;
        showingLowBatteryPopup = true;
        lowBatteryPopupTime = millis();
        
        // Auto-enable battery saver
        if (!batterySaverMode) {
            batterySaverMode = true;
            batterySaverAutoEnabled = true;
        }
    }
    else if (batteryPercent <= LOW_BATTERY_WARNING && !lowBatteryWarningShown) {
        lowBatteryWarningShown = true;
        showingLowBatteryPopup = true;
        lowBatteryPopupTime = millis();
    }
    
    // Auto-dismiss popup after 5 seconds
    if (showingLowBatteryPopup && millis() - lowBatteryPopupTime > 5000) {
        showingLowBatteryPopup = false;
        redrawNeeded = true;
    }
}

void toggleBatterySaver() {
    batterySaverMode = !batterySaverMode;
    batterySaverAutoEnabled = false;
    
    if (batterySaverMode) {
        gfx->setBrightness(100);  // Dim screen
    } else {
        gfx->setBrightness(200);  // Normal brightness
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  DATA PERSISTENCE
// ═══════════════════════════════════════════════════════════════════════════

void saveUserData() {
    prefs.begin("minios", false);
    prefs.putUInt("steps", stepCount);
    prefs.putUInt("stepGoal", stepGoal);
    prefs.putUInt("clicker", clickerScore);
    prefs.putBool("saver", batterySaverMode);
    
    // Save battery stats
    prefs.putFloat("avgDrain", batteryStats.avgDrainPerHour);
    prefs.putFloat("wDrain", batteryStats.weightedDrainRate);
    
    // Save daily data
    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "dOn%d", i);
        prefs.putFloat(key, batteryStats.dailyAvgScreenOnHours[i]);
        snprintf(key, sizeof(key), "dDr%d", i);
        prefs.putFloat(key, batteryStats.dailyAvgDrainRate[i]);
        snprintf(key, sizeof(key), "hist%d", i);
        prefs.putUInt(key, stepHistory[i]);
    }
    
    // Save card usage
    for (int i = 0; i < CARD_USAGE_SLOTS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "card%d", i);
        prefs.putUInt(key, batteryStats.cardUsageTime[i]);
    }
    
    prefs.end();
    lastSaveTime = millis();
    Serial.println("[OK] Data saved");
}

void loadUserData() {
    prefs.begin("minios", true);
    stepCount = prefs.getUInt("steps", 0);
    stepGoal = prefs.getUInt("stepGoal", 10000);
    clickerScore = prefs.getUInt("clicker", 0);
    batterySaverMode = prefs.getBool("saver", false);
    
    batteryStats.avgDrainPerHour = prefs.getFloat("avgDrain", 5.0f);
    batteryStats.weightedDrainRate = prefs.getFloat("wDrain", 5.0f);
    
    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "dOn%d", i);
        batteryStats.dailyAvgScreenOnHours[i] = prefs.getFloat(key, 0);
        snprintf(key, sizeof(key), "dDr%d", i);
        batteryStats.dailyAvgDrainRate[i] = prefs.getFloat(key, 0);
        snprintf(key, sizeof(key), "hist%d", i);
        stepHistory[i] = prefs.getUInt(key, 0);
    }
    
    for (int i = 0; i < CARD_USAGE_SLOTS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "card%d", i);
        batteryStats.cardUsageTime[i] = prefs.getUInt(key, 0);
    }
    
    prefs.end();
    Serial.println("[OK] Data loaded");
}

void factoryReset() {
    Serial.println("[WARN] FACTORY RESET");
    prefs.begin("minios", false);
    prefs.clear();
    prefs.end();
    delay(500);
    ESP.restart();
}

// ═══════════════════════════════════════════════════════════════════════════
//  SCREEN CONTROL
// ═══════════════════════════════════════════════════════════════════════════

void screenOff() {
    if (!screenOn) return;
    
    // Track screen on time
    batteryStats.screenOnTimeMs += (millis() - screenOnStartMs);
    screenOffStartMs = millis();
    
    screenOn = false;
    gfx->setBrightness(0);
}

void screenOnFunc() {
    if (screenOn) return;
    
    // Track screen off time
    batteryStats.screenOffTimeMs += (millis() - screenOffStartMs);
    screenOnStartMs = millis();
    
    screenOn = true;
    lastActivityMs = millis();
    gfx->setBrightness(batterySaverMode ? 100 : 200);
    redrawNeeded = true;
}

void shutdownDevice() {
    saveUserData();
    gfx->fillScreen(COL_BG);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(100, LCD_HEIGHT/2);
    gfx->print("Shutting down...");
    delay(1000);
    gfx->setBrightness(0);
    if (hasPMU) pmu.powerOff();
    esp_deep_sleep_start();
}

// ═══════════════════════════════════════════════════════════════════════════
//  UI DRAWING HELPERS
// ═══════════════════════════════════════════════════════════════════════════

void drawGradientCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c1, uint16_t c2) {
    for (int16_t i = 0; i < h; i++) {
        float t = (float)i / h;
        uint8_t r = ((c1 >> 11) & 0x1F) + (((c2 >> 11) & 0x1F) - ((c1 >> 11) & 0x1F)) * t;
        uint8_t g = ((c1 >> 5) & 0x3F) + (((c2 >> 5) & 0x3F) - ((c1 >> 5) & 0x3F)) * t;
        uint8_t b = (c1 & 0x1F) + ((c2 & 0x1F) - (c1 & 0x1F)) * t;
        gfx->drawFastHLine(x, y + i, w, ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F));
    }
}

void drawCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    gfx->fillRoundRect(x, y, w, h, CARD_RADIUS, color);
}

void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint16_t color) {
    gfx->fillRoundRect(x, y, w, h, BTN_RADIUS, color);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(x + (w - strlen(label) * 12) / 2, y + (h - 16) / 2);
    gfx->print(label);
}

void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, float progress, uint16_t fg, uint16_t bg) {
    gfx->fillRoundRect(x, y, w, h, h/2, bg);
    int16_t fw = (int16_t)(w * progress);
    if (fw > 0) gfx->fillRoundRect(x, y, fw, h, h/2, fg);
}

// ═══════════════════════════════════════════════════════════════════════════
//  MINI STATUS BAR (shown on all cards)
// ═══════════════════════════════════════════════════════════════════════════

void drawStatusBar() {
    // Background bar
    gfx->fillRect(0, 0, LCD_WIDTH, 35, COL_BG);
    
    // WiFi indicator
    gfx->setTextSize(1);
    gfx->setTextColor(wifiConnected ? COL_SUCCESS : COL_TEXT_DIMMER);
    gfx->setCursor(MARGIN, 10);
    gfx->print("W");
    
    // Battery saver indicator
    if (batterySaverMode) {
        gfx->setTextColor(COL_WARNING);
        gfx->setCursor(MARGIN + 15, 10);
        gfx->print("S");
    }
    
    // Mini battery estimate (center)
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(LCD_WIDTH/2 - 30, 10);
    
    if (isCharging) {
        gfx->setTextColor(COL_CHARGING);
        gfx->print("Charging");
    } else {
        uint32_t hrs = batteryStats.combinedEstimateMins / 60;
        uint32_t mins = batteryStats.combinedEstimateMins % 60;
        if (hrs > 0) {
            gfx->printf("~%luh %lum", hrs, mins);
        } else {
            gfx->printf("~%lum", mins);
        }
    }
    
    // Battery percentage with color
    gfx->setTextColor(batteryPercent > 20 ? COL_SUCCESS : COL_DANGER);
    gfx->setCursor(LCD_WIDTH - 45, 10);
    gfx->printf("%d%%", batteryPercent);
    
    // Charging animation or battery icon
    drawBatteryIconMini(LCD_WIDTH - 25, 8);
}

void drawBatteryIconMini(int16_t x, int16_t y) {
    uint16_t col = batteryPercent > 20 ? COL_SUCCESS : COL_DANGER;
    
    if (isCharging) {
        // Animated charging icon
        col = COL_CHARGING;
        int fill = (chargingAnimFrame * 18) / 4;
        gfx->drawRect(x, y + 2, 18, 8, col);
        gfx->fillRect(x + 18, y + 4, 2, 4, col);
        gfx->fillRect(x + 1, y + 3, fill, 6, col);
    } else {
        gfx->drawRect(x, y + 2, 18, 8, COL_TEXT_DIM);
        gfx->fillRect(x + 18, y + 4, 2, 4, COL_TEXT_DIM);
        int fillW = (16 * batteryPercent) / 100;
        gfx->fillRect(x + 1, y + 3, fillW, 6, col);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOW BATTERY POPUP
// ═══════════════════════════════════════════════════════════════════════════

void drawLowBatteryPopup() {
    if (!showingLowBatteryPopup) return;
    
    // Darken background
    gfx->fillRect(30, 100, LCD_WIDTH - 60, 200, COL_BG);
    gfx->drawRoundRect(30, 100, LCD_WIDTH - 60, 200, 16, COL_DANGER);
    
    gfx->setTextColor(COL_DANGER);
    gfx->setTextSize(2);
    gfx->setCursor(80, 130);
    
    if (batteryPercent <= CRITICAL_BATTERY_WARNING) {
        gfx->print("CRITICAL!");
    } else {
        gfx->print("Low Battery");
    }
    
    gfx->setTextSize(5);
    gfx->setCursor(LCD_WIDTH/2 - 50, 170);
    gfx->printf("%d%%", batteryPercent);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(60, 240);
    
    if (batterySaverAutoEnabled) {
        gfx->print("Battery Saver enabled");
    } else {
        gfx->print("Enable Battery Saver?");
    }
    
    gfx->setCursor(100, 270);
    gfx->print("Tap to dismiss");
}

// ═══════════════════════════════════════════════════════════════════════════
//  CHARGING ANIMATION
// ═══════════════════════════════════════════════════════════════════════════

void updateChargingAnimation() {
    if (!isCharging) return;
    
    if (millis() - lastChargingAnimMs > 500) {
        chargingAnimFrame = (chargingAnimFrame + 1) % 5;
        lastChargingAnimMs = millis();
        if (screenOn) redrawNeeded = true;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN CARD SCREENS
// ═══════════════════════════════════════════════════════════════════════════

void drawMainClock() {
    gfx->fillScreen(COL_BG);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 45, LCD_WIDTH - 2*MARGIN, 270, 0x4A5F, 0x001F);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 65);
    gfx->print("S3 MiniOS v3.1");
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 105);
    gfx->print(daysOfWeek[currentDay]);
    
    gfx->setTextSize(7);
    gfx->setTextColor(COL_TEXT);
    char timeStr[10];
    sprintf(timeStr, "%02d:%02d", clockHour, clockMinute);
    gfx->setCursor(MARGIN + 20, 140);
    gfx->print(timeStr);
    
    gfx->setTextSize(4);
    gfx->setTextColor(COL_CLOCK);
    gfx->setCursor(MARGIN + 260, 165);
    gfx->printf("%02d", clockSecond);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 255);
    gfx->print(weatherLocation);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe to navigate");
    
    if (showingLowBatteryPopup) drawLowBatteryPopup();
}

void drawMainSteps() {
    gfx->fillScreen(COL_BG);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 45, LCD_WIDTH - 2*MARGIN, 270, 0x5A5F, 0x4011);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 65);
    gfx->print("Daily Steps");
    
    gfx->setTextSize(7);
    gfx->setCursor(MARGIN + 20, 120);
    gfx->printf("%lu", stepCount);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 200, 165);
    gfx->printf("/ %lu", stepGoal);
    
    float progress = (float)stepCount / (float)stepGoal;
    if (progress > 1.0f) progress = 1.0f;
    drawProgressBar(MARGIN + 15, 230, LCD_WIDTH - 2*MARGIN - 30, 14, progress, COL_STEPS, COL_CARD_LIGHT);
    
    // Step correlation with battery
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 260);
    gfx->printf("Today's activity: %lu steps", stepCount);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down for graph");
    
    if (showingLowBatteryPopup) drawLowBatteryPopup();
}

void drawMainMusic() {
    gfx->fillScreen(COL_BG);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 45, LCD_WIDTH - 2*MARGIN, 310, 0xF81F, 0x001F);
    
    gfx->fillRoundRect(MARGIN + 20, 75, 120, 120, 12, COL_CARD_LIGHT);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 50, 130);
    gfx->print("Album");
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN + 160, 95);
    gfx->print(musicTitle);
    
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 160, 125);
    gfx->print(musicArtist);
    
    int16_t ctrlY = 240;
    int16_t ctrlCX = LCD_WIDTH / 2;
    
    gfx->fillCircle(ctrlCX - 60, ctrlY, 18, COL_CARD_LIGHT);
    gfx->fillCircle(ctrlCX, ctrlY, 25, COL_TEXT);
    gfx->fillCircle(ctrlCX + 60, ctrlY, 18, COL_CARD_LIGHT);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(ctrlCX - 69, ctrlY - 8);
    gfx->print("|<");
    gfx->setCursor(ctrlCX + 51, ctrlY - 8);
    gfx->print(">|");
    
    gfx->setTextColor(COL_BG);
    gfx->setTextSize(3);
    gfx->setCursor(ctrlCX - 6, ctrlY - 12);
    gfx->print(musicPlaying ? "||" : ">");
    
    drawProgressBar(MARGIN + 20, 310, LCD_WIDTH - 2*MARGIN - 40, 8, 
        (float)musicCurrent / musicDuration, COL_MUSIC, COL_CARD_LIGHT);
    
    if (showingLowBatteryPopup) drawLowBatteryPopup();
}

void drawMainGames() {
    gfx->fillScreen(COL_BG);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 45, LCD_WIDTH - 2*MARGIN, 270, 0xFD20, 0xF800);
    
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 75);
    gfx->print("GAMES HUB");
    
    const char* games[] = {"Yes or No", "Clicker", "Reaction"};
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN + 20, 140);
    gfx->print(games[spotlightGame]);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 180);
    gfx->printf("Clicker Score: %lu", clickerScore);
    
    // Most used card indicator
    uint32_t maxUsage = 0;
    int mostUsed = 0;
    for (int i = 0; i < CARD_USAGE_SLOTS; i++) {
        if (batteryStats.cardUsageTime[i] > maxUsage) {
            maxUsage = batteryStats.cardUsageTime[i];
            mostUsed = i;
        }
    }
    
    const char* cardNames[] = {"Clock", "Steps", "Music", "Games", "Weather", "System"};
    gfx->setCursor(MARGIN + 20, 210);
    gfx->printf("Most used: %s", cardNames[mostUsed]);
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down to play");
    
    if (showingLowBatteryPopup) drawLowBatteryPopup();
}

void drawMainWeather() {
    gfx->fillScreen(COL_BG);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 45, LCD_WIDTH - 2*MARGIN, 270, 0x07E0, 0x001F);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 65);
    gfx->print(weatherLocation);
    
    gfx->setTextSize(8);
    gfx->setCursor(MARGIN + 40, 110);
    gfx->printf("%d", weatherTemp);
    gfx->setTextSize(4);
    gfx->setCursor(MARGIN + 150, 115);
    gfx->print("C");
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 210);
    gfx->print(weatherCondition);
    
    gfx->setTextSize(1);
    gfx->setCursor(MARGIN + 20, 250);
    gfx->printf("Humidity: %d%%  Wind: %d km/h", weatherHumidity, weatherWind);
    
    // Show connected network
    if (wifiConnected && connectedNetworkIndex >= 0) {
        gfx->setTextColor(COL_SUCCESS);
        gfx->setCursor(MARGIN + 20, 270);
        gfx->printf("WiFi: %s", wifiNetworks[connectedNetworkIndex].ssid);
    } else if (wifiConfigFromSD) {
        gfx->setTextColor(COL_WARNING);
        gfx->setCursor(MARGIN + 20, 270);
        gfx->printf("WiFi: Disconnected (%d networks)", numWifiNetworks);
    }
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 50, LCD_HEIGHT - 25);
    gfx->print("Swipe down for more");
    
    if (showingLowBatteryPopup) drawLowBatteryPopup();
}

void drawMainSystem() {
    gfx->fillScreen(COL_BG);
    drawStatusBar();
    
    drawGradientCard(MARGIN, 45, LCD_WIDTH - 2*MARGIN, 330, 0xFFE0, 0x4208);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 65);
    gfx->print("BATTERY STATUS");
    
    // Large battery percentage
    gfx->setTextSize(5);
    gfx->setTextColor(batteryPercent > 20 ? COL_SUCCESS : COL_DANGER);
    gfx->setCursor(MARGIN + 40, 100);
    gfx->printf("%d%%", batteryPercent);
    
    // Charging status with animation
    gfx->setTextSize(2);
    if (isCharging) {
        gfx->setTextColor(COL_CHARGING);
        gfx->setCursor(MARGIN + 20, 165);
        const char* dots[] = {"Charging", "Charging.", "Charging..", "Charging..."};
        gfx->print(dots[chargingAnimFrame % 4]);
    } else {
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setCursor(MARGIN + 20, 165);
        gfx->print("On Battery");
    }
    
    // Combined battery estimate
    calculateBatteryEstimates();
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 20, 200);
    
    uint32_t hrs = batteryStats.combinedEstimateMins / 60;
    uint32_t mins = batteryStats.combinedEstimateMins % 60;
    gfx->printf("Estimated: %luh %lum remaining", hrs, mins);
    
    // Battery saver toggle
    gfx->setCursor(MARGIN + 20, 225);
    gfx->setTextColor(batterySaverMode ? COL_WARNING : COL_TEXT_DIM);
    gfx->printf("Battery Saver: %s", batterySaverMode ? "ON" : "OFF");
    
    // Voltage
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 250);
    gfx->printf("Voltage: %dmV", batteryVoltage);
    
    // System info
    gfx->setCursor(MARGIN + 20, 270);
    gfx->printf("Free RAM: %lu KB", freeRAM / 1024);
    
    // WiFi status with network name
    gfx->setCursor(MARGIN + 20, 290);
    if (wifiConnected && connectedNetworkIndex >= 0) {
        gfx->setTextColor(COL_SUCCESS);
        gfx->printf("WiFi: %s", wifiNetworks[connectedNetworkIndex].ssid);
    } else if (numWifiNetworks > 0) {
        gfx->setTextColor(COL_WARNING);
        gfx->printf("WiFi: Disconnected (%d saved)", numWifiNetworks);
    } else {
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->print("WiFi: Not configured");
    }
    
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 310);
    gfx->printf("SD Card: %s", hasSD ? "OK" : "None");
    
    // Tap hint for battery saver
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(MARGIN + 20, 340);
    gfx->print("Tap 'Saver' to toggle | Swipe for stats");
    
    gfx->setCursor(LCD_WIDTH/2 - 60, LCD_HEIGHT - 25);
    gfx->print("Swipe down for details");
    
    if (showingLowBatteryPopup) drawLowBatteryPopup();
}

// ═══════════════════════════════════════════════════════════════════════════
//  SUB-CARD SCREENS
// ═══════════════════════════════════════════════════════════════════════════

void drawSubBatteryStats() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 15);
    gfx->print("< Battery Stats");
    
    drawCard(MARGIN, 50, LCD_WIDTH - 2*MARGIN, 180, COL_CARD);
    
    // Usage graph (24 hour screen time)
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 60);
    gfx->print("Screen Time (24h)");
    
    // Draw bar graph
    int16_t graphX = MARGIN + 15;
    int16_t graphY = 80;
    int16_t graphW = LCD_WIDTH - 2*MARGIN - 30;
    int16_t graphH = 60;
    int16_t barW = graphW / USAGE_HISTORY_SIZE;
    
    // Find max for scaling
    uint16_t maxMins = 1;
    for (int i = 0; i < USAGE_HISTORY_SIZE; i++) {
        if (batteryStats.hourlyScreenOnMins[i] > maxMins)
            maxMins = batteryStats.hourlyScreenOnMins[i];
    }
    
    for (int i = 0; i < USAGE_HISTORY_SIZE; i++) {
        int16_t barH = (batteryStats.hourlyScreenOnMins[i] * graphH) / maxMins;
        if (barH < 2) barH = 2;
        uint16_t col = (i == batteryStats.currentHourIndex) ? COL_CLOCK : COL_CARD_LIGHT;
        gfx->fillRect(graphX + i * barW, graphY + graphH - barH, barW - 1, barH, col);
    }
    
    // Estimate breakdown
    gfx->setCursor(MARGIN + 15, 155);
    gfx->setTextColor(COL_TEXT);
    gfx->print("Estimates:");
    
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 175);
    gfx->printf("Simple: %lum", batteryStats.simpleEstimateMins);
    gfx->setCursor(MARGIN + 120, 175);
    gfx->printf("Weighted: %lum", batteryStats.weightedEstimateMins);
    
    gfx->setCursor(MARGIN + 15, 195);
    gfx->printf("Learned: %lum", batteryStats.learnedEstimateMins);
    gfx->setCursor(MARGIN + 120, 195);
    gfx->setTextColor(COL_SUCCESS);
    gfx->printf("Combined: %lum", batteryStats.combinedEstimateMins);
    
    // Card usage breakdown
    drawCard(MARGIN, 240, LCD_WIDTH - 2*MARGIN, 120, COL_CARD);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 250);
    gfx->print("Card Usage (time spent)");
    
    const char* cardNames[] = {"Clock", "Steps", "Music", "Games", "Weather", "System"};
    uint16_t cardColors[] = {COL_CLOCK, COL_STEPS, COL_MUSIC, COL_GAMES, COL_WEATHER, COL_SYSTEM};
    
    // Find total for percentages
    uint32_t totalUsage = 0;
    for (int i = 0; i < CARD_USAGE_SLOTS; i++) {
        totalUsage += batteryStats.cardUsageTime[i];
    }
    if (totalUsage == 0) totalUsage = 1;
    
    for (int i = 0; i < CARD_USAGE_SLOTS; i++) {
        int16_t barY = 270 + i * 14;
        int pct = (batteryStats.cardUsageTime[i] * 100) / totalUsage;
        
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setCursor(MARGIN + 15, barY);
        gfx->printf("%s:", cardNames[i]);
        
        int16_t barLen = (pct * 150) / 100;
        gfx->fillRect(MARGIN + 80, barY, barLen, 10, cardColors[i]);
        
        gfx->setCursor(MARGIN + 240, barY);
        gfx->printf("%d%%", pct);
    }
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

void drawSubUsagePatterns() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 15);
    gfx->print("< Usage Patterns");
    
    drawCard(MARGIN, 50, LCD_WIDTH - 2*MARGIN, 150, COL_CARD);
    
    // Weekly screen time
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 15, 60);
    gfx->print("Weekly Screen Time (hours)");
    
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    
    int16_t barY = 80;
    for (int i = 0; i < 7; i++) {
        gfx->setCursor(MARGIN + 15, barY + i * 17);
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->print(days[i]);
        
        float hours = batteryStats.dailyAvgScreenOnHours[i];
        int16_t barLen = (int16_t)(hours * 20);  // 20px per hour
        if (barLen > 200) barLen = 200;
        
        uint16_t col = (i == batteryStats.currentDayIndex) ? COL_SUCCESS : COL_CARD_LIGHT;
        gfx->fillRect(MARGIN + 50, barY + i * 17, barLen, 12, col);
        
        gfx->setCursor(MARGIN + 260, barY + i * 17);
        gfx->printf("%.1fh", hours);
    }
    
    // Drain rates
    drawCard(MARGIN, 210, LCD_WIDTH - 2*MARGIN, 100, COL_CARD);
    
    gfx->setCursor(MARGIN + 15, 220);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->print("Battery Drain Analysis");
    
    gfx->setCursor(MARGIN + 15, 245);
    gfx->printf("Avg drain/hour: %.1f%%", batteryStats.avgDrainPerHour);
    
    gfx->setCursor(MARGIN + 15, 265);
    gfx->printf("Recent drain rate: %.1f%%", batteryStats.weightedDrainRate);
    
    gfx->setCursor(MARGIN + 15, 285);
    if (batteryStats.avgDrainPerHour > 0) {
        float dailyDrain = batteryStats.avgDrainPerHour * 24;
        gfx->printf("Est. full battery: %.0f hours", 100.0f / batteryStats.avgDrainPerHour);
    }
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

void drawSubSystemReset() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 15);
    gfx->print("< Factory Reset");
    
    drawCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 160, COL_CARD);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_WARNING);
    gfx->setCursor(MARGIN + 20, 80);
    gfx->print("WARNING: This will clear:");
    
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 20, 105);
    gfx->print("- Steps, games, all settings");
    gfx->setCursor(MARGIN + 20, 125);
    gfx->print("- Battery learning data");
    gfx->setCursor(MARGIN + 20, 145);
    gfx->print("- Usage patterns");
    
    gfx->setTextColor(COL_SUCCESS);
    gfx->setCursor(MARGIN + 20, 175);
    gfx->print("PRESERVED: SD card, firmware");
    
    drawButton(MARGIN + 50, 250, LCD_WIDTH - 2*MARGIN - 100, 60, "RESET ALL", COL_DANGER);
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to cancel");
}

void drawSubClockWorld() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 15);
    gfx->print("< World Clock");
    
    const char* cities[] = {"New York", "London", "Tokyo"};
    int offsets[] = {-5, 0, 9};  // GMT offsets
    
    for (int i = 0; i < 3; i++) {
        drawCard(MARGIN, 60 + i * 90, LCD_WIDTH - 2*MARGIN, 80, COL_CARD);
        
        gfx->setTextSize(1);
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setCursor(MARGIN + 15, 70 + i * 90);
        gfx->print(cities[i]);
        
        int h = (clockHour + offsets[i] + 24) % 24;
        gfx->setTextSize(3);
        gfx->setTextColor(COL_TEXT);
        gfx->setCursor(MARGIN + 15, 95 + i * 90);
        gfx->printf("%02d:%02d", h, clockMinute);
    }
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

void drawSubStepsGraph() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 15);
    gfx->print("< Weekly Steps");
    
    drawCard(MARGIN, 50, LCD_WIDTH - 2*MARGIN, 300, COL_CARD);
    
    const char* days[] = {"M", "T", "W", "T", "F", "S", "S"};
    int16_t barW = 35;
    int16_t barSpacing = 10;
    int16_t chartY = 310;
    int16_t chartH = 200;
    
    uint32_t maxSteps = 1;
    for (int i = 0; i < 7; i++) {
        if (stepHistory[i] > maxSteps) maxSteps = stepHistory[i];
    }
    
    for (int i = 0; i < 7; i++) {
        int16_t barX = MARGIN + 20 + i * (barW + barSpacing);
        int16_t barH = (stepHistory[i] * chartH) / maxSteps;
        if (barH < 5) barH = 5;
        
        uint16_t col = (i == currentDay) ? COL_STEPS : COL_CARD_LIGHT;
        gfx->fillRoundRect(barX, chartY - barH, barW, barH, 4, col);
        
        gfx->setTextSize(1);
        gfx->setTextColor(COL_TEXT_DIM);
        gfx->setCursor(barX + 12, chartY + 10);
        gfx->print(days[i]);
    }
    
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

void drawSubGamesClicker() {
    gfx->fillScreen(COL_BG);
    
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(2);
    gfx->setCursor(MARGIN, 15);
    gfx->print("< Clicker");
    
    drawCard(MARGIN, 60, LCD_WIDTH - 2*MARGIN, 100, COL_CARD);
    
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(MARGIN + 30, 80);
    gfx->print("Score:");
    
    gfx->setTextSize(4);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(MARGIN + 100, 105);
    gfx->printf("%lu", clickerScore);
    
    gfx->fillCircle(LCD_WIDTH / 2, 260, 70, COL_GAMES);
    gfx->setTextSize(3);
    gfx->setTextColor(COL_TEXT);
    gfx->setCursor(LCD_WIDTH / 2 - 40, 245);
    gfx->print("CLICK");
    
    gfx->setTextSize(1);
    gfx->setTextColor(COL_TEXT_DIMMER);
    gfx->setCursor(LCD_WIDTH/2 - 35, LCD_HEIGHT - 25);
    gfx->print("Swipe up to exit");
}

// ═══════════════════════════════════════════════════════════════════════════
//  GESTURE HANDLING
// ═══════════════════════════════════════════════════════════════════════════

GestureType detectGesture() {
    if (gestureStartX < 0 || gestureEndX < 0) return GESTURE_NONE;
    
    int16_t dx = gestureEndX - gestureStartX;
    int16_t dy = gestureEndY - gestureStartY;
    
    if (abs(dx) < 30 && abs(dy) < 30) return GESTURE_TAP;
    
    if (abs(dx) > abs(dy)) {
        if (abs(dx) > SWIPE_THRESHOLD) return (dx > 0) ? GESTURE_RIGHT : GESTURE_LEFT;
    } else {
        if (abs(dy) > SWIPE_THRESHOLD) return (dy > 0) ? GESTURE_DOWN : GESTURE_UP;
    }
    
    return GESTURE_NONE;
}

void handleTap() {
    // Dismiss low battery popup
    if (showingLowBatteryPopup) {
        showingLowBatteryPopup = false;
        redrawNeeded = true;
        return;
    }
    
    // System card - battery saver toggle
    if (currentMainCard == CARD_SYSTEM && currentSubCard == SUB_NONE) {
        if (touchY > 215 && touchY < 240) {
            toggleBatterySaver();
            redrawNeeded = true;
            return;
        }
    }
    
    // Reset button
    if (currentSubCard == SUB_SYSTEM_RESET) {
        if (touchX > MARGIN + 50 && touchX < LCD_WIDTH - MARGIN - 50 &&
            touchY > 250 && touchY < 310) {
            factoryReset();
            return;
        }
    }
    
    // Clicker game
    if (currentSubCard == SUB_GAMES_CLICKER) {
        if (touchX > LCD_WIDTH/2 - 70 && touchX < LCD_WIDTH/2 + 70 &&
            touchY > 190 && touchY < 330) {
            clickerScore++;
            redrawNeeded = true;
            return;
        }
    }
    
    // Music play/pause
    if (currentMainCard == CARD_MUSIC && currentSubCard == SUB_NONE) {
        if (touchX > LCD_WIDTH/2 - 25 && touchX < LCD_WIDTH/2 + 25 &&
            touchY > 215 && touchY < 265) {
            musicPlaying = !musicPlaying;
            redrawNeeded = true;
            return;
        }
    }
}

void handleGesture(GestureType gesture) {
    if (gesture == GESTURE_NONE) return;
    if (gesture == GESTURE_TAP) { handleTap(); return; }
    
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
                switch (currentMainCard) {
                    case CARD_CLOCK: currentSubCard = SUB_CLOCK_WORLD; break;
                    case CARD_STEPS: currentSubCard = SUB_STEPS_GRAPH; break;
                    case CARD_GAMES: currentSubCard = SUB_GAMES_CLICKER; break;
                    case CARD_SYSTEM: currentSubCard = SUB_SYSTEM_BATTERY_STATS; break;
                    default: break;
                }
                redrawNeeded = true;
                break;
            default: break;
        }
    } else {
        if (gesture == GESTURE_UP) {
            currentSubCard = SUB_NONE;
            redrawNeeded = true;
        } else if (gesture == GESTURE_LEFT || gesture == GESTURE_RIGHT) {
            // Navigate between sub-cards in System
            if (currentMainCard == CARD_SYSTEM) {
                if (gesture == GESTURE_LEFT) {
                    if (currentSubCard == SUB_SYSTEM_BATTERY_STATS) currentSubCard = SUB_SYSTEM_USAGE;
                    else if (currentSubCard == SUB_SYSTEM_USAGE) currentSubCard = SUB_SYSTEM_RESET;
                    else currentSubCard = SUB_SYSTEM_BATTERY_STATS;
                } else {
                    if (currentSubCard == SUB_SYSTEM_RESET) currentSubCard = SUB_SYSTEM_USAGE;
                    else if (currentSubCard == SUB_SYSTEM_USAGE) currentSubCard = SUB_SYSTEM_BATTERY_STATS;
                    else currentSubCard = SUB_SYSTEM_RESET;
                }
                redrawNeeded = true;
            }
        }
    }
}

void handlePowerButton() {
    bool inPowerArea = (touchX > LCD_WIDTH - 60 && touchY < 50);
    
    if (inPowerArea && !buttonPressed) {
        buttonPressed = true;
        buttonPressStart = millis();
    }
    else if (!inPowerArea && buttonPressed) {
        unsigned long duration = millis() - buttonPressStart;
        buttonPressed = false;
        
        if (duration >= BUTTON_LONG_PRESS_MS) {
            shutdownDevice();
        } else if (duration > 50) {
            if (screenOn) screenOff(); else screenOnFunc();
        }
    }
    
    // Long press progress bar
    if (buttonPressed && screenOn && millis() - buttonPressStart > 500) {
        int progress = min((int)((millis() - buttonPressStart - 500) * 100 / (BUTTON_LONG_PRESS_MS - 500)), 100);
        gfx->fillRect(LCD_WIDTH - 50, 30, 40, 5, COL_CARD);
        gfx->fillRect(LCD_WIDTH - 50, 30, (40 * progress) / 100, 5, COL_DANGER);
    }
}

void handleTouch() {
    bool t = touch.touched();
    
    if (t) {
        touch.getPoint(&touchX, &touchY);
        lastActivityMs = millis();
        
        if (!screenOn) { screenOnFunc(); return; }
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
            handleGesture(detectGesture());
        }
        
        gestureInProgress = false;
        gestureStartX = gestureStartY = gestureEndX = gestureEndY = -1;
    }
    
    if (t && screenOn) handlePowerButton();
    
    lastTouchState = t;
}

// ═══════════════════════════════════════════════════════════════════════════
//  BACKGROUND SERVICES
// ═══════════════════════════════════════════════════════════════════════════

void serviceClock() {
    if (millis() - lastClockUpdate >= 1000) {
        lastClockUpdate = millis();
        uint8_t oldHour = clockHour;
        uint8_t oldDay = currentDay;
        
        if (hasRTC) {
            rtc.getTime(&clockHour, &clockMinute, &clockSecond);
            uint8_t day, weekday, month, year;
            rtc.getDate(&day, &weekday, &month, &year);
            currentDay = weekday;
        } else {
            clockSecond++;
            if (clockSecond >= 60) { clockSecond = 0; clockMinute++; }
            if (clockMinute >= 60) { clockMinute = 0; clockHour++; }
            if (clockHour >= 24) { clockHour = 0; currentDay = (currentDay + 1) % 7; }
        }
        
        // Hourly stats update
        if (clockHour != oldHour) {
            updateHourlyStats();
        }
        
        // Daily stats update
        if (currentDay != oldDay) {
            updateDailyStats();
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
    if (millis() - lastBatteryUpdate >= 3000) {
        lastBatteryUpdate = millis();
        batteryVoltage = pmu.getBattVoltage();
        batteryPercent = pmu.getBattPercent();
        isCharging = pmu.isCharging();
        freeRAM = ESP.getFreeHeap();
        
        calculateBatteryEstimates();
        checkLowBattery();
        
        if (screenOn && currentMainCard == CARD_SYSTEM) redrawNeeded = true;
    }
}

void serviceMusic() {
    if (musicPlaying && millis() - lastMusicUpdate >= 1000) {
        lastMusicUpdate = millis();
        musicCurrent++;
        if (musicCurrent >= musicDuration) { musicCurrent = 0; musicPlaying = false; }
        if (screenOn && currentMainCard == CARD_MUSIC) redrawNeeded = true;
    }
}

void serviceGames() {
    if (millis() - lastGameRotate >= 300000) {
        lastGameRotate = millis();
        spotlightGame = (spotlightGame + 1) % 3;
        if (screenOn && currentMainCard == CARD_GAMES && currentSubCard == SUB_NONE) redrawNeeded = true;
    }
}

void serviceWeather() {
    if (wifiConnected && millis() - lastWeatherUpdate >= 1800000) fetchWeather();
}

void serviceWiFi() {
    // Check WiFi connection every 30 seconds and reconnect if needed
    static unsigned long lastWiFiCheck = 0;
    if (millis() - lastWiFiCheck >= 30000) {
        lastWiFiCheck = millis();
        checkWiFiReconnect();
    }
}

void serviceSave() {
    if (millis() - lastSaveTime >= SAVE_INTERVAL_MS) saveUserData();
}

void serviceScreenTimeout() {
    unsigned long timeout = batterySaverMode ? SCREEN_OFF_TIMEOUT_SAVER_MS : SCREEN_OFF_TIMEOUT_MS;
    if (screenOn && millis() - lastActivityMs >= timeout) screenOff();
}

void serviceUsageTracking() {
    if (millis() - lastUsageUpdate >= 60000) {  // Every minute
        updateUsageTracking();
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
            case SUB_CLOCK_WORLD:          drawSubClockWorld();     break;
            case SUB_STEPS_GRAPH:          drawSubStepsGraph();     break;
            case SUB_GAMES_CLICKER:        drawSubGamesClicker();   break;
            case SUB_SYSTEM_BATTERY_STATS: drawSubBatteryStats();   break;
            case SUB_SYSTEM_USAGE:         drawSubUsagePatterns();  break;
            case SUB_SYSTEM_RESET:         drawSubSystemReset();    break;
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
    Serial.println("  S3 MiniOS v3.1 - Battery Intelligence");
    Serial.println("═══════════════════════════════════════════\n");
    
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    
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
    }
    
    gfx->begin();
    gfx->fillScreen(COL_BG);
    for (int i = 0; i <= 255; i += 5) { gfx->setBrightness(i); delay(2); }
    
    // Splash
    gfx->setTextSize(3);
    gfx->setTextColor(COL_CLOCK);
    gfx->setCursor(60, 140);
    gfx->print("S3 MiniOS");
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT_DIM);
    gfx->setCursor(130, 180);
    gfx->print("v3.1");
    gfx->setTextSize(1);
    gfx->setCursor(70, 210);
    gfx->print("Battery Intelligence");
    gfx->setCursor(100, 250);
    gfx->print("Loading...");
    
    hasSD = initSDCard();
    loadWiFiFromSD();
    
    if (touch.begin()) Serial.println("[OK] Touch");
    hasRTC = rtc.begin();
    if (hasRTC) rtc.getTime(&clockHour, &clockMinute, &clockSecond);
    hasPMU = pmu.begin();
    if (hasPMU) { batteryPercent = pmu.getBattPercent(); batteryVoltage = pmu.getBattVoltage(); isCharging = pmu.isCharging(); }
    hasIMU = imu.begin();
    
    loadUserData();
    
    if (numWifiNetworks > 0) {
        gfx->setCursor(80, 280);
        gfx->printf("Trying %d WiFi networks...", numWifiNetworks);
        connectWiFi();
        if (wifiConnected) {
            getLocationFromIP();
            fetchWeather();
        }
    }
    
    snprintf(weatherLocation, 63, "%s, %s", weatherCity, weatherCountry);
    
    // Initialize battery stats
    batteryStats.sessionStartMs = millis();
    batteryStats.batteryAtHourStart = batteryPercent;
    screenOnStartMs = millis();
    
    delay(1000);
    
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("  NEW FEATURES:");
    Serial.println("  - Multi-WiFi support (up to 5 networks)");
    Serial.println("  - Auto-reconnect on disconnect");
    Serial.println("  - Usage tracking (24h screen time)");
    Serial.println("  - ML-style battery estimation");
    Serial.println("  - Battery Stats graph card");
    Serial.println("  - Low battery popup warning");
    Serial.println("  - Battery Saver mode (tap to toggle)");
    Serial.println("  - Charging animation");
    Serial.println("═══════════════════════════════════════════\n");
    
    lastActivityMs = millis();
    lastSaveTime = millis();
    lastUsageUpdate = millis();
    lastHourlyUpdate = millis();
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
    serviceWiFi();
    serviceSave();
    serviceScreenTimeout();
    serviceUsageTracking();
    updateChargingAnimation();
    handleTouch();
    renderScreen();
    
    delay(10);
}
