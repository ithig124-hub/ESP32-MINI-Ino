/**
 * ═══════════════════════════════════════════════════════════════════════════════
 *  Widget OS v1.0.0 (A180) - FUSION LABS WEB EDITION
 *  ESP32-S3-Touch-AMOLED-1.8" Smartwatch Firmware
 * ═══════════════════════════════════════════════════════════════════════════════
 * 
 *  === WIDGET OS SD CARD STORAGE SPEC v1.1 ===
 *  
 *  SD Card Structure (Auto-Created on First Boot):
 *  /WATCH/
 *  ├── SYSTEM/
 *  │   ├── device.json      (Device info: WOS-180A)
 *  │   ├── os.json          (OS version info)
 *  │   ├── build.txt        (Human-readable build info)
 *  │   └── logs/
 *  │       └── boot.log     (Boot logging)
 *  ├── CONFIG/
 *  │   ├── user.json        (User preferences)
 *  │   ├── display.json     (Display settings)
 *  │   └── power.json       (Power management)
 *  ├── FACES/
 *  │   ├── custom/          (User-added faces)
 *  │   └── imported/        (Imported faces from web)
 *  ├── IMAGES/              (User images - auto-created)
 *  ├── MUSIC/               (User music - auto-created)
 *  ├── CACHE/
 *  │   └── temp/
 *  ├── UPDATE/
 *  │   └── README.txt
 *  └── wifi/
 *      └── config.txt       (WiFi configuration template)
 *
 *  CORE RULES:
 *  ✅ Firmware updates never erase user data
 *  ✅ Default watch faces live in firmware, not SD
 *  ✅ SD may override or add, never required to boot
 *  ✅ OS boots even if SD is missing (uses defaults)
 *  ✅ Same SD layout works across board sizes (1.8" & 2.06")
 *
 *  WEB FEATURES (Fusion Labs Integration):
 *  ✅ USB Serial WiFi Configuration
 *  ✅ USB Serial Watch Face Installation
 *  ✅ Device Status Query via Serial
 *
 *  BOARD: A180 (1.8" AMOLED)
 *  Target: Productivity / utility / clarity focused smartwatch OS
 * 
 *  Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.8
 *    • Display: SH8601 QSPI AMOLED 368x448
 *    • Touch: FT3168
 *    • IMU: QMI8658
 *    • RTC: PCF85063
 *    • PMU: AXP2101
 *    • I/O Expander: XCA9554 (for reset control)
 *    • SD: SD_MMC 1-bit mode
 * 
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#include <lvgl.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>
#include <Arduino.h>
#include "pin_config.h"

// Fix macro conflict: SensorLib also defines PCF85063_SLAVE_ADDRESS as a const
#ifdef PCF85063_SLAVE_ADDRESS
#undef PCF85063_SLAVE_ADDRESS
#endif

#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include <Adafruit_XCA9554.h>
#include "SensorQMI8658.hpp"
#include "SensorPCF85063.hpp"
#include "XPowersLib.h"
#include <FS.h>
#include <SD_MMC.h>
#include "HWCDC.h"
#include <math.h>
#include <Preferences.h>
#include <esp_sleep.h>

// ═══════════════════════════════════════════════════════════════════════════════
//  WIDGET OS - VERSION & DEVICE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
#define WIDGET_OS_NAME      "Widget OS"
#define WIDGET_OS_VERSION   "1.0.0"
#define WIDGET_OS_BUILD     "stable"
#define DEVICE_ID           "WOS-180A"
#define DEVICE_SCREEN       "1.8"
#define DEVICE_HW_REV       "A"

// ═══════════════════════════════════════════════════════════════════════════════
//  WIDGET OS - SD CARD STORAGE PATHS (LOCKED - DO NOT CHANGE)
// ═══════════════════════════════════════════════════════════════════════════════
#define SD_ROOT_PATH            "/WATCH"
#define SD_SYSTEM_PATH          "/WATCH/SYSTEM"
#define SD_SYSTEM_LOGS_PATH     "/WATCH/SYSTEM/logs"
#define SD_CONFIG_PATH          "/WATCH/CONFIG"
#define SD_FACES_PATH           "/WATCH/FACES"
#define SD_FACES_CUSTOM_PATH    "/WATCH/FACES/custom"
#define SD_FACES_IMPORTED_PATH  "/WATCH/FACES/imported"
#define SD_IMAGES_PATH          "/WATCH/IMAGES"
#define SD_MUSIC_PATH           "/WATCH/MUSIC"
#define SD_CACHE_PATH           "/WATCH/CACHE"
#define SD_CACHE_TEMP_PATH      "/WATCH/CACHE/temp"
#define SD_UPDATE_PATH          "/WATCH/UPDATE"
#define SD_WIFI_PATH            "/WATCH/wifi"

// System files
#define SD_DEVICE_JSON          "/WATCH/SYSTEM/device.json"
#define SD_OS_JSON              "/WATCH/SYSTEM/os.json"
#define SD_BUILD_TXT            "/WATCH/SYSTEM/build.txt"
#define SD_BOOT_LOG             "/WATCH/SYSTEM/logs/boot.log"

// Config files
#define SD_USER_JSON            "/WATCH/CONFIG/user.json"
#define SD_DISPLAY_JSON         "/WATCH/CONFIG/display.json"
#define SD_POWER_JSON           "/WATCH/CONFIG/power.json"

// Other files
#define SD_UPDATE_README        "/WATCH/UPDATE/README.txt"
#define SD_WIFI_CONFIG          "/WATCH/wifi/config.txt"

// Legacy path for backward compatibility
#define WIFI_CONFIG_PATH        "/WATCH/wifi/config.txt"

// ═══════════════════════════════════════════════════════════════════════════════
//  SD CARD STATUS & ERROR HANDLING
// ═══════════════════════════════════════════════════════════════════════════════
enum SDCardStatus {
    SD_STATUS_NOT_PRESENT = 0,
    SD_STATUS_MOUNTED_OK,
    SD_STATUS_MOUNT_FAILED,
    SD_STATUS_CORRUPT,
    SD_STATUS_READ_ONLY,
    SD_STATUS_INIT_IN_PROGRESS
};

SDCardStatus sdCardStatus = SD_STATUS_NOT_PRESENT;
bool sdCardInitialized = false;
bool sdStructureCreated = false;
String sdErrorMessage = "";
uint64_t sdCardSizeMB = 0;
uint64_t sdCardUsedMB = 0;
String sdCardType = "Unknown";
unsigned long lastBackupTimeMs = 0;
bool hasLastBackup = false;

// Auto backup configuration
#define AUTO_BACKUP_INTERVAL_MS 86400000UL  // 24 hours in milliseconds
unsigned long lastAutoBackupMs = 0;
bool autoBackupEnabled = true;
bool backupInProgress = false;
uint8_t backupProgress = 0;
bool showingBackupComplete = false;
unsigned long backupCompleteShownMs = 0;

// ═══════════════════════════════════════════════════════════════════════════════
//  USB SERIAL COMPATIBILITY FIX
// ═══════════════════════════════════════════════════════════════════════════════
#if ARDUINO_USB_CDC_ON_BOOT
  #define USBSerial Serial
#else
  #if !defined(USBSerial)
    HWCDC USBSerial;
  #endif
#endif

// ═══════════════════════════════════════════════════════════════════════════════
//  BOARD-SPECIFIC CONFIGURATION (1.8" SH8601 - A180)
// ═══════════════════════════════════════════════════════════════════════════════
#define LCD_WIDTH       368
#define LCD_HEIGHT      448

// ═══════════════════════════════════════════════════════════════════════════════
//  WIDGET OS - WIFI CONFIGURATION (Widget OS compatible)
// ═══════════════════════════════════════════════════════════════════════════════
#define MAX_WIFI_NETWORKS 5
#define MAX_OPEN_NETWORKS 10
#define WIFI_SCAN_TIMEOUT_MS 5000
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_RECONNECT_INTERVAL_MS 60000
#define MIN_RSSI_THRESHOLD -85

struct WiFiNetwork {
    char ssid[64];
    char password[64];
    bool valid;
    bool isOpen;
    int32_t rssi;
};

WiFiNetwork wifiNetworks[MAX_WIFI_NETWORKS];
int numWifiNetworks = 0;
int connectedNetworkIndex = -1;

struct OpenNetwork {
    char ssid[64];
    int32_t rssi;
    bool valid;
};
OpenNetwork openNetworks[MAX_OPEN_NETWORKS];
int numOpenNetworks = 0;
bool connectedToOpenNetwork = false;
unsigned long lastWiFiCheck = 0;
unsigned long lastWiFiScan = 0;

char weatherCity[64] = "Perth";
char weatherCountry[8] = "AU";
long gmtOffsetSec = 8 * 3600;
const char* NTP_SERVER = "pool.ntp.org";
const int DAYLIGHT_OFFSET_SEC = 0;

// API Keys
const char* OPENWEATHER_API = "3795c13a0d3f7e17799d638edda60e3c";
const char* ALPHAVANTAGE_API = "UHLX28BF7GQ4T8J3";
const char* COINAPI_KEY = "11afad22-b6ea-4f18-9056-c7a1d7ed14a1";
const char* CURRENCY_API_KEY = "cur_live_ROqsDnwrNd40cRqegQakXb4pO6tQuihpU9OQr4Nx";

bool wifiConnected = false;
bool wifiConfigFromSD = false;

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY INTELLIGENCE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
#define SAVE_INTERVAL_MS 7200000UL
#define SCREEN_OFF_TIMEOUT_MS 30000
#define SCREEN_OFF_TIMEOUT_SAVER_MS 10000
#define BATTERY_CAPACITY_MAH 350
#define SCREEN_ON_CURRENT_MA 80
#define SCREEN_OFF_CURRENT_MA 15
#define SAVER_MODE_CURRENT_MA 40
#define LOW_BATTERY_WARNING 20
#define CRITICAL_BATTERY_WARNING 10
#define USAGE_HISTORY_SIZE 24
#define CARD_USAGE_SLOTS 12

// ═══════════════════════════════════════════════════════════════════════════════
//  LVGL CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
#define LVGL_TICK_PERIOD_MS 2
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

// ═══════════════════════════════════════════════════════════════════════════════
//  HARDWARE OBJECTS (1.8" board uses I/O expander)
// ═══════════════════════════════════════════════════════════════════════════════
Adafruit_XCA9554 expander;
SensorQMI8658 qmi;
SensorPCF85063 rtc;
XPowersPMU power;
IMUdata acc, gyr;
Preferences prefs;

// Hardware objects initialized with pin_config.h values
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_SH8601 *gfx = new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

// SD Card pins from pin_config.h (1.8" board):
// SDMMC_CLK = GPIO 2
// SDMMC_CMD = GPIO 1
// SDMMC_DATA = GPIO 3

// ═══════════════════════════════════════════════════════════════════════════════
//  IDENTITY SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════
#define NUM_IDENTITIES 15

// ═══════════════════════════════════════════════════════════════════════════════
//  NAVIGATION SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════
#define NUM_CATEGORIES 15
enum Category {
  CAT_CLOCK = 0, CAT_COMPASS, CAT_ACTIVITY, CAT_GAMES,
  CAT_WEATHER, CAT_STOCKS, CAT_MEDIA, CAT_TIMER,
  CAT_STREAK, CAT_CALENDAR, CAT_TORCH, CAT_TOOLS,
  CAT_SETTINGS, CAT_SYSTEM, CAT_IDENTITY
};

int currentCategory = CAT_CLOCK;
int currentSubCard = 0;
const int maxSubCards[] = {5, 3, 4, 3, 2, 2, 2, 4, 3, 1, 2, 4, 2, 3, 2};

bool isTransitioning = false;
int transitionDir = 0;
float transitionProgress = 0.0;
unsigned long transitionStartMs = 0;
const unsigned long TRANSITION_DURATION = 200;

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY INTELLIGENCE DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════
struct BatteryStats {
    uint32_t screenOnTimeMs;
    uint32_t screenOffTimeMs;
    uint32_t sessionStartMs;
    uint16_t hourlyScreenOnMins[USAGE_HISTORY_SIZE];
    uint16_t hourlyScreenOffMins[USAGE_HISTORY_SIZE];
    uint16_t hourlySteps[USAGE_HISTORY_SIZE];
    uint8_t currentHourIndex;
    uint32_t cardUsageTime[CARD_USAGE_SLOTS];
    uint8_t batteryAtHourStart;
    float avgDrainPerHour;
    float weightedDrainRate;
    float dailyAvgScreenOnHours[7];
    float dailyAvgDrainRate[7];
    uint8_t currentDayIndex;
    uint32_t simpleEstimateMins;
    uint32_t weightedEstimateMins;
    uint32_t learnedEstimateMins;
    uint32_t combinedEstimateMins;
};

BatteryStats batteryStats = {0};

bool batterySaverMode = false;
bool batterySaverAutoEnabled = false;
bool lowBatteryWarningShown = false;
bool criticalBatteryWarningShown = false;
unsigned long lowBatteryPopupTime = 0;
bool showingLowBatteryPopup = false;
uint8_t chargingAnimFrame = 0;
unsigned long lastChargingAnimMs = 0;

// ═══════════════════════════════════════════════════════════════════════════════
//  USER DATA (Persistent)
// ═══════════════════════════════════════════════════════════════════════════════
struct UserData {
  uint32_t steps;
  uint32_t dailyGoal;
  int stepStreak;
  float totalDistance;
  float totalCalories;
  uint32_t stepHistory[7];
  int blackjackStreak;
  int gamesWon;
  int gamesPlayed;
  uint32_t clickerScore;
  int brightness;
  int screenTimeout;
  int themeIndex;
  int compassMode;
  int wallpaperIndex;
  bool identitiesUnlocked[NUM_IDENTITIES];
  uint32_t identityProgress[NUM_IDENTITIES];
  int selectedIdentity;
  uint32_t compassUseCount;
  uint32_t consecutiveDays;
  uint32_t lastUseDayOfYear;
} userData = {0, 10000, 7, 0.0, 0.0, {0}, 0, 0, 0, 0, 200, 1, 0, 0, 0};

// ═══════════════════════════════════════════════════════════════════════════════
//  RUNTIME STATE
// ═══════════════════════════════════════════════════════════════════════════════
bool screenOn = true;
unsigned long lastActivityMs = 0;
unsigned long screenOnStartMs = 0;
unsigned long screenOffStartMs = 0;
bool hasIMU = false, hasRTC = false, hasPMU = false, hasSD = false;

// Clock
uint8_t clockHour = 10, clockMinute = 30, clockSecond = 0;
uint8_t currentDay = 3;

// Weather
float weatherTemp = 24.0;
String weatherDesc = "Sunny";
float weatherHigh = 28.0;
float weatherLow = 18.0;

// Battery
uint16_t batteryVoltage = 4100;
uint8_t batteryPercent = 85;
bool isCharging = false;

// SD Watch Faces
#define MAX_SD_FACES 20
struct SDFace {
    char name[32];
    char author[32];
    char version[16];
    char path[64];
    bool valid;
};
SDFace sdFaces[MAX_SD_FACES];
int numSDFaces = 0;

// Active watch face (for web face loading)
char activeWatchFaceId[32] = "default";

// ═══════════════════════════════════════════════════════════════════════════════
//  FUSION LABS WEB SERIAL HANDLER - GLOBAL VARIABLES
// ═══════════════════════════════════════════════════════════════════════════════
String serialBuffer = "";
bool receivingWifiConfig = false;
String wifiConfigBuffer = "";
bool receivingFaceData = false;
String faceIdBuffer = "";
String faceDataBuffer = "";
bool waitingForFaceId = false;

// ═══════════════════════════════════════════════════════════════════════════════
//  TOUCH INTERRUPT
// ═══════════════════════════════════════════════════════════════════════════════
void Arduino_IIC_Touch_Interrupt(void) {
  FT3168->IIC_Interrupt_Flag = true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DISPLAY FLUSH
// ═══════════════════════════════════════════════════════════════════════════════
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
  lv_disp_flush_ready(disp);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SD CARD HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════════

const char* getSDCardStatusString() {
    switch (sdCardStatus) {
        case SD_STATUS_NOT_PRESENT: return "Not Present";
        case SD_STATUS_MOUNTED_OK: return "OK";
        case SD_STATUS_MOUNT_FAILED: return "Mount Failed";
        case SD_STATUS_CORRUPT: return "Corrupt";
        case SD_STATUS_READ_ONLY: return "Read Only";
        case SD_STATUS_INIT_IN_PROGRESS: return "Initializing...";
        default: return "Unknown";
    }
}

bool createDirIfNotExists(const char* path) {
    if (SD_MMC.exists(path)) {
        return true;
    }
    
    USBSerial.printf("[SD] Creating directory: %s\n", path);
    if (SD_MMC.mkdir(path)) {
        return true;
    } else {
        USBSerial.printf("[SD] ERROR: Failed to create %s\n", path);
        return false;
    }
}

void logToBootLog(const char* message) {
    if (!sdCardInitialized) return;
    
    File logFile = SD_MMC.open(SD_BOOT_LOG, FILE_APPEND);
    if (logFile) {
        char timestamp[32];
        if (hasRTC) {
            RTC_DateTime dt = rtc.getDateTime();
            snprintf(timestamp, sizeof(timestamp), "[%04d-%02d-%02d %02d:%02d:%02d] ",
                     dt.getYear(), dt.getMonth(), dt.getDay(),
                     dt.getHour(), dt.getMinute(), dt.getSecond());
        } else {
            snprintf(timestamp, sizeof(timestamp), "[%lu] ", millis());
        }
        logFile.print(timestamp);
        logFile.println(message);
        logFile.close();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WIFI CONFIG - LOAD FROM SD
// ═══════════════════════════════════════════════════════════════════════════════
void loadWiFiConfigFromSD() {
    if (!hasSD || !sdCardInitialized) {
        USBSerial.println("[WIFI] No SD card available, using defaults");
        return;
    }
    
    if (!SD_MMC.exists(SD_WIFI_CONFIG)) {
        USBSerial.println("[WIFI] No WiFi config file found on SD");
        return;
    }
    
    File configFile = SD_MMC.open(SD_WIFI_CONFIG, FILE_READ);
    if (!configFile) {
        USBSerial.println("[WIFI] Failed to open WiFi config file");
        return;
    }
    
    USBSerial.println("[WIFI] Loading WiFi config from SD card...");
    numWifiNetworks = 0;
    
    while (configFile.available() && numWifiNetworks < MAX_WIFI_NETWORKS) {
        String line = configFile.readStringUntil('\n');
        line.trim();
        
        // Skip comments and empty lines
        if (line.length() == 0 || line.startsWith("#")) {
            continue;
        }
        
        int eqPos = line.indexOf('=');
        if (eqPos > 0) {
            String key = line.substring(0, eqPos);
            String value = line.substring(eqPos + 1);
            key.trim();
            value.trim();
            
            if (key == "SSID" || key.startsWith("WIFI") && key.endsWith("_SSID")) {
                strncpy(wifiNetworks[numWifiNetworks].ssid, value.c_str(), 63);
                wifiNetworks[numWifiNetworks].ssid[63] = '\0';
            } else if (key == "PASSWORD" || key.startsWith("WIFI") && key.endsWith("_PASS")) {
                strncpy(wifiNetworks[numWifiNetworks].password, value.c_str(), 63);
                wifiNetworks[numWifiNetworks].password[63] = '\0';
                wifiNetworks[numWifiNetworks].valid = true;
                numWifiNetworks++;
            } else if (key == "CITY") {
                strncpy(weatherCity, value.c_str(), 63);
                weatherCity[63] = '\0';
            } else if (key == "COUNTRY") {
                strncpy(weatherCountry, value.c_str(), 7);
                weatherCountry[7] = '\0';
            } else if (key == "GMT_OFFSET") {
                gmtOffsetSec = value.toInt() * 3600;
            }
        }
    }
    
    configFile.close();
    wifiConfigFromSD = (numWifiNetworks > 0);
    USBSerial.printf("[WIFI] Loaded %d WiFi networks from SD card\n", numWifiNetworks);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  FUSION LABS WEB SERIAL HANDLER - WIFI CONFIG
// ═══════════════════════════════════════════════════════════════════════════════

void sendDeviceStatus() {
    USBSerial.println("WIDGET_STATUS_START");
    USBSerial.println("DEVICE:Widget OS Watch");
    USBSerial.println("VERSION:" WIDGET_OS_VERSION);
    USBSerial.printf("BOARD:%dx%d\n", LCD_WIDTH, LCD_HEIGHT);
    USBSerial.printf("DEVICE_ID:%s\n", DEVICE_ID);
    USBSerial.printf("SD_CARD:%s\n", (hasSD && sdCardInitialized) ? "YES" : "NO");
    USBSerial.printf("WIFI:%s\n", (WiFi.status() == WL_CONNECTED) ? WiFi.SSID().c_str() : "Not Connected");
    USBSerial.printf("BATTERY:%d%%\n", batteryPercent);
    USBSerial.printf("FACES:%d\n", numSDFaces);
    USBSerial.println("WIDGET_STATUS_END");
}

void sendWifiConfig() {
    if (!hasSD || !sdCardInitialized) {
        USBSerial.println("ERROR:SD_CARD_NOT_AVAILABLE");
        return;
    }
    
    if (!SD_MMC.exists(SD_WIFI_CONFIG)) {
        USBSerial.println("ERROR:CONFIG_FILE_NOT_FOUND");
        return;
    }
    
    File configFile = SD_MMC.open(SD_WIFI_CONFIG, FILE_READ);
    if (!configFile) {
        USBSerial.println("ERROR:CANNOT_OPEN_FILE");
        return;
    }
    
    USBSerial.println("WIFI_CONFIG_START");
    while (configFile.available()) {
        String line = configFile.readStringUntil('\n');
        line.trim();
        if (line.length() > 0 && !line.startsWith("#")) {
            USBSerial.println(line);
        }
    }
    configFile.close();
    USBSerial.println("WIFI_CONFIG_END");
}

void saveWifiConfig(String config) {
    if (!hasSD || !sdCardInitialized) {
        USBSerial.println("ERROR:SD_CARD_NOT_AVAILABLE");
        return;
    }
    
    // Ensure directory exists
    if (!SD_MMC.exists(SD_WIFI_PATH)) {
        SD_MMC.mkdir(SD_WIFI_PATH);
    }
    
    File configFile = SD_MMC.open(SD_WIFI_CONFIG, FILE_WRITE);
    if (!configFile) {
        USBSerial.println("ERROR:CANNOT_CREATE_FILE");
        return;
    }
    
    configFile.println("# ═══════════════════════════════════════════════════════════════════");
    configFile.println("#  Widget OS - WiFi Configuration");
    configFile.println("#  Updated via Fusion Labs Web Serial Tool");
    configFile.println("# ═══════════════════════════════════════════════════════════════════");
    configFile.println();
    configFile.print(config);
    configFile.close();
    
    USBSerial.println("CONFIG_WRITTEN");
    
    // Reload WiFi config
    loadWiFiConfigFromSD();
    USBSerial.println("CONFIG_RELOADED");
    logToBootLog("WiFi config updated via web serial");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  FUSION LABS WEB SERIAL HANDLER - WATCH FACES
// ═══════════════════════════════════════════════════════════════════════════════

void listInstalledFaces() {
    USBSerial.println("FACE_LIST_START");
    
    // List default/built-in faces
    USBSerial.println("FACE:default,Default,Built-in,1.0.0");
    USBSerial.println("FACE:minimal,Minimal Dark,Built-in,1.0.0");
    USBSerial.println("FACE:digital,Digital,Built-in,1.0.0");
    
    // List SD card faces
    for (int i = 0; i < numSDFaces; i++) {
        if (sdFaces[i].valid) {
            char faceLine[128];
            snprintf(faceLine, sizeof(faceLine), "FACE:%s,%s,%s,%s",
                     sdFaces[i].path, sdFaces[i].name, sdFaces[i].author, sdFaces[i].version);
            USBSerial.println(faceLine);
        }
    }
    
    USBSerial.printf("ACTIVE_FACE:%s\n", activeWatchFaceId);
    USBSerial.println("FACE_LIST_END");
}

void handleFaceSerialData(String line) {
    if (waitingForFaceId) {
        faceIdBuffer = line;
        waitingForFaceId = false;
        faceDataBuffer = "";
        USBSerial.println("READY_FOR_FACE_DATA");
        return;
    }
    
    if (line == "END_FACE_DATA") {
        receivingFaceData = false;
        saveFaceToSD(faceIdBuffer, faceDataBuffer);
        faceIdBuffer = "";
        faceDataBuffer = "";
        return;
    }
    
    faceDataBuffer += line + "\n";
}

void saveFaceToSD(String faceId, String faceData) {
    if (!hasSD || !sdCardInitialized) {
        USBSerial.println("ERROR:SD_CARD_NOT_AVAILABLE");
        return;
    }
    
    // Ensure faces directories exist
    if (!SD_MMC.exists(SD_FACES_IMPORTED_PATH)) {
        SD_MMC.mkdir(SD_FACES_PATH);
        SD_MMC.mkdir(SD_FACES_IMPORTED_PATH);
    }
    
    // Create face file path
    String facePath = String(SD_FACES_IMPORTED_PATH) + "/" + faceId + ".json";
    
    File faceFile = SD_MMC.open(facePath.c_str(), FILE_WRITE);
    if (!faceFile) {
        USBSerial.println("ERROR:CANNOT_CREATE_FACE_FILE");
        return;
    }
    
    faceFile.print(faceData);
    faceFile.close();
    
    USBSerial.println("FACE_SAVED:" + faceId);
    
    // Reload faces from SD
    scanSDFaces();
    
    logToBootLog(("Watch face installed: " + faceId).c_str());
}

void setActiveFace(String command) {
    // Extract face ID from command like "WIDGET_SET_FACE:faceid"
    int colonPos = command.indexOf(':');
    if (colonPos > 0) {
        String faceId = command.substring(colonPos + 1);
        faceId.trim();
        strncpy(activeWatchFaceId, faceId.c_str(), 31);
        activeWatchFaceId[31] = '\0';
        USBSerial.println("FACE_SET:" + faceId);
        logToBootLog(("Active face changed to: " + faceId).c_str());
    } else {
        USBSerial.println("ERROR:INVALID_FACE_COMMAND");
    }
}

void deleteFace(String command) {
    // Extract face ID from command like "WIDGET_DELETE_FACE:faceid"
    int colonPos = command.indexOf(':');
    if (colonPos > 0) {
        String faceId = command.substring(colonPos + 1);
        faceId.trim();
        
        String facePath = String(SD_FACES_IMPORTED_PATH) + "/" + faceId + ".json";
        
        if (SD_MMC.exists(facePath.c_str())) {
            SD_MMC.remove(facePath.c_str());
            USBSerial.println("FACE_DELETED:" + faceId);
            scanSDFaces();
            logToBootLog(("Watch face deleted: " + faceId).c_str());
        } else {
            USBSerial.println("ERROR:FACE_NOT_FOUND");
        }
    } else {
        USBSerial.println("ERROR:INVALID_DELETE_COMMAND");
    }
}

void scanSDFaces() {
    numSDFaces = 0;
    
    if (!hasSD || !sdCardInitialized) return;
    
    // Scan imported faces
    File dir = SD_MMC.open(SD_FACES_IMPORTED_PATH);
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry && numSDFaces < MAX_SD_FACES) {
            String filename = entry.name();
            if (filename.endsWith(".json")) {
                // Parse face JSON
                StaticJsonDocument<512> doc;
                DeserializationError error = deserializeJson(doc, entry);
                
                if (!error) {
                    strncpy(sdFaces[numSDFaces].name, doc["name"] | "Unknown", 31);
                    strncpy(sdFaces[numSDFaces].author, doc["author"] | "Unknown", 31);
                    strncpy(sdFaces[numSDFaces].version, doc["version"] | "1.0.0", 15);
                    strncpy(sdFaces[numSDFaces].path, filename.c_str(), 63);
                    sdFaces[numSDFaces].valid = true;
                    numSDFaces++;
                }
            }
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();
    }
    
    USBSerial.printf("[FACES] Found %d custom faces on SD card\n", numSDFaces);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  FUSION LABS WEB SERIAL HANDLER - MAIN PROCESSOR
// ═══════════════════════════════════════════════════════════════════════════════

void processSerialCommand(String command) {
    command.trim();
    
    // Ping/Pong for connection test
    if (command == "WIDGET_PING") {
        USBSerial.println("WIDGET_PONG");
        return;
    }
    
    // Device status
    if (command == "WIDGET_STATUS") {
        sendDeviceStatus();
        return;
    }
    
    // WiFi config commands
    if (command == "WIDGET_READ_WIFI") {
        sendWifiConfig();
        return;
    }
    
    if (command == "WIDGET_WRITE_WIFI") {
        receivingWifiConfig = true;
        wifiConfigBuffer = "";
        USBSerial.println("READY_FOR_CONFIG");
        return;
    }
    
    // Handle WiFi config data
    if (receivingWifiConfig) {
        if (command == "END_WIFI_CONFIG") {
            receivingWifiConfig = false;
            saveWifiConfig(wifiConfigBuffer);
        } else {
            wifiConfigBuffer += command + "\n";
        }
        return;
    }
    
    // Watch face commands
    if (command == "WIDGET_LIST_FACES") {
        listInstalledFaces();
        return;
    }
    
    if (command == "WIDGET_WRITE_FACE") {
        receivingFaceData = true;
        waitingForFaceId = true;
        USBSerial.println("SEND_FACE_ID");
        return;
    }
    
    if (command.startsWith("WIDGET_SET_FACE:")) {
        setActiveFace(command);
        return;
    }
    
    if (command.startsWith("WIDGET_DELETE_FACE:")) {
        deleteFace(command);
        return;
    }
    
    // Handle face data
    if (receivingFaceData) {
        handleFaceSerialData(command);
        return;
    }
    
    // Unknown command
    if (command.length() > 0) {
        USBSerial.println("UNKNOWN_COMMAND:" + command);
    }
}

void handleSerialConfig() {
    while (USBSerial.available()) {
        char c = USBSerial.read();
        
        if (c == '\n') {
            processSerialCommand(serialBuffer);
            serialBuffer = "";
        } else if (c != '\r') {
            serialBuffer += c;
            
            // Prevent buffer overflow
            if (serialBuffer.length() > 4096) {
                serialBuffer = "";
                USBSerial.println("ERROR:BUFFER_OVERFLOW");
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SD CARD INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════════

bool createWidgetOSSDStructure() {
    USBSerial.println("[SD] Creating Widget OS folder structure...");
    
    bool success = true;
    
    success &= createDirIfNotExists(SD_ROOT_PATH);
    success &= createDirIfNotExists(SD_SYSTEM_PATH);
    success &= createDirIfNotExists(SD_SYSTEM_LOGS_PATH);
    success &= createDirIfNotExists(SD_CONFIG_PATH);
    success &= createDirIfNotExists(SD_FACES_PATH);
    success &= createDirIfNotExists(SD_FACES_CUSTOM_PATH);
    success &= createDirIfNotExists(SD_FACES_IMPORTED_PATH);
    success &= createDirIfNotExists(SD_IMAGES_PATH);
    success &= createDirIfNotExists(SD_MUSIC_PATH);
    success &= createDirIfNotExists(SD_CACHE_PATH);
    success &= createDirIfNotExists(SD_CACHE_TEMP_PATH);
    success &= createDirIfNotExists(SD_UPDATE_PATH);
    success &= createDirIfNotExists(SD_WIFI_PATH);
    
    if (success) {
        USBSerial.println("[SD] Folder structure created successfully");
        sdStructureCreated = true;
    }
    
    return success;
}

void createDeviceJson() {
    if (SD_MMC.exists(SD_DEVICE_JSON)) return;
    
    File file = SD_MMC.open(SD_DEVICE_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["device_id"] = DEVICE_ID;
        doc["screen"] = DEVICE_SCREEN;
        doc["storage"] = "sd";
        doc["hw_rev"] = DEVICE_HW_REV;
        serializeJsonPretty(doc, file);
        file.close();
    }
}

void createOSJson() {
    File file = SD_MMC.open(SD_OS_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["name"] = WIDGET_OS_NAME;
        doc["version"] = WIDGET_OS_VERSION;
        doc["build"] = WIDGET_OS_BUILD;
        serializeJsonPretty(doc, file);
        file.close();
    }
}

void createWifiConfigTemplate() {
    if (SD_MMC.exists(SD_WIFI_CONFIG)) return;
    
    File file = SD_MMC.open(SD_WIFI_CONFIG, FILE_WRITE);
    if (file) {
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println("#  Widget OS - WiFi Configuration");
        file.println("#  Board: " DEVICE_ID);
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println("#");
        file.println("#  Edit values below with your WiFi credentials.");
        file.println("#  Use Fusion Labs web tool for easier configuration!");
        file.println("#");
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println();
        file.println("SSID=YourWiFiNetworkName");
        file.println("PASSWORD=YourWiFiPassword");
        file.println();
        file.println("# Optional additional networks");
        file.println("WIFI1_SSID=HomeNetwork");
        file.println("WIFI1_PASS=HomePassword");
        file.println();
        file.println("# Weather location");
        file.println("CITY=Perth");
        file.println("COUNTRY=AU");
        file.println("GMT_OFFSET=8");
        file.close();
    }
}

void initSDCard() {
    sdCardStatus = SD_STATUS_INIT_IN_PROGRESS;
    
    if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT)) {
        sdCardStatus = SD_STATUS_MOUNT_FAILED;
        USBSerial.println("[SD] Mount failed");
        return;
    }
    
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        sdCardStatus = SD_STATUS_NOT_PRESENT;
        USBSerial.println("[SD] No card present");
        return;
    }
    
    switch (cardType) {
        case CARD_MMC:  sdCardType = "MMC"; break;
        case CARD_SD:   sdCardType = "SD"; break;
        case CARD_SDHC: sdCardType = "SDHC"; break;
        default:        sdCardType = "Unknown"; break;
    }
    
    sdCardSizeMB = SD_MMC.cardSize() / (1024 * 1024);
    sdCardUsedMB = SD_MMC.usedBytes() / (1024 * 1024);
    
    hasSD = true;
    sdCardInitialized = true;
    sdCardStatus = SD_STATUS_MOUNTED_OK;
    
    USBSerial.printf("[SD] Card: %s, Size: %llu MB\n", sdCardType.c_str(), sdCardSizeMB);
    
    // Create folder structure and system files
    createWidgetOSSDStructure();
    createDeviceJson();
    createOSJson();
    createWifiConfigTemplate();
    
    // Scan for custom faces
    scanSDFaces();
    
    // Load WiFi config
    loadWiFiConfigFromSD();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SCREEN CONTROL
// ═══════════════════════════════════════════════════════════════════════════════
void screenOff() {
    if (!screenOn) return;
    screenOn = false;
    screenOffStartMs = millis();
    batteryStats.screenOnTimeMs += (millis() - screenOnStartMs);
    gfx->displayOff();
    USBSerial.println("[POWER] Screen off");
}

void screenOnFunc() {
    if (screenOn) return;
    screenOn = true;
    screenOnStartMs = millis();
    batteryStats.screenOffTimeMs += (millis() - screenOffStartMs);
    gfx->displayOn();
    lastActivityMs = millis();
    USBSerial.println("[POWER] Screen on");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  BOOT SCREEN
// ═══════════════════════════════════════════════════════════════════════════════
void showBootScreen() {
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, WIDGET_OS_NAME);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);
    
    lv_obj_t *version = lv_label_create(lv_scr_act());
    char verBuf[32];
    snprintf(verBuf, sizeof(verBuf), "v%s", WIDGET_OS_VERSION);
    lv_label_set_text(version, verBuf);
    lv_obj_set_style_text_color(version, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_14, 0);
    lv_obj_align(version, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_t *device = lv_label_create(lv_scr_act());
    lv_label_set_text(device, DEVICE_ID);
    lv_obj_set_style_text_color(device, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_text_font(device, &lv_font_montserrat_12, 0);
    lv_obj_align(device, LV_ALIGN_CENTER, 0, 30);
    
    lv_obj_t *webNote = lv_label_create(lv_scr_act());
    lv_label_set_text(webNote, "Fusion Labs Web Ready");
    lv_obj_set_style_text_color(webNote, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_text_font(webNote, &lv_font_montserrat_10, 0);
    lv_obj_align(webNote, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    lv_task_handler();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
    USBSerial.begin(115200);
    delay(500);
    
    USBSerial.println("\n═══════════════════════════════════════════════════════════════════");
    USBSerial.println("  WIDGET OS - FUSION LABS WEB EDITION");
    USBSerial.println("  Board: " DEVICE_ID " (" DEVICE_SCREEN "\" AMOLED)");
    USBSerial.println("═══════════════════════════════════════════════════════════════════\n");
    
    // Initialize I2C
    Wire.begin(IIC_SDA, IIC_SCL);
    
    // Initialize I/O Expander (for 1.8" board)
    if (expander.begin(0x20, &Wire)) {
        USBSerial.println("[BOOT] I/O Expander initialized");
    }
    
    // Initialize display
    gfx->begin();
    gfx->fillScreen(BLACK);
    gfx->Display_Brightness(userData.brightness);
    USBSerial.println("[BOOT] Display initialized");
    
    // Initialize touch
    FT3168->IIC_Device_Reset();
    FT3168->IIC_Init();
    USBSerial.println("[BOOT] Touch initialized");
    
    // Initialize SD card
    initSDCard();
    
    // Initialize RTC
    if (rtc.begin(Wire, PCF85063_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        hasRTC = true;
        USBSerial.println("[BOOT] RTC initialized");
    }
    
    // Initialize IMU
    if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        hasIMU = true;
        qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_250Hz, SensorQMI8658::LPF_MODE_3);
        qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS, SensorQMI8658::GYR_ODR_250Hz, SensorQMI8658::LPF_MODE_3);
        qmi.enableAccelerometer();
        qmi.enableGyroscope();
        USBSerial.println("[BOOT] IMU initialized");
    }
    
    // Initialize PMU
    if (power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        hasPMU = true;
        power.disableTSPinMeasure();
        power.enableBattVoltageMeasure();
        power.enableVbusVoltageMeasure();
        USBSerial.println("[BOOT] PMU initialized");
    }
    
    // Initialize LVGL
    lv_init();
    
    size_t bufferSize = LCD_WIDTH * 50;
    buf1 = (lv_color_t *)heap_caps_malloc(bufferSize * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1) buf1 = (lv_color_t *)malloc(bufferSize * sizeof(lv_color_t));
    
    if (buf1) {
        lv_disp_draw_buf_init(&draw_buf, buf1, NULL, bufferSize);
        
        static lv_disp_drv_t disp_drv;
        lv_disp_drv_init(&disp_drv);
        disp_drv.hor_res = LCD_WIDTH;
        disp_drv.ver_res = LCD_HEIGHT;
        disp_drv.flush_cb = my_disp_flush;
        disp_drv.draw_buf = &draw_buf;
        lv_disp_drv_register(&disp_drv);
    }
    
    // Initialize timing
    lastActivityMs = millis();
    screenOnStartMs = millis();
    batteryStats.sessionStartMs = millis();
    
    // Log boot
    if (hasSD && sdCardInitialized) {
        logToBootLog("Boot completed - Fusion Labs Web Edition");
    }
    
    USBSerial.println("\n═══════════════════════════════════════════════════════════════════");
    USBSerial.println("  WIDGET OS - Boot Complete!");
    USBSerial.println("═══════════════════════════════════════════════════════════════════");
    USBSerial.printf("  SD Card: %s\n", getSDCardStatusString());
    USBSerial.printf("  RTC: %s\n", hasRTC ? "OK" : "Not found");
    USBSerial.printf("  IMU: %s\n", hasIMU ? "OK" : "Not found");
    USBSerial.printf("  PMU: %s\n", hasPMU ? "OK" : "Not found");
    USBSerial.printf("  Custom Faces: %d found\n", numSDFaces);
    USBSerial.println("  Web Serial: Ready (WIDGET_PING to test)");
    USBSerial.println("═══════════════════════════════════════════════════════════════════\n");
    
    showBootScreen();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
    lv_task_handler();
    
    // Handle web serial commands (Fusion Labs integration)
    handleSerialConfig();
    
    // Update RTC time
    static unsigned long lastTimeUpdate = 0;
    if (hasRTC && millis() - lastTimeUpdate > 1000) {
        RTC_DateTime dt = rtc.getDateTime();
        clockHour = dt.getHour();
        clockMinute = dt.getMinute();
        clockSecond = dt.getSecond();
        lastTimeUpdate = millis();
    }
    
    // Check screen timeout
    unsigned long timeout = batterySaverMode ? SCREEN_OFF_TIMEOUT_SAVER_MS : SCREEN_OFF_TIMEOUT_MS;
    if (screenOn && millis() - lastActivityMs > timeout) {
        screenOff();
    }
    
    // Handle touch interrupt
    if (FT3168->IIC_Interrupt_Flag) {
        FT3168->IIC_Interrupt_Flag = false;
        lastActivityMs = millis();
        if (!screenOn) {
            screenOnFunc();
        }
    }
    
    delay(5);
}
