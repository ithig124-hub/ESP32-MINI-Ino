/**
 *
 *  S3 MiniOS v4.3 - WIDGET OS SD CARD STORAGE EDITION
 *  ESP32-S3-Touch-AMOLED-2.06" Smartwatch Firmware
 *
 *  === WIDGET OS SD CARD STORAGE SPEC v1.1 ===
 *  
 *  SD Card Structure (Auto-Created on First Boot):
 *  /WATCH/
 *  ├── SYSTEM/
 *  │   ├── device.json      (Device info: WOS-206A)
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
 *  Hardware: Waveshare ESP32-S3-Touch-AMOLED-2.06
 *     Display: CO5300 QSPI AMOLED 410x502
 *     Touch: FT3168
 *     IMU: QMI8658
 *     RTC: PCF85063
 *     PMU: AXP2101
 *     SD: SD_MMC 1-bit mode
 *
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
#define DEVICE_ID           "WOS-206A"
#define DEVICE_SCREEN       "2.06"
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
String sdCardType = "Unknown";

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
//  HARDWARE OBJECTS
// ═══════════════════════════════════════════════════════════════════════════════
SensorQMI8658 qmi;
SensorPCF85063 rtc;
XPowersPMU power;
IMUdata acc, gyr;
Preferences prefs;

// Hardware objects initialized with pin_config.h values
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, LCD_COL_OFFSET1, LCD_ROW_OFFSET1, LCD_COL_OFFSET2, LCD_ROW_OFFSET2);
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

// SD Card pins from pin_config.h:
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
const int maxSubCards[] = {5, 3, 4, 3, 2, 2, 2, 4, 3, 1, 2, 4, 1, 3, 2};

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
//  USER DATA (Persistent) - Widget OS Compatible
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
bool showingNotification = false;
unsigned long notificationStartMs = 0;
const unsigned long NOTIFICATION_DURATION = 3000;
lv_obj_t *notificationOverlay = NULL;

// System buttons (BOOT_BUTTON defined in pin_config.h)
// PWR_BUTTON not available on 2.06" board - uses touch gestures for power control

const unsigned long POWER_BUTTON_SHUTDOWN_MS = 10000;
const unsigned long POWER_BUTTON_DEBOUNCE_MS = 150;
const unsigned long POWER_BUTTON_MIN_TAP_MS = 100;
const unsigned long POWER_BUTTON_CONFIRM_SAMPLES = 20;
const unsigned long NTP_RESYNC_INTERVAL_MS = 3600000;

volatile bool navigationLocked = false;
unsigned long lastNavigationMs = 0;
const unsigned long NAVIGATION_COOLDOWN_MS = 150;

bool powerButtonPressed = false;
unsigned long powerButtonPressStartMs = 0;
bool bootButtonPressed = false;
unsigned long bootButtonPressStartMs = 0;

bool showingShutdownProgress = false;
unsigned long shutdownProgressStartMs = 0;
lv_obj_t *shutdownPopup = NULL;
lv_obj_t *shutdownProgressArc = NULL;
lv_obj_t *shutdownProgressLabel = NULL;

uint8_t clockHour = 10, clockMinute = 30, clockSecond = 0;
uint8_t currentDay = 3;

float weatherTemp = 24.0;
String weatherDesc = "Sunny";
float weatherHigh = 28.0;
float weatherLow = 18.0;

// Hardware flags
bool hasIMU = false, hasRTC = false, hasPMU = false, hasSD = false;

// ═══════════════════════════════════════════════════════════════════════════════
//  WIDGET OS - SD CARD INITIALIZATION & FOLDER STRUCTURE
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * Log message to boot.log file on SD card
 */
void logToBootLog(const char* message) {
    if (!sdCardInitialized) return;
    
    File logFile = SD_MMC.open(SD_BOOT_LOG, FILE_APPEND);
    if (logFile) {
        // Get timestamp if RTC available
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

/**
 * Create a directory if it doesn't exist
 */
bool createDirectoryIfNotExists(const char* path) {
    if (SD_MMC.exists(path)) {
        USBSerial.printf("[SD] Directory exists: %s\n", path);
        return true;
    }
    
    if (SD_MMC.mkdir(path)) {
        USBSerial.printf("[SD] Created directory: %s\n", path);
        return true;
    } else {
        USBSerial.printf("[SD] ERROR: Failed to create directory: %s\n", path);
        return false;
    }
}

/**
 * Create device.json - Device identification file
 */
void createDeviceJson() {
    if (SD_MMC.exists(SD_DEVICE_JSON)) {
        USBSerial.println("[SD] device.json already exists");
        return;
    }
    
    File file = SD_MMC.open(SD_DEVICE_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["device_id"] = DEVICE_ID;
        doc["screen"] = DEVICE_SCREEN;
        doc["storage"] = "sd";
        doc["hw_rev"] = DEVICE_HW_REV;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] Created device.json");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create device.json");
    }
}

/**
 * Create os.json - OS version information
 */
void createOsJson() {
    if (SD_MMC.exists(SD_OS_JSON)) {
        USBSerial.println("[SD] os.json already exists");
        return;
    }
    
    File file = SD_MMC.open(SD_OS_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["name"] = WIDGET_OS_NAME;
        doc["version"] = WIDGET_OS_VERSION;
        doc["build"] = WIDGET_OS_BUILD;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] Created os.json");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create os.json");
    }
}

/**
 * Create build.txt - Human-readable build information
 */
void createBuildTxt() {
    if (SD_MMC.exists(SD_BUILD_TXT)) {
        USBSerial.println("[SD] build.txt already exists");
        return;
    }
    
    File file = SD_MMC.open(SD_BUILD_TXT, FILE_WRITE);
    if (file) {
        file.printf("%s %s\n", WIDGET_OS_NAME, WIDGET_OS_VERSION);
        file.printf("Board: %s\n", DEVICE_ID);
        file.printf("Display: %s AMOLED\n", DEVICE_SCREEN);
        file.printf("Build: %s\n", WIDGET_OS_BUILD);
        file.close();
        USBSerial.println("[SD] Created build.txt");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create build.txt");
    }
}

/**
 * Create user.json - User preferences (survives updates)
 */
void createUserJson() {
    if (SD_MMC.exists(SD_USER_JSON)) {
        USBSerial.println("[SD] user.json already exists");
        return;
    }
    
    File file = SD_MMC.open(SD_USER_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["watch_face"] = "MinimalDark";
        doc["brightness"] = 70;
        doc["vibration"] = true;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] Created user.json");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create user.json");
    }
}

/**
 * Create display.json - Display settings (survives updates)
 */
void createDisplayJson() {
    if (SD_MMC.exists(SD_DISPLAY_JSON)) {
        USBSerial.println("[SD] display.json already exists");
        return;
    }
    
    File file = SD_MMC.open(SD_DISPLAY_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["always_on"] = false;
        doc["timeout"] = 10;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] Created display.json");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create display.json");
    }
}

/**
 * Create power.json - Power management settings
 */
void createPowerJson() {
    if (SD_MMC.exists(SD_POWER_JSON)) {
        USBSerial.println("[SD] power.json already exists");
        return;
    }
    
    File file = SD_MMC.open(SD_POWER_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["battery_saver_auto"] = true;
        doc["battery_saver_threshold"] = 20;
        doc["sleep_mode"] = "normal";
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] Created power.json");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create power.json");
    }
}

/**
 * Create UPDATE/README.txt - Instructions for update folder
 */
void createUpdateReadme() {
    if (SD_MMC.exists(SD_UPDATE_README)) {
        USBSerial.println("[SD] UPDATE/README.txt already exists");
        return;
    }
    
    File file = SD_MMC.open(SD_UPDATE_README, FILE_WRITE);
    if (file) {
        file.println("═══════════════════════════════════════════════════════════════════");
        file.println("  Widget OS - UPDATE FOLDER");
        file.println("═══════════════════════════════════════════════════════════════════");
        file.println("");
        file.println("This folder is reserved for system updates.");
        file.println("Do not place files here unless instructed.");
        file.println("Firmware updates are handled via USB.");
        file.println("");
        file.println("For more information, visit the Widget OS documentation.");
        file.println("");
        file.close();
        USBSerial.println("[SD] Created UPDATE/README.txt");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create UPDATE/README.txt");
    }
}

/**
 * Create wifi/config.txt - WiFi configuration template
 */
void createWifiConfigTemplate() {
    if (SD_MMC.exists(SD_WIFI_CONFIG)) {
        USBSerial.println("[SD] wifi/config.txt already exists");
        return;
    }
    
    File file = SD_MMC.open(SD_WIFI_CONFIG, FILE_WRITE);
    if (file) {
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println("#  Widget OS - WiFi Configuration");
        file.println("#  Board: " DEVICE_ID);
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println("#");
        file.println("#  INSTRUCTIONS:");
        file.println("#  1. Edit the values below with your WiFi credentials");
        file.println("#  2. Save this file");
        file.println("#  3. Reboot the watch");
        file.println("#  4. Watch will auto-connect to WiFi on boot!");
        file.println("#");
        file.println("#  FORMAT: KEY=VALUE (no spaces around equals sign)");
        file.println("#  Lines starting with # are comments (ignored)");
        file.println("#");
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println("");
        file.println("# Primary WiFi network");
        file.println("SSID=YourWiFiNetworkName");
        file.println("PASSWORD=YourWiFiPassword");
        file.println("");
        file.println("# Additional WiFi networks (optional)");
        file.println("WIFI1_SSID=HomeNetwork");
        file.println("WIFI1_PASS=HomePassword");
        file.println("");
        file.println("WIFI2_SSID=WorkNetwork");
        file.println("WIFI2_PASS=WorkPassword");
        file.println("");
        file.println("WIFI3_SSID=PhoneHotspot");
        file.println("WIFI3_PASS=HotspotPass");
        file.println("");
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println("#  OPTIONAL: Weather Location");
        file.println("#  Leave blank to auto-detect from your IP address");
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println("");
        file.println("# City name for weather (default: auto-detect from IP)");
        file.println("CITY=Perth");
        file.println("");
        file.println("# Country code (2 letters, e.g., AU, US, UK, etc.)");
        file.println("COUNTRY=AU");
        file.println("");
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println("#  OPTIONAL: Timezone Setting");
        file.println("# ═══════════════════════════════════════════════════════════════════");
        file.println("");
        file.println("# Timezone offset from GMT in hours");
        file.println("# Examples: Perth=8, Sydney=10, London=0, New York=-5");
        file.println("GMT_OFFSET=8");
        file.println("");
        file.close();
        USBSerial.println("[SD] Created wifi/config.txt template");
    } else {
        USBSerial.println("[SD] ERROR: Failed to create wifi/config.txt");
    }
}

/**
 * Initialize boot.log with header
 */
void initBootLog() {
    // Create new boot log (overwrite previous)
    File file = SD_MMC.open(SD_BOOT_LOG, FILE_WRITE);
    if (file) {
        file.println("═══════════════════════════════════════════════════════════════════");
        file.printf("  %s %s - Boot Log\n", WIDGET_OS_NAME, WIDGET_OS_VERSION);
        file.printf("  Device: %s\n", DEVICE_ID);
        file.println("═══════════════════════════════════════════════════════════════════");
        file.println("");
        file.close();
        USBSerial.println("[SD] Initialized boot.log");
    }
}

/**
 * Create Widget OS folder structure on SD card
 * This is the main initialization function called on first boot
 */
bool createWidgetOSFolderStructure() {
    USBSerial.println("\n═══════════════════════════════════════════════════════════════════");
    USBSerial.println("  WIDGET OS - SD Card Structure Initialization");
    USBSerial.println("═══════════════════════════════════════════════════════════════════\n");
    
    bool success = true;
    
    // Check if WATCH folder exists, if corrupted rename it
    if (SD_MMC.exists("/WATCH_BROKEN")) {
        USBSerial.println("[SD] WARNING: Previous WATCH_BROKEN folder found");
    }
    
    // Create main folder structure
    USBSerial.println("[SD] Creating folder structure...");
    
    // Root
    success &= createDirectoryIfNotExists(SD_ROOT_PATH);
    
    // SYSTEM
    success &= createDirectoryIfNotExists(SD_SYSTEM_PATH);
    success &= createDirectoryIfNotExists(SD_SYSTEM_LOGS_PATH);
    
    // CONFIG
    success &= createDirectoryIfNotExists(SD_CONFIG_PATH);
    
    // FACES
    success &= createDirectoryIfNotExists(SD_FACES_PATH);
    success &= createDirectoryIfNotExists(SD_FACES_CUSTOM_PATH);
    success &= createDirectoryIfNotExists(SD_FACES_IMPORTED_PATH);
    
    // IMAGES (user-added)
    success &= createDirectoryIfNotExists(SD_IMAGES_PATH);
    
    // MUSIC (user-added)
    success &= createDirectoryIfNotExists(SD_MUSIC_PATH);
    
    // CACHE
    success &= createDirectoryIfNotExists(SD_CACHE_PATH);
    success &= createDirectoryIfNotExists(SD_CACHE_TEMP_PATH);
    
    // UPDATE
    success &= createDirectoryIfNotExists(SD_UPDATE_PATH);
    
    // wifi
    success &= createDirectoryIfNotExists(SD_WIFI_PATH);
    
    if (!success) {
        USBSerial.println("[SD] ERROR: Failed to create some directories");
        sdCardStatus = SD_STATUS_CORRUPT;
        return false;
    }
    
    // Create system files
    USBSerial.println("\n[SD] Creating system files...");
    initBootLog();
    logToBootLog("Widget OS boot started");
    
    createDeviceJson();
    logToBootLog("Created device.json");
    
    createOsJson();
    logToBootLog("Created os.json");
    
    createBuildTxt();
    logToBootLog("Created build.txt");
    
    // Create config files
    USBSerial.println("\n[SD] Creating config files...");
    createUserJson();
    logToBootLog("Created user.json");
    
    createDisplayJson();
    logToBootLog("Created display.json");
    
    createPowerJson();
    logToBootLog("Created power.json");
    
    // Create other files
    USBSerial.println("\n[SD] Creating other files...");
    createUpdateReadme();
    logToBootLog("Created UPDATE/README.txt");
    
    createWifiConfigTemplate();
    logToBootLog("Created wifi/config.txt template");
    
    logToBootLog("Widget OS folder structure created successfully");
    
    USBSerial.println("\n═══════════════════════════════════════════════════════════════════");
    USBSerial.println("  WIDGET OS - SD Card Structure Created Successfully!");
    USBSerial.println("═══════════════════════════════════════════════════════════════════\n");
    
    sdStructureCreated = true;
    return true;
}

/**
 * Initialize SD card and create Widget OS structure
 * Called during setup()
 */
bool initWidgetOSSDCard() {
    USBSerial.println("\n[SD] Initializing Widget OS SD Card...");
    sdCardStatus = SD_STATUS_INIT_IN_PROGRESS;
    
    // Set SD_MMC pins (from Waveshare example)
    // Using 1-bit SD_MMC mode for compatibility
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    
    // Try to mount SD card
    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        USBSerial.println("[SD] Card Mount Failed - SD card not present or damaged");
        sdCardStatus = SD_STATUS_MOUNT_FAILED;
        sdErrorMessage = "SD card mount failed";
        hasSD = false;
        return false;
    }
    
    // Check card type
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        USBSerial.println("[SD] No SD card attached");
        sdCardStatus = SD_STATUS_NOT_PRESENT;
        sdErrorMessage = "No SD card detected";
        hasSD = false;
        return false;
    }
    
    // Get card info
    switch(cardType) {
        case CARD_MMC:  sdCardType = "MMC"; break;
        case CARD_SD:   sdCardType = "SDSC"; break;
        case CARD_SDHC: sdCardType = "SDHC"; break;
        default:        sdCardType = "UNKNOWN"; break;
    }
    
    sdCardSizeMB = SD_MMC.cardSize() / (1024 * 1024);
    
    USBSerial.printf("[SD] Card Type: %s\n", sdCardType.c_str());
    USBSerial.printf("[SD] Card Size: %llu MB\n", sdCardSizeMB);
    USBSerial.printf("[SD] Total Space: %llu MB\n", SD_MMC.totalBytes() / (1024 * 1024));
    USBSerial.printf("[SD] Used Space: %llu MB\n", SD_MMC.usedBytes() / (1024 * 1024));
    
    sdCardInitialized = true;
    hasSD = true;
    sdCardStatus = SD_STATUS_MOUNTED_OK;
    
    // Create Widget OS folder structure
    if (!createWidgetOSFolderStructure()) {
        USBSerial.println("[SD] WARNING: Failed to create complete folder structure");
        // Continue anyway - partial structure is better than none
    }
    
    return true;
}

/**
 * Load user config from SD card (user.json)
 */
void loadUserConfigFromSD() {
    if (!hasSD || !sdCardInitialized) {
        USBSerial.println("[SD] Cannot load user config - SD not available");
        return;
    }
    
    if (!SD_MMC.exists(SD_USER_JSON)) {
        USBSerial.println("[SD] user.json not found");
        return;
    }
    
    File file = SD_MMC.open(SD_USER_JSON, FILE_READ);
    if (file) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (error) {
            USBSerial.printf("[SD] ERROR parsing user.json: %s\n", error.c_str());
            logToBootLog("ERROR: Failed to parse user.json");
            return;
        }
        
        // Load user preferences
        if (doc.containsKey("brightness")) {
            userData.brightness = doc["brightness"].as<int>();
        }
        if (doc.containsKey("vibration")) {
            // Future: vibration setting
        }
        
        USBSerial.println("[SD] Loaded user config from user.json");
        logToBootLog("Loaded user config from SD");
    }
}

/**
 * Load display config from SD card (display.json)
 */
void loadDisplayConfigFromSD() {
    if (!hasSD || !sdCardInitialized) return;
    
    if (!SD_MMC.exists(SD_DISPLAY_JSON)) return;
    
    File file = SD_MMC.open(SD_DISPLAY_JSON, FILE_READ);
    if (file) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (error) {
            USBSerial.printf("[SD] ERROR parsing display.json: %s\n", error.c_str());
            return;
        }
        
        if (doc.containsKey("timeout")) {
            userData.screenTimeout = doc["timeout"].as<int>();
        }
        
        USBSerial.println("[SD] Loaded display config from display.json");
    }
}

/**
 * Save user config to SD card
 */
void saveUserConfigToSD() {
    if (!hasSD || !sdCardInitialized) return;
    
    File file = SD_MMC.open(SD_USER_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["watch_face"] = "MinimalDark";  // TODO: Get actual selected face
        doc["brightness"] = userData.brightness;
        doc["vibration"] = true;
        
        serializeJsonPretty(doc, file);
        file.close();
        USBSerial.println("[SD] Saved user config to user.json");
    }
}

/**
 * Load WiFi configuration from SD card
 */
bool loadWiFiConfigFromSD() {
    if (!hasSD || !sdCardInitialized) {
        USBSerial.println("[WIFI] SD card not available for WiFi config");
        return false;
    }
    
    if (!SD_MMC.exists(SD_WIFI_CONFIG)) {
        USBSerial.println("[WIFI] wifi/config.txt not found");
        return false;
    }
    
    File file = SD_MMC.open(SD_WIFI_CONFIG, FILE_READ);
    if (!file) {
        USBSerial.println("[WIFI] Failed to open wifi/config.txt");
        return false;
    }
    
    USBSerial.println("[WIFI] Loading WiFi config from SD card...");
    numWifiNetworks = 0;
    
    while (file.available() && numWifiNetworks < MAX_WIFI_NETWORKS) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        // Skip comments and empty lines
        if (line.length() == 0 || line.startsWith("#")) continue;
        
        int eqPos = line.indexOf('=');
        if (eqPos <= 0) continue;
        
        String key = line.substring(0, eqPos);
        String value = line.substring(eqPos + 1);
        key.trim();
        value.trim();
        
        // Primary network
        if (key == "SSID" && numWifiNetworks == 0) {
            strncpy(wifiNetworks[0].ssid, value.c_str(), sizeof(wifiNetworks[0].ssid) - 1);
            wifiNetworks[0].valid = true;
            if (numWifiNetworks < 1) numWifiNetworks = 1;
        }
        else if (key == "PASSWORD" && numWifiNetworks >= 1) {
            strncpy(wifiNetworks[0].password, value.c_str(), sizeof(wifiNetworks[0].password) - 1);
        }
        // Additional networks
        else if (key.startsWith("WIFI") && key.endsWith("_SSID")) {
            int netNum = key.substring(4, key.length() - 5).toInt();
            if (netNum >= 1 && netNum <= MAX_WIFI_NETWORKS - 1) {
                strncpy(wifiNetworks[netNum].ssid, value.c_str(), sizeof(wifiNetworks[netNum].ssid) - 1);
                wifiNetworks[netNum].valid = true;
                if (numWifiNetworks <= netNum) numWifiNetworks = netNum + 1;
            }
        }
        else if (key.startsWith("WIFI") && key.endsWith("_PASS")) {
            int netNum = key.substring(4, key.length() - 5).toInt();
            if (netNum >= 1 && netNum <= MAX_WIFI_NETWORKS - 1) {
                strncpy(wifiNetworks[netNum].password, value.c_str(), sizeof(wifiNetworks[netNum].password) - 1);
            }
        }
        // Weather location
        else if (key == "CITY") {
            strncpy(weatherCity, value.c_str(), sizeof(weatherCity) - 1);
        }
        else if (key == "COUNTRY") {
            strncpy(weatherCountry, value.c_str(), sizeof(weatherCountry) - 1);
        }
        else if (key == "GMT_OFFSET") {
            gmtOffsetSec = value.toInt() * 3600;
        }
    }
    
    file.close();
    
    if (numWifiNetworks > 0) {
        wifiConfigFromSD = true;
        USBSerial.printf("[WIFI] Loaded %d WiFi networks from SD card\n", numWifiNetworks);
        logToBootLog("Loaded WiFi config from SD card");
        return true;
    }
    
    return false;
}

/**
 * Get SD card status string for UI display
 */
const char* getSDCardStatusString() {
    switch (sdCardStatus) {
        case SD_STATUS_NOT_PRESENT:     return "No SD Card";
        case SD_STATUS_MOUNTED_OK:      return "SD Card OK";
        case SD_STATUS_MOUNT_FAILED:    return "SD Mount Failed";
        case SD_STATUS_CORRUPT:         return "SD Corrupt";
        case SD_STATUS_READ_ONLY:       return "SD Read-Only";
        case SD_STATUS_INIT_IN_PROGRESS: return "Initializing...";
        default:                         return "Unknown";
    }
}

/**
 * List contents of a directory on SD card (for debugging)
 */
void listSDDirectory(const char* dirname, uint8_t levels) {
    if (!hasSD || !sdCardInitialized) {
        USBSerial.println("[SD] Cannot list directory - SD not available");
        return;
    }
    
    USBSerial.printf("[SD] Listing: %s\n", dirname);
    
    File root = SD_MMC.open(dirname);
    if (!root) {
        USBSerial.println("[SD] Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        USBSerial.println("[SD] Not a directory");
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            USBSerial.printf("  [DIR]  %s\n", file.name());
            if (levels > 0) {
                listSDDirectory(file.path(), levels - 1);
            }
        } else {
            USBSerial.printf("  [FILE] %s (%d bytes)\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
    root.close();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WIDGET OS - FACE LOADING SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════

#define MAX_SD_FACES 20

struct SDFace {
    char folderPath[128];
    char name[32];
    char author[32];
    char version[16];
    bool supportsCurrentScreen;
    bool valid;
};

SDFace sdFaces[MAX_SD_FACES];
int numSDFaces = 0;

/**
 * Scan for custom faces on SD card
 */
void scanSDFaces() {
    if (!hasSD || !sdCardInitialized) {
        USBSerial.println("[FACES] SD card not available");
        return;
    }
    
    numSDFaces = 0;
    USBSerial.println("[FACES] Scanning for custom faces...");
    
    // Scan custom faces
    scanFacesInDirectory(SD_FACES_CUSTOM_PATH);
    
    // Scan imported faces
    scanFacesInDirectory(SD_FACES_IMPORTED_PATH);
    
    USBSerial.printf("[FACES] Found %d custom faces on SD card\n", numSDFaces);
    logToBootLog("Scanned SD card for custom faces");
}

/**
 * Scan faces in a specific directory
 */
void scanFacesInDirectory(const char* dirPath) {
    if (!SD_MMC.exists(dirPath)) return;
    
    File dir = SD_MMC.open(dirPath);
    if (!dir || !dir.isDirectory()) return;
    
    File faceDir = dir.openNextFile();
    while (faceDir && numSDFaces < MAX_SD_FACES) {
        if (faceDir.isDirectory()) {
            // Check for face.json
            String facePath = String(faceDir.path()) + "/face.json";
            if (SD_MMC.exists(facePath.c_str())) {
                // Load face info
                File faceJson = SD_MMC.open(facePath.c_str(), FILE_READ);
                if (faceJson) {
                    StaticJsonDocument<512> doc;
                    DeserializationError error = deserializeJson(doc, faceJson);
                    faceJson.close();
                    
                    if (!error) {
                        strncpy(sdFaces[numSDFaces].folderPath, faceDir.path(), sizeof(sdFaces[numSDFaces].folderPath) - 1);
                        
                        if (doc.containsKey("name")) {
                            strncpy(sdFaces[numSDFaces].name, doc["name"].as<const char*>(), sizeof(sdFaces[numSDFaces].name) - 1);
                        } else {
                            strncpy(sdFaces[numSDFaces].name, faceDir.name(), sizeof(sdFaces[numSDFaces].name) - 1);
                        }
                        
                        if (doc.containsKey("author")) {
                            strncpy(sdFaces[numSDFaces].author, doc["author"].as<const char*>(), sizeof(sdFaces[numSDFaces].author) - 1);
                        }
                        
                        if (doc.containsKey("version")) {
                            strncpy(sdFaces[numSDFaces].version, doc["version"].as<const char*>(), sizeof(sdFaces[numSDFaces].version) - 1);
                        }
                        
                        // Check screen support
                        sdFaces[numSDFaces].supportsCurrentScreen = true;
                        if (doc.containsKey("supports")) {
                            JsonArray supports = doc["supports"].as<JsonArray>();
                            sdFaces[numSDFaces].supportsCurrentScreen = false;
                            for (JsonVariant v : supports) {
                                if (String(v.as<const char*>()) == DEVICE_SCREEN) {
                                    sdFaces[numSDFaces].supportsCurrentScreen = true;
                                    break;
                                }
                            }
                        }
                        
                        sdFaces[numSDFaces].valid = true;
                        USBSerial.printf("[FACES] Found: %s by %s\n", sdFaces[numSDFaces].name, sdFaces[numSDFaces].author);
                        numSDFaces++;
                    }
                }
            }
        }
        faceDir = dir.openNextFile();
    }
    dir.close();
}

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

void lvgl_tick_cb(void *arg) { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

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
    
    // Initialize display
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
    }
    
    delay(5);
}
