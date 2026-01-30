/**
 * ═══════════════════════════════════════════════════════════════════════════════
 *  Widget OS v1.0.0 (A180)
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
 *  │   └── imported/        (Imported faces)
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
//  WIFI CONFIGURATION (Widget OS compatible)
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
const int maxSubCards[] = {5, 3, 4, 3, 2, 2, 2, 4, 3, 1, 2, 4, 2, 3, 2};  // Settings now has 2 cards (General + SD Health)

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
//  WIDGET OS - SD CARD INITIALIZATION (CRITICAL)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Get human-readable SD card status string
 */
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

/**
 * Create a directory if it doesn't exist
 */
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

/**
 * Create the Widget OS SD card folder structure
 * This is called on first boot or if structure is missing
 */
bool createWidgetOSSDStructure() {
    USBSerial.println("[SD] Creating Widget OS folder structure...");
    
    bool success = true;
    
    // Create main directories
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
    } else {
        USBSerial.println("[SD] ERROR: Failed to create some directories");
    }
    
    return success;
}

/**
 * Create device.json with hardware info
 */
void createDeviceJson() {
    if (SD_MMC.exists(SD_DEVICE_JSON)) {
        USBSerial.println("[SD] device.json already exists");
        return;
    }
    
    USBSerial.println("[SD] Creating device.json...");
    
    File file = SD_MMC.open(SD_DEVICE_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["device_id"] = DEVICE_ID;
        doc["screen"] = DEVICE_SCREEN;
        doc["storage"] = "sd";
        doc["hw_rev"] = DEVICE_HW_REV;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] device.json created");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create device.json");
    }
}

/**
 * Create os.json with version info
 */
void createOSJson() {
    // Always update os.json to reflect current firmware version
    USBSerial.println("[SD] Creating/updating os.json...");
    
    File file = SD_MMC.open(SD_OS_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["name"] = WIDGET_OS_NAME;
        doc["version"] = WIDGET_OS_VERSION;
        doc["build"] = WIDGET_OS_BUILD;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] os.json created");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create os.json");
    }
}

/**
 * Create build.txt with human-readable build info
 */
void createBuildTxt() {
    // Always update build.txt
    USBSerial.println("[SD] Creating/updating build.txt...");
    
    File file = SD_MMC.open(SD_BUILD_TXT, FILE_WRITE);
    if (file) {
        file.printf("%s %s\n", WIDGET_OS_NAME, WIDGET_OS_VERSION);
        file.printf("Board: %s\n", DEVICE_ID);
        file.printf("Display: %s\" AMOLED\n", DEVICE_SCREEN);
        file.printf("Build: %s\n", WIDGET_OS_BUILD);
        file.close();
        USBSerial.println("[SD] build.txt created");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create build.txt");
    }
}

/**
 * Create default user.json config
 */
void createDefaultUserJson() {
    if (SD_MMC.exists(SD_USER_JSON)) {
        USBSerial.println("[SD] user.json already exists - preserving user settings");
        return;
    }
    
    USBSerial.println("[SD] Creating default user.json...");
    
    File file = SD_MMC.open(SD_USER_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["watch_face"] = "Minimal";
        doc["brightness"] = 70;
        doc["vibration"] = true;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] user.json created");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create user.json");
    }
}

/**
 * Create default display.json config
 */
void createDefaultDisplayJson() {
    if (SD_MMC.exists(SD_DISPLAY_JSON)) {
        return;
    }
    
    USBSerial.println("[SD] Creating default display.json...");
    
    File file = SD_MMC.open(SD_DISPLAY_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["always_on"] = false;
        doc["timeout"] = 10;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] display.json created");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create display.json");
    }
}

/**
 * Create default power.json config
 */
void createDefaultPowerJson() {
    if (SD_MMC.exists(SD_POWER_JSON)) {
        return;
    }
    
    USBSerial.println("[SD] Creating default power.json...");
    
    File file = SD_MMC.open(SD_POWER_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["battery_saver_threshold"] = 20;
        doc["auto_brightness"] = false;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] power.json created");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create power.json");
    }
}

/**
 * Create UPDATE/README.txt
 */
void createUpdateReadme() {
    if (SD_MMC.exists(SD_UPDATE_README)) {
        return;
    }
    
    USBSerial.println("[SD] Creating UPDATE/README.txt...");
    
    File file = SD_MMC.open(SD_UPDATE_README, FILE_WRITE);
    if (file) {
        file.println("This folder is reserved for system updates.");
        file.println("Do not place files here unless instructed.");
        file.println("Firmware updates are handled via USB.");
        file.close();
        USBSerial.println("[SD] README.txt created");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create README.txt");
    }
}

/**
 * Create default WiFi config template
 */
void createDefaultWiFiConfig() {
    if (SD_MMC.exists(SD_WIFI_CONFIG)) {
        USBSerial.println("[SD] WiFi config already exists - preserving");
        return;
    }
    
    USBSerial.println("[SD] Creating default WiFi config template...");
    
    File file = SD_MMC.open(SD_WIFI_CONFIG, FILE_WRITE);
    if (file) {
        file.println("WIFI1_SSID=YourHomeNetwork");
        file.println("WIFI1_PASS=YourHomePassword");
        file.println("WIFI2_SSID=YourWorkNetwork");
        file.println("WIFI2_PASS=YourWorkPassword");
        file.println("#WIFI3_SSID=MyPhoneHotspot");
        file.println("#WIFI3_PASS=HotspotPassword");
        file.println("CITY=Perth");
        file.println("COUNTRY=AU");
        file.println("GMT_OFFSET=8");
        file.close();
        USBSerial.println("[SD] WiFi config template created");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create WiFi config");
    }
}

/**
 * Log message to boot.log on SD card
 */
void logToBootLog(const char* message) {
    if (!sdCardInitialized) return;
    
    File file = SD_MMC.open(SD_BOOT_LOG, FILE_APPEND);
    if (file) {
        // Get timestamp if RTC available
        char timestamp[32];
        if (hasRTC) {
            snprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d]", 
                     clockHour, clockMinute, clockSecond);
        } else {
            snprintf(timestamp, sizeof(timestamp), "[%lu]", millis());
        }
        
        file.printf("%s %s\n", timestamp, message);
        file.close();
    }
}

/**
 * Check if /WATCH folder exists and is valid
 * If corrupt, rename to /WATCH_BROKEN
 */
bool validateSDStructure() {
    if (!SD_MMC.exists(SD_ROOT_PATH)) {
        USBSerial.println("[SD] /WATCH not found - will create");
        return false;
    }
    
    // Check if essential subdirectories exist
    if (!SD_MMC.exists(SD_SYSTEM_PATH) || !SD_MMC.exists(SD_CONFIG_PATH)) {
        USBSerial.println("[SD] WARNING: /WATCH structure appears corrupt");
        
        // Rename corrupt folder
        if (SD_MMC.rename(SD_ROOT_PATH, "/WATCH_BROKEN")) {
            USBSerial.println("[SD] Renamed corrupt folder to /WATCH_BROKEN");
        }
        
        return false;
    }
    
    USBSerial.println("[SD] /WATCH structure validated");
    return true;
}

/**
 * Initialize Widget OS SD Card system
 * This is the main SD card initialization function
 */
bool initWidgetOSSDCard() {
    USBSerial.println("\n═══════════════════════════════════════════════════════════════════");
    USBSerial.println("  WIDGET OS - SD Card Initialization");
    USBSerial.println("═══════════════════════════════════════════════════════════════════");
    
    sdCardStatus = SD_STATUS_INIT_IN_PROGRESS;
    
    // Set SD_MMC pins for 1.8" board (1-bit mode)
    // Using pins from pin_config.h: SDMMC_CLK=2, SDMMC_CMD=1, SDMMC_DATA=3
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    
    // Try to mount SD card in 1-bit mode
    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        USBSerial.println("[SD] ERROR: Card mount failed");
        sdCardStatus = SD_STATUS_MOUNT_FAILED;
        sdErrorMessage = "Mount failed - check SD card";
        hasSD = false;
        return false;
    }
    
    // Get card type
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        USBSerial.println("[SD] ERROR: No SD card detected");
        sdCardStatus = SD_STATUS_NOT_PRESENT;
        sdErrorMessage = "No SD card inserted";
        hasSD = false;
        return false;
    }
    
    // Get card info
    switch (cardType) {
        case CARD_MMC: sdCardType = "MMC"; break;
        case CARD_SD: sdCardType = "SD"; break;
        case CARD_SDHC: sdCardType = "SDHC"; break;
        default: sdCardType = "Unknown"; break;
    }
    
    sdCardSizeMB = SD_MMC.cardSize() / (1024 * 1024);
    sdCardUsedMB = SD_MMC.usedBytes() / (1024 * 1024);
    
    USBSerial.printf("[SD] Card Type: %s\n", sdCardType.c_str());
    USBSerial.printf("[SD] Card Size: %llu MB\n", sdCardSizeMB);
    USBSerial.printf("[SD] Total: %llu MB, Used: %llu MB\n", 
                     SD_MMC.totalBytes() / (1024 * 1024),
                     sdCardUsedMB);
    
    // Validate or create folder structure
    if (!validateSDStructure()) {
        if (!createWidgetOSSDStructure()) {
            sdCardStatus = SD_STATUS_CORRUPT;
            sdErrorMessage = "Failed to create folder structure";
            hasSD = false;
            return false;
        }
    }
    
    // Create/update system files
    createDeviceJson();
    createOSJson();
    createBuildTxt();
    
    // Create default config files (preserves existing)
    createDefaultUserJson();
    createDefaultDisplayJson();
    createDefaultPowerJson();
    
    // Create other required files
    createUpdateReadme();
    createDefaultWiFiConfig();
    
    // Clear boot log for this session
    if (SD_MMC.exists(SD_BOOT_LOG)) {
        SD_MMC.remove(SD_BOOT_LOG);
    }
    
    sdCardStatus = SD_STATUS_MOUNTED_OK;
    sdCardInitialized = true;
    hasSD = true;
    
    // Log boot start
    logToBootLog("═══════════════════════════════════════════════════════════════════");
    char bootMsg[64];
    snprintf(bootMsg, sizeof(bootMsg), "%s %s - Boot started", WIDGET_OS_NAME, WIDGET_OS_VERSION);
    logToBootLog(bootMsg);
    snprintf(bootMsg, sizeof(bootMsg), "Device: %s (%s\" AMOLED)", DEVICE_ID, DEVICE_SCREEN);
    logToBootLog(bootMsg);
    
    USBSerial.println("[SD] Widget OS SD card initialized successfully!");
    USBSerial.println("═══════════════════════════════════════════════════════════════════\n");
    
    return true;
}

/**
 * Load user config from SD card
 */
void loadUserConfigFromSD() {
    if (!sdCardInitialized) return;
    
    File file = SD_MMC.open(SD_USER_JSON, FILE_READ);
    if (file) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (!error) {
            if (doc.containsKey("brightness")) {
                userData.brightness = doc["brightness"].as<int>();
            }
            // Note: watch_face is handled by firmware defaults
            USBSerial.println("[SD] User config loaded from SD");
        }
    }
}

/**
 * Save user config to SD card
 */
void saveUserConfigToSD() {
    if (!sdCardInitialized) return;
    
    File file = SD_MMC.open(SD_USER_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<512> doc;
        doc["watch_face"] = "Minimal";  // Current face name
        doc["brightness"] = userData.brightness;
        doc["vibration"] = true;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] User config saved to SD");
        
        // Update last backup time
        lastBackupTimeMs = millis();
        hasLastBackup = true;
        
        // Update used space
        sdCardUsedMB = SD_MMC.usedBytes() / (1024 * 1024);
    }
}

/**
 * Load display config from SD card
 */
void loadDisplayConfigFromSD() {
    if (!sdCardInitialized) return;
    
    File file = SD_MMC.open(SD_DISPLAY_JSON, FILE_READ);
    if (file) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (!error) {
            if (doc.containsKey("timeout")) {
                userData.screenTimeout = doc["timeout"].as<int>();
            }
            USBSerial.println("[SD] Display config loaded from SD");
        }
    }
}

/**
 * Load WiFi config from SD card
 */
void loadWiFiConfigFromSD() {
    if (!sdCardInitialized) return;
    
    File file = SD_MMC.open(SD_WIFI_CONFIG, FILE_READ);
    if (!file) {
        USBSerial.println("[SD] No WiFi config found - using defaults");
        return;
    }
    
    USBSerial.println("[SD] Loading WiFi config from SD...");
    numWifiNetworks = 0;
    
    while (file.available() && numWifiNetworks < MAX_WIFI_NETWORKS) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        // Skip comments and empty lines
        if (line.length() == 0 || line.startsWith("#")) continue;
        
        int eqPos = line.indexOf('=');
        if (eqPos > 0) {
            String key = line.substring(0, eqPos);
            String value = line.substring(eqPos + 1);
            value.trim();
            
            if (key.startsWith("WIFI") && key.endsWith("_SSID")) {
                int netNum = key.charAt(4) - '1';
                if (netNum >= 0 && netNum < MAX_WIFI_NETWORKS) {
                    strncpy(wifiNetworks[netNum].ssid, value.c_str(), 63);
                    wifiNetworks[netNum].valid = true;
                    wifiNetworks[netNum].isOpen = false;
                    if (netNum >= numWifiNetworks) numWifiNetworks = netNum + 1;
                }
            } else if (key.startsWith("WIFI") && key.endsWith("_PASS")) {
                int netNum = key.charAt(4) - '1';
                if (netNum >= 0 && netNum < MAX_WIFI_NETWORKS) {
                    strncpy(wifiNetworks[netNum].password, value.c_str(), 63);
                    if (value.length() == 0) {
                        wifiNetworks[netNum].isOpen = true;
                    }
                }
            } else if (key == "CITY") {
                strncpy(weatherCity, value.c_str(), 63);
            } else if (key == "COUNTRY") {
                strncpy(weatherCountry, value.c_str(), 7);
            } else if (key == "GMT_OFFSET") {
                gmtOffsetSec = value.toInt() * 3600;
            }
        }
    }
    
    file.close();
    wifiConfigFromSD = true;
    
    USBSerial.printf("[SD] Loaded %d WiFi networks\n", numWifiNetworks);
    USBSerial.printf("[SD] Location: %s, %s (GMT%+ld)\n", weatherCity, weatherCountry, gmtOffsetSec/3600);
    
    logToBootLog("WiFi config loaded from SD");
}

/**
 * Scan for custom watch faces on SD card
 */
void scanSDFaces() {
    if (!sdCardInitialized) return;
    
    numSDFaces = 0;
    USBSerial.println("[SD] Scanning for custom watch faces...");
    
    // Scan custom faces
    File customDir = SD_MMC.open(SD_FACES_CUSTOM_PATH);
    if (customDir && customDir.isDirectory()) {
        File faceFolder = customDir.openNextFile();
        while (faceFolder && numSDFaces < MAX_SD_FACES) {
            if (faceFolder.isDirectory()) {
                // Check for face.json
                String facePath = String(SD_FACES_CUSTOM_PATH) + "/" + faceFolder.name() + "/face.json";
                if (SD_MMC.exists(facePath.c_str())) {
                    File faceJson = SD_MMC.open(facePath.c_str(), FILE_READ);
                    if (faceJson) {
                        StaticJsonDocument<256> doc;
                        if (deserializeJson(doc, faceJson) == DeserializationError::Ok) {
                            strncpy(sdFaces[numSDFaces].name, doc["name"] | faceFolder.name(), 31);
                            strncpy(sdFaces[numSDFaces].author, doc["author"] | "Unknown", 31);
                            strncpy(sdFaces[numSDFaces].version, doc["version"] | "1.0", 15);
                            snprintf(sdFaces[numSDFaces].path, 63, "%s/%s", SD_FACES_CUSTOM_PATH, faceFolder.name());
                            sdFaces[numSDFaces].valid = true;
                            
                            USBSerial.printf("[SD] Found face: %s by %s\n", 
                                           sdFaces[numSDFaces].name, 
                                           sdFaces[numSDFaces].author);
                            numSDFaces++;
                        }
                        faceJson.close();
                    }
                }
            }
            faceFolder = customDir.openNextFile();
        }
        customDir.close();
    }
    
    // Also scan imported faces
    File importedDir = SD_MMC.open(SD_FACES_IMPORTED_PATH);
    if (importedDir && importedDir.isDirectory()) {
        File faceFolder = importedDir.openNextFile();
        while (faceFolder && numSDFaces < MAX_SD_FACES) {
            if (faceFolder.isDirectory()) {
                String facePath = String(SD_FACES_IMPORTED_PATH) + "/" + faceFolder.name() + "/face.json";
                if (SD_MMC.exists(facePath.c_str())) {
                    File faceJson = SD_MMC.open(facePath.c_str(), FILE_READ);
                    if (faceJson) {
                        StaticJsonDocument<256> doc;
                        if (deserializeJson(doc, faceJson) == DeserializationError::Ok) {
                            strncpy(sdFaces[numSDFaces].name, doc["name"] | faceFolder.name(), 31);
                            strncpy(sdFaces[numSDFaces].author, doc["author"] | "Unknown", 31);
                            strncpy(sdFaces[numSDFaces].version, doc["version"] | "1.0", 15);
                            snprintf(sdFaces[numSDFaces].path, 63, "%s/%s", SD_FACES_IMPORTED_PATH, faceFolder.name());
                            sdFaces[numSDFaces].valid = true;
                            
                            USBSerial.printf("[SD] Found imported face: %s\n", sdFaces[numSDFaces].name);
                            numSDFaces++;
                        }
                        faceJson.close();
                    }
                }
            }
            faceFolder = importedDir.openNextFile();
        }
        importedDir.close();
    }
    
    USBSerial.printf("[SD] Total custom faces found: %d\n", numSDFaces);
    
    char logMsg[64];
    snprintf(logMsg, sizeof(logMsg), "Found %d custom watch faces", numSDFaces);
    logToBootLog(logMsg);
}

/**
 * List SD card directory contents (for debugging)
 */
void listSDDirectory(const char* path, int depth) {
    if (!sdCardInitialized || depth < 0) return;
    
    File dir = SD_MMC.open(path);
    if (!dir || !dir.isDirectory()) return;
    
    File entry = dir.openNextFile();
    while (entry) {
        for (int i = 0; i < (3 - depth); i++) USBSerial.print("  ");
        
        if (entry.isDirectory()) {
            USBSerial.printf("📁 %s/\n", entry.name());
            if (depth > 0) {
                String subPath = String(path) + "/" + entry.name();
                listSDDirectory(subPath.c_str(), depth - 1);
            }
        } else {
            USBSerial.printf("📄 %s (%d bytes)\n", entry.name(), entry.size());
        }
        entry = dir.openNextFile();
    }
    dir.close();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SCREEN CONTROL
// ═══════════════════════════════════════════════════════════════════════════════
void screenOff() {
    if (!screenOn) return;
    batteryStats.screenOnTimeMs += (millis() - screenOnStartMs);
    screenOffStartMs = millis();
    screenOn = false;
    gfx->setBrightness(0);
}

void screenOnFunc() {
    if (screenOn) return;
    batteryStats.screenOffTimeMs += (millis() - screenOffStartMs);
    screenOnStartMs = millis();
    screenOn = true;
    lastActivityMs = millis();
    gfx->setBrightness(batterySaverMode ? 100 : userData.brightness);
}

void shutdownDevice() {
    // Save user data to SD before shutdown
    if (hasSD && sdCardInitialized) {
        saveUserConfigToSD();
        logToBootLog("Shutdown initiated - user data saved");
    }
    
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Shutting down...");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);

    lv_task_handler();
    delay(1000);

    gfx->setBrightness(0);
    if (hasPMU) power.shutdown();
    esp_deep_sleep_start();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETUP - Widget OS Initialization
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
    // Initialize USB Serial
    USBSerial.begin(115200);
    delay(500);
    
    USBSerial.println("\n");
    USBSerial.println("═══════════════════════════════════════════════════════════════════");
    USBSerial.printf("  %s %s\n", WIDGET_OS_NAME, WIDGET_OS_VERSION);
    USBSerial.printf("  Device: %s (%s\" AMOLED)\n", DEVICE_ID, DEVICE_SCREEN);
    USBSerial.println("═══════════════════════════════════════════════════════════════════");
    USBSerial.println("");
    
    // Initialize I2C
    Wire.begin(IIC_SDA, IIC_SCL);
    USBSerial.println("[BOOT] I2C initialized");
    
    // Initialize I/O expander (1.8" board specific)
    if (!expander.begin(0x20)) {
        USBSerial.println("[BOOT] WARNING: XCA9554 I/O expander not found");
    } else {
        USBSerial.println("[BOOT] I/O expander initialized");
        expander.pinMode(7, OUTPUT);
        expander.digitalWrite(7, HIGH);  // Enable LCD power
    }
    
    // Initialize display
    delay(100);  // Wait for power to stabilize
    if (!gfx->begin()) {
        USBSerial.println("[BOOT] ERROR: Display init failed!");
    } else {
        USBSerial.println("[BOOT] Display initialized");
    }
    gfx->fillScreen(RGB565_BLACK);
    gfx->setBrightness(200);
    
    // Initialize touch controller
    int touchRetries = 0;
    while (FT3168->begin() == false && touchRetries < 5) {
        USBSerial.println("[BOOT] Touch init retry...");
        delay(500);
        touchRetries++;
    }
    if (touchRetries < 5) {
        USBSerial.println("[BOOT] Touch controller initialized");
        FT3168->IIC_Write_Device_State(FT3168->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                       FT3168->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
    } else {
        USBSerial.println("[BOOT] WARNING: Touch init failed");
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    //  WIDGET OS - SD CARD INITIALIZATION (CRITICAL)
    // ═══════════════════════════════════════════════════════════════════════════
    USBSerial.println("\n[BOOT] Initializing SD card...");
    
    if (initWidgetOSSDCard()) {
        USBSerial.println("[BOOT] SD card initialized successfully");
        
        // Load configs from SD
        loadUserConfigFromSD();
        loadDisplayConfigFromSD();
        loadWiFiConfigFromSD();
        
        // Scan for custom faces
        scanSDFaces();
        
        // List SD contents for debugging
        USBSerial.println("\n[BOOT] SD Card Contents:");
        listSDDirectory(SD_ROOT_PATH, 2);
    } else {
        USBSerial.println("[BOOT] SD card not available - using defaults");
        // OS continues with internal defaults (as per spec)
    }
    
    // Initialize RTC
    if (rtc.begin()) {
        hasRTC = true;
        USBSerial.println("[BOOT] RTC initialized");
        
        RTC_DateTime dt = rtc.getDateTime();
        clockHour = dt.getHour();
        clockMinute = dt.getMinute();
        clockSecond = dt.getSecond();
    } else {
        USBSerial.println("[BOOT] WARNING: RTC not found");
    }
    
    // Initialize IMU
    if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        hasIMU = true;
        USBSerial.println("[BOOT] IMU initialized");
        qmi.configAccelerometer(
            SensorQMI8658::ACC_RANGE_4G,
            SensorQMI8658::ACC_ODR_250Hz,
            SensorQMI8658::LPF_MODE_0
        );
        qmi.configGyroscope(
            SensorQMI8658::GYR_RANGE_512DPS,
            SensorQMI8658::GYR_ODR_250Hz,
            SensorQMI8658::LPF_MODE_3
        );
        qmi.enableAccelerometer();
        qmi.enableGyroscope();
    } else {
        USBSerial.println("[BOOT] WARNING: IMU not found");
    }
    
    // Initialize PMU
    if (power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        hasPMU = true;
        USBSerial.println("[BOOT] PMU initialized");
        power.disableTSPinMeasure();
        power.enableBattVoltageMeasure();
        power.enableVbusVoltageMeasure();
        power.enableSystemVoltageMeasure();
    } else {
        USBSerial.println("[BOOT] WARNING: PMU not found");
    }
    
    // Initialize LVGL
    lv_init();
    
    size_t bufferSize = LCD_WIDTH * 50;
    buf1 = (lv_color_t *)heap_caps_malloc(bufferSize * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!buf1) {
        buf1 = (lv_color_t *)malloc(bufferSize * sizeof(lv_color_t));
    }
    
    if (buf1) {
        lv_disp_draw_buf_init(&draw_buf, buf1, NULL, bufferSize);
        
        static lv_disp_drv_t disp_drv;
        lv_disp_drv_init(&disp_drv);
        disp_drv.hor_res = LCD_WIDTH;
        disp_drv.ver_res = LCD_HEIGHT;
        disp_drv.flush_cb = my_disp_flush;
        disp_drv.draw_buf = &draw_buf;
        lv_disp_drv_register(&disp_drv);
        
        USBSerial.println("[BOOT] LVGL initialized");
    } else {
        USBSerial.println("[BOOT] ERROR: LVGL buffer allocation failed");
    }
    
    // Initialize touch input for LVGL
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    // Note: Add touch read callback here
    lv_indev_drv_register(&indev_drv);
    
    // Load preferences from SPIFFS (backup)
    prefs.begin("watchdata", false);
    
    // Initialize timing
    lastActivityMs = millis();
    screenOnStartMs = millis();
    batteryStats.sessionStartMs = millis();
    
    // Log boot completion
    if (hasSD && sdCardInitialized) {
        logToBootLog("Boot completed successfully");
        logToBootLog("═══════════════════════════════════════════════════════════════════");
    }
    
    USBSerial.println("\n═══════════════════════════════════════════════════════════════════");
    USBSerial.println("  WIDGET OS - Boot Complete!");
    USBSerial.println("═══════════════════════════════════════════════════════════════════");
    USBSerial.printf("  SD Card: %s\n", getSDCardStatusString());
    USBSerial.printf("  RTC: %s\n", hasRTC ? "OK" : "Not found");
    USBSerial.printf("  IMU: %s\n", hasIMU ? "OK" : "Not found");
    USBSerial.printf("  PMU: %s\n", hasPMU ? "OK" : "Not found");
    USBSerial.printf("  Custom Faces: %d found\n", numSDFaces);
    USBSerial.println("═══════════════════════════════════════════════════════════════════\n");
    
    // Show boot screen
    showBootScreen();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SD CARD HEALTH WIDGET (Settings Category - Card 1)
// ═══════════════════════════════════════════════════════════════════════════════

// LVGL objects for backup UI
lv_obj_t *backupBtn = NULL;
lv_obj_t *backupProgressBar = NULL;
lv_obj_t *backupStatusLabel = NULL;
lv_obj_t *sdHealthScreen = NULL;
lv_obj_t *autoBackupSwitch = NULL;
lv_obj_t *autoBackupLabel = NULL;

/**
 * Get formatted last backup time string
 */
String getLastBackupTimeString() {
    if (!hasLastBackup) {
        return "Never";
    }
    
    unsigned long elapsedMs = millis() - lastBackupTimeMs;
    unsigned long elapsedSec = elapsedMs / 1000;
    unsigned long elapsedMin = elapsedSec / 60;
    unsigned long elapsedHour = elapsedMin / 60;
    
    if (elapsedSec < 60) {
        return "Just now";
    } else if (elapsedMin < 60) {
        return String(elapsedMin) + " min ago";
    } else if (elapsedHour < 24) {
        return String(elapsedHour) + " hr ago";
    } else {
        return String(elapsedHour / 24) + " day(s) ago";
    }
}

/**
 * Get storage usage percentage
 */
uint8_t getStorageUsagePercent() {
    if (sdCardSizeMB == 0) return 0;
    return (uint8_t)((sdCardUsedMB * 100) / sdCardSizeMB);
}

/**
 * Perform full backup with progress tracking
 */
void performBackupWithProgress() {
    if (!hasSD || !sdCardInitialized || backupInProgress) return;
    
    backupInProgress = true;
    backupProgress = 0;
    showingBackupComplete = false;
    
    USBSerial.println("[BACKUP] Starting backup...");
    logToBootLog("Manual backup initiated");
    
    // Step 1: Save user config (40%)
    backupProgress = 10;
    delay(100);  // Small delay for visual feedback
    
    File file = SD_MMC.open(SD_USER_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<512> doc;
        doc["watch_face"] = "Minimal";
        doc["brightness"] = userData.brightness;
        doc["vibration"] = true;
        doc["step_goal"] = userData.dailyGoal;
        doc["steps"] = userData.steps;
        serializeJsonPretty(doc, file);
        file.close();
    }
    backupProgress = 40;
    
    // Step 2: Save display config (60%)
    delay(100);
    File dispFile = SD_MMC.open(SD_DISPLAY_JSON, FILE_WRITE);
    if (dispFile) {
        StaticJsonDocument<256> doc;
        doc["always_on"] = false;
        doc["timeout"] = userData.screenTimeout;
        doc["theme"] = userData.themeIndex;
        serializeJsonPretty(doc, dispFile);
        dispFile.close();
    }
    backupProgress = 60;
    
    // Step 3: Save power config (80%)
    delay(100);
    File pwrFile = SD_MMC.open(SD_POWER_JSON, FILE_WRITE);
    if (pwrFile) {
        StaticJsonDocument<256> doc;
        doc["battery_saver_threshold"] = LOW_BATTERY_WARNING;
        doc["auto_brightness"] = false;
        doc["battery_saver_auto"] = batterySaverAutoEnabled;
        serializeJsonPretty(doc, pwrFile);
        pwrFile.close();
    }
    backupProgress = 80;
    
    // Step 4: Update stats and finalize (100%)
    delay(100);
    sdCardUsedMB = SD_MMC.usedBytes() / (1024 * 1024);
    lastBackupTimeMs = millis();
    hasLastBackup = true;
    lastAutoBackupMs = millis();
    
    backupProgress = 100;
    backupInProgress = false;
    showingBackupComplete = true;
    backupCompleteShownMs = millis();
    
    USBSerial.println("[BACKUP] Backup completed successfully!");
    logToBootLog("Backup completed successfully");
}

/**
 * Check and perform auto backup if needed
 */
void checkAutoBackup() {
    if (!hasSD || !sdCardInitialized || !autoBackupEnabled) return;
    if (backupInProgress) return;
    
    // Check if 24 hours have passed since last backup
    if (millis() - lastAutoBackupMs >= AUTO_BACKUP_INTERVAL_MS) {
        USBSerial.println("[BACKUP] Auto backup triggered (24hr interval)");
        logToBootLog("Auto backup triggered");
        performBackupWithProgress();
    }
}

/**
 * Show SD Card Health Widget (Settings > SD Card Health)
 * Displays: Connection status, Storage used, Face count, Last backup, Backup button
 */
void showSDCardHealthWidget() {
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    sdHealthScreen = lv_scr_act();
    
    // Title
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "SD Card Health");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    
    // Connection Status
    lv_obj_t *connLabel = lv_label_create(lv_scr_act());
    char connBuf[48];
    if (hasSD && sdCardInitialized) {
        snprintf(connBuf, sizeof(connBuf), "● Connected (%s)", sdCardType.c_str());
        lv_label_set_text(connLabel, connBuf);
        lv_obj_set_style_text_color(connLabel, lv_color_hex(0x30D158), 0);
    } else {
        snprintf(connBuf, sizeof(connBuf), "○ %s", getSDCardStatusString());
        lv_label_set_text(connLabel, connBuf);
        lv_obj_set_style_text_color(connLabel, lv_color_hex(0xFF453A), 0);
    }
    lv_obj_set_style_text_font(connLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(connLabel, LV_ALIGN_TOP_MID, 0, 45);
    
    // Storage Bar Background
    lv_obj_t *storageBarBg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(storageBarBg, 180, 16);
    lv_obj_set_style_bg_color(storageBarBg, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(storageBarBg, 8, 0);
    lv_obj_set_style_border_width(storageBarBg, 0, 0);
    lv_obj_set_style_pad_all(storageBarBg, 0, 0);
    lv_obj_align(storageBarBg, LV_ALIGN_TOP_MID, 0, 70);
    
    // Storage Bar Fill
    if (hasSD && sdCardSizeMB > 0) {
        uint8_t usagePercent = getStorageUsagePercent();
        int fillWidth = (usagePercent * 176) / 100;
        if (fillWidth < 4) fillWidth = 4;
        
        lv_obj_t *storageBarFill = lv_obj_create(storageBarBg);
        lv_obj_set_size(storageBarFill, fillWidth, 12);
        lv_obj_set_style_radius(storageBarFill, 6, 0);
        lv_obj_set_style_border_width(storageBarFill, 0, 0);
        lv_obj_align(storageBarFill, LV_ALIGN_LEFT_MID, 2, 0);
        
        if (usagePercent < 70) {
            lv_obj_set_style_bg_color(storageBarFill, lv_color_hex(0x30D158), 0);
        } else if (usagePercent < 90) {
            lv_obj_set_style_bg_color(storageBarFill, lv_color_hex(0xFF9F0A), 0);
        } else {
            lv_obj_set_style_bg_color(storageBarFill, lv_color_hex(0xFF453A), 0);
        }
    }
    
    // Storage Text
    lv_obj_t *storageLabel = lv_label_create(lv_scr_act());
    char storageBuf[48];
    if (hasSD) {
        snprintf(storageBuf, sizeof(storageBuf), "%llu / %llu MB (%d%%)", 
                 sdCardUsedMB, sdCardSizeMB, getStorageUsagePercent());
    } else {
        snprintf(storageBuf, sizeof(storageBuf), "N/A");
    }
    lv_label_set_text(storageLabel, storageBuf);
    lv_obj_set_style_text_color(storageLabel, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(storageLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(storageLabel, LV_ALIGN_TOP_MID, 0, 90);
    
    // Face Count
    lv_obj_t *faceLabel = lv_label_create(lv_scr_act());
    char faceBuf[32];
    snprintf(faceBuf, sizeof(faceBuf), "Faces: %d", numSDFaces);
    lv_label_set_text(faceLabel, faceBuf);
    lv_obj_set_style_text_color(faceLabel, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_text_font(faceLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(faceLabel, LV_ALIGN_TOP_MID, 0, 115);
    
    // Last Backup
    lv_obj_t *backupTimeLabel = lv_label_create(lv_scr_act());
    char backupTimeBuf[48];
    String backupTime = getLastBackupTimeString();
    snprintf(backupTimeBuf, sizeof(backupTimeBuf), "Last Backup: %s", backupTime.c_str());
    lv_label_set_text(backupTimeLabel, backupTimeBuf);
    lv_obj_set_style_text_color(backupTimeLabel, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(backupTimeLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(backupTimeLabel, LV_ALIGN_TOP_MID, 0, 140);
    
    // ═══════════════════════════════════════════════════════════════════════════
    // AUTO BACKUP TOGGLE SWITCH
    // ═══════════════════════════════════════════════════════════════════════════
    // Auto Backup Label
    autoBackupLabel = lv_label_create(lv_scr_act());
    lv_label_set_text(autoBackupLabel, "Auto Backup");
    lv_obj_set_style_text_color(autoBackupLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(autoBackupLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(autoBackupLabel, LV_ALIGN_TOP_MID, -40, 165);
    
    // Toggle Switch
    autoBackupSwitch = lv_switch_create(lv_scr_act());
    lv_obj_set_size(autoBackupSwitch, 50, 26);
    lv_obj_align(autoBackupSwitch, LV_ALIGN_TOP_MID, 50, 162);
    
    // Style the switch
    lv_obj_set_style_bg_color(autoBackupSwitch, lv_color_hex(0x48484A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(autoBackupSwitch, lv_color_hex(0x30D158), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(autoBackupSwitch, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_radius(autoBackupSwitch, 13, LV_PART_MAIN);
    lv_obj_set_style_radius(autoBackupSwitch, 13, LV_PART_INDICATOR);
    lv_obj_set_style_radius(autoBackupSwitch, 11, LV_PART_KNOB);
    lv_obj_set_style_pad_all(autoBackupSwitch, -2, LV_PART_KNOB);
    
    // Set initial state
    if (autoBackupEnabled) {
        lv_obj_add_state(autoBackupSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(autoBackupSwitch, LV_STATE_CHECKED);
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // BACKUP NOW BUTTON
    // ═══════════════════════════════════════════════════════════════════════════
    backupBtn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(backupBtn, 140, 40);
    lv_obj_align(backupBtn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(backupBtn, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_radius(backupBtn, 20, 0);
    lv_obj_set_style_shadow_width(backupBtn, 0, 0);
    
    lv_obj_t *btnLabel = lv_label_create(backupBtn);
    lv_label_set_text(btnLabel, "Backup Now");
    lv_obj_set_style_text_color(btnLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btnLabel, &lv_font_montserrat_14, 0);
    lv_obj_center(btnLabel);
    
    // Disable button if no SD card
    if (!hasSD || !sdCardInitialized) {
        lv_obj_set_style_bg_color(backupBtn, lv_color_hex(0x48484A), 0);
        lv_obj_add_state(backupBtn, LV_STATE_DISABLED);
    }
    
    // ═══════════════════════════════════════════════════════════════════════════
    // BACKUP PROGRESS BAR (Hidden initially)
    // ═══════════════════════════════════════════════════════════════════════════
    backupProgressBar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(backupProgressBar, 160, 12);
    lv_obj_align(backupProgressBar, LV_ALIGN_CENTER, 0, 110);
    lv_bar_set_range(backupProgressBar, 0, 100);
    lv_bar_set_value(backupProgressBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(backupProgressBar, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(backupProgressBar, lv_color_hex(0x30D158), LV_PART_INDICATOR);
    lv_obj_set_style_radius(backupProgressBar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(backupProgressBar, 6, LV_PART_INDICATOR);
    lv_obj_add_flag(backupProgressBar, LV_OBJ_FLAG_HIDDEN);
    
    // Backup Status Label
    backupStatusLabel = lv_label_create(lv_scr_act());
    lv_label_set_text(backupStatusLabel, "");
    lv_obj_set_style_text_color(backupStatusLabel, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_text_font(backupStatusLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(backupStatusLabel, LV_ALIGN_CENTER, 0, 130);
    lv_obj_add_flag(backupStatusLabel, LV_OBJ_FLAG_HIDDEN);
    
    // Footer
    lv_obj_t *hintLabel = lv_label_create(lv_scr_act());
    lv_label_set_text(hintLabel, "Settings > SD Card");
    lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x48484A), 0);
    lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    lv_task_handler();
}

/**
 * Start backup with visual progress (call when button pressed)
 */
void startBackupWithUI() {
    if (!hasSD || !sdCardInitialized || backupInProgress) return;
    
    // Show progress bar, hide button
    lv_obj_add_flag(backupBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(backupProgressBar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(backupStatusLabel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(backupStatusLabel, "Backing up...");
    
    lv_task_handler();
    
    // Perform backup
    performBackupWithProgress();
}

/**
 * Update backup progress UI (call from loop when backup in progress)
 */
void updateBackupProgressUI() {
    if (backupProgressBar == NULL) return;
    
    if (backupInProgress) {
        lv_bar_set_value(backupProgressBar, backupProgress, LV_ANIM_ON);
        
        if (backupProgress < 40) {
            lv_label_set_text(backupStatusLabel, "Saving user data...");
        } else if (backupProgress < 60) {
            lv_label_set_text(backupStatusLabel, "Saving display config...");
        } else if (backupProgress < 80) {
            lv_label_set_text(backupStatusLabel, "Saving power config...");
        } else {
            lv_label_set_text(backupStatusLabel, "Finalizing...");
        }
    }
    
    if (showingBackupComplete) {
        lv_bar_set_value(backupProgressBar, 100, LV_ANIM_OFF);
        lv_label_set_text(backupStatusLabel, "✓ Backup Complete!");
        lv_obj_set_style_text_color(backupStatusLabel, lv_color_hex(0x30D158), 0);
        
        // Hide after 2 seconds and show button again
        if (millis() - backupCompleteShownMs > 2000) {
            showingBackupComplete = false;
            lv_obj_add_flag(backupProgressBar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(backupStatusLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(backupBtn, LV_OBJ_FLAG_HIDDEN);
            
            // Refresh the widget to show updated backup time
            showSDCardHealthWidget();
        }
    }
}

/**
 * Refresh SD card health data
 */
void refreshSDCardHealth() {
    if (hasSD && sdCardInitialized) {
        sdCardUsedMB = SD_MMC.usedBytes() / (1024 * 1024);
    }
}

/**
 * Handle touch on SD Health Widget (for backup button and toggle)
 */
void handleSDHealthTouch(int16_t touchX, int16_t touchY) {
    if (backupInProgress || showingBackupComplete) return;
    
    int centerX = LCD_WIDTH / 2;
    int centerY = LCD_HEIGHT / 2;
    
    // Check if touch is on backup button area (center, y around 60 offset from center)
    int btnCenterY = centerY + 60;
    if (touchX > centerX - 70 && touchX < centerX + 70 &&
        touchY > btnCenterY - 20 && touchY < btnCenterY + 20) {
        startBackupWithUI();
        return;
    }
    
    // Check if touch is on auto backup toggle area (y around 165 from top)
    int toggleY = 165;
    int toggleX = centerX + 50;
    if (touchX > toggleX - 30 && touchX < toggleX + 30 &&
        touchY > toggleY - 15 && touchY < toggleY + 15) {
        // Toggle auto backup
        autoBackupEnabled = !autoBackupEnabled;
        
        // Update switch visual state
        if (autoBackupSwitch != NULL) {
            if (autoBackupEnabled) {
                lv_obj_add_state(autoBackupSwitch, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(autoBackupSwitch, LV_STATE_CHECKED);
            }
        }
        
        USBSerial.printf("[BACKUP] Auto backup %s\n", autoBackupEnabled ? "enabled" : "disabled");
        logToBootLog(autoBackupEnabled ? "Auto backup enabled" : "Auto backup disabled");
    }
}

/**
 * Show Widget OS boot screen
 */
void showBootScreen() {
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    
    // Widget OS logo/text
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, WIDGET_OS_NAME);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);
    
    // Version
    lv_obj_t *version = lv_label_create(lv_scr_act());
    char verBuf[32];
    snprintf(verBuf, sizeof(verBuf), "v%s", WIDGET_OS_VERSION);
    lv_label_set_text(version, verBuf);
    lv_obj_set_style_text_color(version, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_14, 0);
    lv_obj_align(version, LV_ALIGN_CENTER, 0, 0);
    
    // Device ID
    lv_obj_t *device = lv_label_create(lv_scr_act());
    lv_label_set_text(device, DEVICE_ID);
    lv_obj_set_style_text_color(device, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_text_font(device, &lv_font_montserrat_12, 0);
    lv_obj_align(device, LV_ALIGN_CENTER, 0, 30);
    
    // SD Status
    lv_obj_t *sdStatus = lv_label_create(lv_scr_act());
    char sdBuf[64];
    if (hasSD) {
        snprintf(sdBuf, sizeof(sdBuf), "SD: %s (%llu MB)", sdCardType.c_str(), sdCardSizeMB);
    } else {
        snprintf(sdBuf, sizeof(sdBuf), "SD: %s", getSDCardStatusString());
    }
    lv_label_set_text(sdStatus, sdBuf);
    lv_obj_set_style_text_color(sdStatus, hasSD ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(sdStatus, &lv_font_montserrat_10, 0);
    lv_obj_align(sdStatus, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    lv_task_handler();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
    lv_task_handler();
    
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
        
        // Get touch coordinates for SD Health widget button handling
        if (currentCategory == CAT_SETTINGS && currentSubCard == 1) {
            int16_t touchX = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
            int16_t touchY = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
            handleSDHealthTouch(touchX, touchY);
        }
    }
    
    // Auto backup check (every loop iteration, function handles timing internally)
    checkAutoBackup();
    
    // Update backup progress UI if on SD Health screen
    if (currentCategory == CAT_SETTINGS && currentSubCard == 1) {
        updateBackupProgressUI();
    }
    
    delay(5);
}
