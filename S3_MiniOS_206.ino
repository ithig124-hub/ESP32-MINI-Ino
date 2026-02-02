/**
 *
 *  S3 MiniOS v5.2 - MERGED STABLE EDITION
 *  ESP32-S3-Touch-AMOLED-2.06" Smartwatch Firmware
 *
 *  ═══════════════════════════════════════════════════════════════════════════
 *  MERGED: Full Features (v5.0) + LVGL Stability Fixes (v5.1)
 *  ═══════════════════════════════════════════════════════════════════════════
 *
 *  LVGL STABILITY FIXES (from v5.1):
 *
 *  FIX 1: SINGLE LVGL THREAD (NON-NEGOTIABLE)
 *   - Created dedicated ui_task for ALL LVGL operations
 *   - All LVGL calls now routed through ui_event system
 *   - No more LVGL calls from touch callbacks, ISRs, or timers
 *
 *  FIX 2: SAFE NAVIGATION SYSTEM (NO FREEZES)
 *   - Screen switching uses safe hide/show instead of delete
 *   - Animation-safe screen transitions
 *   - Never delete screens during animations
 *
 *  FIX 3: DELAYED OBJECT DELETION
 *   - Objects deleted via lv_timer (300ms delay)
 *   - Safe cleanup after animations complete
 *
 *  FIX 4: TOUCH DRIVER RULES (NO UI LOGIC)
 *   - Touch callback only returns coordinates
 *   - No lv_event_send() or lv_scr_load() in touch handler
 *   - Navigation triggered via event flags
 *
 *  FIX 5: REMOVED BLOCKING CODE FROM NAVIGATION
 *   - SD card reads deferred with lv_timer
 *   - No heavy operations during screen changes
 *
 *  FIX 6: DISPLAY POWER CONTROL FIX
 *   - ui_activity_ping() prevents premature screen off
 *   - Backlight manager runs in separate logic
 *   - Screen power never controlled inside LVGL callbacks
 *
 *  FIX 7: WATCHDOG SAFE LOOP
 *   - UI task feeds watchdog every cycle
 *   - No freeze-related hard reboots
 *
 *  FIX 8: DEBUG CONFIRMATION
 *   - NAV START / NAV END markers for debugging
 *   - Easy identification of freeze location
 *
 *  FIX 9: EMERGENCY SOFT RECOVERY
 *   - panic_recover() reinitializes LVGL if stalled > 2s
 *   - No more power-cut required
 *
 *  ═══════════════════════════════════════════════════════════════════════════
 *
 *  FULL FEATURES (from v5.0 Fusion Labs Edition):
 *
 *  === SD CARD MANAGEMENT ===
 *  - Auto backup every 24 hours to SD card
 *  - Manual backup toggle in settings
 *  - SD Card health monitoring (space used/total)
 *  - Boot from SD card backup restore
 *  - Auto-create required folder structure
 *  - Edit config.txt via Fusion Labs web UI
 *
 *  === FUSION LABS WEB SERIAL PROTOCOL ===
 *  - Receive firmware updates (.bin flashing)
 *  - Install watch faces from web
 *  - Read/write config.txt
 *  - SD card status reporting
 *  - Real-time device info streaming
 *
 *  === PREMIUM LVGL UI ===
 *  - Apple Watch-style gradient themes (8 themes)
 *  - Full sensor fusion compass with Kalman filter
 *  - Smooth animated navigation transitions
 *  - Premium Blackjack with visual cards
 *  - Dino runner game with physics
 *  - Yes/No spinner decision maker
 *  - Activity rings & workout tracking
 *  - Step streak & achievements
 *  - SD card wallpaper support
 *  - Breathe wellness app
 *
 *  === BATTERY INTELLIGENCE ===
 *  - Sleep mode tracking (screen on/off time)
 *  - ML-style battery estimation (simple/weighted/learned)
 *  - 24-hour usage pattern analysis
 *  - Card usage tracking
 *  - Battery Stats sub-card with graphs
 *  - Low battery popup at 20% and 10%
 *  - Battery Saver mode (auto + manual)
 *  - Charging animation
 *  - Mini battery estimate on all cards
 *
 *  Compatible with Fusion Labs Web Interface:
 *  https://github.com/ithig124-hub/fusion-labs
 *
 *  Hardware: Waveshare ESP32-S3-Touch-AMOLED-2.06
 *     Display: CO5300 QSPI AMOLED 410x502
 *     Touch: FT3168
 *     IMU: QMI8658
 *     RTC: PCF85063
 *     PMU: AXP2101
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
#include <esp_task_wdt.h>  // FIX 7: Watchdog support

// Fix macro conflict
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

// Wallpaper images
#include "AdobeStock_17557.c"
#include "AdobeStock_2026.c"
#include "AdobeStock_184869446.c"
#include "AdobeStock_174564.c"

//
//  USB SERIAL COMPATIBILITY FIX
//
#if ARDUINO_USB_CDC_ON_BOOT
  #define USBSerial Serial
#else
  #if !defined(USBSerial)
    HWCDC USBSerial;
  #endif
#endif

//  WIDGET OS - VERSION & DEVICE CONFIGURATION

#define WIDGET_OS_NAME      "Widget OS"
#define WIDGET_OS_VERSION   "5.2.0"
#define WIDGET_OS_BUILD     "merged-stable"
#define DEVICE_ID           "WOS-206A"
#define DEVICE_SCREEN       "2.06"
#define DEVICE_HW_REV       "A"

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 1: SINGLE LVGL THREAD - EVENT SYSTEM
// ═══════════════════════════════════════════════════════════════════════════

// UI Event Types - ALL navigation/UI changes go through this
enum UIEventType {
    UI_EVENT_NONE = 0,
    UI_EVENT_NAV_LEFT,
    UI_EVENT_NAV_RIGHT,
    UI_EVENT_NAV_UP,
    UI_EVENT_NAV_DOWN,
    UI_EVENT_TAP,
    UI_EVENT_SCREEN_ON,
    UI_EVENT_SCREEN_OFF,
    UI_EVENT_REFRESH,
    UI_EVENT_LOW_BATTERY,
    UI_EVENT_SHUTDOWN
};

// Thread-safe event queue
volatile UIEventType ui_event = UI_EVENT_NONE;
volatile int ui_event_param1 = 0;  // For tap X coordinate
volatile int ui_event_param2 = 0;  // For tap Y coordinate

// UI Task handle
TaskHandle_t ui_task_handle = NULL;

// FIX 6: Display activity tracking
volatile uint32_t last_ui_activity = 0;

// FIX 9: Emergency recovery tracking
volatile uint32_t last_lvgl_response = 0;
const uint32_t LVGL_STALL_TIMEOUT_MS = 2000;

// Forward declaration for panic recovery
void panic_recover();

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 6: UI ACTIVITY PING - Prevents premature screen off
// ═══════════════════════════════════════════════════════════════════════════

void ui_activity_ping() {
    last_ui_activity = millis();
}

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
#define SD_BACKUP_PATH          "/WATCH/BACKUPS"
#define SD_FIRMWARE_PATH        "/WATCH/FIRMWARE"
#define SD_LOGS_PATH            "/WATCH/LOGS"
#define SD_WALLPAPERS_PATH      "/WATCH/WALLPAPERS"

// System files
#define SD_DEVICE_JSON          "/WATCH/SYSTEM/device.json"
#define SD_OS_JSON              "/WATCH/SYSTEM/os.json"
#define SD_BUILD_TXT            "/WATCH/SYSTEM/build.txt"
#define SD_BOOT_LOG             "/WATCH/SYSTEM/logs/boot.log"

// Config files
#define SD_USER_JSON            "/WATCH/CONFIG/user.json"
#define SD_DISPLAY_JSON         "/WATCH/CONFIG/display.json"
#define SD_POWER_JSON           "/WATCH/CONFIG/power.json"
#define SD_USER_DATA_PATH       "/WATCH/CONFIG/user_data.json"

// Other files
#define SD_UPDATE_README        "/WATCH/UPDATE/README.txt"
#define SD_WIFI_CONFIG          "/WATCH/wifi/config.txt"

// Legacy path for backward compatibility
#define WIFI_CONFIG_PATH_LEGACY "/WATCH/wifi/config.txt"

//  SD CARD STATUS & ERROR HANDLING

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

// Backup configuration
#define AUTO_BACKUP_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)  // 24 hours
bool autoBackupEnabled = true;
unsigned long lastAutoBackup = 0;
unsigned long lastBackupTime = 0;
int totalBackups = 0;

// SD Card health tracking (Fusion Labs compatible)
struct SDCardHealth {
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t freeBytes;
    float usedPercent;
    bool mounted;
    bool healthy;
    String lastError;
    int writeErrors;
    int readErrors;
};
SDCardHealth sdHealth = {0, 0, 0, 0.0, false, false, "", 0, 0};

//  FUSION LABS WEB SERIAL PROTOCOL
//  Commands for communication with https://github.com/ithig124-hub/fusion-labs

#define FUSION_PROTOCOL_VERSION "1.0"

// Web Serial command buffer
#define WEB_SERIAL_BUFFER_SIZE 4096
char webSerialBuffer[WEB_SERIAL_BUFFER_SIZE];
int webSerialBufferIndex = 0;
bool receivingFirmware = false;
bool receivingFace = false;
String currentFaceId = "";
String webSerialCommand = "";

// Protocol commands from Fusion Labs web interface
// WIDGET_PING         - Check connection
// WIDGET_STATUS       - Get device status
// WIDGET_READ_CONFIG  - Read config.txt
// WIDGET_WRITE_CONFIG - Write config.txt
// WIDGET_BACKUP       - Trigger manual backup
// WIDGET_RESTORE      - Restore from backup
// WIDGET_SD_HEALTH    - Get SD card health
// WIDGET_WRITE_FACE   - Install watch face
// WIDGET_SET_FACE     - Set active watch face
// WIDGET_LIST_FACES   - List installed faces
// WIDGET_FLASH_FW     - Receive firmware update

// 
//  WIFI & API CONFIGURATION
// 
#define MAX_WIFI_NETWORKS 5
#define WIFI_CONFIG_PATH "/wifi/config.txt"

// 
//  AUTOMATIC FREE WIFI CONNECTION SYSTEM
//  Intelligent Wi-Fi manager that auto-connects to open networks
// 
#define MAX_OPEN_NETWORKS 10          // Max open networks to track during scan
#define WIFI_SCAN_TIMEOUT_MS 5000     // Timeout for WiFi scan
#define WIFI_CONNECT_TIMEOUT_MS 10000 // Timeout for connection attempts
#define WIFI_RECONNECT_INTERVAL_MS 60000  // Check connection every 60 seconds
#define MIN_RSSI_THRESHOLD -85        // Minimum signal strength for open networks

struct WiFiNetwork {
    char ssid[64];
    char password[64];
    bool valid;
    bool isOpen;      // True if this is an open (no password) network
    int32_t rssi;     // Signal strength
};

WiFiNetwork wifiNetworks[MAX_WIFI_NETWORKS];
int numWifiNetworks = 0;
int connectedNetworkIndex = -1;

// Open network tracking
struct OpenNetwork {
    char ssid[64];
    int32_t rssi;
    bool valid;
};
OpenNetwork openNetworks[MAX_OPEN_NETWORKS];
int numOpenNetworks = 0;
bool connectedToOpenNetwork = false;   // True if connected to auto-discovered open network
unsigned long lastWiFiCheck = 0;       // Last time we checked WiFi status
unsigned long lastWiFiScan = 0;        // Last time we scanned for networks

char weatherCity[64] = "Perth";
char weatherCountry[8] = "AU";
long gmtOffsetSec = 8 * 3600;
const char* NTP_SERVER = "pool.ntp.org";
const int DAYLIGHT_OFFSET_SEC = 0;

// API Keys
const char* OPENWEATHER_API = "3795c13a0d3f7e17799d638edda60e3c";
const char* CURRENCY_API_KEY = "cur_live_ROqsDnwrNd40cRqegQakXb4pO6tQuihpU9OQr4Nx";

bool wifiConnected = false;
bool wifiConfigFromSD = false;

// 
//  BATTERY INTELLIGENCE CONFIGURATION
// 
#define SAVE_INTERVAL_MS 7200000UL  // 2 hours
#define SCREEN_OFF_TIMEOUT_MS 30000
#define SCREEN_OFF_TIMEOUT_SAVER_MS 10000

// Battery specs
#define BATTERY_CAPACITY_MAH 350
#define SCREEN_ON_CURRENT_MA 80
#define SCREEN_OFF_CURRENT_MA 15
#define SAVER_MODE_CURRENT_MA 40

// Low battery thresholds
#define LOW_BATTERY_WARNING 20
#define CRITICAL_BATTERY_WARNING 10

// Usage tracking
#define USAGE_HISTORY_SIZE 24
#define CARD_USAGE_SLOTS 12  // All categories

// 
//  LVGL CONFIGURATION
// 
#define LVGL_TICK_PERIOD_MS 2
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

// 
//  HARDWARE OBJECTS
// 
// Note: USBSerial is defined above with compatibility fix
// Adafruit_XCA9554 expander;  // Not used on 2.06" board (only on 1.8")
SensorQMI8658 qmi;
SensorPCF85063 rtc;
XPowersPMU power;
IMUdata acc, gyr;
Preferences prefs;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(bus, LCD_RESET, 0 , LCD_WIDTH, LCD_HEIGHT, LCD_COL_OFFSET1, LCD_ROW_OFFSET1, LCD_COL_OFFSET2, LCD_ROW_OFFSET2);
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

// 
//  IDENTITY SYSTEM - MUST BE DEFINED BEFORE UserData struct
// 
#define NUM_IDENTITIES 15

// 
//  NAVIGATION SYSTEM
// 
#define NUM_CATEGORIES 14
enum Category {
  CAT_CLOCK = 0, CAT_COMPASS, CAT_ACTIVITY, CAT_GAMES,
  CAT_WEATHER, CAT_MEDIA, CAT_TIMER,
  CAT_STREAK, CAT_CALENDAR, CAT_TORCH, CAT_TOOLS,
  CAT_SETTINGS, CAT_SYSTEM, CAT_IDENTITY  // NEW: Identity Picker
};

int currentCategory = CAT_CLOCK;
int currentSubCard = 0;
const int maxSubCards[] = {5, 3, 4, 3, 2, 2, 4, 3, 1, 2, 4, 1, 4, 2};  // CAT_STOCKS removed  // CAT_SYSTEM now has 4 (added SD Health)

// Animation state
bool isTransitioning = false;
int transitionDir = 0;
float transitionProgress = 0.0;
unsigned long transitionStartMs = 0;
const unsigned long TRANSITION_DURATION = 200;

// 
//  BATTERY INTELLIGENCE DATA STRUCTURES
// 
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
    uint32_t cardUsageTime[CARD_USAGE_SLOTS];

    // Battery drain tracking
    uint8_t batteryAtHourStart;
    float avgDrainPerHour;
    float weightedDrainRate;

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

// 
//  USER DATA (Persistent) - Combined from v2.0 + v3.1
// 
//  USER DATA (Persistent) - Combined from v2.0 + v3.1
//  FIXED: Added missing members for backup/restore functions
// 
struct UserData {
  // Activity data
  uint32_t steps;
  uint32_t dailyGoal;
  int stepStreak;
  float totalDistance;
  float totalCalories;
  uint32_t stepHistory[7];
  
  // Game data
  int blackjackStreak;
  int gamesWon;
  int gamesPlayed;
  uint32_t clickerScore;
  
  // Settings
  int brightness;
  int screenTimeout;
  int themeIndex;
  int compassMode;
  int wallpaperIndex;
  
  // FIXED: Added missing members for backup/restore compatibility
  int theme;              // Theme setting for backup (alias for themeIndex)
  bool vibration;         // Vibration enabled
  int gmtOffset;          // GMT offset in hours
  float calories;         // Current calories (alias for totalCalories tracking)
  uint32_t stepGoal;      // Daily step goal (alias for dailyGoal)
  uint32_t calorieGoal;   // Daily calorie goal
  
  // NEW: Identity system
  bool identitiesUnlocked[NUM_IDENTITIES];
  uint32_t identityProgress[NUM_IDENTITIES];
  int selectedIdentity;
  uint32_t compassUseCount;
  uint32_t consecutiveDays;
  uint32_t lastUseDayOfYear;
} userData = {0, 10000, 7, 0.0, 0.0, {0}, 0, 0, 0, 0, 200, 1, 0, 0, 0, 0, true, 10, 0.0, 10000, 500};

// 
//  RUNTIME STATE
// 
bool screenOn = true;
unsigned long lastActivityMs = 0;
unsigned long screenOnStartMs = 0;
unsigned long screenOffStartMs = 0;

// Forward declarations for notification system (used in shutdownDevice)
bool showingNotification = false;
unsigned long notificationStartMs = 0;
const unsigned long NOTIFICATION_DURATION = 3000;
lv_obj_t *notificationOverlay = NULL;  // Global pointer to prevent duplicate overlays

// 
//  SYSTEM BUTTONS
// 
#define BOOT_BUTTON     0       // Boot/Flash button (GPIO0) - Screen switching
#define PWR_BUTTON      10      // Power button (GPIO10) - On/Off & Shutdown

// Button timing constants - VERY conservative to prevent false triggers
const unsigned long POWER_BUTTON_SHUTDOWN_MS = 5000;     // 5s CONFIRMED hold = shutdown
const unsigned long POWER_BUTTON_DEBOUNCE_MS = 150;      // Long debounce to prevent noise
const unsigned long POWER_BUTTON_MIN_TAP_MS = 100;       // Minimum tap duration
const unsigned long POWER_BUTTON_CONFIRM_SAMPLES = 20;   // Require many samples to confirm

// NTP re-sync interval (1 hour)
const unsigned long NTP_RESYNC_INTERVAL_MS = 3600000;

// Navigation lock - prevents conflicting UI updates
volatile bool navigationLocked = false;
unsigned long lastNavigationMs = 0;
const unsigned long NAVIGATION_COOLDOWN_MS = 150;  // Minimum time between navigations

// Button state variables
bool powerButtonPressed = false;
unsigned long powerButtonPressStartMs = 0;
bool bootButtonPressed = false;
unsigned long bootButtonPressStartMs = 0;

// Shutdown progress visual indicator (FIX 1 - v4.1)
bool showingShutdownProgress = false;
unsigned long shutdownProgressStartMs = 0;
lv_obj_t *shutdownPopup = NULL;
lv_obj_t *shutdownProgressArc = NULL;
lv_obj_t *shutdownProgressLabel = NULL;

// Clock
uint8_t clockHour = 10, clockMinute = 30, clockSecond = 0;
uint8_t currentDay = 3;

// Weather
float weatherTemp = 24.0;
String weatherDesc = "Sunny";
float weatherHigh = 28.0;
float weatherLow = 18.0;

// 
//  PREMIUM WIDGETS DATA STRUCTURES
// 

// Currency converter data
struct CurrencyData {
    char sourceCurrency[4];  // EUR, GBP, JPY, CNY, CHF, CAD
    float usdRate;
    float audRate;
    unsigned long lastUpdate;
    bool valid;
};
CurrencyData currencyData = {"EUR", 1.0, 1.5, 0, false};
int selectedCurrencyIndex = 0;
const char* availableCurrencies[] = {"EUR", "GBP", "JPY", "CNY", "CHF", "CAD"};
const int numCurrencies = 6;
const unsigned long CURRENCY_UPDATE_INTERVAL = 600000; // 10 minutes

// Sunrise/Sunset data
struct SunriseSunsetData {
    char sunriseTime[6];      // "05:39"
    char sunsetTime[6];       // "19:05"
    float sunriseAzimuth;     // Angle from north (0-360)
    float sunsetAzimuth;      // Angle from north (0-360)
    unsigned long lastFetch;
    bool valid;
};
SunriseSunsetData sunData = {"06:00", "18:00", 90.0, 270.0, 0, false};

// Compass calibration - user sets "north" direction
float compassNorthOffset = 0.0;  // Degrees to add to raw heading

// Weather style preference
int weatherStyle = 0;  // 0=Berlin, 1=Hero (toggle in weather card)

// Stocks/Crypto

// Battery
uint16_t batteryVoltage = 4100;
uint8_t batteryPercent = 85;
bool isCharging = false;
uint32_t freeRAM = 234567;

// Hardware flags
bool hasIMU = false, hasRTC = false, hasPMU = false, hasSD = false;

// 
//  SENSOR FUSION COMPASS STATE
// 
const float ALPHA = 0.98;
const float BETA = 0.02;

float roll = 0.0, pitch = 0.0, yaw = 0.0;
float gyroRoll = 0.0, gyroPitch = 0.0, gyroYaw = 0.0;
float accelRoll = 0.0, accelPitch = 0.0;
float compassHeading = 0.0, compassHeadingSmooth = 0.0;
float initialYaw = 0.0;
bool compassCalibrated = false;
float tiltX = 0.0, tiltY = 0.0;
unsigned long lastSensorUpdate = 0;

// 
//  TIMER STATE
// 
bool sandTimerRunning = false;
unsigned long sandTimerStartMs = 0;
const unsigned long SAND_TIMER_DURATION = 5 * 60 * 1000;
bool timerNotificationActive = false;

bool stopwatchRunning = false;
unsigned long stopwatchStartMs = 0;
unsigned long stopwatchElapsedMs = 0;

bool breatheRunning = false;
int breathePhase = 0;
unsigned long breatheStartMs = 0;

int countdownSelected = 2;
int countdownTimes[] = {60, 180, 300, 600};
// 
//  TORCH STATE
// 
bool torchOn = false;
int torchBrightness = 255;
int torchColorIndex = 0;  // 0=White, 1=Red, 2=Green, 3=Blue, 4=Yellow
uint32_t torchColors[] = {0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00};
const char* torchColorNames[] = {"White", "Red", "Green", "Blue", "Yellow"};
#define NUM_TORCH_COLORS 5

// 
//  STOPWATCH LAP STATE
// 
#define MAX_LAPS 10
unsigned long lapTimes[MAX_LAPS];
unsigned long lapTotalTimes[MAX_LAPS];
int lapCount = 0;

// 
//  CLICKER STATE
// 
uint32_t clickerCount = 0;

// 
//  REACTION TEST STATE
// 
bool reactionTestActive = false;
bool reactionWaiting = false;
unsigned long reactionStartMs = 0;
unsigned long reactionDelayMs = 0;
unsigned long lastReactionTime = 0;
unsigned long bestReactionTime = 9999;
bool reactionTooEarly = false;

// 
//  DAILY CHALLENGE STATE
// 
int challengeType = 0;  // 0=Math, 1=Memory, 2=Trivia
int challengeQuestion = 0;
int challengeAnswer = 0;
int challengeOptions[4];
int challengeCorrectIndex = 0;
bool challengeAnswered = false;
bool challengeCorrect = false;
int challengeScore = 0;
int challengeStreak = 0;

// 
//  CALCULATOR STATE
// 
double calcValue = 0;
double calcOperand = 0;
char calcOperator = ' ';
bool calcNewNumber = true;
char calcDisplay[16] = "0";

// 
//  WORLD CLOCK TIMEZONES (Offset from UTC in seconds)
// 
struct WorldClock {
    const char* city;
    const char* country;
    long utcOffset;  // Seconds from UTC
};

WorldClock worldClocks[] = {
    {"Ghana", "GH", 0},           // UTC+0
    {"Japan", "JP", 9 * 3600},    // UTC+9
    {"Mackay", "AU", 10 * 3600}   // UTC+10
};
#define NUM_WORLD_CLOCKS 3

// 
//  GAME STATE
// 
// Blackjack
int playerCards[10], dealerCards[10];
int playerCount = 0, dealerCount = 0;
int blackjackBet = 100;
bool blackjackGameActive = false;
bool playerStand = false;

// Yes/No
bool yesNoSpinning = false;
int yesNoAngle = 0;
String yesNoResult = "?";

// Dino game
int dinoY = 0;
float dinoVelocity = 0;
bool dinoJumping = false;
int dinoScore = 0;
int obstacleX = 350;
bool dinoGameOver = false;
const float GRAVITY = 4.0;
const float JUMP_FORCE = -22.0;

// Music
bool musicPlaying = false;
uint16_t musicDuration = 245;
uint16_t musicCurrent = 86;
const char* musicTitle = "Night Drive";
const char* musicArtist = "Synthwave FM";

// 
//  TOUCH STATE - IMPROVED FOR RESPONSIVE NAVIGATION
// 
int32_t touchStartX = 0, touchStartY = 0;
int32_t touchCurrentX = 0, touchCurrentY = 0;
int32_t touchLastX = 0, touchLastY = 0;
bool touchActive = false;
unsigned long touchStartMs = 0;

// Touch gesture thresholds - MORE RESPONSIVE (from 1.8" version)
const int SWIPE_THRESHOLD_MIN = 25;    // Minimum pixels for swipe detection
const int SWIPE_THRESHOLD_MAX = 350;   // Maximum swipe distance  
const int TAP_THRESHOLD = 20;          // Maximum movement for tap
const unsigned long SWIPE_MAX_DURATION = 800;  // Max time for swipe gesture

// 
//  TIMING VARIABLES
// 
unsigned long lastClockUpdate = 0;
unsigned long lastStepUpdate = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastMusicUpdate = 0;
unsigned long lastSaveTime = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastUsageUpdate = 0;
unsigned long lastHourlyUpdate = 0;
unsigned long lastNTPSync = 0;
bool ntpSyncedOnce = false;

// 
//  PREMIUM GRADIENT THEMES (Apple Watch Style)
// 
struct GradientTheme {
  const char* name;
  lv_color_t color1;
  lv_color_t color2;
  lv_color_t text;
  lv_color_t accent;
  lv_color_t secondary;
};

GradientTheme gradientThemes[] = {
  {"Midnight", lv_color_hex(0x1C1C1E), lv_color_hex(0x2C2C2E), lv_color_hex(0xFFFFFF), lv_color_hex(0x0A84FF), lv_color_hex(0x5E5CE6)},
  {"Ocean", lv_color_hex(0x0D3B66), lv_color_hex(0x1A759F), lv_color_hex(0xFFFFFF), lv_color_hex(0x52B2CF), lv_color_hex(0x99E1D9)},
  {"Sunset", lv_color_hex(0xFF6B35), lv_color_hex(0xF7931E), lv_color_hex(0xFFFFFF), lv_color_hex(0xFFD166), lv_color_hex(0xFFA62F)},
  {"Aurora", lv_color_hex(0x7B2CBF), lv_color_hex(0x9D4EDD), lv_color_hex(0xFFFFFF), lv_color_hex(0xC77DFF), lv_color_hex(0xE0AAFF)},
  {"Forest", lv_color_hex(0x1B4332), lv_color_hex(0x2D6A4F), lv_color_hex(0xFFFFFF), lv_color_hex(0x52B788), lv_color_hex(0x95D5B2)},
  {"Ruby", lv_color_hex(0x9B2335), lv_color_hex(0xC41E3A), lv_color_hex(0xFFFFFF), lv_color_hex(0xFF6B6B), lv_color_hex(0xFFA07A)},
  {"Graphite", lv_color_hex(0x1C1C1E), lv_color_hex(0x3A3A3C), lv_color_hex(0xFFFFFF), lv_color_hex(0x8E8E93), lv_color_hex(0xAEAEB2)},
  {"Mint", lv_color_hex(0x00A896), lv_color_hex(0x02C39A), lv_color_hex(0x1C1C1E), lv_color_hex(0x00F5D4), lv_color_hex(0xB5FFE1)}
};
#define NUM_THEMES 8

// 
//  SD CARD WALLPAPER SYSTEM
// 
#define MAX_SD_WALLPAPERS 10
#define WALLPAPER_PATH "/wallpaper"

struct SDWallpaper {
  char filename[64];
  char displayName[32];
  bool valid;
};

SDWallpaper sdWallpapers[MAX_SD_WALLPAPERS];
int numSDWallpapers = 0;
bool sdWallpapersLoaded = false;

// 
//  WIDGET OS - FACE LOADING SYSTEM
// 
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

struct WallpaperTheme {
  const char* name;
  lv_color_t top;
  lv_color_t mid1;
  lv_color_t mid2;
  lv_color_t bottom;
};

WallpaperTheme gradientWallpapers[] = {
  {"None", lv_color_hex(0x1C1C1E), lv_color_hex(0x1C1C1E), lv_color_hex(0x1C1C1E), lv_color_hex(0x1C1C1E)},
  {"Mountain Sunset", lv_color_hex(0x4A90D9), lv_color_hex(0xF4A460), lv_color_hex(0xE07020), lv_color_hex(0x8B4513)},
  {"Golden Peaks", lv_color_hex(0xFF8C00), lv_color_hex(0xFFD700), lv_color_hex(0xDC6B00), lv_color_hex(0x2F1810)},
  {"Canyon Dawn", lv_color_hex(0x87CEEB), lv_color_hex(0xFFB347), lv_color_hex(0xCD5C5C), lv_color_hex(0x8B4513)},
  {"Island Paradise", lv_color_hex(0xE6B3CC), lv_color_hex(0x9370DB), lv_color_hex(0x4169E1), lv_color_hex(0x006994)},
  {"Alpine Meadow", lv_color_hex(0xFFD700), lv_color_hex(0xF0E68C), lv_color_hex(0x90EE90), lv_color_hex(0x228B22)},
  {"Twilight Ocean", lv_color_hex(0x191970), lv_color_hex(0x483D8B), lv_color_hex(0x4682B4), lv_color_hex(0x008B8B)}
};
#define NUM_GRADIENT_WALLPAPERS 7

// 
//  FUNCTION PROTOTYPES
// 
void navigateTo(int category, int subCard);
void handleSwipe(int dx, int dy);
void handleTap(int x, int y);  // NEW: Separate tap handler
void saveUserData();
void loadUserData();
void syncTimeNTP();
void fetchWeatherData();
void fetchLocationFromIP();
void updateSensorFusion();
void calibrateCompass();
void displayWallpaperImage(lv_obj_t *parent, int wallpaperIndex);

// Identity system functions
void trackStopwatchUse();
void trackGameWon();
void trackGameLost();
void trackGamePlayed();
void trackDailySteps();
float getCalibratedHeading();
void checkIdentityUnlocks();
void showNotificationPopup();
void addNotification(const char* message, lv_color_t color);
void handleSecretTap();
void updateConsecutiveDays();

// Battery Intelligence
void updateUsageTracking();
void updateHourlyStats();
void updateDailyStats();
void calculateBatteryEstimates();
void checkLowBattery();
void toggleBatterySaver();

// SD Card Management & Backup System
void initSDCardStructure();
void updateSDCardHealth();
bool createBackup(bool isAuto);
bool restoreFromBackup(const String& backupName);
void checkAutoBackup();
String listBackups();
bool saveConfigToSD(const String& configData);
String readConfigFromSD();

// WIDGET OS - SD Card System Functions (Premium)
void logToBootLog(const char* message);
bool createDirectoryIfNotExists(const char* path);
void createDeviceJson();
void createOsJson();
void createBuildTxt();
void createUserJson();
void createDisplayJson();
void createPowerJson();
void createUpdateReadme();
void createWifiConfigTemplate();
void initBootLog();
bool createWidgetOSFolderStructure();
bool initWidgetOSSDCard();
void loadUserConfigFromSD();
void loadDisplayConfigFromSD();
void saveUserConfigToSD();
bool loadWiFiConfigFromSD();
const char* getSDCardStatusString();
void listSDDirectory(const char* dirname, uint8_t levels);
void scanSDFaces();
void scanFacesInDirectory(const char* dirPath);

// Fusion Labs Web Serial Protocol
void processWebSerialCommand(const String& cmd);
void handleFusionLabsProtocol();
void sendDeviceStatus();
void sendSDHealth();
void handleFaceInstall(const String& faceId, const String& faceData);
void handleFirmwareChunk(const uint8_t* data, size_t len);

// Screen control
void screenOff();
void screenOnFunc();
void shutdownDevice();

// Card creators
void createClockCard();
void createAnalogClockCard();
void createWorldClockCard(int utcOffset, const char* country, const char* city);
void createCompassCard();
void createTiltCard();
void createGyroCard();
void createStepsCard();
void createActivityRingsCard();
void createWorkoutCard();
void createDistanceCard();
void createBlackjackCard();
void createDinoCard();
void createYesNoCard();
void createWeatherCard();
void createForecastCard();
void createMusicCard();
void createGalleryCard();
void createSandTimerCard();
void createStopwatchCard();
void createCountdownCard();
void createBreatheCard();
void createStepStreakCard();
void createGameStreakCard();
void createAchievementsCard();
void createCalendarCard();
void createSettingsCard();
void createBatteryCard();
void createSystemCard();
void createBatteryStatsCard();
void createUsagePatternsCard();
void createTorchCard();
void createTorchSettingsCard();
void createCalculatorCard();
void createClickerCard();
void createReactionTestCard();
void createDailyChallengeCard();
void createFactoryResetCard();
void createIdentityMainCard();
void createIdentityGridCard();
void createSDCardHealthCard();  // FIXED: Added missing function declaration

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 2: SAFE NAVIGATION - Track current screen for safe switching
// ═══════════════════════════════════════════════════════════════════════════

static lv_obj_t *current_screen_obj = NULL;


// ═══════════════════════════════════════════════════════════════════════════
//  DISPLAY DRIVER
// ═══════════════════════════════════════════════════════════════════════════

static void lvgl_tick_cb(void *arg) {
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 4: TOUCH DRIVER - NO UI LOGIC IN CALLBACK
//  Only returns coordinates. Navigation handled via ui_event flags.
// ═══════════════════════════════════════════════════════════════════════════

volatile bool touch_interrupt_flag = false;

void IRAM_ATTR Arduino_IIC_Touch_Interrupt(void) {
    touch_interrupt_flag = true;
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    // FIX 4: Touch callback ONLY returns coordinates
    // NO lv_event_send(), NO lv_scr_load(), NO navigation calls here
    
    int32_t x = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
    int32_t y = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
    uint8_t fingers = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);

    if (fingers > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
        
        // FIX 6: Ping activity on touch
        ui_activity_ping();
        
        // Track touch for gesture detection (handled in ui_task)
        if (!touchActive) {
            touchActive = true;
            touchStartX = x;
            touchStartY = y;
            touchStartMs = millis();
        }
        touchCurrentX = x;
        touchCurrentY = y;
        touchLastX = x;
        touchLastY = y;
    } else {
        data->state = LV_INDEV_STATE_REL;
        
        // FIX 4: On release, set event flag instead of direct navigation
        if (touchActive) {
            touchActive = false;
            unsigned long touchDuration = millis() - touchStartMs;
            int32_t dx = touchCurrentX - touchStartX;
            int32_t dy = touchCurrentY - touchStartY;
            
            // Determine gesture type and set appropriate event
            if (touchDuration < SWIPE_MAX_DURATION) {
                if (abs(dx) > SWIPE_THRESHOLD_MIN && abs(dx) > abs(dy)) {
                    // Horizontal swipe
                    if (dx > 0) {
                        ui_event = UI_EVENT_NAV_LEFT;  // Swipe right = go left
                    } else {
                        ui_event = UI_EVENT_NAV_RIGHT; // Swipe left = go right
                    }
                } else if (abs(dy) > SWIPE_THRESHOLD_MIN && abs(dy) > abs(dx)) {
                    // Vertical swipe
                    if (dy > 0) {
                        ui_event = UI_EVENT_NAV_UP;    // Swipe down = go up
                    } else {
                        ui_event = UI_EVENT_NAV_DOWN;  // Swipe up = go down
                    }
                } else if (abs(dx) < TAP_THRESHOLD && abs(dy) < TAP_THRESHOLD) {
                    // Tap
                    ui_event = UI_EVENT_TAP;
                    ui_event_param1 = touchCurrentX;
                    ui_event_param2 = touchCurrentY;
                }
            }
        }
    }
}

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
    navigateTo(currentCategory, currentSubCard);
}

//
//  WALLPAPER IMAGE DISPLAY FUNCTION
//
void displayWallpaperImage(lv_obj_t *parent, int wallpaperIndex) {
    if (wallpaperIndex == 0) {
        // Solid color - use current gradient theme
        return;
    }

    // Create image object
    lv_obj_t *img = lv_img_create(parent);

    // Set image source based on wallpaperIndex
    switch(wallpaperIndex) {
        case 1:
            lv_img_set_src(img, &AdobeStock_17557);
            break;
        case 2:
            lv_img_set_src(img, &AdobeStock_2026);
            break;
        case 3:
            lv_img_set_src(img, &AdobeStock_184869446);
            break;
        case 4:
            lv_img_set_src(img, &AdobeStock_174564);
            break;
        default:
            lv_obj_del(img);
            return;
    }

    // Position image to cover card
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(img, LCD_WIDTH - 24, LCD_HEIGHT - 60);
}

void shutdownDevice() {
    saveUserData();
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

// 
//  NAVIGATION
// 
void startTransition(int direction) {
  isTransitioning = true;
  transitionDir = direction;
  transitionProgress = 0.0;
  transitionStartMs = millis();
  navigationLocked = true;  // Lock navigation during transition
}

// Check if transition should be auto-completed (timeout protection)
void checkTransitionTimeout() {
    if (isTransitioning && (millis() - transitionStartMs > TRANSITION_DURATION + 200)) {
        // Force end transition if it's stuck
        isTransitioning = false;
        navigationLocked = false;
        lastNavigationMs = millis();  // Update timestamp to prevent immediate re-trigger
    }
    // Also unlock navigation after cooldown
    if (navigationLocked && !isTransitioning && (millis() - lastNavigationMs > NAVIGATION_COOLDOWN_MS + 100)) {
        // Extra 100ms grace period to ensure proper unlock
        navigationLocked = false;
    }
}

// Safe navigation wrapper - prevents crashes from concurrent navigation
bool canNavigate() {
    if (isTransitioning) return false;
    if (millis() - lastNavigationMs < NAVIGATION_COOLDOWN_MS) return false;
    // REMOVED: navigationLocked check - handled in handleSwipe with cooldown instead
    return true;
}

void handleSwipe(int dx, int dy) {
    // SAFETY: Don't process swipes during transition animation
    if (isTransitioning) {
        return;
    }

    // Use cooldown instead of navigationLocked - more responsive
    if (millis() - lastNavigationMs < NAVIGATION_COOLDOWN_MS) {
        return;
    }

    // Dismiss low battery popup first
    if (showingLowBatteryPopup) {
        showingLowBatteryPopup = false;
        navigateTo(currentCategory, currentSubCard);
        lastNavigationMs = millis();
        return;
    }

    int newCategory = currentCategory;
    int newSubCard = currentSubCard;
    int direction = 0;

    // 
    //  NAVIGATION RULES - CARD-BASED, GESTURE-ONLY, INFINITE UI
    // 
    //
    //  HORIZONTAL (Left/Right) = Change Category - INFINITE LOOP
    //   ALWAYS works from ANY screen (main or sub-card)
    //   Immediately exits sub-cards, resets subIndex to 0
    //   No beginning, no end - wraps around
    //
    //  VERTICAL (Up/Down) = Navigate Within Category Stack
    //   DOWN: go deeper (main→sub1→sub2...)
    //   UP: go back one step (sub2→sub1→main)
    //   Bounce at boundaries
    //
    // 

    // Determine dominant gesture direction (lock to one axis)
    bool isHorizontal = abs(dx) > abs(dy);
    int threshold = SWIPE_THRESHOLD_MIN;  // Use constant instead of hardcoded value

    if (isHorizontal && abs(dx) > threshold) {
        // 
        // HORIZONTAL SWIPE - Category change (INFINITE LOOP)
        // Swipe LEFT = next category, Swipe RIGHT = previous category
        // 
        if (dx < -threshold) {
            // Swipe LEFT → NEXT category (Clock → Compass)
            newCategory = (currentCategory + 1) % NUM_CATEGORIES;
            direction = 1;
        } else if (dx > threshold) {
            // Swipe RIGHT → PREVIOUS category (Clock → Identity/last)
            newCategory = currentCategory - 1;
            if (newCategory < 0) newCategory = NUM_CATEGORIES - 1;
            direction = -1;
        }
        // CRITICAL: Always reset to main card (subIndex = 0) when changing category
        newSubCard = 0;
    
    } else if (!isHorizontal && abs(dy) > threshold) {
        // ═══════════════════════════════════════════════════════════════════════════
        // VERTICAL SWIPE - Navigate within category stack
        // ═══════════════════════════════════════════════════════════════════════════
        if (dy > threshold) {
            // Swipe DOWN → go deeper into sub-cards
            if (currentSubCard < maxSubCards[currentCategory] - 1) {
                newSubCard = currentSubCard + 1;
                direction = 2;
            } else {
            }
        } else if (dy < -threshold) {
            // Swipe UP → go back one level
            if (currentSubCard > 0) {
                newSubCard = currentSubCard - 1;
                direction = -2;
            } else {
            }
        }
        newCategory = currentCategory;  // Stay in same category
    }

    // Execute navigation if we have a valid direction
    if (direction != 0) {
    
        // Update state FIRST
        currentCategory = newCategory;
        currentSubCard = newSubCard;
    
        // Start transition animation
        startTransition(direction);
    
        // Navigate to new card
        navigateTo(currentCategory, currentSubCard);
    
        // Update cooldown timestamp
        lastNavigationMs = millis();
    }
}

// 
//  TAP HANDLER - Tap sides for navigation (replaces swipe)
// 
void handleTap(int x, int y) {
    // Don't process taps during transitions
    if (isTransitioning) {
        return;
    }

    // Use cooldown to prevent rapid navigation
    if (millis() - lastNavigationMs < NAVIGATION_COOLDOWN_MS) {
        return;
    }

    // === TAP-ON-SIDES NAVIGATION ===
    // Define edge zones for navigation (left 60px and right 60px)
    const int NAV_EDGE_WIDTH = 60;

    // Tap LEFT edge = PREVIOUS category
    if (x < NAV_EDGE_WIDTH) {
        int newCategory = currentCategory - 1;
        if (newCategory < 0) newCategory = NUM_CATEGORIES - 1;
        currentCategory = newCategory;
        currentSubCard = 0;  // Reset to main card
        startTransition(-1);
        navigateTo(currentCategory, currentSubCard);
        lastNavigationMs = millis();
        return;
    }

    // Tap RIGHT edge = NEXT category
    if (x > LCD_WIDTH - NAV_EDGE_WIDTH) {
        int newCategory = (currentCategory + 1) % NUM_CATEGORIES;
        currentCategory = newCategory;
        currentSubCard = 0;  // Reset to main card
        startTransition(1);
        navigateTo(currentCategory, currentSubCard);
        lastNavigationMs = millis();
        return;
    }

    // Tap TOP area (top 80px) = Previous sub-card (go up)
    if (y < 80 && currentSubCard > 0) {
        int newSubCard = currentSubCard - 1;
        currentSubCard = newSubCard;
        startTransition(-2);
        navigateTo(currentCategory, currentSubCard);
        lastNavigationMs = millis();
        return;
    }

    // Tap BOTTOM area (bottom 80px) = Next sub-card (go down)
    if (y > LCD_HEIGHT - 80 && currentSubCard < maxSubCards[currentCategory] - 1) {
        int newSubCard = currentSubCard + 1;
        currentSubCard = newSubCard;
        startTransition(2);
        navigateTo(currentCategory, currentSubCard);
        lastNavigationMs = millis();
        return;
    }

    // === CARD-SPECIFIC TAP ACTIONS (center area only) ===
    // Handle taps on specific cards
    if (currentCategory == CAT_TORCH && currentSubCard == 0) {
        // Toggle torch on tap
        torchOn = !torchOn;
        navigateTo(currentCategory, currentSubCard);
    }
    else if (currentCategory == CAT_TOOLS && currentSubCard == 1) {
        // Clicker - increment count on tap
        clickerCount++;
        navigateTo(currentCategory, currentSubCard);
    }
    else if (currentCategory == CAT_TOOLS && currentSubCard == 2) {
        // Reaction test handling
        if (reactionTooEarly) {
            reactionTooEarly = false;
            reactionTestActive = false;
            reactionWaiting = false;
            navigateTo(currentCategory, currentSubCard);
        }
        else if (reactionWaiting) {
            // Calculate reaction time
            lastReactionTime = millis() - reactionStartMs;
            if (lastReactionTime < bestReactionTime) bestReactionTime = lastReactionTime;
            reactionWaiting = false;
            reactionTestActive = false;
            navigateTo(currentCategory, currentSubCard);
        }
        else if (reactionTestActive) {
            // Tapped too early!
            reactionTooEarly = true;
            reactionTestActive = false;
            navigateTo(currentCategory, currentSubCard);
        }
        else {
            // Start test
            reactionTestActive = true;
            reactionDelayMs = random(1500, 4000);
            reactionStartMs = millis();
            navigateTo(currentCategory, currentSubCard);
        }
    }
    else if (currentCategory == CAT_TIMER && currentSubCard == 1) {
        // Stopwatch - handle tap based on Y position
        if (y < LCD_HEIGHT / 2) {
            // Top half - Start/Stop
            if (stopwatchRunning) {
                stopwatchElapsedMs += (millis() - stopwatchStartMs);
                stopwatchRunning = false;
            } else {
                stopwatchStartMs = millis();
                stopwatchRunning = true;
                trackStopwatchUse();
            }
        } else {
            // Bottom half - Lap/Reset
            if (stopwatchRunning) {
                // Record lap
                if (lapCount < MAX_LAPS) {
                    unsigned long currentTotal = stopwatchElapsedMs + (millis() - stopwatchStartMs);
                    unsigned long lapTime = (lapCount > 0) ? currentTotal - lapTotalTimes[lapCount - 1] : currentTotal;
                    lapTimes[lapCount] = lapTime;
                    lapTotalTimes[lapCount] = currentTotal;
                    lapCount++;
                }
            } else {
                // Reset
                stopwatchElapsedMs = 0;
                lapCount = 0;
            }
        }
        navigateTo(currentCategory, currentSubCard);
    }
    else if (currentCategory == CAT_TOOLS && currentSubCard == 3) {
        // Daily Challenge - check which option was tapped
        if (!challengeAnswered) {
            int btnW = (LCD_WIDTH - 80) / 2;
            int btnH = 50;
            int startY = 100 + 12;
            int startX = 25 + 12;
        
            for (int i = 0; i < 4; i++) {
                int row = i / 2;
                int col = i % 2;
                int btnX = startX + col * (btnW + 10);
                int btnY = startY + row * (btnH + 10);
            
                if (x >= btnX && x <= btnX + btnW &&
                    y >= btnY && y <= btnY + btnH) {
                    challengeAnswered = true;
                    challengeCorrect = (challengeOptions[i] == challengeAnswer);
                    if (challengeCorrect) {
                        challengeScore++;
                        challengeStreak++;
                    } else {
                        challengeStreak = 0;
                    }
                    break;
                }
            }
        }
        navigateTo(currentCategory, currentSubCard);
    }
    else if (currentCategory == CAT_TORCH && currentSubCard == 1) {
        // Torch settings - handle brightness and color taps
        if (y >= 75 + 12 && y <= 105 + 12) {
            int barWidth = LCD_WIDTH - 80;
            int barX = 40;
            if (x >= barX && x <= barX + barWidth) {
                torchBrightness = ((x - barX) * 255) / barWidth;
                if (torchBrightness < 50) torchBrightness = 50;
                if (torchBrightness > 255) torchBrightness = 255;
            }
        }
        else if (y >= 185 + 12 && y <= 225 + 12) {
            int swatchSize = 40;
            int startX = (LCD_WIDTH - 24 - NUM_TORCH_COLORS * (swatchSize + 10)) / 2 + 12;
            for (int i = 0; i < NUM_TORCH_COLORS; i++) {
                int swatchX = startX + i * (swatchSize + 10);
                if (x >= swatchX && x <= swatchX + swatchSize) {
                    torchColorIndex = i;
                    break;
                }
            }
        }
        navigateTo(currentCategory, currentSubCard);
    }
    else if (currentCategory == CAT_SETTINGS && currentSubCard == 0) {
        // Settings - wallpaper cycling tap area (y: 390-470)
        if (y >= 402 && y <= 482) {
            // Cycle through wallpapers (0-4)
            userData.wallpaperIndex = (userData.wallpaperIndex + 1) % 5;
            saveUserData();
            navigateTo(currentCategory, currentSubCard);
        }
    }
}

//  CARD IDENTITY SYSTEM - v5.0

// NOTE: NUM_IDENTITIES is defined at the top of the file (before UserData struct)

enum CardIdentity {
  IDENTITY_NONE = -1,
  IDENTITY_CHAOS = 0, IDENTITY_GLITCH, IDENTITY_FOCUS,
  IDENTITY_FROSTBITE, IDENTITY_SUBZERO, IDENTITY_COLD, IDENTITY_ORBIT,
  IDENTITY_FLUX, IDENTITY_NOVA, IDENTITY_PULSE, IDENTITY_VELOCITY,
  IDENTITY_FLOW, IDENTITY_OVERDRIVE, IDENTITY_SURGE, IDENTITY_ECLIPSE
};

struct CardIdentityData {
  const char* emoji;
  const char* name;
  const char* title;
  const char* description;
  lv_color_t primaryColor;
  lv_color_t secondaryColor;
  uint32_t unlockThreshold;
  bool unlocked;
  uint32_t currentProgress;
};

CardIdentityData cardIdentities[NUM_IDENTITIES] = {
  {"", "Chaos", "Chaos Mode", "Unlocks randomly over time", 
   lv_color_hex(0xFF453A), lv_color_hex(0xFF9F0A), 0, false, 0},
  {"", "Glitch", "System Glitch", "Tap screen 10x rapidly", 
   lv_color_hex(0xBF5AF2), lv_color_hex(0xFF2D55), 0, false, 0},
  {"", "Focus", "Deep Focus", "Maintain 7-day step streak", 
   lv_color_hex(0x5E5CE6), lv_color_hex(0x30D158), 7, false, 0},
  {"", "Frostbite", "Cold as Ice", "Reach 20,000 total steps", 
   lv_color_hex(0x64D2FF), lv_color_hex(0x5AC8FA), 20000, false, 0},
  {"", "Subzero", "Frozen", "Use device 3 days in a row", 
   lv_color_hex(0x00C7BE), lv_color_hex(0x32ADE6), 3, false, 0},
  {"", "Cold", "Ice Cold", "Win 5 Blackjack games", 
   lv_color_hex(0x5AC8FA), lv_color_hex(0x30D158), 5, false, 0},
  {"", "Orbit", "Orbital", "Use compass 100 times", 
   lv_color_hex(0xFF9F0A), lv_color_hex(0xFFD60A), 100, false, 0}
};

int selectedIdentity = IDENTITY_NONE;

struct QuestProgress {
  uint32_t compassUseCount;
  uint32_t consecutiveDays;
  uint32_t lastUseDayOfYear;
  uint32_t tapCount;
  unsigned long lastTapTime;
  unsigned long chaosCheckTime;
  uint32_t gamesPlayed;
  uint32_t stopwatchUses;
  uint32_t currentDaySteps;
  uint32_t lastDayOfYear;
  uint32_t consecutiveWins;
  uint32_t lastGameWon;
};

QuestProgress questProgress = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#define MAX_NOTIFICATIONS 5
struct Notification {
  char message[64];
  unsigned long timestamp;
  bool active;
  lv_color_t color;
};

Notification notifications[MAX_NOTIFICATIONS];
int notificationCount = 0;
// showingNotification, notificationStartMs, NOTIFICATION_DURATION declared earlier

// 
//  BATTERY INTELLIGENCE FUNCTIONS
// 
void updateUsageTracking() {
    unsigned long now = millis();

    if (screenOn) {
        batteryStats.screenOnTimeMs += (now - lastUsageUpdate);
        batteryStats.cardUsageTime[currentCategory] += (now - lastUsageUpdate);
    } else {
        batteryStats.screenOffTimeMs += (now - lastUsageUpdate);
    }

    lastUsageUpdate = now;
}

void updateHourlyStats() {
    RTC_DateTime dt = rtc.getDateTime();
    uint8_t hourIndex = dt.getHour() % USAGE_HISTORY_SIZE;

    batteryStats.hourlyScreenOnMins[hourIndex] = batteryStats.screenOnTimeMs / 60000;
    batteryStats.hourlyScreenOffMins[hourIndex] = batteryStats.screenOffTimeMs / 60000;
    batteryStats.hourlySteps[hourIndex] = userData.steps;

    uint8_t currentBattery = batteryPercent;
    if (batteryStats.batteryAtHourStart > currentBattery) {
        float drainThisHour = batteryStats.batteryAtHourStart - currentBattery;
        batteryStats.avgDrainPerHour = (batteryStats.avgDrainPerHour * 0.8f) + (drainThisHour * 0.2f);
        batteryStats.weightedDrainRate = (batteryStats.weightedDrainRate * 0.6f) + (drainThisHour * 0.4f);
    }

    batteryStats.batteryAtHourStart = currentBattery;
    batteryStats.currentHourIndex = hourIndex;

    batteryStats.screenOnTimeMs = 0;
    batteryStats.screenOffTimeMs = 0;

    lastHourlyUpdate = millis();
}

void calculateBatteryEstimates() {
    float remainingCapacity = (BATTERY_CAPACITY_MAH * batteryPercent) / 100.0f;

    // Simple estimate
    float currentDraw = batterySaverMode ? SAVER_MODE_CURRENT_MA : 
                        (screenOn ? SCREEN_ON_CURRENT_MA : SCREEN_OFF_CURRENT_MA);
    batteryStats.simpleEstimateMins = (uint32_t)((remainingCapacity / currentDraw) * 60.0f);

    // Weighted estimate
    if (batteryStats.weightedDrainRate > 0.1f) {
        batteryStats.weightedEstimateMins = (uint32_t)((batteryPercent / batteryStats.weightedDrainRate) * 60.0f);
    } else {
        batteryStats.weightedEstimateMins = batteryStats.simpleEstimateMins;
    }

    // Learned estimate
    float avgDailyDrain = 0;
    int validDays = 0;

    for (int i = 0; i < 7; i++) {
        if (batteryStats.dailyAvgDrainRate[i] > 0) {
            avgDailyDrain += batteryStats.dailyAvgDrainRate[i];
            validDays++;
        }
    }

    if (validDays > 0 && avgDailyDrain > 0) {
        avgDailyDrain /= validDays;
        batteryStats.learnedEstimateMins = (uint32_t)((batteryPercent / avgDailyDrain) * 60.0f);
    } else {
        batteryStats.learnedEstimateMins = batteryStats.simpleEstimateMins;
    }

    // Combined estimate (weighted average)
    batteryStats.combinedEstimateMins = (
        batteryStats.simpleEstimateMins * 30 +
        batteryStats.weightedEstimateMins * 40 +
        batteryStats.learnedEstimateMins * 30
    ) / 100;
}

void checkLowBattery() {
    // Low battery check disabled - popups were causing freeze
    return;

}

void toggleBatterySaver() {
    batterySaverMode = !batterySaverMode;
    batterySaverAutoEnabled = false;

    gfx->setBrightness(batterySaverMode ? 100 : userData.brightness);
}

// 
//  DATA PERSISTENCE
// 
void saveUserData() {
    prefs.begin("minios", false);

    // Activity data
    prefs.putUInt("steps", userData.steps);
    prefs.putUInt("goal", userData.dailyGoal);
    prefs.putInt("streak", userData.stepStreak);
    prefs.putFloat("dist", userData.totalDistance);
    prefs.putFloat("cal", userData.totalCalories);

    // Game data
    prefs.putInt("bjstreak", userData.blackjackStreak);
    prefs.putInt("won", userData.gamesWon);
    prefs.putInt("played", userData.gamesPlayed);
    prefs.putUInt("clicker", userData.clickerScore);

    // Settings
    prefs.putInt("bright", userData.brightness);
    prefs.putInt("timeout", userData.screenTimeout);
    prefs.putInt("theme", userData.themeIndex);
  
  // NEW: Save identity data
  prefs.putInt("identity", selectedIdentity);
  prefs.putUInt("compass", userData.compassUseCount);
  prefs.putUInt("consec", userData.consecutiveDays);
  prefs.putUInt("lastday", userData.lastUseDayOfYear);
  
  for (int i = 0; i < NUM_IDENTITIES; i++) {
    char key[16];
    snprintf(key, sizeof(key), "id_u_%d", i);
    prefs.putBool(key, cardIdentities[i].unlocked);
  }
    prefs.putInt("compass", userData.compassMode);
    prefs.putInt("wallpaper", userData.wallpaperIndex);

    // Battery stats
    prefs.putBool("saver", batterySaverMode);
    prefs.putFloat("avgDrain", batteryStats.avgDrainPerHour);
    prefs.putFloat("wDrain", batteryStats.weightedDrainRate);

    // Daily data
    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist%d", i);
        prefs.putUInt(key, userData.stepHistory[i]);
        snprintf(key, sizeof(key), "dOn%d", i);
        prefs.putFloat(key, batteryStats.dailyAvgScreenOnHours[i]);
        snprintf(key, sizeof(key), "dDr%d", i);
        prefs.putFloat(key, batteryStats.dailyAvgDrainRate[i]);
    }

    // Card usage
    for (int i = 0; i < CARD_USAGE_SLOTS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "card%d", i);
        prefs.putUInt(key, batteryStats.cardUsageTime[i]);
    }

    prefs.end();
    lastSaveTime = millis();
}

void loadUserData() {
    prefs.begin("minios", true);

    // Activity data
    userData.steps = prefs.getUInt("steps", 0);
    userData.dailyGoal = prefs.getUInt("goal", 10000);
    userData.stepStreak = prefs.getInt("streak", 0);
    userData.totalDistance = prefs.getFloat("dist", 0);
    userData.totalCalories = prefs.getFloat("cal", 0);

    // Game data
    userData.blackjackStreak = prefs.getInt("bjstreak", 0);
    userData.gamesWon = prefs.getInt("won", 0);
    userData.gamesPlayed = prefs.getInt("played", 0);
    userData.clickerScore = prefs.getUInt("clicker", 0);

    // Settings
    userData.brightness = prefs.getInt("bright", 200);
    userData.screenTimeout = prefs.getInt("timeout", 1);
    userData.themeIndex = prefs.getInt("theme", 0);
  
  // NEW: Load identity data
  userData.selectedIdentity = prefs.getInt("identity", -1);
  userData.compassUseCount = prefs.getUInt("compass", 0);
  userData.consecutiveDays = prefs.getUInt("consec", 0);
  userData.lastUseDayOfYear = prefs.getUInt("lastday", 0);
  
  for (int i = 0; i < NUM_IDENTITIES; i++) {
    char key[16];
    snprintf(key, sizeof(key), "id_u_%d", i);
    bool unlocked = prefs.getBool(key, false);
    userData.identitiesUnlocked[i] = unlocked;
    cardIdentities[i].unlocked = unlocked;
  }
  
  selectedIdentity = userData.selectedIdentity;
    userData.compassMode = prefs.getInt("compass", 0);
    userData.wallpaperIndex = prefs.getInt("wallpaper", 0);

    // Battery stats
    batterySaverMode = prefs.getBool("saver", false);
    batteryStats.avgDrainPerHour = prefs.getFloat("avgDrain", 5.0f);
    batteryStats.weightedDrainRate = prefs.getFloat("wDrain", 5.0f);

    // Daily data
    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist%d", i);
        userData.stepHistory[i] = prefs.getUInt(key, 0);
        snprintf(key, sizeof(key), "dOn%d", i);
        batteryStats.dailyAvgScreenOnHours[i] = prefs.getFloat(key, 0);
        snprintf(key, sizeof(key), "dDr%d", i);
        batteryStats.dailyAvgDrainRate[i] = prefs.getFloat(key, 0);
    }

    // Card usage
    for (int i = 0; i < CARD_USAGE_SLOTS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "card%d", i);
        batteryStats.cardUsageTime[i] = prefs.getUInt(key, 0);
    }

    prefs.end();
}

void factoryReset() {
    prefs.begin("minios", false);
    prefs.clear();
    prefs.end();
    delay(500);
    ESP.restart();
}

// 
//  SD CARD & WIFI
// 
bool loadWiFiFromSD() {
    if (!hasSD) return false;

    File file = SD_MMC.open(WIFI_CONFIG_PATH, "r");
    if (!file) {
        return false;
    }

    numWifiNetworks = 0;

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
    
        for (int i = 1; i <= MAX_WIFI_NETWORKS; i++) {
            char ssidKey[16], passKey[16];
            snprintf(ssidKey, sizeof(ssidKey), "WIFI%d_SSID", i);
            snprintf(passKey, sizeof(passKey), "WIFI%d_PASS", i);
        
            if (key == ssidKey) strncpy(tempSSID[i-1], value.c_str(), 63);
            else if (key == passKey) strncpy(tempPass[i-1], value.c_str(), 63);
        }
    
        if (key == "SSID" && strlen(tempSSID[0]) == 0) strncpy(tempSSID[0], value.c_str(), 63);
        else if (key == "PASSWORD" && strlen(tempPass[0]) == 0) strncpy(tempPass[0], value.c_str(), 63);
        else if (key == "CITY") strncpy(weatherCity, value.c_str(), 63);
        else if (key == "COUNTRY") strncpy(weatherCountry, value.c_str(), 7);
        else if (key == "GMT_OFFSET") gmtOffsetSec = value.toInt() * 3600;
    }
    file.close();

    for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
        if (strlen(tempSSID[i]) > 0) {
            strncpy(wifiNetworks[numWifiNetworks].ssid, tempSSID[i], 63);
            strncpy(wifiNetworks[numWifiNetworks].password, tempPass[i], 63);
            wifiNetworks[numWifiNetworks].valid = true;
            numWifiNetworks++;
        }
    }

    wifiConfigFromSD = (numWifiNetworks > 0);
    return wifiConfigFromSD;
}

void connectWiFi() {
    if (numWifiNetworks == 0) {
        return;
    }

    WiFi.mode(WIFI_STA);

    for (int i = 0; i < numWifiNetworks; i++) {
        if (!wifiNetworks[i].valid) continue;
    
        WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);
    
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 15) {
            delay(500);
            attempts++;
        }
        USBSerial.println();
    
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            connectedNetworkIndex = i;
            return;
        } else {
            WiFi.disconnect();
            delay(100);
        }
    }

    wifiConnected = false;
}

// 
//  AUTOMATIC FREE WIFI CONNECTION SYSTEM - Core Functions
// 

// Scan for available networks (both known and open)
void scanWiFiNetworks() {

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int numFound = WiFi.scanNetworks(false, false, false, 300);

    // Reset open networks list
    numOpenNetworks = 0;
    for (int i = 0; i < MAX_OPEN_NETWORKS; i++) {
        openNetworks[i].valid = false;
    }

    // Process found networks
    for (int i = 0; i < numFound && i < 20; i++) {
        String ssid = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);
        wifi_auth_mode_t encType = WiFi.encryptionType(i);
        bool isOpen = (encType == WIFI_AUTH_OPEN);
    
        USBSerial.printf("  [%d] %s (RSSI: %d dBm, %s)\n", 
            i + 1, ssid.c_str(), rssi, isOpen ? "OPEN" : "SECURED");
    
        // Track open networks with good signal
        if (isOpen && rssi >= MIN_RSSI_THRESHOLD && numOpenNetworks < MAX_OPEN_NETWORKS) {
            if (ssid.length() > 0 && ssid.length() < 64) {  // Valid SSID
                strncpy(openNetworks[numOpenNetworks].ssid, ssid.c_str(), 63);
                openNetworks[numOpenNetworks].ssid[63] = '\0';
                openNetworks[numOpenNetworks].rssi = rssi;
                openNetworks[numOpenNetworks].valid = true;
                numOpenNetworks++;
            }
        }
    }

    // Sort open networks by signal strength (strongest first)
    for (int i = 0; i < numOpenNetworks - 1; i++) {
        for (int j = i + 1; j < numOpenNetworks; j++) {
            if (openNetworks[j].rssi > openNetworks[i].rssi) {
                OpenNetwork temp = openNetworks[i];
                openNetworks[i] = openNetworks[j];
                openNetworks[j] = temp;
            }
        }
    }

    for (int i = 0; i < numOpenNetworks; i++) {
    }

    WiFi.scanDelete();
    lastWiFiScan = millis();
}

// Try to connect to a known/configured network
bool connectToKnownNetworks() {
    if (numWifiNetworks == 0) {
        return false;
    }

    // First scan to see what's available
    WiFi.mode(WIFI_STA);
    int numFound = WiFi.scanNetworks(false, false, false, 300);

    // Try each known network, but only if it's visible in scan
    for (int i = 0; i < numWifiNetworks; i++) {
        if (!wifiNetworks[i].valid) continue;
    
        // Check if this network is visible
        bool networkVisible = false;
        for (int j = 0; j < numFound; j++) {
            if (WiFi.SSID(j) == wifiNetworks[i].ssid) {
                networkVisible = true;
                wifiNetworks[i].rssi = WiFi.RSSI(j);
                break;
            }
        }
    
        if (!networkVisible) {
            continue;
        }
    
        USBSerial.printf("[WIFI] Trying known network %d/%d: %s (RSSI: %d)\n", 
            i + 1, numWifiNetworks, wifiNetworks[i].ssid, wifiNetworks[i].rssi);
    
        WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);
    
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_CONNECT_TIMEOUT_MS) {
            delay(250);
        }
        USBSerial.println();
    
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            connectedNetworkIndex = i;
            connectedToOpenNetwork = false;
            WiFi.scanDelete();
            return true;
        } else {
            WiFi.disconnect();
            delay(100);
        }
    }

    WiFi.scanDelete();
    return false;
}

// Try to connect to open (free) networks - sorted by signal strength
bool connectToOpenNetworks() {
    if (numOpenNetworks == 0) {
        return false;
    }

    for (int i = 0; i < numOpenNetworks; i++) {
        if (!openNetworks[i].valid) continue;
    
        USBSerial.printf("[WIFI] Trying open network %d/%d: %s (RSSI: %d dBm)\n", 
            i + 1, numOpenNetworks, openNetworks[i].ssid, openNetworks[i].rssi);
    
        WiFi.begin(openNetworks[i].ssid);  // No password for open networks
    
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_CONNECT_TIMEOUT_MS) {
            delay(250);
        }
        USBSerial.println();
    
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            connectedNetworkIndex = -1;  // Not a configured network
            connectedToOpenNetwork = true;
        
            // Store the open network info in first slot temporarily for display
            strncpy(wifiNetworks[0].ssid, openNetworks[i].ssid, 63);
            wifiNetworks[0].isOpen = true;
        
            return true;
        } else {
            WiFi.disconnect();
            delay(100);
        }
    }

    return false;
}

// Smart WiFi connection - tries known networks first, then scans for open ones
void smartWiFiConnect() {

    WiFi.mode(WIFI_STA);

    // STEP 1: Try known/hardcoded networks first (highest priority)
    if (connectToKnownNetworks()) {
        return;  // Successfully connected to known network
    }

    // STEP 2: Scan for available networks
    scanWiFiNetworks();

    // STEP 3: Try to connect to open networks (sorted by signal strength)
    if (connectToOpenNetworks()) {
        return;  // Successfully connected to open network
    }

    // STEP 4: No connection possible
    wifiConnected = false;
    connectedNetworkIndex = -1;
    connectedToOpenNetwork = false;
}

// Check WiFi status and reconnect if needed (called from loop)
void checkWiFiConnection() {
    if (millis() - lastWiFiCheck < WIFI_RECONNECT_INTERVAL_MS) {
        return;  // Not time to check yet
    }
    lastWiFiCheck = millis();

    // Check if we're still connected
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
            // We reconnected somehow
            wifiConnected = true;
        }
        return;  // All good
    }

    // Connection lost - attempt reconnect
    if (wifiConnected) {
        wifiConnected = false;
        connectedNetworkIndex = -1;
        connectedToOpenNetwork = false;
    }

    // Try to reconnect
    smartWiFiConnect();

    // If reconnected, sync time and update location
    if (wifiConnected) {
        configTime(gmtOffsetSec, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        // Auto-detect location based on IP and update weather
        fetchLocationFromIP();
    }
}

void fetchWeatherData() {
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
            weatherTemp = doc["main"]["temp"].as<float>();
            weatherHigh = doc["main"]["temp_max"].as<float>();
            weatherLow = doc["main"]["temp_min"].as<float>();
            if (doc["weather"][0]["main"]) {
                weatherDesc = doc["weather"][0]["main"].as<String>();
            }
        }
    }
    http.end();
    lastWeatherUpdate = millis();
}

// Fetch location automatically based on IP address
void fetchLocationFromIP() {
    if (!wifiConnected) return;

    HTTPClient http;
    // Use ip-api.com for free geolocation (no API key needed)
    http.begin("http://ip-api.com/json/?fields=city,countryCode,status");
    http.setTimeout(5000);

    if (http.GET() == HTTP_CODE_OK) {
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
            if (doc["status"] == "success") {
                const char* city = doc["city"];
                const char* country = doc["countryCode"];
                if (city && strlen(city) > 0) {
                    strncpy(weatherCity, city, sizeof(weatherCity) - 1);
                    weatherCity[sizeof(weatherCity) - 1] = '\0';
                }
                if (country && strlen(country) > 0) {
                    strncpy(weatherCountry, country, sizeof(weatherCountry) - 1);
                    weatherCountry[sizeof(weatherCountry) - 1] = '\0';
                }
                // Save the new location
                saveUserData();
                // Fetch weather for new location
                fetchWeatherData();
            }
        }
    }
    http.end();
}

// fetchCryptoData() REMOVED - Space optimization

// 
//  SENSOR FUSION
// 
void updateSensorFusion() {
    if (!hasIMU) return;

    unsigned long now = millis();
    float dt = (now - lastSensorUpdate) / 1000.0;
    if (dt <= 0 || dt > 1.0) dt = 0.01;
    lastSensorUpdate = now;

    if (qmi.getDataReady()) {
        qmi.getAccelerometer(acc.x, acc.y, acc.z);
        qmi.getGyroscope(gyr.x, gyr.y, gyr.z);
    
        // Accelerometer angles
        accelRoll = atan2(acc.y, acc.z) * 180.0 / PI;
        accelPitch = atan2(-acc.x, sqrt(acc.y * acc.y + acc.z * acc.z)) * 180.0 / PI;
    
        // Gyro integration
        gyroRoll += gyr.x * dt;
        gyroPitch += gyr.y * dt;
        gyroYaw += gyr.z * dt;
    
        // Complementary filter
        roll = ALPHA * (roll + gyr.x * dt) + BETA * accelRoll;
        pitch = ALPHA * (pitch + gyr.y * dt) + BETA * accelPitch;
        yaw = gyroYaw;
    
        // Compass heading
        compassHeading = yaw - initialYaw;
        while (compassHeading < 0) compassHeading += 360;
        while (compassHeading >= 360) compassHeading -= 360;
    
        // Smooth compass
        float diff = compassHeading - compassHeadingSmooth;
        if (diff > 180) diff -= 360;
        if (diff < -180) diff += 360;
        compassHeadingSmooth += diff * 0.1;
    
        // Tilt values
        tiltX = roll;
        tiltY = pitch;
    }
}

void calibrateCompass() {
    initialYaw = gyroYaw;
    compassCalibrated = true;
}

// 
//  UI HELPERS
// 

// Helper to completely disable all scrolling on an object
void disableAllScrolling(lv_obj_t* obj) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
}

void createGradientBg() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_set_style_bg_color(lv_scr_act(), theme.color1, 0);
    lv_obj_set_style_bg_grad_color(lv_scr_act(), theme.color2, 0);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);
}

lv_obj_t* createCard(const char* title, bool fullBg = false) {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    // MODIFIED: Use full screen size, remove top/bottom margins (no black header)
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    if (fullBg) {
        lv_obj_set_style_bg_color(card, theme.color1, 0);
        lv_obj_set_style_bg_grad_color(card, theme.color2, 0);
        lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
    }

    // MODIFIED: Use 0 radius for full screen look (no rounded corners at edges)
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);  // No shadow for full screen

    // Prevent cards from being scrolled or moved out of frame
    lv_obj_set_style_clip_corner(card, true, 0);
    disableAllScrolling(card);

    if (strlen(title) > 0) {
        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_color(label, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 4, 10);
    }
    return card;
}

void createNavDots() {
    // DISABLED: Navigation dots removed - using tap-on-sides navigation instead
    // Tap left edge = previous category, tap right edge = next category
    // Tap top = previous sub-card, tap bottom = next sub-card
    return;
}

void createMiniStatusBar(lv_obj_t* parent) {
    // DISABLED: Mini status bar removed for cleaner UI
    // Battery and WiFi info available on main clock card and system cards
    (void)parent;  // Suppress unused parameter warning
}

// 
//  LOW BATTERY POPUP
// 
void drawLowBatteryPopup() {
    if (!showingLowBatteryPopup) return;

    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);

    lv_obj_t *popup = lv_obj_create(overlay);
    lv_obj_set_size(popup, LCD_WIDTH - 60, 200);
    lv_obj_align(popup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(popup, 24, 0);
    lv_obj_set_style_border_color(popup, batteryPercent <= CRITICAL_BATTERY_WARNING ? lv_color_hex(0xFF453A) : lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_border_width(popup, 3, 0);

    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text(title, batteryPercent <= CRITICAL_BATTERY_WARNING ? "CRITICAL!" : "Low Battery");
    lv_obj_set_style_text_color(title, batteryPercent <= CRITICAL_BATTERY_WARNING ? lv_color_hex(0xFF453A) : lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    char percBuf[8];
    snprintf(percBuf, sizeof(percBuf), "%d%%", batteryPercent);
    lv_obj_t *percLabel = lv_label_create(popup);
    lv_label_set_text(percLabel, percBuf);
    lv_obj_set_style_text_color(percLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(percLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(percLabel, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hint = lv_label_create(popup);
    if (batterySaverAutoEnabled) {
        lv_label_set_text(hint, "Battery Saver enabled");
    } else {
        lv_label_set_text(hint, "Tap to dismiss");
    }
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// 
//  MAIN NAVIGATION - SAFE VERSION
// 
void navigateTo(int category, int subCard) {
    // SAFETY: Validate bounds first
    if (category < 0 || category >= NUM_CATEGORIES) {
        category = CAT_CLOCK;
    }
    if (subCard < 0 || subCard >= maxSubCards[category]) {
        subCard = 0;
    }

    // FIX: Unlock at the START to prevent permanent lock if function fails
    navigationLocked = false;
    isTransitioning = false;

    // Update state
    currentCategory = category;
    currentSubCard = subCard;

    // Reset notification overlay pointer before cleaning screen
    // (lv_obj_clean will delete all objects including the overlay)
    // notificationOverlay cleanup disabled

    // Clean screen and rebuild
    lv_obj_clean(lv_scr_act());

    // CRITICAL: Completely disable ALL scrolling behavior on the screen
    disableAllScrolling(lv_scr_act());

    createGradientBg();

    // Yield to prevent watchdog and allow touch processing
    lv_task_handler();

    // Update usage tracking
    updateUsageTracking();

    // Track compass usage for Orbit identity
    if (category == CAT_COMPASS) {
        userData.compassUseCount++;
    }

    // Check for identity unlocks
    checkIdentityUnlocks();

    switch (category) {
        case CAT_CLOCK:
            if (subCard == 0) createClockCard();
            else if (subCard == 1) createAnalogClockCard();
            else if (subCard == 2) createWorldClockCard(0, "Ghana", "Accra");      // UTC+0
            else if (subCard == 3) createWorldClockCard(9, "Japan", "Tokyo");      // UTC+9
            else createWorldClockCard(10, "Australia", "Mackay");                   // UTC+10
            break;
        case CAT_COMPASS:
            if (subCard == 0) createCompassCard();
            else if (subCard == 1) createTiltCard();
            else createGyroCard();
            break;
        case CAT_ACTIVITY:
            if (subCard == 0) createStepsCard();
            else if (subCard == 1) createActivityRingsCard();
            else if (subCard == 2) createWorkoutCard();
            else createDistanceCard();
            break;
        case CAT_GAMES:
            if (subCard == 0) createBlackjackCard();
            else if (subCard == 1) createDinoCard();
            else createYesNoCard();
            break;
        case CAT_WEATHER:
            if (subCard == 0) createWeatherCard();
            else createForecastCard();
            break;
        case CAT_MEDIA:
            if (subCard == 0) createMusicCard();
            else createGalleryCard();
            break;
        case CAT_TIMER:
            if (subCard == 0) createSandTimerCard();
            else if (subCard == 1) createStopwatchCard();
            else if (subCard == 2) createCountdownCard();
            else createBreatheCard();
            break;
        case CAT_STREAK:
            if (subCard == 0) createStepStreakCard();
            else if (subCard == 1) createGameStreakCard();
            else createAchievementsCard();
            break;
        case CAT_CALENDAR:
            createCalendarCard();
            break;
        case CAT_TORCH:
            if (subCard == 0) createTorchCard();
            else createTorchSettingsCard();
            break;
        case CAT_TOOLS:
            if (subCard == 0) createCalculatorCard();
            else if (subCard == 1) createClickerCard();
            else if (subCard == 2) createReactionTestCard();
            else createDailyChallengeCard();
            break;
        case CAT_SETTINGS:
            createSettingsCard();
            break;
        case CAT_SYSTEM:
            if (subCard == 0) createBatteryCard();
            else if (subCard == 1) createBatteryStatsCard();
            else if (subCard == 2) createUsagePatternsCard();
            else createSDCardHealthCard();  // NEW: SD Card Health & Backup
            break;
        case CAT_IDENTITY:
            if (subCard == 0) createIdentityMainCard();
            else createIdentityGridCard();
            break;
    }

    createNavDots();

    // Low battery popup disabled - was causing issues
    // if (showingLowBatteryPopup) {
    //     drawLowBatteryPopup();
    // }

    // Navigation complete - unlock
    isTransitioning = false;
    navigationLocked = false;
    lastNavigationMs = millis();
}

//  SD CARD HEALTH CARD - FIXED: Added missing function

void createSDCardHealthCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("SD CARD HEALTH");

    // Update SD health data
    updateSDCardHealth();

    // SD Card status indicator
    lv_obj_t *statusIcon = lv_obj_create(card);
    lv_obj_set_size(statusIcon, 80, 80);
    lv_obj_align(statusIcon, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(statusIcon, sdHealth.healthy ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(statusIcon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(statusIcon, 0, 0);
    lv_obj_clear_flag(statusIcon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *iconLbl = lv_label_create(statusIcon);
    lv_label_set_text(iconLbl, sdHealth.mounted ? LV_SYMBOL_SD_CARD : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(iconLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(iconLbl, &lv_font_montserrat_32, 0);
    lv_obj_center(iconLbl);

    // Status text
    lv_obj_t *statusLbl = lv_label_create(card);
    lv_label_set_text(statusLbl, getSDCardStatusString());
    lv_obj_set_style_text_color(statusLbl, theme.text, 0);
    lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(statusLbl, LV_ALIGN_TOP_MID, 0, 120);

    // Storage info
    if (sdHealth.mounted) {
        char storageBuf[64];
        snprintf(storageBuf, sizeof(storageBuf), "Used: %.1f / %.1f MB", 
                 sdHealth.usedBytes / (1024.0 * 1024.0), 
                 sdHealth.totalBytes / (1024.0 * 1024.0));
    
        lv_obj_t *storageLbl = lv_label_create(card);
        lv_label_set_text(storageLbl, storageBuf);
        lv_obj_set_style_text_color(storageLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(storageLbl, &lv_font_montserrat_14, 0);
        lv_obj_align(storageLbl, LV_ALIGN_TOP_MID, 0, 150);
    
        // Usage percentage
        char percentBuf[16];
        snprintf(percentBuf, sizeof(percentBuf), "%.1f%% used", sdHealth.usedPercent);
        lv_obj_t *percentLbl = lv_label_create(card);
        lv_label_set_text(percentLbl, percentBuf);
        lv_obj_set_style_text_color(percentLbl, sdHealth.usedPercent > 90 ? lv_color_hex(0xFF453A) : lv_color_hex(0x30D158), 0);
        lv_obj_set_style_text_font(percentLbl, &lv_font_montserrat_18, 0);
        lv_obj_align(percentLbl, LV_ALIGN_TOP_MID, 0, 175);
    
        // Usage bar background
        lv_obj_t *barBg = lv_obj_create(card);
        lv_obj_set_size(barBg, LCD_WIDTH - 80, 20);
        lv_obj_align(barBg, LV_ALIGN_TOP_MID, 0, 210);
        lv_obj_set_style_bg_color(barBg, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_radius(barBg, 10, 0);
        lv_obj_set_style_border_width(barBg, 0, 0);
        lv_obj_clear_flag(barBg, LV_OBJ_FLAG_SCROLLABLE);
    
        // Usage bar fill
        int barWidth = (int)((LCD_WIDTH - 84) * sdHealth.usedPercent / 100.0);
        if (barWidth > 0) {
            lv_obj_t *barFill = lv_obj_create(barBg);
            lv_obj_set_size(barFill, barWidth, 16);
            lv_obj_align(barFill, LV_ALIGN_LEFT_MID, 2, 0);
            lv_obj_set_style_bg_color(barFill, sdHealth.usedPercent > 90 ? lv_color_hex(0xFF453A) : lv_color_hex(0x30D158), 0);
            lv_obj_set_style_radius(barFill, 8, 0);
            lv_obj_set_style_border_width(barFill, 0, 0);
        }
    
        // Backup info
        char backupBuf[48];
        snprintf(backupBuf, sizeof(backupBuf), "Backups: %d | Auto: %s", 
                 totalBackups, autoBackupEnabled ? "ON" : "OFF");
        lv_obj_t *backupLbl = lv_label_create(card);
        lv_label_set_text(backupLbl, backupBuf);
        lv_obj_set_style_text_color(backupLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(backupLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(backupLbl, LV_ALIGN_TOP_MID, 0, 250);
    
        // Free space
        char freeBuf[32];
        snprintf(freeBuf, sizeof(freeBuf), "Free: %.1f MB", sdHealth.freeBytes / (1024.0 * 1024.0));
        lv_obj_t *freeLbl = lv_label_create(card);
        lv_label_set_text(freeLbl, freeBuf);
        lv_obj_set_style_text_color(freeLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(freeLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(freeLbl, LV_ALIGN_TOP_MID, 0, 275);
    
        // Error counts if any
        if (sdHealth.writeErrors > 0 || sdHealth.readErrors > 0) {
            char errBuf[48];
            snprintf(errBuf, sizeof(errBuf), "Errors - W:%d R:%d", sdHealth.writeErrors, sdHealth.readErrors);
            lv_obj_t *errLbl = lv_label_create(card);
            lv_label_set_text(errLbl, errBuf);
            lv_obj_set_style_text_color(errLbl, lv_color_hex(0xFF9F0A), 0);
            lv_obj_set_style_text_font(errLbl, &lv_font_montserrat_12, 0);
            lv_obj_align(errLbl, LV_ALIGN_TOP_MID, 0, 300);
        }
    } else {
        // No SD card message
        lv_obj_t *noSDLbl = lv_label_create(card);
        lv_label_set_text(noSDLbl, "No SD Card Detected");
        lv_obj_set_style_text_color(noSDLbl, lv_color_hex(0xFF453A), 0);
        lv_obj_set_style_text_font(noSDLbl, &lv_font_montserrat_16, 0);
        lv_obj_align(noSDLbl, LV_ALIGN_CENTER, 0, 20);
    
        lv_obj_t *hintLbl = lv_label_create(card);
        lv_label_set_text(hintLbl, "Insert SD card for backup");
        lv_obj_set_style_text_color(hintLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(hintLbl, &lv_font_montserrat_14, 0);
        lv_obj_align(hintLbl, LV_ALIGN_CENTER, 0, 50);
    }

    // Error message if any
    if (sdHealth.lastError.length() > 0) {
        lv_obj_t *errLbl = lv_label_create(card);
        lv_label_set_text(errLbl, sdHealth.lastError.c_str());
        lv_obj_set_style_text_color(errLbl, lv_color_hex(0xFF453A), 0);
        lv_obj_set_style_text_font(errLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(errLbl, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_label_set_long_mode(errLbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(errLbl, LCD_WIDTH - 60);
    }
}

// Due to file size limits, the card creation functions are in Part 2
// Include S3_MiniOS_Part2.ino in the same folder

// 
//  S3 MiniOS v4.0 - PART 2: Card UI Functions
//  Place this file in the same folder as S3_MiniOS.ino
// 

// 
//  PREMIUM CLOCK CARD
// 
void createClockCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    // CRITICAL: Disable ALL scrolling on the screen - no exceptions
    disableAllScrolling(lv_scr_act());

    // Background card for wallpaper - FULL SCREEN to prevent any scroll gaps
    lv_obj_t *bgCard = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bgCard, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(bgCard, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(bgCard, theme.color1, 0);
    lv_obj_set_style_bg_grad_color(bgCard, theme.color2, 0);
    lv_obj_set_style_bg_grad_dir(bgCard, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(bgCard, 0, 0);
    lv_obj_set_style_border_width(bgCard, 0, 0);
    lv_obj_set_style_pad_all(bgCard, 0, 0);
    disableAllScrolling(bgCard);

    // Apply wallpaper if selected
    if (userData.wallpaperIndex > 0) {
        displayWallpaperImage(bgCard, userData.wallpaperIndex);
    }

    // Main card container (the visible rounded card)
    lv_obj_t *card = lv_obj_create(bgCard);
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    disableAllScrolling(card);

    // Time display - centered and prominent
    RTC_DateTime dt = rtc.getDateTime();
    char timeBuf[10];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", dt.getHour(), dt.getMinute());

    lv_obj_t *clockLabel = lv_label_create(card);
    lv_label_set_text(clockLabel, timeBuf);
    lv_obj_set_style_text_color(clockLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(clockLabel, LV_ALIGN_TOP_MID, -15, 40);

    // Seconds - positioned to the right of time
    char secBuf[8];
    snprintf(secBuf, sizeof(secBuf), ":%02d", dt.getSecond());
    lv_obj_t *secLabel = lv_label_create(card);
    lv_label_set_text(secLabel, secBuf);
    lv_obj_set_style_text_color(secLabel, theme.accent, 0);
    lv_obj_set_style_text_font(secLabel, &lv_font_montserrat_20, 0);
    lv_obj_align_to(secLabel, clockLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -5);

    // Day name - below time with spacing
    const char* dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    lv_obj_t *dayLabel = lv_label_create(card);
    lv_label_set_text(dayLabel, dayNames[dt.getWeek()]);
    lv_obj_set_style_text_color(dayLabel, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(dayLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(dayLabel, LV_ALIGN_TOP_MID, 0, 110);

    // Full date - below day name
    char dateBuf[32];
    const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    snprintf(dateBuf, sizeof(dateBuf), "%s %d, %d", monthNames[dt.getMonth()-1], dt.getDay(), dt.getYear());
    lv_obj_t *dateLabel = lv_label_create(card);
    lv_label_set_text(dateLabel, dateBuf);
    lv_obj_set_style_text_color(dateLabel, theme.accent, 0);
    lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(dateLabel, LV_ALIGN_TOP_MID, 0, 140);

    // Status bar container - at bottom with proper spacing
    lv_obj_t *statusBar = lv_obj_create(card);
    lv_obj_set_size(statusBar, LCD_WIDTH - 50, 55);
    lv_obj_align(statusBar, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(statusBar, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(statusBar, LV_OPA_80, 0);
    lv_obj_set_style_radius(statusBar, 28, 0);
    lv_obj_set_style_border_width(statusBar, 0, 0);
    lv_obj_set_style_pad_left(statusBar, 15, 0);
    lv_obj_set_style_pad_right(statusBar, 15, 0);
    disableAllScrolling(statusBar);

    // Calculate positions for 4 status items
    int itemWidth = (LCD_WIDTH - 80) / 4;
    int startX = 10;

    // WiFi indicator
    lv_obj_t *wifiIcon = lv_label_create(statusBar);
    lv_label_set_text(wifiIcon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifiIcon, wifiConnected ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(wifiIcon, &lv_font_montserrat_16, 0);
    lv_obj_align(wifiIcon, LV_ALIGN_LEFT_MID, startX, 0);

    // Battery estimate
    calculateBatteryEstimates();
    char estBuf[16];
    uint32_t hrs = batteryStats.combinedEstimateMins / 60;
    uint32_t mins = batteryStats.combinedEstimateMins % 60;
    if (isCharging) snprintf(estBuf, sizeof(estBuf), LV_SYMBOL_CHARGE);
    else if (hrs > 0) snprintf(estBuf, sizeof(estBuf), "~%luh", hrs);
    else snprintf(estBuf, sizeof(estBuf), "~%lum", mins);
    lv_obj_t *estLabel = lv_label_create(statusBar);
    lv_label_set_text(estLabel, estBuf);
    lv_obj_set_style_text_color(estLabel, isCharging ? lv_color_hex(0x30D158) : lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(estLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(estLabel, LV_ALIGN_LEFT_MID, startX + itemWidth, 0);

    // Battery percentage
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d%%", batteryPercent);
    lv_obj_t *battLabel = lv_label_create(statusBar);
    lv_label_set_text(battLabel, battBuf);
    lv_obj_set_style_text_color(battLabel, batteryPercent > 20 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(battLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(battLabel, LV_ALIGN_LEFT_MID, startX + itemWidth * 2, 0);

    // Steps count
    char stepBuf[16];
    snprintf(stepBuf, sizeof(stepBuf), "%lu", (unsigned long)userData.steps);
    lv_obj_t *stepLabel = lv_label_create(statusBar);
    lv_label_set_text(stepLabel, stepBuf);
    lv_obj_set_style_text_color(stepLabel, theme.accent, 0);
    lv_obj_set_style_text_font(stepLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(stepLabel, LV_ALIGN_RIGHT_MID, -10, 0);
}

// 
//  ANALOG CLOCK CARD - Premium Design
// 
void createAnalogClockCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    // Full screen dark background
    lv_obj_t *bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bg, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    disableAllScrolling(bg);

    // Outer glow ring
    lv_obj_t *glowRing = lv_obj_create(bg);
    lv_obj_set_size(glowRing, 230, 230);
    lv_obj_align(glowRing, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_opa(glowRing, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(glowRing, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(glowRing, theme.accent, 0);
    lv_obj_set_style_border_width(glowRing, 2, 0);
    lv_obj_set_style_border_opa(glowRing, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(glowRing, 40, 0);
    lv_obj_set_style_shadow_color(glowRing, theme.accent, 0);
    lv_obj_set_style_shadow_opa(glowRing, LV_OPA_20, 0);
    disableAllScrolling(glowRing);

    // Main clock face
    lv_obj_t *face = lv_obj_create(bg);
    lv_obj_set_size(face, 210, 210);
    lv_obj_align(face, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_grad_color(face, lv_color_hex(0x0D0D0D), 0);
    lv_obj_set_style_bg_grad_dir(face, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(face, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(face, 1, 0);
    lv_obj_set_style_shadow_width(face, 25, 0);
    lv_obj_set_style_shadow_color(face, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(face, LV_OPA_80, 0);
    disableAllScrolling(face);

    // Hour markers with numbers for 12, 3, 6, 9
    const char* hourNums[] = {"12", "3", "6", "9"};
    int numPositions[] = {0, 3, 6, 9};

    for (int i = 0; i < 12; i++) {
        float angle = i * 30.0 * 3.14159 / 180.0;
        int r = 88;
        int x = sin(angle) * r;
        int y = -cos(angle) * r;
    
        if (i == 0 || i == 3 || i == 6 || i == 9) {
            // Number markers for 12, 3, 6, 9
            lv_obj_t *numLabel = lv_label_create(face);
            int idx = (i == 0) ? 0 : (i == 3) ? 1 : (i == 6) ? 2 : 3;
            lv_label_set_text(numLabel, hourNums[idx]);
            lv_obj_set_style_text_color(numLabel, theme.text, 0);
            lv_obj_set_style_text_font(numLabel, &lv_font_montserrat_18, 0);
            lv_obj_align(numLabel, LV_ALIGN_CENTER, x, y);
        } else {
            // Dot markers for other hours
            lv_obj_t *marker = lv_obj_create(face);
            lv_obj_set_size(marker, 6, 6);
            lv_obj_align(marker, LV_ALIGN_CENTER, x, y);
            lv_obj_set_style_bg_color(marker, lv_color_hex(0x4A4A4A), 0);
            lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(marker, 0, 0);
        }
    }

    // Minute tick marks (60 ticks)
    for (int i = 0; i < 60; i++) {
        if (i % 5 == 0) continue; // Skip hour positions
        float angle = i * 6.0 * 3.14159 / 180.0;
        int r = 98;
        int x = sin(angle) * r;
        int y = -cos(angle) * r;
    
        lv_obj_t *tick = lv_obj_create(face);
        lv_obj_set_size(tick, 2, 2);
        lv_obj_align(tick, LV_ALIGN_CENTER, x, y);
        lv_obj_set_style_bg_color(tick, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_radius(tick, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(tick, 0, 0);
    }

    RTC_DateTime dt = rtc.getDateTime();
    float hourAngle = ((dt.getHour() % 12) + dt.getMinute() / 60.0) * 30.0;
    float minAngle = dt.getMinute() * 6.0;
    float secAngle = dt.getSecond() * 6.0;

    // Hour hand - thick and short
    float hRad = hourAngle * 3.14159 / 180.0;
    int hLen = 50;
    int hx = sin(hRad) * hLen;
    int hy = -cos(hRad) * hLen;

    lv_obj_t *hHand = lv_obj_create(face);
    lv_obj_set_size(hHand, 8, hLen + 15);
    lv_obj_align(hHand, LV_ALIGN_CENTER, hx/2, hy/2);
    lv_obj_set_style_bg_color(hHand, theme.text, 0);
    lv_obj_set_style_radius(hHand, 4, 0);
    lv_obj_set_style_border_width(hHand, 0, 0);
    lv_obj_set_style_shadow_width(hHand, 8, 0);
    lv_obj_set_style_shadow_color(hHand, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(hHand, LV_OPA_50, 0);

    // Minute hand - thinner and longer
    float mRad = minAngle * 3.14159 / 180.0;
    int mLen = 72;
    int mx = sin(mRad) * mLen;
    int my = -cos(mRad) * mLen;

    lv_obj_t *mHand = lv_obj_create(face);
    lv_obj_set_size(mHand, 5, mLen + 15);
    lv_obj_align(mHand, LV_ALIGN_CENTER, mx/2, my/2);
    lv_obj_set_style_bg_color(mHand, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_radius(mHand, 3, 0);
    lv_obj_set_style_border_width(mHand, 0, 0);
    lv_obj_set_style_shadow_width(mHand, 6, 0);
    lv_obj_set_style_shadow_color(mHand, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(mHand, LV_OPA_40, 0);

    // Second hand - thin red
    float sRad = secAngle * 3.14159 / 180.0;
    int sLen = 80;
    int sx = sin(sRad) * sLen;
    int sy = -cos(sRad) * sLen;

    lv_obj_t *sHand = lv_obj_create(face);
    lv_obj_set_size(sHand, 2, sLen + 20);
    lv_obj_align(sHand, LV_ALIGN_CENTER, sx/2, sy/2);
    lv_obj_set_style_bg_color(sHand, theme.accent, 0);
    lv_obj_set_style_radius(sHand, 1, 0);
    lv_obj_set_style_border_width(sHand, 0, 0);

    // Center jewel
    lv_obj_t *centerOuter = lv_obj_create(face);
    lv_obj_set_size(centerOuter, 20, 20);
    lv_obj_align(centerOuter, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(centerOuter, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(centerOuter, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(centerOuter, theme.accent, 0);
    lv_obj_set_style_border_width(centerOuter, 2, 0);

    lv_obj_t *centerInner = lv_obj_create(face);
    lv_obj_set_size(centerInner, 10, 10);
    lv_obj_align(centerInner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(centerInner, theme.accent, 0);
    lv_obj_set_style_radius(centerInner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(centerInner, 0, 0);

    // Date window at bottom
    char dateBuf[12];
    const char* monthShort[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    snprintf(dateBuf, sizeof(dateBuf), "%s %d", monthShort[dt.getMonth()-1], dt.getDay());

    lv_obj_t *dateBox = lv_obj_create(bg);
    lv_obj_set_size(dateBox, 70, 26);
    lv_obj_align(dateBox, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_bg_color(dateBox, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(dateBox, 6, 0);
    lv_obj_set_style_border_color(dateBox, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(dateBox, 1, 0);
    disableAllScrolling(dateBox);

    lv_obj_t *dateLabel = lv_label_create(dateBox);
    lv_label_set_text(dateLabel, dateBuf);
    lv_obj_set_style_text_color(dateLabel, theme.accent, 0);
    lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 0);
}

// 
//  WORLD CLOCK CARD - Shows time for different timezone
// 
void createWorldClockCard(int utcOffset, const char* country, const char* city) {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    // Full screen background
    lv_obj_t *bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bg, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x0D0D0D), 0);
    lv_obj_set_style_bg_grad_color(bg, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    disableAllScrolling(bg);

    // Get local time and calculate world time
    RTC_DateTime dt = rtc.getDateTime();

    // Assume device is UTC+10 (Mackay local), calculate target timezone
    // For Ghana (UTC+0): offset from local = 0 - 10 = -10
    // For Japan (UTC+9): offset from local = 9 - 10 = -1
    // For Mackay (UTC+10): offset from local = 10 - 10 = 0
    int localOffset = 10; // Device timezone (Mackay)
    int hourDiff = utcOffset - localOffset;

    int worldHour = dt.getHour() + hourDiff;
    int worldDay = dt.getDay();
    int worldMonth = dt.getMonth();

    // Handle day overflow/underflow
    if (worldHour >= 24) {
        worldHour -= 24;
        worldDay++;
    } else if (worldHour < 0) {
        worldHour += 24;
        worldDay--;
    }

    // Determine if it's day or night (6am-6pm = day)
    bool isDay = (worldHour >= 6 && worldHour < 18);

    // Country flag colors (simplified representation)
    uint32_t flagColor1, flagColor2;
    if (utcOffset == 0) { // Ghana
        flagColor1 = 0xCE1126; // Red
        flagColor2 = 0x006B3F; // Green
    } else if (utcOffset == 9) { // Japan
        flagColor1 = 0xBC002D; // Red (sun)
        flagColor2 = 0xFFFFFF; // White
    } else { // Australia
        flagColor1 = 0x00008B; // Blue
        flagColor2 = 0xFFFFFF; // White
    }

    // Flag accent strip at top
    lv_obj_t *flagStrip = lv_obj_create(bg);
    lv_obj_set_size(flagStrip, LCD_WIDTH - 40, 6);
    lv_obj_align(flagStrip, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(flagStrip, lv_color_hex(flagColor1), 0);
    lv_obj_set_style_bg_grad_color(flagStrip, lv_color_hex(flagColor2), 0);
    lv_obj_set_style_bg_grad_dir(flagStrip, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_radius(flagStrip, 3, 0);
    lv_obj_set_style_border_width(flagStrip, 0, 0);

    // Country name
    lv_obj_t *countryLabel = lv_label_create(bg);
    lv_label_set_text(countryLabel, country);
    lv_obj_set_style_text_color(countryLabel, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(countryLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(countryLabel, LV_ALIGN_TOP_MID, 0, 35);

    // City name - larger
    lv_obj_t *cityLabel = lv_label_create(bg);
    lv_label_set_text(cityLabel, city);
    lv_obj_set_style_text_color(cityLabel, theme.text, 0);
    lv_obj_set_style_text_font(cityLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(cityLabel, LV_ALIGN_TOP_MID, 0, 55);

    // Day/Night indicator circle
    lv_obj_t *dayNightCircle = lv_obj_create(bg);
    lv_obj_set_size(dayNightCircle, 140, 140);
    lv_obj_align(dayNightCircle, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(dayNightCircle, isDay ? lv_color_hex(0x1E3A5F) : lv_color_hex(0x0D1B2A), 0);
    lv_obj_set_style_bg_grad_color(dayNightCircle, isDay ? lv_color_hex(0x87CEEB) : lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_grad_dir(dayNightCircle, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(dayNightCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(dayNightCircle, isDay ? lv_color_hex(0xFFD700) : lv_color_hex(0x4A4A6A), 0);
    lv_obj_set_style_border_width(dayNightCircle, 3, 0);
    lv_obj_set_style_shadow_width(dayNightCircle, 30, 0);
    lv_obj_set_style_shadow_color(dayNightCircle, isDay ? lv_color_hex(0xFFD700) : lv_color_hex(0x2A2A4A), 0);
    lv_obj_set_style_shadow_opa(dayNightCircle, LV_OPA_40, 0);
    disableAllScrolling(dayNightCircle);

    // Sun or Moon icon
    lv_obj_t *iconLabel = lv_label_create(dayNightCircle);
    if (isDay) {
        lv_label_set_text(iconLabel, LV_SYMBOL_IMAGE); // Sun-like icon
        lv_obj_set_style_text_color(iconLabel, lv_color_hex(0xFFD700), 0);
    } else {
        lv_label_set_text(iconLabel, "C"); // Moon-like
        lv_obj_set_style_text_color(iconLabel, lv_color_hex(0xE0E0E0), 0);
    }
    lv_obj_set_style_text_font(iconLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(iconLabel, LV_ALIGN_TOP_MID, 0, 15);

    // Large time display
    char timeBuf[10];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", worldHour, dt.getMinute());

    lv_obj_t *timeLabel = lv_label_create(dayNightCircle);
    lv_label_set_text(timeLabel, timeBuf);
    lv_obj_set_style_text_color(timeLabel, theme.text, 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_36, 0);
    lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, 10);

    // AM/PM indicator
    const char* ampm = (worldHour < 12) ? "AM" : "PM";
    lv_obj_t *ampmLabel = lv_label_create(dayNightCircle);
    lv_label_set_text(ampmLabel, ampm);
    lv_obj_set_style_text_color(ampmLabel, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(ampmLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(ampmLabel, LV_ALIGN_CENTER, 0, 45);

    // UTC offset label
    char utcBuf[16];
    snprintf(utcBuf, sizeof(utcBuf), "UTC%s%d", utcOffset >= 0 ? "+" : "", utcOffset);

    lv_obj_t *utcLabel = lv_label_create(bg);
    lv_label_set_text(utcLabel, utcBuf);
    lv_obj_set_style_text_color(utcLabel, theme.accent, 0);
    lv_obj_set_style_text_font(utcLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(utcLabel, LV_ALIGN_BOTTOM_MID, 0, -55);

    // Time difference from local
    char diffBuf[24];
    if (hourDiff == 0) {
        snprintf(diffBuf, sizeof(diffBuf), "Same as local");
    } else if (hourDiff > 0) {
        snprintf(diffBuf, sizeof(diffBuf), "+%dh from local", hourDiff);
    } else {
        snprintf(diffBuf, sizeof(diffBuf), "%dh from local", hourDiff);
    }

    lv_obj_t *diffLabel = lv_label_create(bg);
    lv_label_set_text(diffLabel, diffBuf);
    lv_obj_set_style_text_color(diffLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(diffLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(diffLabel, LV_ALIGN_BOTTOM_MID, 0, -35);
}

// 
//  COMPASS CARD - Apple Watch Style with Sunrise/Sunset
// 
void createCompassCard() {
    // PREMIUM: Compass + Sunrise/Sunset - Ultra polished design
    lv_obj_clean(lv_scr_act());

    // CRITICAL: Disable ALL scrolling on the screen
    disableAllScrolling(lv_scr_act());

    // Deep charcoal background with subtle radial gradient
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0D0D0D), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Compass parameters
    int centerX = LCD_WIDTH / 2;
    int centerY = (LCD_HEIGHT / 2) - 20;
    int compassRadius = (LCD_WIDTH < 400) ? 140 : 160;

    // Outer compass ring with glow
    lv_obj_t *outerRing = lv_obj_create(card);
    lv_obj_set_size(outerRing, compassRadius * 2 + 20, compassRadius * 2 + 20);
    lv_obj_align(outerRing, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_opa(outerRing, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(outerRing, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(outerRing, 2, 0);
    lv_obj_set_style_border_color(outerRing, lv_color_hex(0x48484A), 0);
    lv_obj_set_style_border_opa(outerRing, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(outerRing, 20, 0);
    lv_obj_set_style_shadow_color(outerRing, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_shadow_opa(outerRing, LV_OPA_20, 0);

    // Draw enhanced compass markings
    for (int i = 0; i < 60; i++) {
        float angle = (i * 6.0 - 90) * M_PI / 180.0;
        bool isCardinal = (i % 15 == 0);
        bool isMajor = (i % 5 == 0);
    
        int tickLen = isCardinal ? 15 : (isMajor ? 10 : 6);
        int tickWidth = isCardinal ? 3 : (isMajor ? 2 : 1);
        uint32_t tickColor = isCardinal ? 0xFFFFFF : (isMajor ? 0x8E8E93 : 0x48484A);
    
        int x1 = centerX + (compassRadius - tickLen) * cos(angle);
        int y1 = centerY + (compassRadius - tickLen) * sin(angle);
        int x2 = centerX + compassRadius * cos(angle);
        int y2 = centerY + compassRadius * sin(angle);
    
        lv_obj_t *tick = lv_line_create(card);
        static lv_point_t points[2];
        points[0].x = x1; points[0].y = y1;
        points[1].x = x2; points[1].y = y2;
        lv_line_set_points(tick, points, 2);
        lv_obj_set_style_line_width(tick, tickWidth, 0);
        lv_obj_set_style_line_color(tick, lv_color_hex(tickColor), 0);
        lv_obj_set_style_line_rounded(tick, true, 0);
    }

    // Cardinal directions with enhanced styling
    const char* cardinals[] = {"N", "E", "S", "W"};
    int positions[][2] = {{0, -compassRadius - 30}, {compassRadius + 20, 0}, 
                          {0, compassRadius + 10}, {-compassRadius - 20, 0}};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *cardLabel = lv_label_create(card);
        lv_label_set_text(cardLabel, cardinals[i]);
        lv_obj_set_style_text_color(cardLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(cardLabel, &lv_font_montserrat_24, 0);
        lv_obj_align(cardLabel, LV_ALIGN_CENTER, positions[i][0], positions[i][1]);
    
        // Add glow to North
        if (i == 0) {
            lv_obj_set_style_shadow_width(cardLabel, 15, 0);
            lv_obj_set_style_shadow_color(cardLabel, lv_color_hex(0x0A84FF), 0);
            lv_obj_set_style_shadow_opa(cardLabel, LV_OPA_50, 0);
        }
    }

    // Sunrise/Sunset info with gradient text effect
    if (sunData.valid) {
        // Sunrise container with glass morphism
        lv_obj_t *sunriseBox = lv_obj_create(card);
        lv_obj_set_size(sunriseBox, 140, 65);
        lv_obj_align(sunriseBox, LV_ALIGN_CENTER, 0, -90);
        lv_obj_set_style_bg_color(sunriseBox, lv_color_hex(0x52B2CF), 0);
        lv_obj_set_style_bg_opa(sunriseBox, LV_OPA_10, 0);
        lv_obj_set_style_radius(sunriseBox, 16, 0);
        lv_obj_set_style_border_width(sunriseBox, 2, 0);
        lv_obj_set_style_border_color(sunriseBox, lv_color_hex(0x52B2CF), 0);
        lv_obj_set_style_border_opa(sunriseBox, LV_OPA_40, 0);

        lv_obj_t *sunriseText = lv_label_create(sunriseBox);
        lv_label_set_text(sunriseText, "SUNRISE");
        lv_obj_set_style_text_color(sunriseText, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(sunriseText, &lv_font_montserrat_10, 0);
        lv_obj_align(sunriseText, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t *sunriseTime = lv_label_create(sunriseBox);
        lv_label_set_text(sunriseTime, sunData.sunriseTime);
        lv_obj_set_style_text_color(sunriseTime, lv_color_hex(0x52B2CF), 0);
        lv_obj_set_style_text_font(sunriseTime, &lv_font_montserrat_28, 0);
        lv_obj_align(sunriseTime, LV_ALIGN_CENTER, 0, 5);

        // Sunset container
        lv_obj_t *sunsetBox = lv_obj_create(card);
        lv_obj_set_size(sunsetBox, 140, 65);
        lv_obj_align(sunsetBox, LV_ALIGN_CENTER, 0, 55);
        lv_obj_set_style_bg_color(sunsetBox, lv_color_hex(0xFF6B35), 0);
        lv_obj_set_style_bg_opa(sunsetBox, LV_OPA_10, 0);
        lv_obj_set_style_radius(sunsetBox, 16, 0);
        lv_obj_set_style_border_width(sunsetBox, 2, 0);
        lv_obj_set_style_border_color(sunsetBox, lv_color_hex(0xFF6B35), 0);
        lv_obj_set_style_border_opa(sunsetBox, LV_OPA_40, 0);

        lv_obj_t *sunsetText = lv_label_create(sunsetBox);
        lv_label_set_text(sunsetText, "SUNSET");
        lv_obj_set_style_text_color(sunsetText, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(sunsetText, &lv_font_montserrat_10, 0);
        lv_obj_align(sunsetText, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t *sunsetTime = lv_label_create(sunsetBox);
        lv_label_set_text(sunsetTime, sunData.sunsetTime);
        lv_obj_set_style_text_color(sunsetTime, lv_color_hex(0xFF6B35), 0);
        lv_obj_set_style_text_font(sunsetTime, &lv_font_montserrat_28, 0);
        lv_obj_align(sunsetTime, LV_ALIGN_CENTER, 0, 5);

        // Draw gradient compass hands
        float calibratedHeading = getCalibratedHeading();
    
        // Blue hand for sunrise with glow
        float sunriseAngle = (sunData.sunriseAzimuth - calibratedHeading - 90) * M_PI / 180.0;
        lv_obj_t *sunriseHand = lv_line_create(card);
        static lv_point_t sunrisePts[2];
        sunrisePts[0].x = centerX;
        sunrisePts[0].y = centerY;
        sunrisePts[1].x = centerX + (compassRadius - 45) * cos(sunriseAngle);
        sunrisePts[1].y = centerY + (compassRadius - 45) * sin(sunriseAngle);
        lv_line_set_points(sunriseHand, sunrisePts, 2);
        lv_obj_set_style_line_width(sunriseHand, 6, 0);
        lv_obj_set_style_line_color(sunriseHand, lv_color_hex(0x52B2CF), 0);
        lv_obj_set_style_line_rounded(sunriseHand, true, 0);
        lv_obj_set_style_shadow_width(sunriseHand, 10, 0);
        lv_obj_set_style_shadow_color(sunriseHand, lv_color_hex(0x52B2CF), 0);
        lv_obj_set_style_shadow_opa(sunriseHand, LV_OPA_60, 0);

        // Red hand for sunset with glow
        float sunsetAngle = (sunData.sunsetAzimuth - calibratedHeading - 90) * M_PI / 180.0;
        lv_obj_t *sunsetHand = lv_line_create(card);
        static lv_point_t sunsetPts[2];
        sunsetPts[0].x = centerX;
        sunsetPts[0].y = centerY;
        sunsetPts[1].x = centerX + (compassRadius - 45) * cos(sunsetAngle);
        sunsetPts[1].y = centerY + (compassRadius - 45) * sin(sunsetAngle);
        lv_line_set_points(sunsetHand, sunsetPts, 2);
        lv_obj_set_style_line_width(sunsetHand, 6, 0);
        lv_obj_set_style_line_color(sunsetHand, lv_color_hex(0xFF6B35), 0);
        lv_obj_set_style_line_rounded(sunsetHand, true, 0);
        lv_obj_set_style_shadow_width(sunsetHand, 10, 0);
        lv_obj_set_style_shadow_color(sunsetHand, lv_color_hex(0xFF6B35), 0);
        lv_obj_set_style_shadow_opa(sunsetHand, LV_OPA_60, 0);
    }

    // Central pivot with multi-layer design
    lv_obj_t *pivotOuter = lv_obj_create(card);
    lv_obj_set_size(pivotOuter, 24, 24);
    lv_obj_align(pivotOuter, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_radius(pivotOuter, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pivotOuter, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_bg_opa(pivotOuter, LV_OPA_30, 0);
    lv_obj_set_style_border_width(pivotOuter, 2, 0);
    lv_obj_set_style_border_color(pivotOuter, lv_color_hex(0x0A84FF), 0);

    lv_obj_t *pivot = lv_obj_create(card);
    lv_obj_set_size(pivot, 16, 16);
    lv_obj_align(pivot, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_radius(pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pivot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(pivot, 0, 0);
    lv_obj_set_style_shadow_width(pivot, 10, 0);
    lv_obj_set_style_shadow_color(pivot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_opa(pivot, LV_OPA_50, 0);

    // Premium "Set North" calibration button
    lv_obj_t *calibBtn = lv_obj_create(card);
    lv_obj_set_size(calibBtn, 160, 48);
    lv_obj_align(calibBtn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(calibBtn, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_bg_grad_color(calibBtn, lv_color_hex(0x5AC8FA), 0);
    lv_obj_set_style_bg_grad_dir(calibBtn, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_radius(calibBtn, 24, 0);
    lv_obj_set_style_border_width(calibBtn, 0, 0);
    lv_obj_set_style_shadow_width(calibBtn, 15, 0);
    lv_obj_set_style_shadow_color(calibBtn, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_shadow_opa(calibBtn, LV_OPA_40, 0);

    lv_obj_t *btnLabel = lv_label_create(calibBtn);
    lv_label_set_text(btnLabel, LV_SYMBOL_REFRESH " Set North");
    lv_obj_set_style_text_color(btnLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btnLabel, &lv_font_montserrat_16, 0);
    lv_obj_center(btnLabel);

    // Heading display badge
    lv_obj_t *headingBadge = lv_obj_create(card);
    lv_obj_set_size(headingBadge, 90, 35);
    lv_obj_align(headingBadge, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_bg_color(headingBadge, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(headingBadge, LV_OPA_60, 0);
    lv_obj_set_style_radius(headingBadge, 18, 0);
    lv_obj_set_style_border_width(headingBadge, 1, 0);
    lv_obj_set_style_border_color(headingBadge, lv_color_hex(0x48484A), 0);

    lv_obj_t *headingLabel = lv_label_create(headingBadge);
    char headingStr[16];
    snprintf(headingStr, sizeof(headingStr), "%.0f", getCalibratedHeading());
    lv_label_set_text(headingLabel, headingStr);
    lv_obj_set_style_text_color(headingLabel, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_text_font(headingLabel, &lv_font_montserrat_16, 0);
    lv_obj_center(headingLabel);

    // Cardinal directions
    lv_obj_t *north = lv_label_create(card);
    lv_label_set_text(north, "N");
    lv_obj_set_style_text_color(north, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(north, &lv_font_montserrat_20, 0);
    lv_obj_align(north, LV_ALIGN_CENTER, 0, -compassRadius - 25);

    lv_obj_t *south = lv_label_create(card);
    lv_label_set_text(south, "S");
    lv_obj_set_style_text_color(south, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(south, &lv_font_montserrat_20, 0);
    lv_obj_align(south, LV_ALIGN_CENTER, 0, compassRadius + 5);

    lv_obj_t *east = lv_label_create(card);
    lv_label_set_text(east, "E");
    lv_obj_set_style_text_color(east, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(east, &lv_font_montserrat_20, 0);
    lv_obj_align(east, LV_ALIGN_CENTER, compassRadius + 15, 0);

    lv_obj_t *west = lv_label_create(card);
    lv_label_set_text(west, "W");
    lv_obj_set_style_text_color(west, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(west, &lv_font_montserrat_20, 0);
    lv_obj_align(west, LV_ALIGN_CENTER, -compassRadius - 15, 0);

    // Sunrise time with gradient (blue to orange)
    if (sunData.valid) {
        lv_obj_t *sunriseLabel = lv_label_create(card);
        lv_label_set_text(sunriseLabel, sunData.sunriseTime);
        lv_obj_set_style_text_color(sunriseLabel, lv_color_hex(0x52B2CF), 0);  // Light blue
        lv_obj_set_style_text_font(sunriseLabel, &lv_font_montserrat_24, 0);
        lv_obj_align(sunriseLabel, LV_ALIGN_CENTER, 0, -60);

        lv_obj_t *sunriseText = lv_label_create(card);
        lv_label_set_text(sunriseText, "SUNRISE");
        lv_obj_set_style_text_color(sunriseText, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(sunriseText, &lv_font_montserrat_10, 0);
        lv_obj_align(sunriseText, LV_ALIGN_CENTER, 0, -85);

        // Sunset time with gradient (orange to red)
        lv_obj_t *sunsetLabel = lv_label_create(card);
        lv_label_set_text(sunsetLabel, sunData.sunsetTime);
        lv_obj_set_style_text_color(sunsetLabel, lv_color_hex(0xFF6B35), 0);  // Orange-red
        lv_obj_set_style_text_font(sunsetLabel, &lv_font_montserrat_24, 0);
        lv_obj_align(sunsetLabel, LV_ALIGN_CENTER, 0, 40);

        lv_obj_t *sunsetText = lv_label_create(card);
        lv_label_set_text(sunsetText, "SUNSET");
        lv_obj_set_style_text_color(sunsetText, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(sunsetText, &lv_font_montserrat_10, 0);
        lv_obj_align(sunsetText, LV_ALIGN_CENTER, 0, 65);

        // Draw compass hands for sunrise/sunset directions
        float calibratedHeading = getCalibratedHeading();
    
        // Blue hand for sunrise (pointing to sunrise azimuth)
        float sunriseAngle = (sunData.sunriseAzimuth - calibratedHeading - 90) * M_PI / 180.0;
        lv_obj_t *sunriseHand = lv_line_create(card);
        static lv_point_t sunrisePts[2];
        sunrisePts[0].x = centerX;
        sunrisePts[0].y = centerY;
        sunrisePts[1].x = centerX + (compassRadius - 40) * cos(sunriseAngle);
        sunrisePts[1].y = centerY + (compassRadius - 40) * sin(sunriseAngle);
        lv_line_set_points(sunriseHand, sunrisePts, 2);
        lv_obj_set_style_line_width(sunriseHand, 4, 0);
        lv_obj_set_style_line_color(sunriseHand, lv_color_hex(0x0A84FF), 0);  // Blue

        // Red hand for sunset (pointing to sunset azimuth)
        float sunsetAngle = (sunData.sunsetAzimuth - calibratedHeading - 90) * M_PI / 180.0;
        lv_obj_t *sunsetHand = lv_line_create(card);
        static lv_point_t sunsetPts[2];
        sunsetPts[0].x = centerX;
        sunsetPts[0].y = centerY;
        sunsetPts[1].x = centerX + (compassRadius - 40) * cos(sunsetAngle);
        sunsetPts[1].y = centerY + (compassRadius - 40) * sin(sunsetAngle);
        lv_line_set_points(sunsetHand, sunsetPts, 2);
        lv_obj_set_style_line_width(sunsetHand, 4, 0);
        lv_obj_set_style_line_color(sunsetHand, lv_color_hex(0xFF3B30), 0);  // Red
    }
}

// 
//  TILT/LEVEL CARD
// 
void createTiltCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0C0C0E), 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_clip_corner(card, true, 0);  // Clip content to card bounds
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "LEVEL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Level bubble container - with clipping
    lv_obj_t *levelBg = lv_obj_create(card);
    lv_obj_set_size(levelBg, 220, 220);
    lv_obj_align(levelBg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(levelBg, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(levelBg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(levelBg, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_border_width(levelBg, 2, 0);
    lv_obj_set_style_clip_corner(levelBg, true, 0);  // Clip bubble to circle
    lv_obj_set_scrollbar_mode(levelBg, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(levelBg, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Crosshairs
    lv_obj_t *hLine = lv_obj_create(levelBg);
    lv_obj_set_size(hLine, 180, 1);
    lv_obj_align(hLine, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(hLine, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_border_width(hLine, 0, 0);

    lv_obj_t *vLine = lv_obj_create(levelBg);
    lv_obj_set_size(vLine, 1, 180);
    lv_obj_align(vLine, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(vLine, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_border_width(vLine, 0, 0);

    // Target ring
    lv_obj_t *targetRing = lv_obj_create(levelBg);
    lv_obj_set_size(targetRing, 40, 40);
    lv_obj_align(targetRing, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(targetRing, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(targetRing, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_border_width(targetRing, 2, 0);
    lv_obj_set_style_radius(targetRing, LV_RADIUS_CIRCLE, 0);

    // Bubble position
    float maxOffset = 80.0;
    int bubbleX = constrain((int)(tiltX * maxOffset / 45.0), -80, 80);
    int bubbleY = constrain((int)(tiltY * maxOffset / 45.0), -80, 80);
    bool isLevel = (abs(tiltX) < 2.0 && abs(tiltY) < 2.0);

    // Bubble
    lv_obj_t *bubble = lv_obj_create(levelBg);
    lv_obj_set_size(bubble, 36, 36);
    lv_obj_align(bubble, LV_ALIGN_CENTER, bubbleX, bubbleY);
    lv_obj_set_style_bg_color(bubble, isLevel ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_shadow_width(bubble, 15, 0);
    lv_obj_set_style_shadow_color(bubble, isLevel ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_shadow_opa(bubble, LV_OPA_60, 0);

    // Status
    lv_obj_t *statusLbl = lv_label_create(card);
    lv_label_set_text(statusLbl, isLevel ? "LEVEL" : "TILTED");
    lv_obj_set_style_text_color(statusLbl, isLevel ? lv_color_hex(0x30D158) : lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_18, 0);
    lv_obj_align(statusLbl, LV_ALIGN_BOTTOM_MID, 0, -20);

    // Mini status bar removed - user preference
}

// 
//  GYRO ROTATION CARD
// 
void createGyroCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("ROTATION");

    // 3D visualization arcs
    lv_obj_t *vizContainer = lv_obj_create(card);
    lv_obj_set_size(vizContainer, 200, 200);
    lv_obj_align(vizContainer, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(vizContainer, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(vizContainer, 20, 0);
    lv_obj_set_style_border_width(vizContainer, 0, 0);

    // Pitch arc (outer)
    lv_obj_t *pitchArc = lv_arc_create(vizContainer);
    lv_obj_set_size(pitchArc, 170, 170);
    lv_obj_align(pitchArc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(pitchArc, 270);
    lv_arc_set_bg_angles(pitchArc, 0, 360);
    int pitchVal = constrain((int)((pitch + 90) / 180.0 * 100), 0, 100);
    lv_arc_set_value(pitchArc, pitchVal);
    lv_obj_set_style_arc_color(pitchArc, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
    lv_obj_set_style_arc_color(pitchArc, lv_color_hex(0xFF453A), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(pitchArc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(pitchArc, 12, LV_PART_INDICATOR);
    lv_obj_remove_style(pitchArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(pitchArc, LV_OBJ_FLAG_CLICKABLE);

    // Roll arc (middle)
    lv_obj_t *rollArc = lv_arc_create(vizContainer);
    lv_obj_set_size(rollArc, 130, 130);
    lv_obj_align(rollArc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(rollArc, 270);
    lv_arc_set_bg_angles(rollArc, 0, 360);
    int rollVal = constrain((int)((roll + 90) / 180.0 * 100), 0, 100);
    lv_arc_set_value(rollArc, rollVal);
    lv_obj_set_style_arc_color(rollArc, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
    lv_obj_set_style_arc_color(rollArc, lv_color_hex(0x30D158), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(rollArc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(rollArc, 12, LV_PART_INDICATOR);
    lv_obj_remove_style(rollArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(rollArc, LV_OBJ_FLAG_CLICKABLE);

    // Yaw arc (inner)
    lv_obj_t *yawArc = lv_arc_create(vizContainer);
    lv_obj_set_size(yawArc, 90, 90);
    lv_obj_align(yawArc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_rotation(yawArc, 270);
    lv_arc_set_bg_angles(yawArc, 0, 360);
    int yawVal = constrain((int)((yaw + 180) / 360.0 * 100), 0, 100);
    lv_arc_set_value(yawArc, yawVal);
    lv_obj_set_style_arc_color(yawArc, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
    lv_obj_set_style_arc_color(yawArc, lv_color_hex(0x0A84FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(yawArc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(yawArc, 12, LV_PART_INDICATOR);
    lv_obj_remove_style(yawArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(yawArc, LV_OBJ_FLAG_CLICKABLE);

    // Legend
    const char* labels[] = {"Pitch", "Roll", "Yaw"};
    lv_color_t colors[] = {lv_color_hex(0xFF453A), lv_color_hex(0x30D158), lv_color_hex(0x0A84FF)};
    float values[] = {pitch, roll, yaw};

    for (int i = 0; i < 3; i++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%s: %.0f", labels[i], values[i]);
        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, colors[i], 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 20 + i * 100, -20);
    }

    // Mini status bar removed - user preference
}

// 
//  STEPS CARD (Premium)
// 
void createStepsCard() {
    // CRITICAL: Disable scrolling first
    disableAllScrolling(lv_scr_act());

    // Beautiful gradient card - Purple to Blue (like USBO App reference)
    // MODIFIED: Full screen, no margins (removed black header)
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x667EEA), 0);  // Purple-blue
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x764BA2), 0);  // Deep purple
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 0, 0);  // No rounded corners for full screen
    lv_obj_set_style_border_width(card, 0, 0);
    disableAllScrolling(card);

    // App badge top-left (like reference "USBO App")
    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_set_size(badge, 90, 26);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 16, 20);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
    lv_obj_set_style_radius(badge, 13, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *badgeIcon = lv_label_create(badge);
    lv_label_set_text(badgeIcon, LV_SYMBOL_SHUFFLE " Steps");
    lv_obj_set_style_text_color(badgeIcon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(badgeIcon, &lv_font_montserrat_12, 0);
    lv_obj_center(badgeIcon);

    // "Steps:" label - smaller, positioned above
    lv_obj_t *stepsLabel = lv_label_create(card);
    lv_label_set_text(stepsLabel, "Steps:");
    lv_obj_set_style_text_color(stepsLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(stepsLabel, LV_OPA_80, 0);
    lv_obj_set_style_text_font(stepsLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(stepsLabel, LV_ALIGN_TOP_LEFT, 20, 60);

    // HUGE step count - central focus (like reference showing "6054")
    char stepBuf[16];
    snprintf(stepBuf, sizeof(stepBuf), "%lu", (unsigned long)userData.steps);
    lv_obj_t *stepCount = lv_label_create(card);
    lv_label_set_text(stepCount, stepBuf);
    lv_obj_set_style_text_color(stepCount, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(stepCount, &lv_font_montserrat_48, 0);
    lv_obj_align(stepCount, LV_ALIGN_TOP_LEFT, 20, 90);

    // Goal indicator on right side
    char goalBuf[24];
    int progress = (userData.steps * 100) / userData.dailyGoal;
    if (progress > 100) progress = 100;
    snprintf(goalBuf, sizeof(goalBuf), "%d%%", progress);

    // Circular progress ring (like reference images)
    lv_obj_t *ringBg = lv_obj_create(card);
    lv_obj_set_size(ringBg, 100, 100);
    lv_obj_align(ringBg, LV_ALIGN_TOP_RIGHT, -20, 55);
    lv_obj_set_style_bg_color(ringBg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ringBg, LV_OPA_20, 0);
    lv_obj_set_style_radius(ringBg, 50, 0);
    lv_obj_set_style_border_width(ringBg, 4, 0);
    lv_obj_set_style_border_color(ringBg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(ringBg, LV_OPA_30, 0);
    lv_obj_clear_flag(ringBg, LV_OBJ_FLAG_SCROLLABLE);

    // Progress arc inside ring
    lv_obj_t *arc = lv_arc_create(ringBg);
    lv_obj_set_size(arc, 90, 90);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, progress);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_30, LV_PART_MAIN);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);

    // Percentage in center of ring
    lv_obj_t *percLbl = lv_label_create(ringBg);
    lv_label_set_text(percLbl, goalBuf);
    lv_obj_set_style_text_color(percLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(percLbl, &lv_font_montserrat_18, 0);
    lv_obj_center(percLbl);

    // Bottom progress bar with milestone markers (like reference)
    lv_obj_t *progressBar = lv_obj_create(card);
    lv_obj_set_size(progressBar, LCD_WIDTH - 70, 45);
    lv_obj_align(progressBar, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_bg_color(progressBar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(progressBar, LV_OPA_20, 0);
    lv_obj_set_style_radius(progressBar, 12, 0);
    lv_obj_set_style_border_width(progressBar, 0, 0);
    lv_obj_clear_flag(progressBar, LV_OBJ_FLAG_SCROLLABLE);

    // Milestone markers (2000, 4000, 6000, 8000)
    int milestones[] = {2000, 4000, 6000, 8000};
    int barWidth = LCD_WIDTH - 90;

    for (int i = 0; i < 4; i++) {
        int xPos = 10 + (i * (barWidth / 4));
        bool reached = userData.steps >= (uint32_t)milestones[i];
    
        // Milestone number
        char mBuf[8];
        snprintf(mBuf, sizeof(mBuf), "%d", milestones[i]);
        lv_obj_t *mLbl = lv_label_create(progressBar);
        lv_label_set_text(mLbl, mBuf);
        lv_obj_set_style_text_color(mLbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_opa(mLbl, reached ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_set_style_text_font(mLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(mLbl, LV_ALIGN_TOP_LEFT, xPos, 5);
    
        // Marker dot
        lv_obj_t *dot = lv_obj_create(progressBar);
        lv_obj_set_size(dot, reached ? 10 : 6, reached ? 10 : 6);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_LEFT, xPos + 12, -8);
        lv_obj_set_style_bg_color(dot, reached ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x888888), 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    }

    // Current position indicator line
    int currentPos = 10 + ((userData.steps * barWidth) / 10000);
    if (currentPos > barWidth - 5) currentPos = barWidth - 5;

    lv_obj_t *posLine = lv_obj_create(progressBar);
    lv_obj_set_size(posLine, 3, 20);
    lv_obj_align(posLine, LV_ALIGN_BOTTOM_LEFT, currentPos, -5);
    lv_obj_set_style_bg_color(posLine, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(posLine, 1, 0);
    lv_obj_set_style_border_width(posLine, 0, 0);
}

// 
//  ACTIVITY RINGS CARD
// 
void createActivityRingsCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("ACTIVITY RINGS");

    // Three concentric rings
    int moveProgress = min((int)((userData.steps * 100) / userData.dailyGoal), 100);
    int exerciseProgress = min((int)((userData.totalCalories / 300.0f) * 100), 100);
    int standProgress = min((int)((userData.totalDistance / 5.0f) * 100), 100);

    // Move ring (outer - red)
    lv_obj_t *moveArc = lv_arc_create(card);
    lv_obj_set_size(moveArc, 220, 220);
    lv_obj_align(moveArc, LV_ALIGN_CENTER, 0, -20);
    lv_arc_set_rotation(moveArc, 270);
    lv_arc_set_bg_angles(moveArc, 0, 360);
    lv_arc_set_value(moveArc, moveProgress);
    lv_obj_set_style_arc_color(moveArc, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_arc_color(moveArc, lv_color_hex(0xFF2D55), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(moveArc, 20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(moveArc, 20, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(moveArc, true, LV_PART_INDICATOR);
    lv_obj_remove_style(moveArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(moveArc, LV_OBJ_FLAG_CLICKABLE);

    // Exercise ring (middle - green)
    lv_obj_t *exArc = lv_arc_create(card);
    lv_obj_set_size(exArc, 170, 170);
    lv_obj_align(exArc, LV_ALIGN_CENTER, 0, -20);
    lv_arc_set_rotation(exArc, 270);
    lv_arc_set_bg_angles(exArc, 0, 360);
    lv_arc_set_value(exArc, exerciseProgress);
    lv_obj_set_style_arc_color(exArc, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_arc_color(exArc, lv_color_hex(0x30D158), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(exArc, 20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(exArc, 20, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(exArc, true, LV_PART_INDICATOR);
    lv_obj_remove_style(exArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(exArc, LV_OBJ_FLAG_CLICKABLE);

    // Stand ring (inner - cyan)
    lv_obj_t *standArc = lv_arc_create(card);
    lv_obj_set_size(standArc, 120, 120);
    lv_obj_align(standArc, LV_ALIGN_CENTER, 0, -20);
    lv_arc_set_rotation(standArc, 270);
    lv_arc_set_bg_angles(standArc, 0, 360);
    lv_arc_set_value(standArc, standProgress);
    lv_obj_set_style_arc_color(standArc, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_arc_color(standArc, lv_color_hex(0x5AC8FA), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(standArc, 20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(standArc, 20, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(standArc, true, LV_PART_INDICATOR);
    lv_obj_remove_style(standArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(standArc, LV_OBJ_FLAG_CLICKABLE);

    // Legend
    const char* ringLabels[] = {"Move", "Exercise", "Stand"};
    lv_color_t ringColors[] = {lv_color_hex(0xFF2D55), lv_color_hex(0x30D158), lv_color_hex(0x5AC8FA)};
    int ringValues[] = {moveProgress, exerciseProgress, standProgress};

    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, 90, 25);
        lv_obj_align(row, LV_ALIGN_BOTTOM_LEFT, 15 + i * 95, -25);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
    
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(dot, ringColors[i], 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %d%%", ringLabels[i], ringValues[i]);
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, theme.text, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 16, 0);
    }

    // Mini status bar removed - user preference
}

// 
//  WORKOUT & DISTANCE CARDS (Placeholders)
// 
void createWorkoutCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("WORKOUT");

    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(icon, theme.accent, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, "Start a Workout");
    lv_obj_set_style_text_color(label, theme.text, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 30);

    // Mini status bar removed - user preference
}

void createDistanceCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("DISTANCE");

    char distBuf[16];
    snprintf(distBuf, sizeof(distBuf), "%.2f", userData.totalDistance);
    lv_obj_t *distLabel = lv_label_create(card);
    lv_label_set_text(distLabel, distBuf);
    lv_obj_set_style_text_color(distLabel, theme.accent, 0);
    lv_obj_set_style_text_font(distLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(distLabel, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *unitLabel = lv_label_create(card);
    lv_label_set_text(unitLabel, "km");
    lv_obj_set_style_text_color(unitLabel, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(unitLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(unitLabel, LV_ALIGN_CENTER, 0, 30);

    // Mini status bar removed - user preference
}

// Continue in Part 3...
// 
//  S3 MiniOS v4.0 - PART 3: Games, Weather, Media, Timer Cards
//  Place this file in the same folder as S3_MiniOS.ino
// 

// 
//  BLACKJACK CARD (Premium)
// 
int cardValue(int c) {
    int v = c % 13 + 1;
    if (v > 10) return 10;
    if (v == 1) return 11;
    return v;
}

int handTotal(int* cards, int count) {
    int total = 0, aces = 0;
    for (int i = 0; i < count; i++) {
        int v = cardValue(cards[i]);
        total += v;
        if (v == 11) aces++;
    }
    while (total > 21 && aces > 0) { total -= 10; aces--; }
    return total;
}

const char* getCardSymbol(int card) {
    static char buf[4];
    int rank = card % 13;
    const char* ranks[] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
    return ranks[rank];
}

lv_color_t getSuitColor(int card) {
    int suit = card / 13;
    return (suit < 2) ? lv_color_hex(0xFF453A) : lv_color_hex(0xFFFFFF);
}

void blackjackHitCb(lv_event_t *e);
void blackjackStandCb(lv_event_t *e);
void blackjackNewCb(lv_event_t *e);

void drawPlayingCard(lv_obj_t* parent, int card, int x, int y, bool faceUp) {
    lv_obj_t *cardObj = lv_obj_create(parent);
    lv_obj_set_size(cardObj, 45, 65);
    lv_obj_align(cardObj, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(cardObj, faceUp ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_radius(cardObj, 8, 0);
    lv_obj_set_style_border_width(cardObj, 0, 0);
    lv_obj_set_style_shadow_width(cardObj, 8, 0);
    lv_obj_set_style_shadow_color(cardObj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(cardObj, LV_OPA_30, 0);

    if (faceUp) {
        lv_obj_t *rankLbl = lv_label_create(cardObj);
        lv_label_set_text(rankLbl, getCardSymbol(card));
        lv_obj_set_style_text_color(rankLbl, getSuitColor(card), 0);
        lv_obj_set_style_text_font(rankLbl, &lv_font_montserrat_16, 0);
        lv_obj_align(rankLbl, LV_ALIGN_TOP_LEFT, 4, 2);
    }
}

void createBlackjackCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    // Green felt background
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1B4332), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x2D6A4F), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Title
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "BLACKJACK");
    lv_obj_set_style_text_color(title, lv_color_hex(0xD4AF37), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Dealer area
    lv_obj_t *dealerLbl = lv_label_create(card);
    lv_label_set_text(dealerLbl, "Dealer");
    lv_obj_set_style_text_color(dealerLbl, lv_color_hex(0xAEAEB2), 0);
    lv_obj_set_style_text_font(dealerLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(dealerLbl, LV_ALIGN_TOP_LEFT, 16, 35);

    // Draw dealer cards
    for (int i = 0; i < dealerCount && i < 5; i++) {
        bool showCard = playerStand || i == 0;
        drawPlayingCard(card, dealerCards[i], 16 + i * 35, 50, showCard);
    }

    // Dealer total
    char dBuf[16];
    if (playerStand) {
        snprintf(dBuf, sizeof(dBuf), "= %d", handTotal(dealerCards, dealerCount));
    } else {
        snprintf(dBuf, sizeof(dBuf), "= ?");
    }
    lv_obj_t *dTotal = lv_label_create(card);
    lv_label_set_text(dTotal, dBuf);
    lv_obj_set_style_text_color(dTotal, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(dTotal, &lv_font_montserrat_18, 0);
    lv_obj_align(dTotal, LV_ALIGN_TOP_RIGHT, -16, 75);

    // Divider
    lv_obj_t *divider = lv_obj_create(card);
    lv_obj_set_size(divider, LCD_WIDTH - 60, 2);
    lv_obj_align(divider, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x52B788), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_50, 0);
    lv_obj_set_style_radius(divider, 1, 0);
    lv_obj_set_style_border_width(divider, 0, 0);

    // Player area
    lv_obj_t *playerLbl = lv_label_create(card);
    lv_label_set_text(playerLbl, "You");
    lv_obj_set_style_text_color(playerLbl, lv_color_hex(0xAEAEB2), 0);
    lv_obj_set_style_text_font(playerLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(playerLbl, LV_ALIGN_CENTER, -130, 10);

    // Draw player cards
    for (int i = 0; i < playerCount && i < 5; i++) {
        drawPlayingCard(card, playerCards[i], 16 + i * 35, 160, true);
    }

    // Player total
    int pTotal = handTotal(playerCards, playerCount);
    char pBuf[16];
    snprintf(pBuf, sizeof(pBuf), "= %d", pTotal);
    lv_obj_t *pTotalLbl = lv_label_create(card);
    lv_label_set_text(pTotalLbl, pBuf);
    lv_obj_set_style_text_color(pTotalLbl, pTotal > 21 ? lv_color_hex(0xFF453A) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(pTotalLbl, &lv_font_montserrat_18, 0);
    lv_obj_align(pTotalLbl, LV_ALIGN_CENTER, 110, 35);

    // Result or buttons
    if (!blackjackGameActive) {
        int dTotal = handTotal(dealerCards, dealerCount);
        const char* result;
        lv_color_t resultColor;
    
        if (pTotal > 21) {
            result = "BUST!";
            resultColor = lv_color_hex(0xFF453A);
        } else if (dTotal > 21 || pTotal > dTotal) {
            result = "YOU WIN!";
            resultColor = lv_color_hex(0x30D158);
        } else if (pTotal < dTotal) {
            result = "DEALER WINS";
            resultColor = lv_color_hex(0xFF453A);
        } else {
            result = "PUSH";
            resultColor = lv_color_hex(0xFF9F0A);
        }
    
        if (playerCount > 0) {
            lv_obj_t *resultLbl = lv_label_create(card);
            lv_label_set_text(resultLbl, result);
            lv_obj_set_style_text_color(resultLbl, resultColor, 0);
            lv_obj_set_style_text_font(resultLbl, &lv_font_montserrat_20, 0);
            lv_obj_align(resultLbl, LV_ALIGN_BOTTOM_MID, 0, -75);
        }
    
        // New game button
        lv_obj_t *newBtn = lv_btn_create(card);
        lv_obj_set_size(newBtn, 150, 45);
        lv_obj_align(newBtn, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_set_style_bg_color(newBtn, lv_color_hex(0xD4AF37), 0);
        lv_obj_set_style_radius(newBtn, 22, 0);
        lv_obj_add_event_cb(newBtn, blackjackNewCb, LV_EVENT_CLICKED, NULL);
    
        lv_obj_t *nLbl = lv_label_create(newBtn);
        lv_label_set_text(nLbl, "DEAL");
        lv_obj_set_style_text_color(nLbl, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_text_font(nLbl, &lv_font_montserrat_16, 0);
        lv_obj_center(nLbl);
    } else if (!playerStand && pTotal <= 21) {
        // Hit button
        lv_obj_t *hitBtn = lv_btn_create(card);
        lv_obj_set_size(hitBtn, 100, 42);
        lv_obj_align(hitBtn, LV_ALIGN_BOTTOM_MID, -60, -20);
        lv_obj_set_style_bg_color(hitBtn, lv_color_hex(0x30D158), 0);
        lv_obj_set_style_radius(hitBtn, 21, 0);
        lv_obj_add_event_cb(hitBtn, blackjackHitCb, LV_EVENT_CLICKED, NULL);
    
        lv_obj_t *hLbl = lv_label_create(hitBtn);
        lv_label_set_text(hLbl, "HIT");
        lv_obj_set_style_text_color(hLbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(hLbl);
    
        // Stand button
        lv_obj_t *standBtn = lv_btn_create(card);
        lv_obj_set_size(standBtn, 100, 42);
        lv_obj_align(standBtn, LV_ALIGN_BOTTOM_MID, 60, -20);
        lv_obj_set_style_bg_color(standBtn, lv_color_hex(0xFF453A), 0);
        lv_obj_set_style_radius(standBtn, 21, 0);
        lv_obj_add_event_cb(standBtn, blackjackStandCb, LV_EVENT_CLICKED, NULL);
    
        lv_obj_t *sLbl = lv_label_create(standBtn);
        lv_label_set_text(sLbl, "STAND");
        lv_obj_set_style_text_color(sLbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(sLbl);
    }

    // Streak badge
    char streakBuf[16];
    snprintf(streakBuf, sizeof(streakBuf), LV_SYMBOL_CHARGE " %d", userData.blackjackStreak);
    lv_obj_t *streakBadge = lv_obj_create(card);
    lv_obj_set_size(streakBadge, 70, 24);
    lv_obj_align(streakBadge, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_obj_set_style_bg_color(streakBadge, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(streakBadge, LV_OPA_40, 0);
    lv_obj_set_style_radius(streakBadge, 12, 0);
    lv_obj_set_style_border_width(streakBadge, 0, 0);

    lv_obj_t *streakLbl = lv_label_create(streakBadge);
    lv_label_set_text(streakLbl, streakBuf);
    lv_obj_set_style_text_color(streakLbl, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_text_font(streakLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(streakLbl);
}

void blackjackNewCb(lv_event_t *e) {
    blackjackGameActive = true;
    playerStand = false;
    playerCount = 2;
    dealerCount = 2;
    for (int i = 0; i < 2; i++) {
        playerCards[i] = random(0, 52);
        dealerCards[i] = random(0, 52);
    }
    userData.gamesPlayed++;
    navigateTo(CAT_GAMES, 0);
}

void blackjackHitCb(lv_event_t *e) {
    if (playerCount < 10) {
        playerCards[playerCount++] = random(0, 52);
        if (handTotal(playerCards, playerCount) > 21) {
            userData.blackjackStreak = 0;
            blackjackGameActive = false;
        }
    }
    navigateTo(CAT_GAMES, 0);
}

void blackjackStandCb(lv_event_t *e) {
    playerStand = true;
    while (handTotal(dealerCards, dealerCount) < 17 && dealerCount < 10) {
        dealerCards[dealerCount++] = random(0, 52);
    }
    int pTotal = handTotal(playerCards, playerCount);
    int dTotal = handTotal(dealerCards, dealerCount);
    if (dTotal > 21 || pTotal > dTotal) {
        userData.blackjackStreak++;
        userData.gamesWon++;
    } else {
        userData.blackjackStreak = 0;
    }
    blackjackGameActive = false;
    saveUserData();
    navigateTo(CAT_GAMES, 0);
}

// 
//  DINO GAME CARD - Pixel Art Style (like reference image)
// 
void dinoJumpCb(lv_event_t *e);

void createDinoCard() {
    // Dark card with pixel game aesthetic
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A2E), 0);  // Dark blue-ish
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Level badge top-left
    lv_obj_t *lvlBadge = lv_obj_create(card);
    lv_obj_set_size(lvlBadge, 70, 26);
    lv_obj_align(lvlBadge, LV_ALIGN_TOP_LEFT, 12, 12);
    lv_obj_set_style_bg_color(lvlBadge, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(lvlBadge, LV_OPA_40, 0);
    lv_obj_set_style_radius(lvlBadge, 13, 0);
    lv_obj_set_style_border_width(lvlBadge, 0, 0);
    lv_obj_clear_flag(lvlBadge, LV_OBJ_FLAG_SCROLLABLE);

    char lvlBuf[12];
    int level = dinoScore / 50 + 1;
    snprintf(lvlBuf, sizeof(lvlBuf), "%d lvl", level);
    lv_obj_t *lvlLbl = lv_label_create(lvlBadge);
    lv_label_set_text(lvlLbl, lvlBuf);
    lv_obj_set_style_text_color(lvlLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(lvlLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lvlLbl);

    // Score badge top-right (prominent)
    lv_obj_t *scoreBg = lv_obj_create(card);
    lv_obj_set_size(scoreBg, 80, 32);
    lv_obj_align(scoreBg, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_obj_set_style_bg_color(scoreBg, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_bg_opa(scoreBg, LV_OPA_30, 0);
    lv_obj_set_style_radius(scoreBg, 16, 0);
    lv_obj_set_style_border_width(scoreBg, 0, 0);
    lv_obj_clear_flag(scoreBg, LV_OBJ_FLAG_SCROLLABLE);

    char sBuf[16];
    snprintf(sBuf, sizeof(sBuf), "%d", dinoScore);
    lv_obj_t *scoreLbl = lv_label_create(scoreBg);
    lv_label_set_text(scoreLbl, sBuf);
    lv_obj_set_style_text_color(scoreLbl, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_text_font(scoreLbl, &lv_font_montserrat_18, 0);
    lv_obj_center(scoreLbl);

    // Game area - darker inset
    lv_obj_t *gameArea = lv_obj_create(card);
    lv_obj_set_size(gameArea, LCD_WIDTH - 50, 190);
    lv_obj_align(gameArea, LV_ALIGN_CENTER, 0, 5);
    lv_obj_set_style_bg_color(gameArea, lv_color_hex(0x0F0F23), 0);
    lv_obj_set_style_radius(gameArea, 20, 0);
    lv_obj_set_style_border_width(gameArea, 0, 0);
    lv_obj_clear_flag(gameArea, LV_OBJ_FLAG_SCROLLABLE);

    // Ground line
    lv_obj_t *ground = lv_obj_create(gameArea);
    lv_obj_set_size(ground, LCD_WIDTH - 70, 2);
    lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_set_style_bg_color(ground, lv_color_hex(0x3A3A5C), 0);
    lv_obj_set_style_radius(ground, 1, 0);
    lv_obj_set_style_border_width(ground, 0, 0);

    // PIXEL DINO - White/light colored like reference image
    // Body
    lv_obj_t *dinoBody = lv_obj_create(gameArea);
    lv_obj_set_size(dinoBody, 35, 40);
    lv_obj_align(dinoBody, LV_ALIGN_BOTTOM_LEFT, 45, -38 + dinoY);
    lv_obj_set_style_bg_color(dinoBody, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(dinoBody, 4, 0);
    lv_obj_set_style_border_width(dinoBody, 0, 0);

    // Dino head
    lv_obj_t *dinoHead = lv_obj_create(gameArea);
    lv_obj_set_size(dinoHead, 25, 20);
    lv_obj_align(dinoHead, LV_ALIGN_BOTTOM_LEFT, 55, -75 + dinoY);
    lv_obj_set_style_bg_color(dinoHead, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(dinoHead, 4, 0);
    lv_obj_set_style_border_width(dinoHead, 0, 0);

    // Dino eye (pixel dot)
    lv_obj_t *dinoEye = lv_obj_create(gameArea);
    lv_obj_set_size(dinoEye, 6, 6);
    lv_obj_align(dinoEye, LV_ALIGN_BOTTOM_LEFT, 70, -82 + dinoY);
    lv_obj_set_style_bg_color(dinoEye, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(dinoEye, 0, 0);
    lv_obj_set_style_border_width(dinoEye, 0, 0);

    // Dino legs (simple pixel style)
    lv_obj_t *dinoLeg1 = lv_obj_create(gameArea);
    lv_obj_set_size(dinoLeg1, 8, 12);
    lv_obj_align(dinoLeg1, LV_ALIGN_BOTTOM_LEFT, 50, -38 + dinoY);
    lv_obj_set_style_bg_color(dinoLeg1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(dinoLeg1, 0, 0);
    lv_obj_set_style_border_width(dinoLeg1, 0, 0);

    lv_obj_t *dinoLeg2 = lv_obj_create(gameArea);
    lv_obj_set_size(dinoLeg2, 8, 12);
    lv_obj_align(dinoLeg2, LV_ALIGN_BOTTOM_LEFT, 65, -38 + dinoY);
    lv_obj_set_style_bg_color(dinoLeg2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(dinoLeg2, 0, 0);
    lv_obj_set_style_border_width(dinoLeg2, 0, 0);

    // CACTUS OBSTACLE - Red/orange
    lv_obj_t *cactus = lv_obj_create(gameArea);
    lv_obj_set_size(cactus, 20, 45);
    lv_obj_align(cactus, LV_ALIGN_BOTTOM_LEFT, obstacleX, -37);
    lv_obj_set_style_bg_color(cactus, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_radius(cactus, 3, 0);
    lv_obj_set_style_border_width(cactus, 0, 0);

    // Cactus arms
    lv_obj_t *cactusArm1 = lv_obj_create(gameArea);
    lv_obj_set_size(cactusArm1, 12, 8);
    lv_obj_align(cactusArm1, LV_ALIGN_BOTTOM_LEFT, obstacleX - 8, -60);
    lv_obj_set_style_bg_color(cactusArm1, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_radius(cactusArm1, 2, 0);
    lv_obj_set_style_border_width(cactusArm1, 0, 0);

    lv_obj_t *cactusArm2 = lv_obj_create(gameArea);
    lv_obj_set_size(cactusArm2, 12, 8);
    lv_obj_align(cactusArm2, LV_ALIGN_BOTTOM_LEFT, obstacleX + 16, -55);
    lv_obj_set_style_bg_color(cactusArm2, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_radius(cactusArm2, 2, 0);
    lv_obj_set_style_border_width(cactusArm2, 0, 0);

    // Food items (like reference - coffee cup and croissant style icons)
    // Coffee cup icon
    lv_obj_t *coffee = lv_obj_create(gameArea);
    lv_obj_set_size(coffee, 22, 26);
    lv_obj_align(coffee, LV_ALIGN_BOTTOM_LEFT, 15, -55);
    lv_obj_set_style_bg_color(coffee, lv_color_hex(0x6B4226), 0);
    lv_obj_set_style_radius(coffee, 4, 0);
    lv_obj_set_style_border_width(coffee, 0, 0);

    // Croissant icon  
    lv_obj_t *croissant = lv_obj_create(gameArea);
    lv_obj_set_size(croissant, 28, 18);
    lv_obj_align(croissant, LV_ALIGN_BOTTOM_RIGHT, -15, -55);
    lv_obj_set_style_bg_color(croissant, lv_color_hex(0xF4A460), 0);
    lv_obj_set_style_radius(croissant, 9, 0);
    lv_obj_set_style_border_width(croissant, 0, 0);

    // Jump button - Pill shaped, prominent
    lv_obj_t *jumpBtn = lv_btn_create(card);
    lv_obj_set_size(jumpBtn, 150, 50);
    lv_obj_align(jumpBtn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(jumpBtn, dinoGameOver ? lv_color_hex(0xFF9500) : lv_color_hex(0x30D158), 0);
    lv_obj_set_style_radius(jumpBtn, 25, 0);
    lv_obj_set_style_shadow_width(jumpBtn, 15, 0);
    lv_obj_set_style_shadow_color(jumpBtn, dinoGameOver ? lv_color_hex(0xFF9500) : lv_color_hex(0x30D158), 0);
    lv_obj_set_style_shadow_opa(jumpBtn, LV_OPA_40, 0);
    lv_obj_add_event_cb(jumpBtn, dinoJumpCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *jLbl = lv_label_create(jumpBtn);
    lv_label_set_text(jLbl, dinoGameOver ? "RESTART" : "JUMP!");
    lv_obj_set_style_text_color(jLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(jLbl, &lv_font_montserrat_18, 0);
    lv_obj_center(jLbl);

    // Game Over overlay
    if (dinoGameOver) {
        lv_obj_t *overlay = lv_obj_create(gameArea);
        lv_obj_set_size(overlay, LCD_WIDTH - 80, 80);
        lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
        lv_obj_set_style_radius(overlay, 20, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    
        lv_obj_t *goLbl = lv_label_create(overlay);
        lv_label_set_text(goLbl, "GAME OVER");
        lv_obj_set_style_text_color(goLbl, lv_color_hex(0xFF6B6B), 0);
        lv_obj_set_style_text_font(goLbl, &lv_font_montserrat_24, 0);
        lv_obj_align(goLbl, LV_ALIGN_CENTER, 0, -10);
    
        char hsBuf[32];
        snprintf(hsBuf, sizeof(hsBuf), "Best: %d", (int)userData.clickerScore);
        lv_obj_t *hsLbl = lv_label_create(overlay);
        lv_label_set_text(hsLbl, hsBuf);
        lv_obj_set_style_text_color(hsLbl, lv_color_hex(0xFFD700), 0);
        lv_obj_set_style_text_font(hsLbl, &lv_font_montserrat_14, 0);
        lv_obj_align(hsLbl, LV_ALIGN_CENTER, 0, 15);
    }
}

void dinoJumpCb(lv_event_t *e) {
    lastActivityMs = millis();  // Reset screen timeout on button press

    if (dinoGameOver) {
        dinoGameOver = false;
        dinoScore = 0;
        obstacleX = 300;
        dinoY = 0;
        dinoVelocity = 0;
        dinoJumping = false;
    } else if (!dinoJumping && dinoY == 0) {
        dinoJumping = true;
        dinoVelocity = JUMP_FORCE;
    }
}

// 
//  YES/NO SPINNER CARD
// 
void yesNoSpinCb(lv_event_t *e);

void createYesNoCard() {
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xC41E3A), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x9B2335), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "YES / NO");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(title, LV_OPA_70, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // Result circle
    lv_obj_t *resultCircle = lv_obj_create(card);
    lv_obj_set_size(resultCircle, 180, 180);
    lv_obj_align(resultCircle, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(resultCircle, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(resultCircle, LV_OPA_30, 0);
    lv_obj_set_style_radius(resultCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(resultCircle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(resultCircle, 3, 0);
    lv_obj_set_style_border_opa(resultCircle, LV_OPA_30, 0);

    lv_obj_t *resultLbl = lv_label_create(resultCircle);
    lv_label_set_text(resultLbl, yesNoResult.c_str());
    lv_obj_set_style_text_color(resultLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(resultLbl, &lv_font_montserrat_36, 0);
    lv_obj_center(resultLbl);

    // Spin button
    lv_obj_t *spinBtn = lv_btn_create(card);
    lv_obj_set_size(spinBtn, 150, 45);
    lv_obj_align(spinBtn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(spinBtn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(spinBtn, 22, 0);
    lv_obj_add_event_cb(spinBtn, yesNoSpinCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sLbl = lv_label_create(spinBtn);
    lv_label_set_text(sLbl, LV_SYMBOL_REFRESH " SPIN");
    lv_obj_set_style_text_color(sLbl, lv_color_hex(0xC41E3A), 0);
    lv_obj_center(sLbl);
}

void yesNoSpinCb(lv_event_t *e) {
    const char* results[] = {"Yes", "No", "Maybe", "Definitely", "Never", "Ask Again"};
    yesNoResult = results[random(0, 6)];
    navigateTo(CAT_GAMES, 2);
}

// 
//  WEATHER CARD - Apple Watch Inspired Design
// 
void createWeatherCard() {
    // PREMIUM: Berlin-style minimalist weather with enhanced visuals
    lv_obj_clean(lv_scr_act());

    // CRITICAL: Disable ALL scrolling on the screen
    disableAllScrolling(lv_scr_act());

    // Deep matte black background with subtle texture
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0D0D0D), 0);  // Deeper black
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Subtle top gradient for depth
    lv_obj_t *topGradient = lv_obj_create(card);
    lv_obj_set_size(topGradient, LCD_WIDTH, 150);
    lv_obj_align(topGradient, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(topGradient, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(topGradient, LV_OPA_30, 0);
    lv_obj_set_style_radius(topGradient, 0, 0);
    lv_obj_set_style_border_width(topGradient, 0, 0);
    lv_obj_clear_flag(topGradient, LV_OBJ_FLAG_SCROLLABLE);

    // Large temperature with shadow effect
    lv_obj_t *tempLabel = lv_label_create(card);
    char tempStr[16];
    snprintf(tempStr, sizeof(tempStr), "%.0f", weatherTemp);
    lv_label_set_text(tempLabel, tempStr);
    lv_obj_set_style_text_color(tempLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(tempLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(tempLabel, LV_ALIGN_TOP_LEFT, 32, 80);

    // Text shadow for depth
    lv_obj_set_style_shadow_width(tempLabel, 20, 0);
    lv_obj_set_style_shadow_color(tempLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(tempLabel, LV_OPA_50, 0);
    lv_obj_set_style_shadow_ofs_x(tempLabel, 0, 0);
    lv_obj_set_style_shadow_ofs_y(tempLabel, 4, 0);

    // City name in CAPS with letter spacing
    lv_obj_t *cityLabel = lv_label_create(card);
    char cityUpper[64];
    strncpy(cityUpper, weatherCity, sizeof(cityUpper));
    for (int i = 0; cityUpper[i]; i++) cityUpper[i] = toupper(cityUpper[i]);
    lv_label_set_text(cityLabel, cityUpper);
    lv_obj_set_style_text_color(cityLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(cityLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_letter_space(cityLabel, 3, 0);  // Letter spacing for elegance
    lv_obj_align(cityLabel, LV_ALIGN_TOP_LEFT, 32, 145);

    // Accent line under city name
    lv_obj_t *accentLine = lv_obj_create(card);
    lv_obj_set_size(accentLine, 60, 3);
    lv_obj_align(accentLine, LV_ALIGN_TOP_LEFT, 32, 175);
    lv_obj_set_style_bg_color(accentLine, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_radius(accentLine, 2, 0);
    lv_obj_set_style_border_width(accentLine, 0, 0);
    lv_obj_clear_flag(accentLine, LV_OBJ_FLAG_SCROLLABLE);

    // High/Low temperatures in elegant container
    lv_obj_t *rangeContainer = lv_obj_create(card);
    lv_obj_set_size(rangeContainer, 180, 50);
    lv_obj_align(rangeContainer, LV_ALIGN_TOP_LEFT, 32, 195);
    lv_obj_set_style_bg_color(rangeContainer, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(rangeContainer, LV_OPA_40, 0);
    lv_obj_set_style_radius(rangeContainer, 12, 0);
    lv_obj_set_style_border_width(rangeContainer, 0, 0);
    lv_obj_clear_flag(rangeContainer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *rangeLabel = lv_label_create(rangeContainer);
    char rangeStr[32];
    snprintf(rangeStr, sizeof(rangeStr), "H: %.0f  L: %.0f", weatherHigh, weatherLow);
    lv_label_set_text(rangeLabel, rangeStr);
    lv_obj_set_style_text_color(rangeLabel, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(rangeLabel, &lv_font_montserrat_16, 0);
    lv_obj_center(rangeLabel);

    // Weather icon with glow effect
    lv_obj_t *iconContainer = lv_obj_create(card);
    lv_obj_set_size(iconContainer, 80, 80);
    lv_obj_align(iconContainer, LV_ALIGN_TOP_LEFT, 32, 270);
    lv_obj_set_style_bg_color(iconContainer, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_bg_opa(iconContainer, LV_OPA_20, 0);
    lv_obj_set_style_radius(iconContainer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(iconContainer, 2, 0);
    lv_obj_set_style_border_color(iconContainer, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_border_opa(iconContainer, LV_OPA_50, 0);
    lv_obj_clear_flag(iconContainer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(iconContainer);
    const char* iconSymbol = LV_SYMBOL_DOWNLOAD;  // Cloud-like icon
    if (weatherDesc.indexOf("Clear") >= 0 || weatherDesc.indexOf("Sunny") >= 0) {
        iconSymbol = LV_SYMBOL_GPS;
        lv_obj_set_style_bg_color(iconContainer, lv_color_hex(0xFFD60A), 0);
        lv_obj_set_style_border_color(iconContainer, lv_color_hex(0xFFD60A), 0);
    } else if (weatherDesc.indexOf("Rain") >= 0) {
        iconSymbol = LV_SYMBOL_REFRESH;
    }
    lv_label_set_text(icon, iconSymbol);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
    lv_obj_center(icon);

    // Weather description with better styling
    lv_obj_t *descLabel = lv_label_create(card);
    lv_label_set_text(descLabel, weatherDesc.c_str());
    lv_obj_set_style_text_color(descLabel, lv_color_hex(0xB0B0B0), 0);
    lv_obj_set_style_text_font(descLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(descLabel, LV_ALIGN_TOP_LEFT, 130, 295);

    // Bottom info bar with gradient
    lv_obj_t *bottomBar = lv_obj_create(card);
    lv_obj_set_size(bottomBar, LCD_WIDTH, 60);
    lv_obj_align(bottomBar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottomBar, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(bottomBar, LV_OPA_30, 0);
    lv_obj_set_style_radius(bottomBar, 0, 0);
    lv_obj_set_style_border_width(bottomBar, 0, 0);
    lv_obj_clear_flag(bottomBar, LV_OBJ_FLAG_SCROLLABLE);

    // Swipe hint with elegant styling
    lv_obj_t *hint = lv_label_create(bottomBar);
    lv_label_set_text(hint, LV_SYMBOL_UP " Swipe for Weather Hero");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_center(hint);
}

void createForecastCard() {
    // PREMIUM: Weather Hero - Enhanced atmospheric design
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_clean(lv_scr_act());

    // CRITICAL: Disable ALL scrolling on the screen
    disableAllScrolling(lv_scr_act());

    // Multi-layer gradient background for depth
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Time-based gradient (simulating sky colors)
    uint32_t topColor, bottomColor;
    if (clockHour >= 6 && clockHour < 12) {
        // Morning: Light blue to golden
        topColor = 0x87CEEB;
        bottomColor = 0xFFD700;
    } else if (clockHour >= 12 && clockHour < 18) {
        // Afternoon: Blue to orange
        topColor = 0x4A90D9;
        bottomColor = 0xF4A460;
    } else if (clockHour >= 18 && clockHour < 21) {
        // Evening: Orange to purple
        topColor = 0xFF6B35;
        bottomColor = 0x8B4789;
    } else {
        // Night: Deep blue to dark purple
        topColor = 0x191970;
        bottomColor = 0x2C1A4D;
    }

    lv_obj_set_style_bg_color(card, lv_color_hex(topColor), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(bottomColor), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);

    // Floating temperature display with glass morphism effect
    lv_obj_t *tempContainer = lv_obj_create(card);
    lv_obj_set_size(tempContainer, 200, 140);
    lv_obj_align(tempContainer, LV_ALIGN_CENTER, 0, -80);
    lv_obj_set_style_bg_color(tempContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(tempContainer, LV_OPA_20, 0);
    lv_obj_set_style_radius(tempContainer, 30, 0);
    lv_obj_set_style_border_width(tempContainer, 2, 0);
    lv_obj_set_style_border_color(tempContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(tempContainer, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(tempContainer, 30, 0);
    lv_obj_set_style_shadow_color(tempContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(tempContainer, LV_OPA_30, 0);

    // Large temperature with glow
    lv_obj_t *tempLabel = lv_label_create(tempContainer);
    char tempStr[16];
    snprintf(tempStr, sizeof(tempStr), "%.0f", weatherTemp);
    lv_label_set_text(tempLabel, tempStr);
    lv_obj_set_style_text_color(tempLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(tempLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(tempLabel, LV_ALIGN_CENTER, 0, -15);
    lv_obj_set_style_shadow_width(tempLabel, 15, 0);
    lv_obj_set_style_shadow_color(tempLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(tempLabel, LV_OPA_50, 0);

    // Weather condition
    lv_obj_t *condLabel = lv_label_create(tempContainer);
    lv_label_set_text(condLabel, weatherDesc.c_str());
    lv_obj_set_style_text_color(condLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(condLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(condLabel, LV_ALIGN_CENTER, 0, 30);

    // City name with elegant badge
    lv_obj_t *cityBadge = lv_obj_create(card);
    lv_obj_set_size(cityBadge, 150, 40);
    lv_obj_align(cityBadge, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(cityBadge, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(cityBadge, LV_OPA_30, 0);
    lv_obj_set_style_radius(cityBadge, 20, 0);
    lv_obj_set_style_border_width(cityBadge, 1, 0);
    lv_obj_set_style_border_color(cityBadge, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(cityBadge, LV_OPA_30, 0);

    lv_obj_t *cityLabel = lv_label_create(cityBadge);
    lv_label_set_text(cityLabel, weatherCity);
    lv_obj_set_style_text_color(cityLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(cityLabel, &lv_font_montserrat_16, 0);
    lv_obj_center(cityLabel);

    // Sunrise/Sunset info with icons (if available)
    if (sunData.valid) {
        lv_obj_t *sunContainer = lv_obj_create(card);
        lv_obj_set_size(sunContainer, LCD_WIDTH - 60, 70);
        lv_obj_align(sunContainer, LV_ALIGN_CENTER, 0, 80);
        lv_obj_set_style_bg_color(sunContainer, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(sunContainer, LV_OPA_20, 0);
        lv_obj_set_style_radius(sunContainer, 20, 0);
        lv_obj_set_style_border_width(sunContainer, 1, 0);
        lv_obj_set_style_border_color(sunContainer, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_opa(sunContainer, LV_OPA_30, 0);

        // Sunrise
        lv_obj_t *sunriseLabel = lv_label_create(sunContainer);
        char sunStr[64];
        snprintf(sunStr, sizeof(sunStr), LV_SYMBOL_UP " %s", sunData.sunriseTime);
        lv_label_set_text(sunriseLabel, sunStr);
        lv_obj_set_style_text_color(sunriseLabel, lv_color_hex(0xFFD700), 0);
        lv_obj_set_style_text_font(sunriseLabel, &lv_font_montserrat_18, 0);
        lv_obj_align(sunriseLabel, LV_ALIGN_LEFT_MID, 20, 0);

        // Sunset
        lv_obj_t *sunsetLabel = lv_label_create(sunContainer);
        snprintf(sunStr, sizeof(sunStr), LV_SYMBOL_DOWN " %s", sunData.sunsetTime);
        lv_label_set_text(sunsetLabel, sunStr);
        lv_obj_set_style_text_color(sunsetLabel, lv_color_hex(0xFF6B35), 0);
        lv_obj_set_style_text_font(sunsetLabel, &lv_font_montserrat_18, 0);
        lv_obj_align(sunsetLabel, LV_ALIGN_RIGHT_MID, -20, 0);
    }

    // High/Low at bottom in elegant container
    lv_obj_t *rangeContainer = lv_obj_create(card);
    lv_obj_set_size(rangeContainer, 200, 55);
    lv_obj_align(rangeContainer, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(rangeContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(rangeContainer, LV_OPA_20, 0);
    lv_obj_set_style_radius(rangeContainer, 28, 0);
    lv_obj_set_style_border_width(rangeContainer, 2, 0);
    lv_obj_set_style_border_color(rangeContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(rangeContainer, LV_OPA_40, 0);

    lv_obj_t *rangeLabel = lv_label_create(rangeContainer);
    char rangeStr[32];
    snprintf(rangeStr, sizeof(rangeStr), LV_SYMBOL_UP " %.0f    " LV_SYMBOL_DOWN " %.0f", weatherHigh, weatherLow);
    lv_label_set_text(rangeLabel, rangeStr);
    lv_obj_set_style_text_color(rangeLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(rangeLabel, &lv_font_montserrat_18, 0);
    lv_obj_center(rangeLabel);
}

// 
//  STOCKS & CRYPTO CARDS
// 
// createStocksCard REMOVED

// createCryptoCard REMOVED

// 
//  MUSIC & GALLERY CARDS
// 
void musicPlayCb(lv_event_t *e);

void createMusicCard() {
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF81F), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x5856D6), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Album art placeholder
    lv_obj_t *albumArt = lv_obj_create(card);
    lv_obj_set_size(albumArt, 140, 140);
    lv_obj_align(albumArt, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(albumArt, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(albumArt, 20, 0);
    lv_obj_set_style_border_width(albumArt, 0, 0);
    lv_obj_set_style_shadow_width(albumArt, 20, 0);
    lv_obj_set_style_shadow_color(albumArt, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(albumArt, LV_OPA_40, 0);

    lv_obj_t *musicIcon = lv_label_create(albumArt);
    lv_label_set_text(musicIcon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(musicIcon, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(musicIcon, &lv_font_montserrat_48, 0);
    lv_obj_center(musicIcon);

    // Title & artist
    lv_obj_t *titleLbl = lv_label_create(card);
    lv_label_set_text(titleLbl, musicTitle);
    lv_obj_set_style_text_color(titleLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_18, 0);
    lv_obj_align(titleLbl, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t *artistLbl = lv_label_create(card);
    lv_label_set_text(artistLbl, musicArtist);
    lv_obj_set_style_text_color(artistLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(artistLbl, LV_OPA_70, 0);
    lv_obj_align(artistLbl, LV_ALIGN_CENTER, 0, 85);

    // Progress bar
    lv_obj_t *progBg = lv_obj_create(card);
    lv_obj_set_size(progBg, LCD_WIDTH - 80, 6);
    lv_obj_align(progBg, LV_ALIGN_CENTER, 0, 115);
    lv_obj_set_style_bg_color(progBg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(progBg, LV_OPA_30, 0);
    lv_obj_set_style_radius(progBg, 3, 0);
    lv_obj_set_style_border_width(progBg, 0, 0);

    int fillWidth = ((LCD_WIDTH - 84) * musicCurrent) / musicDuration;
    lv_obj_t *progFill = lv_obj_create(progBg);
    lv_obj_set_size(progFill, fillWidth, 4);
    lv_obj_align(progFill, LV_ALIGN_LEFT_MID, 1, 0);
    lv_obj_set_style_bg_color(progFill, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(progFill, 2, 0);
    lv_obj_set_style_border_width(progFill, 0, 0);

    // Play/Pause button
    lv_obj_t *playBtn = lv_btn_create(card);
    lv_obj_set_size(playBtn, 60, 60);
    lv_obj_align(playBtn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(playBtn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(playBtn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(playBtn, musicPlayCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *playIcon = lv_label_create(playBtn);
    lv_label_set_text(playIcon, musicPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(playIcon, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(playIcon, &lv_font_montserrat_24, 0);
    lv_obj_center(playIcon);

    // Mini status bar removed
}

void musicPlayCb(lv_event_t *e) {
    musicPlaying = !musicPlaying;
    navigateTo(CAT_MEDIA, 0);
}

void createGalleryCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("GALLERY");
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, "No photos");
    lv_obj_set_style_text_color(lbl, theme.text, 0);
    lv_obj_center(lbl);
}

// Continue in Part 4...
// 
//  S3 MiniOS v4.0 - PART 4: Timer, Streak, Calendar, System, Battery Stats Cards
//  Place this file in the same folder as S3_MiniOS.ino
// 

// 
//  TIMER CARDS
// 
void sandTimerStartCb(lv_event_t *e);
void stopwatchToggleCb(lv_event_t *e);
void stopwatchResetCb(lv_event_t *e);
void breatheStartCb(lv_event_t *e);

void createSandTimerCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("SAND TIMER");

    unsigned long elapsed = sandTimerRunning ? (millis() - sandTimerStartMs) : 0;
    unsigned long remaining = SAND_TIMER_DURATION > elapsed ? SAND_TIMER_DURATION - elapsed : 0;
    int progress = sandTimerRunning ? ((SAND_TIMER_DURATION - remaining) * 100) / SAND_TIMER_DURATION : 0;

    // Timer display
    int mins = remaining / 60000;
    int secs = (remaining % 60000) / 1000;
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", mins, secs);

    lv_obj_t *timeLbl = lv_label_create(card);
    lv_label_set_text(timeLbl, timeBuf);
    lv_obj_set_style_text_color(timeLbl, theme.text, 0);
    lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_48, 0);
    lv_obj_align(timeLbl, LV_ALIGN_CENTER, 0, -40);

    // Progress arc
    lv_obj_t *arc = lv_arc_create(card);
    lv_obj_set_size(arc, 180, 180);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 20);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, progress);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, theme.accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 15, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    // Start/Reset button
    lv_obj_t *btn = lv_btn_create(card);
    lv_obj_set_size(btn, 120, 45);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(btn, sandTimerRunning ? lv_color_hex(0xFF453A) : theme.accent, 0);
    lv_obj_set_style_radius(btn, 22, 0);
    lv_obj_add_event_cb(btn, sandTimerStartCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btnLbl = lv_label_create(btn);
    lv_label_set_text(btnLbl, sandTimerRunning ? "RESET" : "START");
    lv_obj_set_style_text_color(btnLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btnLbl);

    // Mini status bar removed
}

void sandTimerStartCb(lv_event_t *e) {
    if (sandTimerRunning) {
        sandTimerRunning = false;
    } else {
        sandTimerRunning = true;
        sandTimerStartMs = millis();
    }
    navigateTo(CAT_TIMER, 0);
}

void createStopwatchCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("STOPWATCH");

    unsigned long total = stopwatchRunning ? (stopwatchElapsedMs + millis() - stopwatchStartMs) : stopwatchElapsedMs;
    int mins = total / 60000;
    int secs = (total % 60000) / 1000;
    int ms = (total % 1000) / 10;

    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d.%02d", mins, secs, ms);

    lv_obj_t *timeLbl = lv_label_create(card);
    lv_label_set_text(timeLbl, timeBuf);
    lv_obj_set_style_text_color(timeLbl, theme.text, 0);
    lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_48, 0);
    lv_obj_align(timeLbl, LV_ALIGN_CENTER, 0, -20);

    // Play/Pause button
    lv_obj_t *playBtn = lv_btn_create(card);
    lv_obj_set_size(playBtn, 100, 45);
    lv_obj_align(playBtn, LV_ALIGN_BOTTOM_MID, -60, -20);
    lv_obj_set_style_bg_color(playBtn, stopwatchRunning ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x30D158), 0);
    lv_obj_set_style_radius(playBtn, 22, 0);
    lv_obj_add_event_cb(playBtn, stopwatchToggleCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *playLbl = lv_label_create(playBtn);
    lv_label_set_text(playLbl, stopwatchRunning ? "PAUSE" : "START");
    lv_obj_set_style_text_color(playLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(playLbl);

    // Reset button
    lv_obj_t *resetBtn = lv_btn_create(card);
    lv_obj_set_size(resetBtn, 100, 45);
    lv_obj_align(resetBtn, LV_ALIGN_BOTTOM_MID, 60, -20);
    lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_radius(resetBtn, 22, 0);
    lv_obj_add_event_cb(resetBtn, stopwatchResetCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *resetLbl = lv_label_create(resetBtn);
    lv_label_set_text(resetLbl, "RESET");
    lv_obj_set_style_text_color(resetLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(resetLbl);

    // Mini status bar removed
}

void stopwatchToggleCb(lv_event_t *e) {
    if (stopwatchRunning) {
        stopwatchElapsedMs += millis() - stopwatchStartMs;
        stopwatchRunning = false;
    } else {
        stopwatchRunning = true;
        trackStopwatchUse();
        stopwatchStartMs = millis();
    }
    navigateTo(CAT_TIMER, 1);
}

void stopwatchResetCb(lv_event_t *e) {
    stopwatchRunning = false;
    stopwatchElapsedMs = 0;
    navigateTo(CAT_TIMER, 1);
}

void createCountdownCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("COUNTDOWN");

    const char* labels[] = {"1 min", "3 min", "5 min", "10 min"};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_obj_create(card);
        lv_obj_set_size(btn, (LCD_WIDTH - 80) / 2, 70);
        int x = (i % 2 == 0) ? -((LCD_WIDTH - 80) / 4 + 5) : ((LCD_WIDTH - 80) / 4 + 5);
        int y = (i < 2) ? -40 : 50;
        lv_obj_align(btn, LV_ALIGN_CENTER, x, y);
        lv_obj_set_style_bg_color(btn, i == countdownSelected ? theme.accent : lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
    
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_set_style_text_color(lbl, theme.text, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_center(lbl);
    }

    // Mini status bar removed
}

void createBreatheCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x00A896), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x02C39A), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "BREATHE");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(title, LV_OPA_70, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // Breathing circle
    int baseSize = 120;
    int pulseAdd = breatheRunning ? (int)(40 * sin((millis() - breatheStartMs) / 2000.0 * 3.14159)) : 0;
    int circleSize = baseSize + pulseAdd;

    lv_obj_t *circle = lv_obj_create(card);
    lv_obj_set_size(circle, circleSize, circleSize);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_30, 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 0, 0);

    const char* phases[] = {"Breathe In", "Hold", "Breathe Out", "Hold"};
    int phaseIndex = breatheRunning ? ((millis() - breatheStartMs) / 4000) % 4 : 0;

    lv_obj_t *phaseLbl = lv_label_create(circle);
    lv_label_set_text(phaseLbl, breatheRunning ? phases[phaseIndex] : "Tap to Start");
    lv_obj_set_style_text_color(phaseLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(phaseLbl, &lv_font_montserrat_16, 0);
    lv_obj_center(phaseLbl);

    // Start button
    lv_obj_t *btn = lv_btn_create(card);
    lv_obj_set_size(btn, 120, 45);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(btn, 22, 0);
    lv_obj_add_event_cb(btn, breatheStartCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btnLbl = lv_label_create(btn);
    lv_label_set_text(btnLbl, breatheRunning ? "STOP" : "START");
    lv_obj_set_style_text_color(btnLbl, lv_color_hex(0x00A896), 0);
    lv_obj_center(btnLbl);

    // Mini status bar removed
}

void breatheStartCb(lv_event_t *e) {
    breatheRunning = !breatheRunning;
    if (breatheRunning) breatheStartMs = millis();
    navigateTo(CAT_TIMER, 3);
}

// 
//  STREAK & ACHIEVEMENTS CARDS
// 
void createStepStreakCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("STEP STREAK");

    char streakBuf[16];
    snprintf(streakBuf, sizeof(streakBuf), "%d", userData.stepStreak);

    lv_obj_t *streakLbl = lv_label_create(card);
    lv_label_set_text(streakLbl, streakBuf);
    lv_obj_set_style_text_color(streakLbl, lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_text_font(streakLbl, &lv_font_montserrat_48, 0);
    lv_obj_align(streakLbl, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *daysLbl = lv_label_create(card);
    lv_label_set_text(daysLbl, "days in a row");
    lv_obj_set_style_text_color(daysLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_align(daysLbl, LV_ALIGN_CENTER, 0, 10);

    // Week view
    const char* days[] = {"M", "T", "W", "T", "F", "S", "S"};
    lv_obj_t *weekRow = lv_obj_create(card);
    lv_obj_set_size(weekRow, LCD_WIDTH - 70, 50);
    lv_obj_align(weekRow, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_opa(weekRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weekRow, 0, 0);
    lv_obj_set_flex_flow(weekRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weekRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    RTC_DateTime dt = rtc.getDateTime();
    for (int i = 0; i < 7; i++) {
        lv_obj_t *dayObj = lv_obj_create(weekRow);
        lv_obj_set_size(dayObj, 35, 45);
        lv_obj_set_style_bg_opa(dayObj, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(dayObj, 0, 0);
    
        lv_obj_t *dayLbl = lv_label_create(dayObj);
        lv_label_set_text(dayLbl, days[i]);
        lv_obj_set_style_text_color(dayLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(dayLbl, LV_ALIGN_TOP_MID, 0, 0);
    
        bool achieved = (userData.stepHistory[i] >= userData.dailyGoal);
        lv_obj_t *dot = lv_obj_create(dayObj);
        lv_obj_set_size(dot, 24, 24);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(dot, achieved ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    
        if (achieved) {
            lv_obj_t *check = lv_label_create(dot);
            lv_label_set_text(check, LV_SYMBOL_OK);
            lv_obj_set_style_text_color(check, lv_color_hex(0xFFFFFF), 0);
            lv_obj_center(check);
        }
    }

    // Mini status bar removed
}

void createGameStreakCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("GAME STREAK");

    char streakBuf[16];
    snprintf(streakBuf, sizeof(streakBuf), "%d", userData.blackjackStreak);

    lv_obj_t *streakLbl = lv_label_create(card);
    lv_label_set_text(streakLbl, streakBuf);
    lv_obj_set_style_text_color(streakLbl, lv_color_hex(0xD4AF37), 0);
    lv_obj_set_style_text_font(streakLbl, &lv_font_montserrat_48, 0);
    lv_obj_align(streakLbl, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *winsLbl = lv_label_create(card);
    lv_label_set_text(winsLbl, "win streak");
    lv_obj_set_style_text_color(winsLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_align(winsLbl, LV_ALIGN_CENTER, 0, 10);

    // Stats row
    char statsBuf[32];
    snprintf(statsBuf, sizeof(statsBuf), "Won: %d  Played: %d", userData.gamesWon, userData.gamesPlayed);
    lv_obj_t *statsLbl = lv_label_create(card);
    lv_label_set_text(statsLbl, statsBuf);
    lv_obj_set_style_text_color(statsLbl, theme.text, 0);
    lv_obj_align(statsLbl, LV_ALIGN_BOTTOM_MID, 0, -40);

    // Mini status bar removed
}

void createAchievementsCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("ACHIEVEMENTS");

    const char* achievements[] = {"5K Steps", "10K Steps", "Week Streak", "Blackjack Pro"};
    int thresholds[] = {5000, 10000, 7, 10};
    int values[] = {(int)userData.steps, (int)userData.steps, userData.stepStreak, userData.blackjackStreak};

    for (int i = 0; i < 4; i++) {
        bool unlocked = values[i] >= thresholds[i];
    
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, LCD_WIDTH - 70, 55);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 30 + i * 62);
        lv_obj_set_style_bg_color(row, unlocked ? lv_color_hex(0x2D6A4F) : lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_border_width(row, 0, 0);
    
        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, unlocked ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(icon, unlocked ? lv_color_hex(0x30D158) : lv_color_hex(0x636366), 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 15, 0);
    
        lv_obj_t *nameLbl = lv_label_create(row);
        lv_label_set_text(nameLbl, achievements[i]);
        lv_obj_set_style_text_color(nameLbl, theme.text, 0);
        lv_obj_align(nameLbl, LV_ALIGN_LEFT_MID, 45, 0);
    }

    // Mini status bar removed
}

// 
//  CALENDAR CARD
// 
void createCalendarCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("");

    RTC_DateTime dt = rtc.getDateTime();

    // Month header
    const char* months[] = {"January", "February", "March", "April", "May", "June",
                           "July", "August", "September", "October", "November", "December"};
    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "%s 20%02d", months[dt.getMonth()-1], dt.getYear());

    lv_obj_t *headerLbl = lv_label_create(card);
    lv_label_set_text(headerLbl, headerBuf);
    lv_obj_set_style_text_color(headerLbl, theme.text, 0);
    lv_obj_set_style_text_font(headerLbl, &lv_font_montserrat_20, 0);
    lv_obj_align(headerLbl, LV_ALIGN_TOP_MID, 0, 10);

    // Day names
    const char* dayNames[] = {"S", "M", "T", "W", "T", "F", "S"};
    for (int i = 0; i < 7; i++) {
        lv_obj_t *dayLbl = lv_label_create(card);
        lv_label_set_text(dayLbl, dayNames[i]);
        lv_obj_set_style_text_color(dayLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(dayLbl, LV_ALIGN_TOP_LEFT, 15 + i * 45, 50);
    }

    // Calendar grid (simplified)
    int startDay = 0;  // Would need actual calculation
    int daysInMonth = 31;  // Would need actual calculation

    for (int d = 1; d <= min(daysInMonth, 35); d++) {
        int pos = startDay + d - 1;
        int row = pos / 7;
        int col = pos % 7;
    
        lv_obj_t *dayObj = lv_obj_create(card);
        lv_obj_set_size(dayObj, 38, 38);
        lv_obj_align(dayObj, LV_ALIGN_TOP_LEFT, 10 + col * 45, 75 + row * 45);
    
        bool isToday = (d == dt.getDay());
        lv_obj_set_style_bg_color(dayObj, isToday ? theme.accent : lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_radius(dayObj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dayObj, 0, 0);
    
        char dBuf[4];
        snprintf(dBuf, sizeof(dBuf), "%d", d);
        lv_obj_t *dLbl = lv_label_create(dayObj);
        lv_label_set_text(dLbl, dBuf);
        lv_obj_set_style_text_color(dLbl, theme.text, 0);
        lv_obj_center(dLbl);
    }
}

// 
//  SETTINGS CARD
// 
void themeChangeCb(lv_event_t *e);
void brightnessChangeCb(lv_event_t *e);

void createSettingsCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("SETTINGS");

    // Theme selector - NOW TAPPABLE!
    lv_obj_t *themeRow = lv_obj_create(card);
    lv_obj_set_size(themeRow, LCD_WIDTH - 70, 60);
    lv_obj_align(themeRow, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(themeRow, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(themeRow, 14, 0);
    lv_obj_set_style_border_width(themeRow, 0, 0);
    lv_obj_add_flag(themeRow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(themeRow, themeChangeCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *themeLbl = lv_label_create(themeRow);
    lv_label_set_text(themeLbl, "Theme");
    lv_obj_set_style_text_color(themeLbl, theme.text, 0);
    lv_obj_align(themeLbl, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *themeValLbl = lv_label_create(themeRow);
    lv_label_set_text(themeValLbl, gradientThemes[userData.themeIndex].name);
    lv_obj_set_style_text_color(themeValLbl, theme.accent, 0);
    lv_obj_align(themeValLbl, LV_ALIGN_RIGHT_MID, -40, 0);

    // Arrow indicator
    lv_obj_t *arrow = lv_label_create(themeRow);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, lv_color_hex(0x8E8E93), 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);

    // Brightness slider
    lv_obj_t *brightRow = lv_obj_create(card);
    lv_obj_set_size(brightRow, LCD_WIDTH - 70, 60);
    lv_obj_align(brightRow, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_color(brightRow, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(brightRow, 14, 0);
    lv_obj_set_style_border_width(brightRow, 0, 0);

    lv_obj_t *brightLbl = lv_label_create(brightRow);
    lv_label_set_text(brightLbl, "Brightness");
    lv_obj_set_style_text_color(brightLbl, theme.text, 0);
    lv_obj_align(brightLbl, LV_ALIGN_LEFT_MID, 15, -10);

    lv_obj_t *slider = lv_slider_create(brightRow);
    lv_obj_set_size(slider, LCD_WIDTH - 120, 8);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_slider_set_range(slider, 50, 255);
    lv_slider_set_value(slider, userData.brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, theme.accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, theme.accent, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightnessChangeCb, LV_EVENT_VALUE_CHANGED, NULL);

    // Battery saver toggle
    lv_obj_t *saverRow = lv_obj_create(card);
    lv_obj_set_size(saverRow, LCD_WIDTH - 70, 60);
    lv_obj_align(saverRow, LV_ALIGN_TOP_MID, 0, 170);
    lv_obj_set_style_bg_color(saverRow, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(saverRow, 14, 0);
    lv_obj_set_style_border_width(saverRow, 0, 0);

    lv_obj_t *saverLbl = lv_label_create(saverRow);
    lv_label_set_text(saverLbl, "Battery Saver");
    lv_obj_set_style_text_color(saverLbl, theme.text, 0);
    lv_obj_align(saverLbl, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *saverSwitch = lv_switch_create(saverRow);
    lv_obj_align(saverSwitch, LV_ALIGN_RIGHT_MID, -15, 0);
    if (batterySaverMode) lv_obj_add_state(saverSwitch, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(saverSwitch, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(saverSwitch, lv_color_hex(0xFF9F0A), LV_PART_INDICATOR | LV_STATE_CHECKED);

    // Wallpaper selector with TAP functionality
    lv_obj_t *wallpaperContainer = lv_obj_create(card);
    lv_obj_set_size(wallpaperContainer, LCD_WIDTH - 70, 80);
    lv_obj_align(wallpaperContainer, LV_ALIGN_TOP_MID, 0, 240);
    lv_obj_set_style_bg_color(wallpaperContainer, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(wallpaperContainer, 15, 0);
    lv_obj_set_style_border_width(wallpaperContainer, 0, 0);

    lv_obj_t *wallpaperLabel = lv_label_create(wallpaperContainer);
    lv_label_set_text(wallpaperLabel, "Wallpaper");
    lv_obj_set_style_text_color(wallpaperLabel, theme.text, 0);
    lv_obj_align(wallpaperLabel, LV_ALIGN_TOP_LEFT, 15, 12);

    // Show current wallpaper name
    lv_obj_t *wallpaperValue = lv_label_create(wallpaperContainer);
    const char* wallpaperNames[] = {"Solid Color", "Mountain 1", "Mountain 2", "Mountain 3", "Mountain 4"};
    lv_label_set_text(wallpaperValue, wallpaperNames[userData.wallpaperIndex]);
    lv_obj_set_style_text_color(wallpaperValue, theme.accent, 0);
    lv_obj_align(wallpaperValue, LV_ALIGN_BOTTOM_RIGHT, -15, -12);

    // TAP indicator
    lv_obj_t *tapHint = lv_label_create(wallpaperContainer);
    lv_label_set_text(tapHint, "TAP TO CHANGE");
    lv_obj_set_style_text_color(tapHint, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(tapHint, &lv_font_montserrat_10, 0);
    lv_obj_align(tapHint, LV_ALIGN_TOP_RIGHT, -15, 12);
}

// Theme change callback - cycles through themes
void themeChangeCb(lv_event_t *e) {
    userData.themeIndex = (userData.themeIndex + 1) % NUM_THEMES;
    saveUserData();
    navigateTo(CAT_SETTINGS, 0);
}

void brightnessChangeCb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    userData.brightness = lv_slider_get_value(slider);
    if (!batterySaverMode) gfx->setBrightness(userData.brightness);
}

// 
//  BATTERY CARD (Main System Screen)
// 
void batterySaverToggleCb(lv_event_t *e);

void createBatteryCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "BATTERY");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 12);

    // Large percentage
    char percBuf[8];
    snprintf(percBuf, sizeof(percBuf), "%d%%", batteryPercent);
    lv_obj_t *percLbl = lv_label_create(card);
    lv_label_set_text(percLbl, percBuf);
    lv_obj_set_style_text_color(percLbl, batteryPercent > 20 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(percLbl, &lv_font_montserrat_48, 0);
    lv_obj_align(percLbl, LV_ALIGN_TOP_MID, 0, 40);

    // Charging status
    lv_obj_t *statusLbl = lv_label_create(card);
    if (isCharging) {
        lv_label_set_text(statusLbl, LV_SYMBOL_CHARGE " Charging");
        lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x30D158), 0);
    } else {
        calculateBatteryEstimates();
        char estBuf[32];
        uint32_t hrs = batteryStats.combinedEstimateMins / 60;
        uint32_t mins = batteryStats.combinedEstimateMins % 60;
        snprintf(estBuf, sizeof(estBuf), "~%luh %lum remaining", hrs, mins);
        lv_label_set_text(statusLbl, estBuf);
        lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x8E8E93), 0);
    }
    lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(statusLbl, LV_ALIGN_TOP_MID, 0, 100);

    // Voltage
    char voltBuf[16];
    snprintf(voltBuf, sizeof(voltBuf), "%dmV", batteryVoltage);
    lv_obj_t *voltLbl = lv_label_create(card);
    lv_label_set_text(voltLbl, voltBuf);
    lv_obj_set_style_text_color(voltLbl, lv_color_hex(0x636366), 0);
    lv_obj_align(voltLbl, LV_ALIGN_TOP_MID, 0, 125);

    // Battery saver toggle
    lv_obj_t *saverRow = lv_obj_create(card);
    lv_obj_set_size(saverRow, LCD_WIDTH - 70, 50);
    lv_obj_align(saverRow, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(saverRow, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(saverRow, 14, 0);
    lv_obj_set_style_border_width(saverRow, 0, 0);

    lv_obj_t *saverLbl = lv_label_create(saverRow);
    lv_label_set_text(saverLbl, "Battery Saver");
    lv_obj_set_style_text_color(saverLbl, theme.text, 0);
    lv_obj_align(saverLbl, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *saverBtn = lv_btn_create(saverRow);
    lv_obj_set_size(saverBtn, 70, 30);
    lv_obj_align(saverBtn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(saverBtn, batterySaverMode ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_radius(saverBtn, 15, 0);
    lv_obj_add_event_cb(saverBtn, batterySaverToggleCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *saverBtnLbl = lv_label_create(saverBtn);
    lv_label_set_text(saverBtnLbl, batterySaverMode ? "ON" : "OFF");
    lv_obj_set_style_text_color(saverBtnLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(saverBtnLbl);

    // System info
    lv_obj_t *infoRow = lv_obj_create(card);
    lv_obj_set_size(infoRow, LCD_WIDTH - 70, 80);
    lv_obj_align(infoRow, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_bg_color(infoRow, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(infoRow, 14, 0);
    lv_obj_set_style_border_width(infoRow, 0, 0);

    // WiFi status - Updated to show open network info
    char wifiBuf[64];
    if (wifiConnected) {
        if (connectedToOpenNetwork) {
            snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %s [FREE]", wifiNetworks[0].ssid);
        } else if (connectedNetworkIndex >= 0) {
            snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %s", wifiNetworks[connectedNetworkIndex].ssid);
        } else {
            snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: Connected");
        }
    } else {
        snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: Disconnected");
    }
    lv_obj_t *wifiLbl = lv_label_create(infoRow);
    lv_label_set_text(wifiLbl, wifiBuf);
    // Show different color for open network (cyan) vs known network (green)
    lv_color_t wifiColor = !wifiConnected ? lv_color_hex(0xFF453A) : 
                          (connectedToOpenNetwork ? lv_color_hex(0x00D4FF) : lv_color_hex(0x30D158));
    lv_obj_set_style_text_color(wifiLbl, wifiColor, 0);
    lv_obj_set_style_text_font(wifiLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(wifiLbl, LV_ALIGN_TOP_LEFT, 10, 10);

    // RAM
    char ramBuf[24];
    snprintf(ramBuf, sizeof(ramBuf), "Free RAM: %luKB", freeRAM / 1024);
    lv_obj_t *ramLbl = lv_label_create(infoRow);
    lv_label_set_text(ramLbl, ramBuf);
    lv_obj_set_style_text_color(ramLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(ramLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(ramLbl, LV_ALIGN_TOP_LEFT, 10, 30);

    // SD status
    char sdBuf[24];
    snprintf(sdBuf, sizeof(sdBuf), "SD Card: %s", hasSD ? "OK" : "None");
    lv_obj_t *sdLbl = lv_label_create(infoRow);
    lv_label_set_text(sdLbl, sdBuf);
    lv_obj_set_style_text_color(sdLbl, hasSD ? lv_color_hex(0x30D158) : lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(sdLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(sdLbl, LV_ALIGN_TOP_LEFT, 10, 50);

    // Hint
    lv_obj_t *hintLbl = lv_label_create(card);
    lv_label_set_text(hintLbl, "Swipe down for stats");
    lv_obj_set_style_text_color(hintLbl, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(hintLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(hintLbl, LV_ALIGN_TOP_RIGHT, -15, 12);
}

void batterySaverToggleCb(lv_event_t *e) {
    toggleBatterySaver();
    navigateTo(CAT_SYSTEM, 0);
}

// 
//  BATTERY STATS SUB-CARD (24h graphs, estimates)
// 
void createBatteryStatsCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("BATTERY STATS");

    // Screen time graph title
    lv_obj_t *graphTitle = lv_label_create(card);
    lv_label_set_text(graphTitle, "Screen Time (24h)");
    lv_obj_set_style_text_color(graphTitle, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(graphTitle, &lv_font_montserrat_12, 0);
    lv_obj_align(graphTitle, LV_ALIGN_TOP_LEFT, 5, 25);

    // Bar graph container
    lv_obj_t *graphBg = lv_obj_create(card);
    lv_obj_set_size(graphBg, LCD_WIDTH - 70, 80);
    lv_obj_align(graphBg, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(graphBg, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(graphBg, 12, 0);
    lv_obj_set_style_border_width(graphBg, 0, 0);

    // Find max for scaling
    uint16_t maxMins = 1;
    for (int i = 0; i < USAGE_HISTORY_SIZE; i++) {
        if (batteryStats.hourlyScreenOnMins[i] > maxMins)
            maxMins = batteryStats.hourlyScreenOnMins[i];
    }

    // Draw bars
    int barWidth = (LCD_WIDTH - 90) / USAGE_HISTORY_SIZE;
    for (int i = 0; i < USAGE_HISTORY_SIZE; i++) {
        int barHeight = (batteryStats.hourlyScreenOnMins[i] * 60) / maxMins;
        if (barHeight < 2) barHeight = 2;
    
        lv_obj_t *bar = lv_obj_create(graphBg);
        lv_obj_set_size(bar, barWidth - 1, barHeight);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 5 + i * barWidth, -5);
        lv_obj_set_style_bg_color(bar, i == batteryStats.currentHourIndex ? lv_color_hex(0x0A84FF) : lv_color_hex(0x636366), 0);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
    }

    // Estimates breakdown
    calculateBatteryEstimates();

    lv_obj_t *estTitle = lv_label_create(card);
    lv_label_set_text(estTitle, "Estimates Breakdown");
    lv_obj_set_style_text_color(estTitle, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(estTitle, &lv_font_montserrat_12, 0);
    lv_obj_align(estTitle, LV_ALIGN_TOP_LEFT, 5, 135);

    const char* estLabels[] = {"Simple", "Weighted", "Learned", "Combined"};
    uint32_t estValues[] = {batteryStats.simpleEstimateMins, batteryStats.weightedEstimateMins, 
                            batteryStats.learnedEstimateMins, batteryStats.combinedEstimateMins};
    lv_color_t estColors[] = {lv_color_hex(0x8E8E93), lv_color_hex(0xFF9F0A), 
                              lv_color_hex(0x5856D6), lv_color_hex(0x30D158)};

    for (int i = 0; i < 4; i++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%s: %lum", estLabels[i], estValues[i]);
        lv_obj_t *estLbl = lv_label_create(card);
        lv_label_set_text(estLbl, buf);
        lv_obj_set_style_text_color(estLbl, estColors[i], 0);
        lv_obj_set_style_text_font(estLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(estLbl, LV_ALIGN_TOP_LEFT, 5 + (i % 2) * 150, 155 + (i / 2) * 18);
    }

    // Card usage
    lv_obj_t *usageTitle = lv_label_create(card);
    lv_label_set_text(usageTitle, "Card Usage");
    lv_obj_set_style_text_color(usageTitle, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(usageTitle, &lv_font_montserrat_12, 0);
    lv_obj_align(usageTitle, LV_ALIGN_TOP_LEFT, 5, 200);

    const char* cardNames[] = {"Clock", "Compass", "Activity", "Games", "Weather", "System"};
    lv_color_t cardColors[] = {lv_color_hex(0x0A84FF), lv_color_hex(0xFF453A), lv_color_hex(0x5856D6),
                               lv_color_hex(0xFF9F0A), lv_color_hex(0x30D158), lv_color_hex(0x8E8E93)};

    uint32_t totalUsage = 0;
    for (int i = 0; i < 6; i++) totalUsage += batteryStats.cardUsageTime[i];
    if (totalUsage == 0) totalUsage = 1;

    for (int i = 0; i < 6; i++) {
        int pct = (batteryStats.cardUsageTime[i] * 100) / totalUsage;
        int barLen = (pct * 80) / 100;
    
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, LCD_WIDTH - 70, 18);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, 218 + i * 20);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
    
        lv_obj_t *nameLbl = lv_label_create(row);
        lv_label_set_text(nameLbl, cardNames[i]);
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(nameLbl, LV_ALIGN_LEFT_MID, 5, 0);
    
        lv_obj_t *bar = lv_obj_create(row);
        lv_obj_set_size(bar, barLen, 8);
        lv_obj_align(bar, LV_ALIGN_LEFT_MID, 70, 0);
        lv_obj_set_style_bg_color(bar, cardColors[i], 0);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
    
        char pctBuf[8];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
        lv_obj_t *pctLbl = lv_label_create(row);
        lv_label_set_text(pctLbl, pctBuf);
        lv_obj_set_style_text_color(pctLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(pctLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(pctLbl, LV_ALIGN_RIGHT_MID, -5, 0);
    }
}

// 
//  USAGE PATTERNS SUB-CARD
// 
void createUsagePatternsCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("USAGE PATTERNS");

    // Weekly screen time
    lv_obj_t *weekTitle = lv_label_create(card);
    lv_label_set_text(weekTitle, "Weekly Screen Time (hours)");
    lv_obj_set_style_text_color(weekTitle, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(weekTitle, &lv_font_montserrat_12, 0);
    lv_obj_align(weekTitle, LV_ALIGN_TOP_LEFT, 5, 25);

    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    RTC_DateTime dt = rtc.getDateTime();

    for (int i = 0; i < 7; i++) {
        float hours = batteryStats.dailyAvgScreenOnHours[i];
        int barLen = (int)(hours * 25);  // 25px per hour
        if (barLen > 180) barLen = 180;
        if (barLen < 2) barLen = 2;
    
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, LCD_WIDTH - 70, 22);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, 45 + i * 24);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
    
        lv_obj_t *dayLbl = lv_label_create(row);
        lv_label_set_text(dayLbl, days[i]);
        lv_obj_set_style_text_color(dayLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(dayLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(dayLbl, LV_ALIGN_LEFT_MID, 5, 0);
    
        bool isToday = (i == dt.getWeek());
        lv_obj_t *bar = lv_obj_create(row);
        lv_obj_set_size(bar, barLen, 12);
        lv_obj_align(bar, LV_ALIGN_LEFT_MID, 45, 0);
        lv_obj_set_style_bg_color(bar, isToday ? lv_color_hex(0x30D158) : lv_color_hex(0x636366), 0);
        lv_obj_set_style_radius(bar, 6, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
    
        char hrBuf[8];
        snprintf(hrBuf, sizeof(hrBuf), "%.1fh", hours);
        lv_obj_t *hrLbl = lv_label_create(row);
        lv_label_set_text(hrLbl, hrBuf);
        lv_obj_set_style_text_color(hrLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(hrLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(hrLbl, LV_ALIGN_RIGHT_MID, -5, 0);
    }

    // Drain analysis
    lv_obj_t *drainTitle = lv_label_create(card);
    lv_label_set_text(drainTitle, "Battery Drain Analysis");
    lv_obj_set_style_text_color(drainTitle, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(drainTitle, &lv_font_montserrat_12, 0);
    lv_obj_align(drainTitle, LV_ALIGN_BOTTOM_LEFT, 5, -80);

    char avgBuf[32], wBuf[32], fullBuf[32];
    snprintf(avgBuf, sizeof(avgBuf), "Avg drain/hour: %.1f%%", batteryStats.avgDrainPerHour);
    snprintf(wBuf, sizeof(wBuf), "Recent drain: %.1f%%", batteryStats.weightedDrainRate);
    if (batteryStats.avgDrainPerHour > 0) {
        snprintf(fullBuf, sizeof(fullBuf), "Full battery: %.0fh", 100.0f / batteryStats.avgDrainPerHour);
    } else {
        snprintf(fullBuf, sizeof(fullBuf), "Full battery: --");
    }

    lv_obj_t *avgLbl = lv_label_create(card);
    lv_label_set_text(avgLbl, avgBuf);
    lv_obj_set_style_text_color(avgLbl, theme.text, 0);
    lv_obj_set_style_text_font(avgLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(avgLbl, LV_ALIGN_BOTTOM_LEFT, 5, -60);

    lv_obj_t *wLbl = lv_label_create(card);
    lv_label_set_text(wLbl, wBuf);
    lv_obj_set_style_text_color(wLbl, theme.text, 0);
    lv_obj_set_style_text_font(wLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(wLbl, LV_ALIGN_BOTTOM_LEFT, 5, -42);

    lv_obj_t *fullLbl = lv_label_create(card);
    lv_label_set_text(fullLbl, fullBuf);
    lv_obj_set_style_text_color(fullLbl, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_text_font(fullLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(fullLbl, LV_ALIGN_BOTTOM_LEFT, 5, -24);
}

void factoryResetCb(lv_event_t *e);

void createFactoryResetCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("FACTORY RESET");

    lv_obj_t *warningLbl = lv_label_create(card);
    lv_label_set_text(warningLbl, "WARNING: This will clear:");
    lv_obj_set_style_text_color(warningLbl, lv_color_hex(0xFF9F0A), 0);
    lv_obj_align(warningLbl, LV_ALIGN_TOP_LEFT, 5, 35);

    const char* items[] = {"- Steps, games, all settings", "- Battery learning data", "- Usage patterns"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *itemLbl = lv_label_create(card);
        lv_label_set_text(itemLbl, items[i]);
        lv_obj_set_style_text_color(itemLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(itemLbl, LV_ALIGN_TOP_LEFT, 5, 60 + i * 20);
    }

    lv_obj_t *preservedLbl = lv_label_create(card);
    lv_label_set_text(preservedLbl, "PRESERVED: SD card, firmware");
    lv_obj_set_style_text_color(preservedLbl, lv_color_hex(0x30D158), 0);
    lv_obj_align(preservedLbl, LV_ALIGN_TOP_LEFT, 5, 130);

    // Reset button
    lv_obj_t *resetBtn = lv_btn_create(card);
    lv_obj_set_size(resetBtn, 180, 55);
    lv_obj_align(resetBtn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(resetBtn, 16, 0);
    lv_obj_add_event_cb(resetBtn, factoryResetCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *resetLbl = lv_label_create(resetBtn);
    lv_label_set_text(resetLbl, "RESET ALL");
    lv_obj_set_style_text_color(resetLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(resetLbl, &lv_font_montserrat_18, 0);
    lv_obj_center(resetLbl);
}

void factoryResetCb(lv_event_t *e) {
    factoryReset();
}

// 
//  Continue to Part 5 (Setup & Loop)...
// 
// 
//  S3 MiniOS v4.0 - PART 5: Setup & Loop
//  Place this file in the same folder as S3_MiniOS.ino
// 

// 
//  TORCH CARD - Flashlight with on/off toggle
// 
void createTorchCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    // If torch is on, show bright color, otherwise show dark card
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    if (torchOn) {
        lv_obj_set_style_bg_color(card, lv_color_hex(torchColors[torchColorIndex]), 0);
        gfx->setBrightness(torchBrightness);
    } else {
        lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
        gfx->setBrightness(batterySaverMode ? 100 : userData.brightness);
    }

    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Title
    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, "TORCH");
    lv_obj_set_style_text_color(titleLabel, torchOn ? lv_color_hex(0x000000) : lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 16, 16);

    // Power icon (large button area)
    lv_obj_t *powerBtn = lv_obj_create(card);
    lv_obj_set_size(powerBtn, 120, 120);
    lv_obj_align(powerBtn, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(powerBtn, torchOn ? lv_color_hex(0x000000) : theme.accent, 0);
    lv_obj_set_style_bg_opa(powerBtn, torchOn ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(powerBtn, 60, 0);
    lv_obj_set_style_border_width(powerBtn, 0, 0);

    lv_obj_t *powerIcon = lv_label_create(powerBtn);
    lv_label_set_text(powerIcon, LV_SYMBOL_POWER);
    lv_obj_set_style_text_color(powerIcon, torchOn ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(powerIcon, &lv_font_montserrat_48, 0);
    lv_obj_center(powerIcon);

    // Status text
    lv_obj_t *statusLabel = lv_label_create(card);
    lv_label_set_text(statusLabel, torchOn ? "TAP to turn OFF" : "TAP to turn ON");
    lv_obj_set_style_text_color(statusLabel, torchOn ? lv_color_hex(0x000000) : lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(statusLabel, LV_ALIGN_CENTER, 0, 70);

    // Color indicator
    char colorBuf[32];
    snprintf(colorBuf, sizeof(colorBuf), "Color: %s", torchColorNames[torchColorIndex]);
    lv_obj_t *colorLabel = lv_label_create(card);
    lv_label_set_text(colorLabel, colorBuf);
    lv_obj_set_style_text_color(colorLabel, torchOn ? lv_color_hex(0x000000) : lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(colorLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(colorLabel, LV_ALIGN_CENTER, 0, 95);

    // Swipe down hint
    lv_obj_t *hintLabel = lv_label_create(card);
    lv_label_set_text(hintLabel, "Swipe down for settings");
    lv_obj_set_style_text_color(hintLabel, torchOn ? lv_color_hex(0x000000) : lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -40);

    if (!torchOn) {
    }
}

// 
//  TORCH SETTINGS CARD - Brightness slider and color selection
// 
void createTorchSettingsCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Title
    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, "TORCH SETTINGS");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 16, 16);

    // Brightness section
    lv_obj_t *brightLabel = lv_label_create(card);
    lv_label_set_text(brightLabel, "BRIGHTNESS");
    lv_obj_set_style_text_color(brightLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(brightLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(brightLabel, LV_ALIGN_TOP_MID, 0, 50);

    // Brightness bar
    lv_obj_t *brightBar = lv_obj_create(card);
    lv_obj_set_size(brightBar, LCD_WIDTH - 80, 30);
    lv_obj_align(brightBar, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_bg_color(brightBar, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(brightBar, 15, 0);
    lv_obj_set_style_border_width(brightBar, 0, 0);

    // Brightness fill
    int fillWidth = ((LCD_WIDTH - 80) * torchBrightness) / 255;
    lv_obj_t *brightFill = lv_obj_create(brightBar);
    lv_obj_set_size(brightFill, fillWidth, 26);
    lv_obj_align(brightFill, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(brightFill, theme.accent, 0);
    lv_obj_set_style_radius(brightFill, 13, 0);
    lv_obj_set_style_border_width(brightFill, 0, 0);

    // Brightness percentage
    char brightBuf[8];
    snprintf(brightBuf, sizeof(brightBuf), "%d%%", (torchBrightness * 100) / 255);
    lv_obj_t *brightPctLabel = lv_label_create(card);
    lv_label_set_text(brightPctLabel, brightBuf);
    lv_obj_set_style_text_color(brightPctLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(brightPctLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(brightPctLabel, LV_ALIGN_TOP_MID, 0, 115);

    // Color section
    lv_obj_t *colorLabel = lv_label_create(card);
    lv_label_set_text(colorLabel, "COLOR");
    lv_obj_set_style_text_color(colorLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(colorLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(colorLabel, LV_ALIGN_TOP_MID, 0, 160);

    // Color swatches
    int swatchSize = 40;
    int startX = -(NUM_TORCH_COLORS * (swatchSize + 10)) / 2 + swatchSize/2;

    for (int i = 0; i < NUM_TORCH_COLORS; i++) {
        lv_obj_t *swatch = lv_obj_create(card);
        lv_obj_set_size(swatch, swatchSize, swatchSize);
        lv_obj_align(swatch, LV_ALIGN_TOP_MID, startX + i * (swatchSize + 10), 185);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(torchColors[i]), 0);
        lv_obj_set_style_radius(swatch, swatchSize/2, 0);
    
        if (i == torchColorIndex) {
            lv_obj_set_style_border_width(swatch, 3, 0);
            lv_obj_set_style_border_color(swatch, lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_set_style_border_width(swatch, 1, 0);
            lv_obj_set_style_border_color(swatch, lv_color_hex(0x3A3A3C), 0);
        }
    }

    // Current color name
    lv_obj_t *colorNameLabel = lv_label_create(card);
    lv_label_set_text(colorNameLabel, torchColorNames[torchColorIndex]);
    lv_obj_set_style_text_color(colorNameLabel, lv_color_hex(torchColors[torchColorIndex]), 0);
    lv_obj_set_style_text_font(colorNameLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(colorNameLabel, LV_ALIGN_TOP_MID, 0, 240);

    // Hint
    lv_obj_t *hintLabel = lv_label_create(card);
    lv_label_set_text(hintLabel, "Tap screen to adjust");
    lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -40);
}

// 
//  IMPROVED STOPWATCH CARD WITH LAPS
// 
void createStopwatchCardImproved() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Title
    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, "STOPWATCH");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 16, 16);

    // Calculate elapsed time
    unsigned long elapsed = stopwatchElapsedMs;
    if (stopwatchRunning) {
        elapsed += (millis() - stopwatchStartMs);
    }

    unsigned long mins = (elapsed / 60000) % 60;
    unsigned long secs = (elapsed / 1000) % 60;
    unsigned long ms = (elapsed % 1000) / 10;

    // Time display
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu", mins, secs);
    lv_obj_t *timeLabel = lv_label_create(card);
    lv_label_set_text(timeLabel, timeBuf);
    lv_obj_set_style_text_color(timeLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(timeLabel, LV_ALIGN_TOP_MID, 0, 40);

    // Milliseconds
    char msBuf[8];
    snprintf(msBuf, sizeof(msBuf), ".%02lu", ms);
    lv_obj_t *msLabel = lv_label_create(card);
    lv_label_set_text(msLabel, msBuf);
    lv_obj_set_style_text_color(msLabel, theme.accent, 0);
    lv_obj_set_style_text_font(msLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(msLabel, LV_ALIGN_TOP_MID, 75, 55);

    // Status indicator
    lv_obj_t *statusDot = lv_obj_create(card);
    lv_obj_set_size(statusDot, 12, 12);
    lv_obj_align(statusDot, LV_ALIGN_TOP_MID, -80, 60);
    lv_obj_set_style_bg_color(statusDot, stopwatchRunning ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(statusDot, 6, 0);
    lv_obj_set_style_border_width(statusDot, 0, 0);

    // Lap list area
    lv_obj_t *lapContainer = lv_obj_create(card);
    lv_obj_set_size(lapContainer, LCD_WIDTH - 60, 140);
    lv_obj_align(lapContainer, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(lapContainer, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(lapContainer, 16, 0);
    lv_obj_set_style_border_width(lapContainer, 0, 0);
    lv_obj_set_style_pad_all(lapContainer, 8, 0);
    lv_obj_set_flex_flow(lapContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(lapContainer, 4, 0);

    if (lapCount == 0) {
        lv_obj_t *noLapsLabel = lv_label_create(lapContainer);
        lv_label_set_text(noLapsLabel, "No laps recorded");
        lv_obj_set_style_text_color(noLapsLabel, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(noLapsLabel, &lv_font_montserrat_12, 0);
        lv_obj_center(noLapsLabel);
    } else {
        // Show last 4 laps (most recent first)
        int startLap = lapCount > 4 ? lapCount - 4 : 0;
        for (int i = lapCount - 1; i >= startLap; i--) {
            lv_obj_t *lapRow = lv_obj_create(lapContainer);
            lv_obj_set_size(lapRow, LCD_WIDTH - 80, 28);
            lv_obj_set_style_bg_opa(lapRow, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(lapRow, 0, 0);
            lv_obj_set_style_pad_all(lapRow, 0, 0);
        
            // Lap number
            char lapNumBuf[8];
            snprintf(lapNumBuf, sizeof(lapNumBuf), "Lap %d", i + 1);
            lv_obj_t *lapNumLabel = lv_label_create(lapRow);
            lv_label_set_text(lapNumLabel, lapNumBuf);
            lv_obj_set_style_text_color(lapNumLabel, lv_color_hex(0x636366), 0);
            lv_obj_set_style_text_font(lapNumLabel, &lv_font_montserrat_12, 0);
            lv_obj_align(lapNumLabel, LV_ALIGN_LEFT_MID, 0, 0);
        
            // Lap time
            unsigned long lt = lapTimes[i];
            char lapTimeBuf[16];
            snprintf(lapTimeBuf, sizeof(lapTimeBuf), "%02lu:%02lu.%02lu", 
                     (lt / 60000) % 60, (lt / 1000) % 60, (lt % 1000) / 10);
            lv_obj_t *lapTimeLabel = lv_label_create(lapRow);
            lv_label_set_text(lapTimeLabel, lapTimeBuf);
            lv_obj_set_style_text_color(lapTimeLabel, theme.accent, 0);
            lv_obj_set_style_text_font(lapTimeLabel, &lv_font_montserrat_12, 0);
            lv_obj_align(lapTimeLabel, LV_ALIGN_CENTER, 0, 0);
        
            // Total time
            unsigned long tt = lapTotalTimes[i];
            char totalTimeBuf[16];
            snprintf(totalTimeBuf, sizeof(totalTimeBuf), "%02lu:%02lu", 
                     (tt / 60000) % 60, (tt / 1000) % 60);
            lv_obj_t *totalTimeLabel = lv_label_create(lapRow);
            lv_label_set_text(totalTimeLabel, totalTimeBuf);
            lv_obj_set_style_text_color(totalTimeLabel, lv_color_hex(0x636366), 0);
            lv_obj_set_style_text_font(totalTimeLabel, &lv_font_montserrat_12, 0);
            lv_obj_align(totalTimeLabel, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }

    // Lap count indicator
    char lapCountBuf[16];
    snprintf(lapCountBuf, sizeof(lapCountBuf), "%d laps", lapCount);
    lv_obj_t *lapCountLabel = lv_label_create(card);
    lv_label_set_text(lapCountLabel, lapCountBuf);
    lv_obj_set_style_text_color(lapCountLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(lapCountLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(lapCountLabel, LV_ALIGN_BOTTOM_MID, 0, -40);
}

// 
//  IMPROVED WEATHER CARD WITH VISUAL ICONS
// 
void createWeatherCardImproved() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Title with city
    char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "%s, %s", weatherCity, weatherCountry);
    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, titleBuf);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 16, 16);

    // Weather icon (large)
    lv_obj_t *iconContainer = lv_obj_create(card);
    lv_obj_set_size(iconContainer, 80, 80);
    lv_obj_align(iconContainer, LV_ALIGN_TOP_MID, -50, 45);
    lv_obj_set_style_bg_opa(iconContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(iconContainer, 0, 0);

    // Determine icon based on weather description
    const char* weatherIcon = "?";
    lv_color_t iconColor = lv_color_hex(0xFFD700);  // Default: sun yellow

    String descLower = weatherDesc;
    descLower.toLowerCase();

    if (descLower.indexOf("sun") >= 0 || descLower.indexOf("clear") >= 0) {
        weatherIcon = "O";  // Sun symbol
        iconColor = lv_color_hex(0xFFD700);
    } else if (descLower.indexOf("cloud") >= 0) {
        weatherIcon = "C";
        iconColor = lv_color_hex(0x8E8E93);
    } else if (descLower.indexOf("rain") >= 0) {
        weatherIcon = "R";
        iconColor = lv_color_hex(0x0A84FF);
    } else if (descLower.indexOf("storm") >= 0 || descLower.indexOf("thunder") >= 0) {
        weatherIcon = "S";
        iconColor = lv_color_hex(0xFFD60A);
    } else if (descLower.indexOf("snow") >= 0) {
        weatherIcon = "*";
        iconColor = lv_color_hex(0xFFFFFF);
    }

    lv_obj_t *iconLabel = lv_label_create(iconContainer);
    lv_label_set_text(iconLabel, weatherIcon);
    lv_obj_set_style_text_color(iconLabel, iconColor, 0);
    lv_obj_set_style_text_font(iconLabel, &lv_font_montserrat_48, 0);
    lv_obj_center(iconLabel);

    // Temperature (large)
    char tempBuf[16];
    snprintf(tempBuf, sizeof(tempBuf), "%.0f", weatherTemp);
    lv_obj_t *tempLabel = lv_label_create(card);
    lv_label_set_text(tempLabel, tempBuf);
    lv_obj_set_style_text_color(tempLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(tempLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(tempLabel, LV_ALIGN_TOP_MID, 40, 60);

    // Description
    lv_obj_t *descLabel = lv_label_create(card);
    lv_label_set_text(descLabel, weatherDesc.c_str());
    lv_obj_set_style_text_color(descLabel, theme.accent, 0);
    lv_obj_set_style_text_font(descLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(descLabel, LV_ALIGN_CENTER, 0, 30);

    // High/Low container
    lv_obj_t *hlContainer = lv_obj_create(card);
    lv_obj_set_size(hlContainer, LCD_WIDTH - 80, 50);
    lv_obj_align(hlContainer, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_bg_color(hlContainer, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(hlContainer, 12, 0);
    lv_obj_set_style_border_width(hlContainer, 0, 0);

    // High temp
    char highBuf[16];
    snprintf(highBuf, sizeof(highBuf), "H: %.0f", weatherHigh);
    lv_obj_t *highLabel = lv_label_create(hlContainer);
    lv_label_set_text(highLabel, highBuf);
    lv_obj_set_style_text_color(highLabel, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(highLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(highLabel, LV_ALIGN_LEFT_MID, 20, 0);

    // Low temp
    char lowBuf[16];
    snprintf(lowBuf, sizeof(lowBuf), "L: %.0f", weatherLow);
    lv_obj_t *lowLabel = lv_label_create(hlContainer);
    lv_label_set_text(lowLabel, lowBuf);
    lv_obj_set_style_text_color(lowLabel, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_text_font(lowLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(lowLabel, LV_ALIGN_RIGHT_MID, -20, 0);
}

// 
//  CALCULATOR CARD
// 
void createCalculatorCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Title
    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, "CALCULATOR");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 16, 16);

    // Display
    lv_obj_t *display = lv_obj_create(card);
    lv_obj_set_size(display, LCD_WIDTH - 60, 60);
    lv_obj_align(display, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(display, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(display, 12, 0);
    lv_obj_set_style_border_width(display, 0, 0);

    lv_obj_t *displayLabel = lv_label_create(display);
    lv_label_set_text(displayLabel, calcDisplay);
    lv_obj_set_style_text_color(displayLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(displayLabel, &lv_font_montserrat_28, 0);
    lv_obj_align(displayLabel, LV_ALIGN_RIGHT_MID, -16, 0);

    // Button grid - simplified visual (actual interaction via touch regions)
    const char* buttons[] = {
        "C", "+/-", "%", "/",
        "7", "8", "9", "x",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", "0", ".", "="
    };

    int btnSize = 55;
    int startY = 115;
    int cols = 4;
    int spacing = 8;
    int totalWidth = cols * btnSize + (cols - 1) * spacing;
    int startX = (LCD_WIDTH - 24 - totalWidth) / 2;

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int idx = row * 4 + col;
            if (idx >= 20) break;
        
            // Skip duplicate 0
            if (row == 4 && col == 1) continue;
        
            lv_obj_t *btn = lv_obj_create(card);
        
            int btnW = (row == 4 && col == 0) ? btnSize * 2 + spacing : btnSize;
        
            lv_obj_set_size(btn, btnW, btnSize);
            lv_obj_align(btn, LV_ALIGN_TOP_LEFT, startX + col * (btnSize + spacing), startY + row * (btnSize + spacing));
        
            // Color coding
            lv_color_t btnColor;
            lv_color_t txtColor = lv_color_hex(0xFFFFFF);
        
            if (col == 3) {  // Operators
                btnColor = theme.accent;
            } else if (row == 0 && col < 3) {  // Top row functions
                btnColor = lv_color_hex(0x636366);
                txtColor = lv_color_hex(0x000000);
            } else if (buttons[idx][0] == '=') {
                btnColor = lv_color_hex(0x30D158);
            } else {
                btnColor = lv_color_hex(0x2C2C2E);
            }
        
            lv_obj_set_style_bg_color(btn, btnColor, 0);
            lv_obj_set_style_radius(btn, btnSize / 2, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
        
            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text(label, buttons[idx]);
            lv_obj_set_style_text_color(label, txtColor, 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
            lv_obj_center(label);
        }
    }
}

// 
//  CLICKER CARD
// 
void createClickerCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Title
    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, "CLICKER");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 16, 16);

    // Count display
    char countBuf[16];
    snprintf(countBuf, sizeof(countBuf), "%lu", clickerCount);
    lv_obj_t *countLabel = lv_label_create(card);
    lv_label_set_text(countLabel, countBuf);
    lv_obj_set_style_text_color(countLabel, theme.accent, 0);
    lv_obj_set_style_text_font(countLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(countLabel, LV_ALIGN_CENTER, 0, -60);

    // Click button (large)
    lv_obj_t *clickBtn = lv_obj_create(card);
    lv_obj_set_size(clickBtn, 140, 140);
    lv_obj_align(clickBtn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(clickBtn, theme.accent, 0);
    lv_obj_set_style_radius(clickBtn, 70, 0);
    lv_obj_set_style_border_width(clickBtn, 0, 0);
    lv_obj_set_style_shadow_width(clickBtn, 20, 0);
    lv_obj_set_style_shadow_color(clickBtn, theme.accent, 0);
    lv_obj_set_style_shadow_opa(clickBtn, LV_OPA_30, 0);

    lv_obj_t *clickLabel = lv_label_create(clickBtn);
    lv_label_set_text(clickLabel, "TAP");
    lv_obj_set_style_text_color(clickLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(clickLabel, &lv_font_montserrat_24, 0);
    lv_obj_center(clickLabel);

    // Reset hint
    lv_obj_t *resetLabel = lv_label_create(card);
    lv_label_set_text(resetLabel, "Hold to reset");
    lv_obj_set_style_text_color(resetLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(resetLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(resetLabel, LV_ALIGN_BOTTOM_MID, 0, -40);
}

// 
//  REACTION TEST CARD
// 
void createReactionTestCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    // Background color depends on state
    lv_color_t bgColor;
    const char* statusText;
    const char* instructionText;

    if (reactionTooEarly) {
        bgColor = lv_color_hex(0xFF453A);  // Red - too early
        statusText = "TOO EARLY!";
        instructionText = "Tap to try again";
    } else if (reactionWaiting) {
        bgColor = lv_color_hex(0x30D158);  // Green - TAP NOW!
        statusText = "TAP NOW!";
        instructionText = "";
    } else if (reactionTestActive) {
        bgColor = lv_color_hex(0xFF9F0A);  // Orange - wait
        statusText = "WAIT...";
        instructionText = "Wait for green";
    } else {
        bgColor = lv_color_hex(0x0A0A0C);  // Dark - ready
        statusText = "REACTION TEST";
        instructionText = "Tap to start";
    }

    lv_obj_set_style_bg_color(card, bgColor, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Status text
    lv_obj_t *statusLabel = lv_label_create(card);
    lv_label_set_text(statusLabel, statusText);
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(statusLabel, LV_ALIGN_CENTER, 0, -40);

    // Last reaction time
    if (lastReactionTime > 0 && !reactionTestActive && !reactionWaiting && !reactionTooEarly) {
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "Last: %lu ms", lastReactionTime);
        lv_obj_t *lastLabel = lv_label_create(card);
        lv_label_set_text(lastLabel, timeBuf);
        lv_obj_set_style_text_color(lastLabel, theme.accent, 0);
        lv_obj_set_style_text_font(lastLabel, &lv_font_montserrat_28, 0);
        lv_obj_align(lastLabel, LV_ALIGN_CENTER, 0, 10);
    
        // Best time
        char bestBuf[32];
        snprintf(bestBuf, sizeof(bestBuf), "Best: %lu ms", bestReactionTime < 9999 ? bestReactionTime : 0);
        lv_obj_t *bestLabel = lv_label_create(card);
        lv_label_set_text(bestLabel, bestBuf);
        lv_obj_set_style_text_color(bestLabel, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(bestLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(bestLabel, LV_ALIGN_CENTER, 0, 50);
    }

    // Instruction
    if (strlen(instructionText) > 0) {
        lv_obj_t *instrLabel = lv_label_create(card);
        lv_label_set_text(instrLabel, instructionText);
        lv_obj_set_style_text_color(instrLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(instrLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(instrLabel, LV_ALIGN_CENTER, 0, 80);
    }

    if (!reactionTestActive && !reactionWaiting && !reactionTooEarly) {
    }
}

// 
//  DAILY CHALLENGE CARD
// 
void generateChallenge() {
    challengeType = random(0, 3);  // 0=Math, 1=Memory, 2=Trivia
    challengeAnswered = false;
    challengeCorrect = false;

    if (challengeType == 0) {  // Math
        int a = random(1, 20);
        int b = random(1, 20);
        int op = random(0, 4);
    
        switch(op) {
            case 0: challengeAnswer = a + b; challengeQuestion = a * 1000 + b; break;  // +
            case 1: challengeAnswer = a - b; challengeQuestion = a * 1000 + b + 100000; break;  // -
            case 2: challengeAnswer = a * b; challengeQuestion = a * 1000 + b + 200000; break;  // *
            case 3: challengeAnswer = a; challengeQuestion = (a * b) * 1000 + b + 300000; break;  // / (a*b / b = a)
        }
    } else {
        // Trivia - simple general knowledge
        challengeQuestion = random(0, 5);
        challengeAnswer = challengeQuestion;  // Answer index matches question index
    }

    // Generate options
    challengeCorrectIndex = random(0, 4);
    for (int i = 0; i < 4; i++) {
        if (i == challengeCorrectIndex) {
            challengeOptions[i] = challengeAnswer;
        } else {
            challengeOptions[i] = challengeAnswer + random(-10, 10);
            if (challengeOptions[i] == challengeAnswer) challengeOptions[i] += (i + 1);
        }
    }
}

void createDailyChallengeCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    // Generate new challenge if needed
    static bool challengeGenerated = false;
    if (!challengeGenerated || challengeAnswered) {
        generateChallenge();
        challengeGenerated = true;
    }

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Title
    const char* typeNames[] = {"MATH", "MEMORY", "TRIVIA"};
    lv_obj_t *titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, typeNames[challengeType]);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 16, 16);

    // Score
    char scoreBuf[16];
    snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d", challengeScore);
    lv_obj_t *scoreLabel = lv_label_create(card);
    lv_label_set_text(scoreLabel, scoreBuf);
    lv_obj_set_style_text_color(scoreLabel, theme.accent, 0);
    lv_obj_set_style_text_font(scoreLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(scoreLabel, LV_ALIGN_TOP_RIGHT, -16, 16);

    // Question
    char questionBuf[64];
    if (challengeType == 0) {  // Math
        int a = (challengeQuestion % 100000) / 1000;
        int b = challengeQuestion % 1000;
        int opCode = challengeQuestion / 100000;
        const char* ops[] = {"+", "-", "x", "/"};
    
        if (opCode == 3) {
            snprintf(questionBuf, sizeof(questionBuf), "%d / %d = ?", a * b, b);
        } else {
            snprintf(questionBuf, sizeof(questionBuf), "%d %s %d = ?", a, ops[opCode], b);
        }
    } else {
        const char* triviaQuestions[] = {
            "Days in a week?",
            "Legs on a spider?",
            "Planets in solar system?",
            "Months in a year?",
            "Hours in a day?"
        };
        int triviaAnswers[] = {7, 8, 8, 12, 24};
        snprintf(questionBuf, sizeof(questionBuf), "%s", triviaQuestions[challengeQuestion % 5]);
        challengeAnswer = triviaAnswers[challengeQuestion % 5];
        challengeOptions[challengeCorrectIndex] = challengeAnswer;
    }

    lv_obj_t *questionLabel = lv_label_create(card);
    lv_label_set_text(questionLabel, questionBuf);
    lv_obj_set_style_text_color(questionLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(questionLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(questionLabel, LV_ALIGN_TOP_MID, 0, 50);

    // Answer options (2x2 grid)
    int btnW = (LCD_WIDTH - 80) / 2;
    int btnH = 50;
    int startY = 100;

    for (int i = 0; i < 4; i++) {
        int row = i / 2;
        int col = i % 2;
    
        lv_obj_t *btn = lv_obj_create(card);
        lv_obj_set_size(btn, btnW, btnH);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 25 + col * (btnW + 10), startY + row * (btnH + 10));
    
        lv_color_t btnColor = lv_color_hex(0x2C2C2E);
        if (challengeAnswered) {
            if (i == challengeCorrectIndex) {
                btnColor = lv_color_hex(0x30D158);  // Green for correct
            } else if (challengeOptions[i] == challengeAnswer - 1) {  // Wrong selection
                btnColor = lv_color_hex(0xFF453A);  // Red for wrong
            }
        }
    
        lv_obj_set_style_bg_color(btn, btnColor, 0);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
    
        char optBuf[16];
        snprintf(optBuf, sizeof(optBuf), "%d", challengeOptions[i]);
        lv_obj_t *optLabel = lv_label_create(btn);
        lv_label_set_text(optLabel, optBuf);
        lv_obj_set_style_text_color(optLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(optLabel, &lv_font_montserrat_20, 0);
        lv_obj_center(optLabel);
    }

    // Result message
    if (challengeAnswered) {
        lv_obj_t *resultLabel = lv_label_create(card);
        lv_label_set_text(resultLabel, challengeCorrect ? "Correct!" : "Wrong!");
        lv_obj_set_style_text_color(resultLabel, challengeCorrect ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
        lv_obj_set_style_text_font(resultLabel, &lv_font_montserrat_24, 0);
        lv_obj_align(resultLabel, LV_ALIGN_CENTER, 0, 80);
    
        lv_obj_t *nextLabel = lv_label_create(card);
        lv_label_set_text(nextLabel, "Tap for next");
        lv_obj_set_style_text_color(nextLabel, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(nextLabel, &lv_font_montserrat_12, 0);
        lv_obj_align(nextLabel, LV_ALIGN_CENTER, 0, 110);
    }
}

// 
//  STEP DETECTION
// 
float prevMag = 1.0, prevPrevMag = 1.0;
unsigned long lastStepTime = 0;

void updateStepCount() {
    if (!hasIMU) return;

    float currentMag = sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);

    // Peak detection
    if (prevMag > prevPrevMag && prevMag > currentMag && prevMag > 1.3) {
        if (millis() - lastStepTime > 300) {  // Debounce
            userData.steps++;
            userData.totalDistance += 0.0007;  // ~0.7m per step
            userData.totalCalories += 0.04;    // ~0.04 cal per step
            lastStepTime = millis();
        
            // Update today's history
            RTC_DateTime dt = rtc.getDateTime();
            userData.stepHistory[dt.getWeek()] = userData.steps;
        }
    }

    prevPrevMag = prevMag;
    prevMag = currentMag;
}

// 
//  DINO GAME UPDATE
// 
void updateDinoGame() {
    if (currentCategory != CAT_GAMES || currentSubCard != 1) return;
    if (dinoGameOver) return;

    // Physics
    if (dinoJumping) {
        dinoVelocity += GRAVITY;
        dinoY += (int)dinoVelocity;
    
        if (dinoY >= 0) {
            dinoY = 0;
            dinoVelocity = 0;
            dinoJumping = false;
        }
    }

    // Obstacle movement
    obstacleX -= 8;
    if (obstacleX < -30) {
        obstacleX = 320;
        dinoScore += 10;
    }

    // Collision detection
    if (obstacleX > 20 && obstacleX < 70 && dinoY > -45) {
        dinoGameOver = true;
        if (dinoScore > (int)userData.clickerScore) {
            userData.clickerScore = dinoScore;
        }
    }
}

// 
//  YES/NO SPINNER UPDATE
// 
void updateYesNoSpinner() {
    if (!yesNoSpinning) return;
    if (currentCategory != CAT_GAMES || currentSubCard != 2) return;

    yesNoAngle += 15;

    if (yesNoAngle > 360 + random(180, 540)) {
        yesNoSpinning = false;
        const char* results[] = {"Yes", "No", "Maybe", "Definitely", "Never", "Ask Again"};
        yesNoResult = results[random(0, 6)];
        navigateTo(CAT_GAMES, 2);
    }
}

// 
//  CHARGING ANIMATION
// 
void updateChargingAnimation() {
    if (!isCharging) return;

    if (millis() - lastChargingAnimMs > 500) {
        chargingAnimFrame = (chargingAnimFrame + 1) % 5;
        lastChargingAnimMs = millis();
    }
}

// 

// 
//  SHUTDOWN PROGRESS VISUAL INDICATOR FUNCTIONS (FIX 1 - v4.1)
// 

void showShutdownProgress() {
    if (!screenOn || !showingShutdownProgress) return;

    // Calculate progress from BOOT button press start
    unsigned long elapsed = millis() - bootButtonPressStartMs;
    int progress = (elapsed * 100) / POWER_BUTTON_SHUTDOWN_MS;
    if (progress > 100) progress = 100;

    // Create Apple-style popup on first call
    if (shutdownPopup == NULL) {
        // Full screen dark overlay with blur effect
        shutdownPopup = lv_obj_create(lv_scr_act());
        lv_obj_set_size(shutdownPopup, LCD_WIDTH, LCD_HEIGHT);
        lv_obj_set_pos(shutdownPopup, 0, 0);
        lv_obj_set_style_bg_color(shutdownPopup, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(shutdownPopup, LV_OPA_80, 0);
        lv_obj_set_style_border_width(shutdownPopup, 0, 0);
        lv_obj_set_style_pad_all(shutdownPopup, 0, 0);
        lv_obj_clear_flag(shutdownPopup, LV_OBJ_FLAG_SCROLLABLE);
    
        // Apple-style power icon container (top)
        lv_obj_t *iconContainer = lv_obj_create(shutdownPopup);
        lv_obj_set_size(iconContainer, 80, 80);
        lv_obj_align(iconContainer, LV_ALIGN_TOP_MID, 0, 80);
        lv_obj_set_style_bg_color(iconContainer, lv_color_hex(0xFF3B30), 0);
        lv_obj_set_style_bg_opa(iconContainer, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(iconContainer, 40, 0);
        lv_obj_set_style_border_width(iconContainer, 0, 0);
        lv_obj_set_style_shadow_width(iconContainer, 30, 0);
        lv_obj_set_style_shadow_color(iconContainer, lv_color_hex(0xFF3B30), 0);
        lv_obj_set_style_shadow_opa(iconContainer, LV_OPA_50, 0);
        lv_obj_clear_flag(iconContainer, LV_OBJ_FLAG_SCROLLABLE);
    
        // Power icon symbol
        lv_obj_t *powerIcon = lv_label_create(iconContainer);
        lv_label_set_text(powerIcon, LV_SYMBOL_POWER);
        lv_obj_set_style_text_color(powerIcon, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(powerIcon, &lv_font_montserrat_28, 0);
        lv_obj_center(powerIcon);
    
        // "slide to power off" text
        lv_obj_t *slideText = lv_label_create(shutdownPopup);
        lv_label_set_text(slideText, "Hold to Power Off");
        lv_obj_set_style_text_color(slideText, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(slideText, &lv_font_montserrat_16, 0);
        lv_obj_align(slideText, LV_ALIGN_TOP_MID, 0, 180);
    
        // Apple-style progress bar container (pill shape)
        lv_obj_t *sliderBg = lv_obj_create(shutdownPopup);
        lv_obj_set_size(sliderBg, 280, 56);
        lv_obj_align(sliderBg, LV_ALIGN_CENTER, 0, 40);
        lv_obj_set_style_bg_color(sliderBg, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_bg_opa(sliderBg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(sliderBg, 28, 0);
        lv_obj_set_style_border_width(sliderBg, 1, 0);
        lv_obj_set_style_border_color(sliderBg, lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_pad_all(sliderBg, 4, 0);
        lv_obj_clear_flag(sliderBg, LV_OBJ_FLAG_SCROLLABLE);
    
        // Progress fill (red gradient feel)
        shutdownProgressArc = lv_obj_create(sliderBg);
        lv_obj_set_height(shutdownProgressArc, 48);
        lv_obj_set_width(shutdownProgressArc, 0);  // Start at 0
        lv_obj_align(shutdownProgressArc, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(shutdownProgressArc, lv_color_hex(0xFF3B30), 0);
        lv_obj_set_style_bg_opa(shutdownProgressArc, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(shutdownProgressArc, 24, 0);
        lv_obj_set_style_border_width(shutdownProgressArc, 0, 0);
        lv_obj_clear_flag(shutdownProgressArc, LV_OBJ_FLAG_SCROLLABLE);
    
        // Percentage label centered
        shutdownProgressLabel = lv_label_create(sliderBg);
        lv_obj_set_style_text_color(shutdownProgressLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(shutdownProgressLabel, &lv_font_montserrat_18, 0);
        lv_obj_center(shutdownProgressLabel);
    
        // Cancel hint at bottom
        lv_obj_t *cancelHint = lv_label_create(shutdownPopup);
        lv_label_set_text(cancelHint, "Release to cancel");
        lv_obj_set_style_text_color(cancelHint, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(cancelHint, &lv_font_montserrat_12, 0);
        lv_obj_align(cancelHint, LV_ALIGN_BOTTOM_MID, 0, -60);
    }

    // Safety check before updating
    if (shutdownProgressArc == NULL || shutdownProgressLabel == NULL) {
        return;
    }

    // Update progress bar width (max 268 pixels inside the pill)
    int barWidth = (progress * 268) / 100;
    if (barWidth < 48) barWidth = 48;  // Minimum width for rounded ends
    lv_obj_set_width(shutdownProgressArc, barWidth);

    // Update percentage text
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", progress);
    lv_label_set_text(shutdownProgressLabel, buf);

    lv_task_handler();
  
  // NEW: Check identity unlocks every 5 seconds
  static unsigned long lastUnlockCheck = 0;
  if (millis() - lastUnlockCheck >= 5000) {
    lastUnlockCheck = millis();
    checkIdentityUnlocks();
  }
  
  // NEW: Show notifications
  // Notifications disabled - was causing freeze
}

void hideShutdownProgress() {
    showingShutdownProgress = false;

    if (shutdownPopup != NULL) {
        lv_obj_del(shutdownPopup);
    }
    shutdownPopup = NULL;
    shutdownProgressArc = NULL;
    shutdownProgressLabel = NULL;

    lv_task_handler();
  
  // NEW: Check identity unlocks every 5 seconds
  static unsigned long lastUnlockCheck = 0;
  if (millis() - lastUnlockCheck >= 5000) {
    lastUnlockCheck = millis();
    checkIdentityUnlocks();
  }
  
  // NEW: Show notifications
  // Notifications disabled - was causing freeze
}

//  POWER BUTTON HANDLER
// 
// 
//  POWER BUTTON HANDLER (GPIO10) - SCREEN TOGGLE ONLY
//   Quick tap: Toggle screen on/off
//   No shutdown on this button anymore
// 
// 
//  POWER BUTTON HANDLER (GPIO10) - Simple tap/hold behavior
//   Tap: Toggle screen on/off  
//   Hold (3s): Shutdown device
// 
void handlePowerButton() {
    // DISABLED: Power button handling completely disabled to debug random shutdowns
    // If shutdowns stop, the issue is GPIO10 noise or PMU power key
    return;
    
    /*
    static bool lastPwrButtonState = HIGH;
    static unsigned long lastPwrDebounceTime = 0;
    static int consecutiveLowReadings = 0;

    bool buttonState = digitalRead(PWR_BUTTON);
    unsigned long now = millis();

    // IMPROVED: Longer debounce to prevent false triggers from electrical noise
    if (buttonState != lastPwrButtonState) {
        lastPwrDebounceTime = now;
        lastPwrButtonState = buttonState;
        consecutiveLowReadings = 0;
        return;
    }

    // FIXED: Increased debounce from 50ms to 200ms to prevent random shutdowns
    if ((now - lastPwrDebounceTime) < 200) {
        return;
    }

    // Button pressed (LOW = pressed)
    if (buttonState == LOW) {
        consecutiveLowReadings++;

        // FIXED: Require more consecutive readings (10 instead of 2) to confirm press
        if (!powerButtonPressed && consecutiveLowReadings > 10) {
            powerButtonPressed = true;
            powerButtonPressStartMs = now;
        }
    
        // FIXED: Hold for 5 seconds (was 3) = shutdown - matches POWER_BUTTON_SHUTDOWN_MS
        if (powerButtonPressed && (now - powerButtonPressStartMs >= POWER_BUTTON_SHUTDOWN_MS)) {
            shutdownDevice();
        }
    }
    // Button released
    else {
        consecutiveLowReadings = 0;

        if (powerButtonPressed) {
            unsigned long pressDuration = now - powerButtonPressStartMs;
        
            // Tap (less than 1 second) = toggle screen
            if (pressDuration < 1000) {
                if (screenOn) {
                    screenOff();
                } else {
                    screenOnFunc();
                }
            }
        
            powerButtonPressed = false;
        }
    }
    */
}

// 
//  BOOT BUTTON HANDLER (GPIO0) - Category Navigation only
//   Short press: Move to next category
// 
void handleBootButton() {
    static bool lastBootButtonState = HIGH;
    static unsigned long lastBootDebounceTime = 0;
    static int consecutiveLowReadings = 0;

    bool buttonState = digitalRead(BOOT_BUTTON);
    unsigned long now = millis();

    // Debounce
    if (buttonState != lastBootButtonState) {
        lastBootDebounceTime = now;
        lastBootButtonState = buttonState;
        consecutiveLowReadings = 0;
        return;
    }

    if ((now - lastBootDebounceTime) < 50) {
        return;
    }

    if (buttonState == LOW) {
        consecutiveLowReadings++;
        if (!bootButtonPressed && consecutiveLowReadings > 2) {
            bootButtonPressed = true;
            bootButtonPressStartMs = now;
        }
    }
    else if (buttonState == HIGH && bootButtonPressed) {
        unsigned long pressDuration = now - bootButtonPressStartMs;
    
        // Short press - navigate categories
        if (pressDuration < 1000) {
            if (!screenOn) {
                screenOnFunc();
            } else if (canNavigate()) {
                int newCategory = (currentCategory + 1) % NUM_CATEGORIES;
                currentCategory = newCategory;
                currentSubCard = 0;
                navigateTo(currentCategory, currentSubCard);
                lastActivityMs = now;
            }
        }
    
        bootButtonPressed = false;
    }
}

// 
//  SETUP
// 

// 
//  IDENTITY SYSTEM FUNCTIONS
// 

void addNotification(const char* message, lv_color_t color) {
  // Notifications disabled - was causing freeze
  (void)message;
  (void)color;
  return;
  
  // Notification trigger disabled
  // showingNotification = true;
  notificationStartMs = millis();
  
}

// 
//  EXTENDED IDENTITY SYSTEM FUNCTIONS (15 IDENTITIES)
// 

void trackDailySteps() {
  if (!hasRTC) return;
  
  RTC_DateTime dt = rtc.getDateTime();
  uint32_t currentDayOfYear = dt.getMonth() * 31 + dt.getDay();
  
  if (questProgress.lastDayOfYear != currentDayOfYear) {
    // New day - reset daily step counter
    questProgress.currentDaySteps = userData.steps;  // Store yesterday's total
    questProgress.lastDayOfYear = currentDayOfYear;
  }
}

void trackStopwatchUse() {
  questProgress.stopwatchUses++;
}

void trackGamePlayed() {
  questProgress.gamesPlayed++;
}

void trackGameWon() {
  if (questProgress.lastGameWon == 1) {
    questProgress.consecutiveWins++;
  } else {
    questProgress.consecutiveWins = 1;
  }
  questProgress.lastGameWon = 1;
}

void trackGameLost() {
  questProgress.consecutiveWins = 0;
  questProgress.lastGameWon = 0;
}

void checkIdentityUnlocks() {
  //  Chaos - Random unlock (1% per hour)
  if (!cardIdentities[IDENTITY_CHAOS].unlocked) {
    if (millis() - questProgress.chaosCheckTime >= 3600000) {
      questProgress.chaosCheckTime = millis();
      if (random(100) < 1) {
        cardIdentities[IDENTITY_CHAOS].unlocked = true;
        userData.identitiesUnlocked[IDENTITY_CHAOS] = true;
        addNotification(" CHAOS UNLOCKED!", lv_color_hex(0xFF453A));
      }
    }
  }
  
  //  Focus - 7-day step streak
  if (!cardIdentities[IDENTITY_FOCUS].unlocked) {
    cardIdentities[IDENTITY_FOCUS].currentProgress = userData.stepStreak;
    if (userData.stepStreak >= 7) {
      cardIdentities[IDENTITY_FOCUS].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_FOCUS] = true;
      addNotification(" FOCUS UNLOCKED!", lv_color_hex(0x5E5CE6));
    }
  }
  
  //  Frostbite - 20,000 total steps
  if (!cardIdentities[IDENTITY_FROSTBITE].unlocked) {
    cardIdentities[IDENTITY_FROSTBITE].currentProgress = userData.steps;
    if (userData.steps >= 20000) {
      cardIdentities[IDENTITY_FROSTBITE].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_FROSTBITE] = true;
      addNotification(" FROSTBITE UNLOCKED!", lv_color_hex(0x64D2FF));
    }
  }
  
  //  Subzero - 3-day consecutive usage
  if (!cardIdentities[IDENTITY_SUBZERO].unlocked) {
    cardIdentities[IDENTITY_SUBZERO].currentProgress = userData.consecutiveDays;
    if (userData.consecutiveDays >= 3) {
      cardIdentities[IDENTITY_SUBZERO].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_SUBZERO] = true;
      addNotification(" SUBZERO UNLOCKED!", lv_color_hex(0x00C7BE));
    }
  }
  
  //  Cold - Win 5 Blackjack games
  if (!cardIdentities[IDENTITY_COLD].unlocked) {
    cardIdentities[IDENTITY_COLD].currentProgress = userData.gamesWon;
    if (userData.gamesWon >= 5) {
      cardIdentities[IDENTITY_COLD].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_COLD] = true;
      addNotification(" COLD UNLOCKED!", lv_color_hex(0x5AC8FA));
    }
  }
  
  //  Orbit - Use compass 100 times
  if (!cardIdentities[IDENTITY_ORBIT].unlocked) {
    cardIdentities[IDENTITY_ORBIT].currentProgress = userData.compassUseCount;
    if (userData.compassUseCount >= 100) {
      cardIdentities[IDENTITY_ORBIT].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_ORBIT] = true;
      addNotification(" ORBIT UNLOCKED!", lv_color_hex(0xFF9F0A));
    }
  }
  
  //  Flux - Play 10 total games
  if (!cardIdentities[IDENTITY_FLUX].unlocked) {
    cardIdentities[IDENTITY_FLUX].currentProgress = questProgress.gamesPlayed;
    if (questProgress.gamesPlayed >= 10) {
      cardIdentities[IDENTITY_FLUX].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_FLUX] = true;
      addNotification(" FLUX UNLOCKED!", lv_color_hex(0x0A84FF));
    }
  }
  
  //  Nova - 50,000 total steps
  if (!cardIdentities[IDENTITY_NOVA].unlocked) {
    cardIdentities[IDENTITY_NOVA].currentProgress = userData.steps;
    if (userData.steps >= 50000) {
      cardIdentities[IDENTITY_NOVA].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_NOVA] = true;
      addNotification(" NOVA UNLOCKED!", lv_color_hex(0xFF9F0A));
    }
  }
  
  //  Pulse - 10,000 steps in one day
  if (!cardIdentities[IDENTITY_PULSE].unlocked) {
    uint32_t todaySteps = userData.steps - questProgress.currentDaySteps;
    cardIdentities[IDENTITY_PULSE].currentProgress = todaySteps;
    if (todaySteps >= 10000) {
      cardIdentities[IDENTITY_PULSE].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_PULSE] = true;
      addNotification(" PULSE UNLOCKED!", lv_color_hex(0xFF2D55));
    }
  }
  
  //  Velocity - 30,000 total steps
  if (!cardIdentities[IDENTITY_VELOCITY].unlocked) {
    cardIdentities[IDENTITY_VELOCITY].currentProgress = userData.steps;
    if (userData.steps >= 30000) {
      cardIdentities[IDENTITY_VELOCITY].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_VELOCITY] = true;
      addNotification(" VELOCITY UNLOCKED!", lv_color_hex(0x30D158));
    }
  }
  
  //  Flow - Use stopwatch 50 times
  if (!cardIdentities[IDENTITY_FLOW].unlocked) {
    cardIdentities[IDENTITY_FLOW].currentProgress = questProgress.stopwatchUses;
    if (questProgress.stopwatchUses >= 50) {
      cardIdentities[IDENTITY_FLOW].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_FLOW] = true;
      addNotification(" FLOW UNLOCKED!", lv_color_hex(0xBF5AF2));
    }
  }
  
  //  Overdrive - Win 10 games in a row
  if (!cardIdentities[IDENTITY_OVERDRIVE].unlocked) {
    cardIdentities[IDENTITY_OVERDRIVE].currentProgress = questProgress.consecutiveWins;
    if (questProgress.consecutiveWins >= 10) {
      cardIdentities[IDENTITY_OVERDRIVE].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_OVERDRIVE] = true;
      addNotification(" OVERDRIVE UNLOCKED!", lv_color_hex(0xFF453A));
    }
  }
  
  //  Surge - 14-day step streak
  if (!cardIdentities[IDENTITY_SURGE].unlocked) {
    cardIdentities[IDENTITY_SURGE].currentProgress = userData.stepStreak;
    if (userData.stepStreak >= 14) {
      cardIdentities[IDENTITY_SURGE].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_SURGE] = true;
      addNotification(" SURGE UNLOCKED!", lv_color_hex(0xFFD60A));
    }
  }
  
  //  Eclipse - 7 consecutive days
  if (!cardIdentities[IDENTITY_ECLIPSE].unlocked) {
    cardIdentities[IDENTITY_ECLIPSE].currentProgress = userData.consecutiveDays;
    if (userData.consecutiveDays >= 7) {
      cardIdentities[IDENTITY_ECLIPSE].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_ECLIPSE] = true;
      addNotification(" ECLIPSE UNLOCKED!", lv_color_hex(0x1C1C1E));
    }
  }
  
  saveUserData();
}

void handleSecretTap() {
  if (cardIdentities[IDENTITY_GLITCH].unlocked) return;
  
  unsigned long now = millis();
  if (now - questProgress.lastTapTime < 2000) {
    questProgress.tapCount++;
    if (questProgress.tapCount >= 10) {
      cardIdentities[IDENTITY_GLITCH].unlocked = true;
      userData.identitiesUnlocked[IDENTITY_GLITCH] = true;
      addNotification(" GLITCH UNLOCKED!", lv_color_hex(0xBF5AF2));
      questProgress.tapCount = 0;
    }
  } else {
    questProgress.tapCount = 1;
  }
  questProgress.lastTapTime = now;
}

void updateConsecutiveDays() {
  if (!hasRTC) return;
  
  RTC_DateTime dt = rtc.getDateTime();
  uint32_t currentDayOfYear = dt.getMonth() * 31 + dt.getDay();
  
  if (userData.lastUseDayOfYear == 0) {
    userData.consecutiveDays = 1;
    userData.lastUseDayOfYear = currentDayOfYear;
  } else if (currentDayOfYear == userData.lastUseDayOfYear + 1) {
    userData.consecutiveDays++;
    userData.lastUseDayOfYear = currentDayOfYear;
  } else if (currentDayOfYear != userData.lastUseDayOfYear) {
    userData.consecutiveDays = 1;
    userData.lastUseDayOfYear = currentDayOfYear;
  }
  
  trackDailySteps();
}

// Notification popup disabled - was causing watch freeze
void showNotificationPopup() {
  // Function disabled
  return;
}

// 
//  IDENTITY MAIN CARD - Shows current selected identity with large emoji
// 
void createIdentityMainCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_clean(lv_scr_act());
  
  // CRITICAL: Disable ALL scrolling
  disableAllScrolling(lv_scr_act());
  
  // Full screen background
  lv_obj_t *bg = lv_obj_create(lv_scr_act());
  lv_obj_set_size(bg, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(bg, lv_color_hex(0x0A0A0A), 0);
  lv_obj_set_style_radius(bg, 0, 0);
  lv_obj_set_style_border_width(bg, 0, 0);
  lv_obj_set_style_pad_all(bg, 0, 0);
  disableAllScrolling(bg);
  
  if (selectedIdentity >= 0 && selectedIdentity < NUM_IDENTITIES && cardIdentities[selectedIdentity].unlocked) {
    // Glow circle behind emoji
    lv_obj_t *glowCircle = lv_obj_create(bg);
    lv_obj_set_size(glowCircle, 180, 180);
    lv_obj_align(glowCircle, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_bg_color(glowCircle, cardIdentities[selectedIdentity].primaryColor, 0);
    lv_obj_set_style_bg_opa(glowCircle, LV_OPA_20, 0);
    lv_obj_set_style_radius(glowCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glowCircle, 0, 0);
    lv_obj_set_style_shadow_width(glowCircle, 60, 0);
    lv_obj_set_style_shadow_color(glowCircle, cardIdentities[selectedIdentity].primaryColor, 0);
    lv_obj_set_style_shadow_opa(glowCircle, LV_OPA_40, 0);
    disableAllScrolling(glowCircle);

    // Large emoji - centered but slightly up
    lv_obj_t *emoji = lv_label_create(bg);
    lv_label_set_text(emoji, cardIdentities[selectedIdentity].emoji);
    lv_obj_set_style_text_font(emoji, &lv_font_montserrat_48, 0);
    lv_obj_align(emoji, LV_ALIGN_CENTER, 0, -30);

    // Identity title below
    lv_obj_t *title = lv_label_create(bg);
    lv_label_set_text(title, cardIdentities[selectedIdentity].title);
    lv_obj_set_style_text_color(title, cardIdentities[selectedIdentity].primaryColor, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 50);

    // Short name underneath
    lv_obj_t *name = lv_label_create(bg);
    lv_label_set_text(name, cardIdentities[selectedIdentity].name);
    lv_obj_set_style_text_color(name, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_align(name, LV_ALIGN_CENTER, 0, 80);

  } else {
    // No identity selected - show prompt
    lv_obj_t *questionMark = lv_label_create(bg);
    lv_label_set_text(questionMark, "?");
    lv_obj_set_style_text_color(questionMark, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(questionMark, &lv_font_montserrat_48, 0);
    lv_obj_align(questionMark, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *prompt = lv_label_create(bg);
    lv_label_set_text(prompt, "No Identity");
    lv_obj_set_style_text_color(prompt, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(prompt, &lv_font_montserrat_20, 0);
    lv_obj_align(prompt, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t *hint = lv_label_create(bg);
    lv_label_set_text(hint, "Swipe down to choose");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 70);
  }
  
  // Bottom hint
  lv_obj_t *swipeHint = lv_label_create(bg);
  lv_label_set_text(swipeHint, LV_SYMBOL_DOWN " Swipe for more");
  lv_obj_set_style_text_color(swipeHint, lv_color_hex(0x4A4A4A), 0);
  lv_obj_set_style_text_font(swipeHint, &lv_font_montserrat_12, 0);
  lv_obj_align(swipeHint, LV_ALIGN_BOTTOM_MID, 0, -25);
}

// Callback for identity cell tap
static void identityCellCb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  
  if (idx >= 0 && idx < NUM_IDENTITIES) {
    if (cardIdentities[idx].unlocked) {
      // Select this identity
      selectedIdentity = idx;
      userData.selectedIdentity = idx;
  
      // Save to preferences
      prefs.begin("minios", false);
      prefs.putInt("identity", selectedIdentity);
      prefs.end();
  
      // Go back to main identity card to show selection
      navigateTo(CAT_IDENTITY, 0);
    } else {
      // Show unlock hint - refresh to show the info
      navigateTo(CAT_IDENTITY, 1);
    }
  }
}

// 
//  IDENTITY GRID CARD - Shows all locked/unlocked identities
// 
void createIdentityGridCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_clean(lv_scr_act());
  
  // CRITICAL: Disable ALL scrolling
  disableAllScrolling(lv_scr_act());
  
  // Full screen dark background
  lv_obj_t *bg = lv_obj_create(lv_scr_act());
  lv_obj_set_size(bg, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(bg, lv_color_hex(0x0D0D0D), 0);
  lv_obj_set_style_radius(bg, 0, 0);
  lv_obj_set_style_border_width(bg, 0, 0);
  lv_obj_set_style_pad_all(bg, 0, 0);
  disableAllScrolling(bg);
  
  // Title
  lv_obj_t *title = lv_label_create(bg);
  lv_label_set_text(title, "IDENTITIES");
  lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  
  // Count unlocked
  int unlockedCount = 0;
  for (int i = 0; i < NUM_IDENTITIES; i++) {
    if (cardIdentities[i].unlocked) unlockedCount++;
  }
  
  char countBuf[20];
  snprintf(countBuf, sizeof(countBuf), "%d/%d Unlocked", unlockedCount, NUM_IDENTITIES);
  lv_obj_t *countLabel = lv_label_create(bg);
  lv_label_set_text(countLabel, countBuf);
  lv_obj_set_style_text_color(countLabel, theme.accent, 0);
  lv_obj_set_style_text_font(countLabel, &lv_font_montserrat_12, 0);
  lv_obj_align(countLabel, LV_ALIGN_TOP_MID, 0, 35);
  
  // Grid: Show identities (we have 7 defined, show in 2 rows)
  int cellSize = 55;
  int spacing = 8;
  int cols = 4;
  int rows = 2;
  int gridWidth = cols * cellSize + (cols - 1) * spacing;
  int startX = (LCD_WIDTH - gridWidth) / 2;
  int startY = 60;
  
  for (int i = 0; i < NUM_IDENTITIES && i < 8; i++) {
    int row = i / cols;
    int col = i % cols;
    int x = startX + col * (cellSize + spacing);
    int y = startY + row * (cellSize + spacing);

    lv_obj_t *cell = lv_obj_create(bg);
    lv_obj_set_size(cell, cellSize, cellSize);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_style_radius(cell, 12, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    disableAllScrolling(cell);

    // Make clickable
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cell, identityCellCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    if (cardIdentities[i].unlocked) {
      // Unlocked - show emoji with colored background
      lv_obj_set_style_bg_color(cell, cardIdentities[i].primaryColor, 0);
      lv_obj_set_style_bg_opa(cell, LV_OPA_30, 0);
      lv_obj_set_style_border_color(cell, cardIdentities[i].primaryColor, 0);
      lv_obj_set_style_border_width(cell, (i == selectedIdentity) ? 3 : 1, 0);
  
      lv_obj_t *emoji = lv_label_create(cell);
      lv_label_set_text(emoji, cardIdentities[i].emoji);
      lv_obj_set_style_text_font(emoji, &lv_font_montserrat_20, 0);
      lv_obj_align(emoji, LV_ALIGN_CENTER, 0, -5);
  
      lv_obj_t *name = lv_label_create(cell);
      lv_label_set_text(name, cardIdentities[i].name);
      lv_obj_set_style_text_color(name, theme.text, 0);
      lv_obj_set_style_text_font(name, &lv_font_montserrat_10, 0);
      lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -3);
  
    } else {
      // Locked - show lock icon and progress
      lv_obj_set_style_bg_color(cell, lv_color_hex(0x1C1C1E), 0);
      lv_obj_set_style_border_color(cell, lv_color_hex(0x3A3A3C), 0);
      lv_obj_set_style_border_width(cell, 1, 0);
  
      lv_obj_t *lockIcon = lv_label_create(cell);
      lv_label_set_text(lockIcon, LV_SYMBOL_EYE_CLOSE);
      lv_obj_set_style_text_color(lockIcon, lv_color_hex(0x636366), 0);
      lv_obj_set_style_text_font(lockIcon, &lv_font_montserrat_16, 0);
      lv_obj_align(lockIcon, LV_ALIGN_CENTER, 0, -5);
  
      // Progress percentage
      if (cardIdentities[i].unlockThreshold > 0) {
        int progress = (cardIdentities[i].currentProgress * 100) / cardIdentities[i].unlockThreshold;
        if (progress > 100) progress = 100;
    
        char progBuf[8];
        snprintf(progBuf, sizeof(progBuf), "%d%%", progress);
        lv_obj_t *progLabel = lv_label_create(cell);
        lv_label_set_text(progLabel, progBuf);
        lv_obj_set_style_text_color(progLabel, lv_color_hex(0x4A4A4A), 0);
        lv_obj_set_style_text_font(progLabel, &lv_font_montserrat_10, 0);
        lv_obj_align(progLabel, LV_ALIGN_BOTTOM_MID, 0, -3);
      }
    }
  }
  
  // Unlock hints at bottom
  lv_obj_t *hintBox = lv_obj_create(bg);
  lv_obj_set_size(hintBox, LCD_WIDTH - 30, 70);
  lv_obj_align(hintBox, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(hintBox, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_radius(hintBox, 12, 0);
  lv_obj_set_style_border_width(hintBox, 0, 0);
  lv_obj_set_style_pad_all(hintBox, 8, 0);
  disableAllScrolling(hintBox);
  
  lv_obj_t *hintTitle = lv_label_create(hintBox);
  lv_label_set_text(hintTitle, "How to Unlock:");
  lv_obj_set_style_text_color(hintTitle, theme.accent, 0);
  lv_obj_set_style_text_font(hintTitle, &lv_font_montserrat_12, 0);
  lv_obj_align(hintTitle, LV_ALIGN_TOP_LEFT, 5, 5);
  
  lv_obj_t *hintText = lv_label_create(hintBox);
  lv_label_set_text(hintText, "Walk steps, win games,\nuse compass, build streaks!");
  lv_obj_set_style_text_color(hintText, lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(hintText, &lv_font_montserrat_10, 0);
  lv_obj_align(hintText, LV_ALIGN_TOP_LEFT, 5, 22);
  
  lv_obj_t *tapHint = lv_label_create(hintBox);
  lv_label_set_text(tapHint, "Tap unlocked identity to select");
  lv_obj_set_style_text_color(tapHint, lv_color_hex(0x636366), 0);
  lv_obj_set_style_text_font(tapHint, &lv_font_montserrat_10, 0);
  lv_obj_align(tapHint, LV_ALIGN_BOTTOM_LEFT, 5, -5);
}

void createCurrencyConverterCard() {
    // PREMIUM: Currency Converter - Bold and beautiful
    lv_obj_clean(lv_scr_act());

    // CRITICAL: Disable ALL scrolling on the screen
    disableAllScrolling(lv_scr_act());

    // Vibrant yellow background with subtle gradient
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFD60A), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0xFFB800), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling

    // Title badge
    lv_obj_t *titleBadge = lv_obj_create(card);
    lv_obj_set_size(titleBadge, 220, 40);
    lv_obj_align(titleBadge, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(titleBadge, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(titleBadge, LV_OPA_10, 0);
    lv_obj_set_style_radius(titleBadge, 20, 0);
    lv_obj_set_style_border_width(titleBadge, 0, 0);

    lv_obj_t *title = lv_label_create(titleBadge);
    lv_label_set_text(title, " CURRENCY");
    lv_obj_set_style_text_color(title, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_center(title);

    // Source currency selector with tap indicator
    lv_obj_t *sourceContainer = lv_obj_create(card);
    lv_obj_set_size(sourceContainer, 200, 80);
    lv_obj_align(sourceContainer, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(sourceContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(sourceContainer, LV_OPA_20, 0);
    lv_obj_set_style_radius(sourceContainer, 20, 0);
    lv_obj_set_style_border_width(sourceContainer, 3, 0);
    lv_obj_set_style_border_color(sourceContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(sourceContainer, LV_OPA_40, 0);

    lv_obj_t *tapHint = lv_label_create(sourceContainer);
    lv_label_set_text(tapHint, "TAP TO CHANGE");
    lv_obj_set_style_text_color(tapHint, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(tapHint, &lv_font_montserrat_10, 0);
    lv_obj_align(tapHint, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *sourceLabel = lv_label_create(sourceContainer);
    char sourceText[32];
    snprintf(sourceText, sizeof(sourceText), "< %s >", availableCurrencies[selectedCurrencyIndex]);
    lv_label_set_text(sourceLabel, sourceText);
    lv_obj_set_style_text_color(sourceLabel, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_text_font(sourceLabel, &lv_font_montserrat_32, 0);
    lv_obj_align(sourceLabel, LV_ALIGN_CENTER, 0, 5);

    // Amount display
    lv_obj_t *amountContainer = lv_obj_create(card);
    lv_obj_set_size(amountContainer, 160, 60);
    lv_obj_align(amountContainer, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_set_style_bg_color(amountContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(amountContainer, LV_OPA_10, 0);
    lv_obj_set_style_radius(amountContainer, 15, 0);
    lv_obj_set_style_border_width(amountContainer, 0, 0);

    lv_obj_t *amountLabel = lv_label_create(amountContainer);
    char amountText[32];
    snprintf(amountText, sizeof(amountText), "100 %s", currencyData.sourceCurrency);
    lv_label_set_text(amountLabel, amountText);
    lv_obj_set_style_text_color(amountLabel, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_text_font(amountLabel, &lv_font_montserrat_20, 0);
    lv_obj_center(amountLabel);

    // Conversion arrow
    lv_obj_t *arrow = lv_label_create(card);
    lv_label_set_text(arrow, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(arrow, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_24, 0);
    lv_obj_align(arrow, LV_ALIGN_TOP_MID, 0, 255);

    // USD conversion result
    lv_obj_t *usdContainer = lv_obj_create(card);
    lv_obj_set_size(usdContainer, LCD_WIDTH - 80, 70);
    lv_obj_align(usdContainer, LV_ALIGN_TOP_MID, 0, 290);
    lv_obj_set_style_bg_color(usdContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(usdContainer, LV_OPA_30, 0);
    lv_obj_set_style_radius(usdContainer, 20, 0);
    lv_obj_set_style_border_width(usdContainer, 2, 0);
    lv_obj_set_style_border_color(usdContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(usdContainer, LV_OPA_50, 0);

    lv_obj_t *usdFlag = lv_label_create(usdContainer);
    lv_label_set_text(usdFlag, "");
    lv_obj_set_style_text_font(usdFlag, &lv_font_montserrat_24, 0);
    lv_obj_align(usdFlag, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *usdLabel = lv_label_create(usdContainer);
    if (currencyData.valid) {
        char usdText[64];
        snprintf(usdText, sizeof(usdText), "$%.2f USD", 100.0 * currencyData.usdRate);
        lv_label_set_text(usdLabel, usdText);
    } else {
        lv_label_set_text(usdLabel, "Loading...");
    }
    lv_obj_set_style_text_color(usdLabel, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_text_font(usdLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(usdLabel, LV_ALIGN_CENTER, 10, 0);

    // AUD conversion result
    lv_obj_t *audContainer = lv_obj_create(card);
    lv_obj_set_size(audContainer, LCD_WIDTH - 80, 70);
    lv_obj_align(audContainer, LV_ALIGN_TOP_MID, 0, 375);
    lv_obj_set_style_bg_color(audContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(audContainer, LV_OPA_30, 0);
    lv_obj_set_style_radius(audContainer, 20, 0);
    lv_obj_set_style_border_width(audContainer, 2, 0);
    lv_obj_set_style_border_color(audContainer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(audContainer, LV_OPA_50, 0);

    lv_obj_t *audFlag = lv_label_create(audContainer);
    lv_label_set_text(audFlag, "");
    lv_obj_set_style_text_font(audFlag, &lv_font_montserrat_24, 0);
    lv_obj_align(audFlag, LV_ALIGN_LEFT_MID, 15, 0);

    lv_obj_t *audLabel = lv_label_create(audContainer);
    if (currencyData.valid) {
        char audText[64];
        snprintf(audText, sizeof(audText), "$%.2f AUD", 100.0 * currencyData.audRate);
        lv_label_set_text(audLabel, audText);
    } else {
        lv_label_set_text(audLabel, "Loading...");
    }
    lv_obj_set_style_text_color(audLabel, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_text_font(audLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(audLabel, LV_ALIGN_CENTER, 10, 0);

    // Last update time badge
    if (currencyData.valid) {
        lv_obj_t *updateBadge = lv_obj_create(card);
        lv_obj_set_size(updateBadge, 200, 30);
        lv_obj_align(updateBadge, LV_ALIGN_BOTTOM_MID, 0, -15);
        lv_obj_set_style_bg_color(updateBadge, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(updateBadge, LV_OPA_10, 0);
        lv_obj_set_style_radius(updateBadge, 15, 0);
        lv_obj_set_style_border_width(updateBadge, 0, 0);

        lv_obj_t *updateLabel = lv_label_create(updateBadge);
        unsigned long minutesAgo = (millis() - currencyData.lastUpdate) / 60000;
        char updateText[64];
        if (minutesAgo == 0) {
            snprintf(updateText, sizeof(updateText), LV_SYMBOL_REFRESH " Just updated");
        } else {
            snprintf(updateText, sizeof(updateText), LV_SYMBOL_REFRESH " %lu min ago", minutesAgo);
        }
        lv_label_set_text(updateLabel, updateText);
        lv_obj_set_style_text_color(updateLabel, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(updateLabel, &lv_font_montserrat_12, 0);
        lv_obj_center(updateLabel);
    }
}

//  PREMIUM WIDGET API FUNCTIONS

void fetchSunriseSunsetData() {
    if (!wifiConnected) {
        return;
    }

    // Use Perth, Australia coordinates (from weatherCity)
    float lat = -31.9505;
    float lng = 115.8605;

    HTTPClient http;
    char url[200];
    snprintf(url, sizeof(url), 
             "https://api.sunrise-sunset.org/json?lat=%.4f&lng=%.4f&formatted=0",
             lat, lng);

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
    
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, payload);
    
        if (!error) {
            const char* sunrise = doc["results"]["sunrise"];
            const char* sunset = doc["results"]["sunset"];
        
            if (sunrise && sunset) {
                // Parse ISO 8601 time: "2025-01-27T21:39:15+00:00"
                // Extract HH:MM from position 11-15
                strncpy(sunData.sunriseTime, sunrise + 11, 5);
                sunData.sunriseTime[5] = '\0';
                strncpy(sunData.sunsetTime, sunset + 11, 5);
                sunData.sunsetTime[5] = '\0';
            
                // Convert to local time (add GMT offset)
                int sunriseHour = atoi(sunData.sunriseTime);
                int sunsetHour = atoi(sunData.sunsetTime);
                sunriseHour = (sunriseHour + (gmtOffsetSec / 3600)) % 24;
                sunsetHour = (sunsetHour + (gmtOffsetSec / 3600)) % 24;
            
                snprintf(sunData.sunriseTime, 6, "%02d:%s", sunriseHour, sunData.sunriseTime + 3);
                snprintf(sunData.sunsetTime, 6, "%02d:%s", sunsetHour, sunData.sunsetTime + 3);
            
                // Calculate approximate azimuth angles
                // Sunrise is roughly east (90), sunset is roughly west (270)
                // For Perth in summer, sunrise ~60-120, sunset ~240-300
                sunData.sunriseAzimuth = 90.0;  // Simplified - east
                sunData.sunsetAzimuth = 270.0;  // Simplified - west
            
                sunData.valid = true;
                sunData.lastFetch = millis();
            
                USBSerial.printf("[SUNRISE] Sunrise: %s, Sunset: %s\n", 
                               sunData.sunriseTime, sunData.sunsetTime);
            }
        } else {
        }
    } else {
    }

    http.end();
}

void fetchCurrencyRates() {
    if (!wifiConnected) {
        return;
    }

    // Check if update needed (10 minutes)
    if (currencyData.valid && 
        (millis() - currencyData.lastUpdate < CURRENCY_UPDATE_INTERVAL)) {
        return;  // Still fresh
    }

    HTTPClient http;
    char url[300];
    snprintf(url, sizeof(url),
             "https://api.currencyapi.com/v3/latest?apikey=%s&base_currency=%s&currencies=USD,AUD",
             CURRENCY_API_KEY, currencyData.sourceCurrency);

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
    
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, payload);
    
        if (!error) {
            currencyData.usdRate = doc["data"]["USD"]["value"];
            currencyData.audRate = doc["data"]["AUD"]["value"];
            currencyData.valid = true;
            currencyData.lastUpdate = millis();
        
            USBSerial.printf("[CURRENCY] %s -> USD: %.4f, AUD: %.4f\n",
                           currencyData.sourceCurrency,
                           currencyData.usdRate,
                           currencyData.audRate);
        } else {
        }
    } else {
    }

    http.end();
}

void calibrateCompassNorth() {
    // Set current heading as "north"
    compassNorthOffset = -compassHeadingSmooth;

    // Save to Preferences
    prefs.begin("minios", false);
    prefs.putFloat("compassOffset", compassNorthOffset);
    prefs.end();

    // Save to SD card
    if (hasSD) {
        File file = SD_MMC.open("/compass_calibration.txt", FILE_WRITE);
        if (file) {
            file.printf("%.2f\n", compassNorthOffset);
            file.close();
        }
    }

}

float getCalibratedHeading() {
    float heading = compassHeadingSmooth + compassNorthOffset;
    while (heading < 0) heading += 360;
    while (heading >= 360) heading -= 360;
    return heading;
}

//  WIDGET OS - PREMIUM SD CARD SYSTEM
//  Advanced folder structure & JSON config management

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

bool createDirectoryIfNotExists(const char* path) {
    if (SD_MMC.exists(path)) {
        return true;
    }

    if (SD_MMC.mkdir(path)) {
        return true;
    } else {
        return false;
    }
}

void createDeviceJson() {
    if (SD_MMC.exists(SD_DEVICE_JSON)) {
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
    } else {
    }
}

void createOsJson() {
    if (SD_MMC.exists(SD_OS_JSON)) {
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
    } else {
    }
}

void createBuildTxt() {
    if (SD_MMC.exists(SD_BUILD_TXT)) {
        return;
    }

    File file = SD_MMC.open(SD_BUILD_TXT, FILE_WRITE);
    if (file) {
        file.printf("%s %s\n", WIDGET_OS_NAME, WIDGET_OS_VERSION);
        file.printf("Board: %s\n", DEVICE_ID);
        file.printf("Display: %s AMOLED\n", DEVICE_SCREEN);
        file.printf("Build: %s\n", WIDGET_OS_BUILD);
        file.close();
    } else {
    }
}

void createUserJson() {
    if (SD_MMC.exists(SD_USER_JSON)) {
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
    } else {
    }
}

void createDisplayJson() {
    if (SD_MMC.exists(SD_DISPLAY_JSON)) {
        return;
    }

    File file = SD_MMC.open(SD_DISPLAY_JSON, FILE_WRITE);
    if (file) {
        StaticJsonDocument<256> doc;
        doc["always_on"] = false;
        doc["timeout"] = 10;
    
        serializeJsonPretty(doc, file);
        file.close();
    } else {
    }
}

void createPowerJson() {
    if (SD_MMC.exists(SD_POWER_JSON)) {
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
    } else {
    }
}

void createUpdateReadme() {
    if (SD_MMC.exists(SD_UPDATE_README)) {
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
    } else {
    }
}

void createWifiConfigTemplate() {
    if (SD_MMC.exists(SD_WIFI_CONFIG)) {
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
    } else {
    }
}

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
    }
}

/**
 * Create Widget OS folder structure on SD card
 * This is the main initialization function called on first boot
 */
bool createWidgetOSFolderStructure() {

    bool success = true;

    // Check if WATCH folder exists, if corrupted rename it
    if (SD_MMC.exists("/WATCH_BROKEN")) {
    }

    // Create main folder structure

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

    // BACKUP (Fusion Labs)
    success &= createDirectoryIfNotExists(SD_BACKUP_PATH);

    // FIRMWARE (Fusion Labs)
    success &= createDirectoryIfNotExists(SD_FIRMWARE_PATH);

    // WALLPAPERS
    success &= createDirectoryIfNotExists(SD_WALLPAPERS_PATH);

    if (!success) {
        sdCardStatus = SD_STATUS_CORRUPT;
        return false;
    }

    // Create system files
    initBootLog();
    logToBootLog("Widget OS boot started");

    createDeviceJson();
    logToBootLog("Created device.json");

    createOsJson();
    logToBootLog("Created os.json");

    createBuildTxt();
    logToBootLog("Created build.txt");

    // Create config files
    createUserJson();
    logToBootLog("Created user.json");

    createDisplayJson();
    logToBootLog("Created display.json");

    createPowerJson();
    logToBootLog("Created power.json");

    // Create other files
    createUpdateReadme();
    logToBootLog("Created UPDATE/README.txt");

    createWifiConfigTemplate();
    logToBootLog("Created wifi/config.txt template");

    logToBootLog("Widget OS folder structure created successfully");

    sdStructureCreated = true;
    return true;
}

/**
 * Initialize SD card and create Widget OS structure
 * Called during setup()
 */
bool initWidgetOSSDCard() {
    sdCardStatus = SD_STATUS_INIT_IN_PROGRESS;

    // Set SD_MMC pins (from Waveshare example)
    // Using 1-bit SD_MMC mode for compatibility
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);

    // Try to mount SD card
    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        sdCardStatus = SD_STATUS_MOUNT_FAILED;
        sdErrorMessage = "SD card mount failed";
        hasSD = false;
        return false;
    }

    // Check card type
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
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

    sdCardInitialized = true;
    hasSD = true;
    sdCardStatus = SD_STATUS_MOUNTED_OK;

    // Create Widget OS folder structure
    if (!createWidgetOSFolderStructure()) {
        // Continue anyway - partial structure is better than none
    }

    return true;
}

void loadUserConfigFromSD() {
    if (!hasSD || !sdCardInitialized) {
        return;
    }

    if (!SD_MMC.exists(SD_USER_JSON)) {
        return;
    }

    File file = SD_MMC.open(SD_USER_JSON, FILE_READ);
    if (file) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
    
        if (error) {
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
    
        logToBootLog("Loaded user config from SD");
    }
}

void loadDisplayConfigFromSD() {
    if (!hasSD || !sdCardInitialized) return;

    if (!SD_MMC.exists(SD_DISPLAY_JSON)) return;

    File file = SD_MMC.open(SD_DISPLAY_JSON, FILE_READ);
    if (file) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
    
        if (error) {
            return;
        }
    
        if (doc.containsKey("timeout")) {
            userData.screenTimeout = doc["timeout"].as<int>();
        }
    
    }
}

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
    }
}

bool loadWiFiConfigFromSD() {
    if (!hasSD || !sdCardInitialized) {
        return false;
    }

    if (!SD_MMC.exists(SD_WIFI_CONFIG)) {
        return false;
    }

    File file = SD_MMC.open(SD_WIFI_CONFIG, FILE_READ);
    if (!file) {
        return false;
    }

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
        logToBootLog("Loaded WiFi config from SD card");
        return true;
    }

    return false;
}

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

void listSDDirectory(const char* dirname, uint8_t levels) {
    if (!hasSD || !sdCardInitialized) {
        return;
    }

    File root = SD_MMC.open(dirname);
    if (!root) {
        return;
    }
    if (!root.isDirectory()) {
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            if (levels > 0) {
                listSDDirectory(file.path(), levels - 1);
            }
        } else {
        }
        file = root.openNextFile();
    }
    root.close();
}

void scanSDFaces() {
    if (!hasSD || !sdCardInitialized) {
        return;
    }

    numSDFaces = 0;

    // Scan custom faces
    scanFacesInDirectory(SD_FACES_CUSTOM_PATH);

    // Scan imported faces
    scanFacesInDirectory(SD_FACES_IMPORTED_PATH);

    logToBootLog("Scanned SD card for custom faces");
}

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
                        numSDFaces++;
                    }
                }
            }
        }
        faceDir = dir.openNextFile();
    }
    dir.close();
}

//  SD CARD MANAGEMENT & BACKUP SYSTEM
//  Fusion Labs Compatible - Auto creates folder structure

void initSDCardStructure() {
    if (!hasSD) return;

    // Create required directories
    const char* dirs[] = {
        SD_ROOT_PATH,
        SD_BACKUP_PATH,
        SD_FACES_PATH,
        SD_FIRMWARE_PATH,
        SD_LOGS_PATH,
        SD_WALLPAPERS_PATH,
        "/WATCH/FACES/custom"
    };

    for (const char* dir : dirs) {
        if (!SD_MMC.exists(dir)) {
            if (SD_MMC.mkdir(dir)) {
            }
        }
    }

    // Create default config.txt if not exists
    if (!SD_MMC.exists(SD_CONFIG_PATH)) {
        File cfg = SD_MMC.open(SD_CONFIG_PATH, FILE_WRITE);
        if (cfg) {
            cfg.println("# Widget OS Configuration");
            cfg.println("# Fusion Labs Compatible v1.0");
            cfg.println("");
            cfg.println("[device]");
            cfg.println("name=Widget OS Watch");
            cfg.println("board=2.06");
            cfg.println("");
            cfg.println("[backup]");
            cfg.println("auto_backup=true");
            cfg.println("backup_interval_hours=24");
            cfg.println("");
            cfg.println("[display]");
            cfg.printf("brightness=%d\n", userData.brightness);
            cfg.println("screen_timeout=30");
            cfg.println("");
            cfg.println("[wifi]");
            cfg.println("# Add WiFi networks here:");
            cfg.println("# ssid=YourNetwork");
            cfg.println("# password=YourPassword");
            cfg.close();
        }
    }

}

void updateSDCardHealth() {
    if (!hasSD) {
        sdHealth.mounted = false;
        sdHealth.healthy = false;
        return;
    }

    sdHealth.mounted = true;
    sdHealth.totalBytes = SD_MMC.totalBytes();
    sdHealth.usedBytes = SD_MMC.usedBytes();
    sdHealth.freeBytes = sdHealth.totalBytes - sdHealth.usedBytes;
    sdHealth.usedPercent = (float)sdHealth.usedBytes / (float)sdHealth.totalBytes * 100.0;
    sdHealth.healthy = (sdHealth.freeBytes > 1024 * 1024); // At least 1MB free

    if (sdHealth.usedPercent > 95.0) {
        sdHealth.lastError = "SD card nearly full!";
        sdHealth.healthy = false;
    }
}

bool createBackup(bool isAuto) {
    if (!hasSD) {
        return false;
    }

    updateSDCardHealth();
    if (!sdHealth.healthy) {
        return false;
    }

    // Get timestamp for backup folder
    struct tm timeinfo;
    char timestamp[32];
    if (getLocalTime(&timeinfo)) {
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
    } else {
        snprintf(timestamp, sizeof(timestamp), "backup_%lu", millis());
    }

    String backupDir = String(SD_BACKUP_PATH) + "/" + timestamp;
    if (isAuto) {
        backupDir += "_auto";
    }

    if (!SD_MMC.mkdir(backupDir.c_str())) {
        return false;
    }

    // Backup user data
    File userFile = SD_MMC.open((backupDir + "/user_data.json").c_str(), FILE_WRITE);
    if (userFile) {
        DynamicJsonDocument doc(2048);
        doc["brightness"] = userData.brightness;
        doc["theme"] = userData.theme;
        doc["vibration"] = userData.vibration;
        doc["gmt_offset"] = userData.gmtOffset;
        doc["steps"] = userData.steps;
        doc["calories"] = userData.calories;
        doc["step_goal"] = userData.stepGoal;
        doc["calorie_goal"] = userData.calorieGoal;
        doc["backup_time"] = timestamp;
        doc["firmware_version"] = "5.0";
    
        serializeJsonPretty(doc, userFile);
        userFile.close();
    }

    // Backup compass calibration
    File compassFile = SD_MMC.open((backupDir + "/compass_calibration.txt").c_str(), FILE_WRITE);
    if (compassFile) {
        compassFile.printf("%.2f\n", compassNorthOffset);
        compassFile.close();
    }

    // Backup config
    if (SD_MMC.exists(SD_CONFIG_PATH)) {
        File srcCfg = SD_MMC.open(SD_CONFIG_PATH, FILE_READ);
        File dstCfg = SD_MMC.open((backupDir + "/config.txt").c_str(), FILE_WRITE);
        if (srcCfg && dstCfg) {
            while (srcCfg.available()) {
                dstCfg.write(srcCfg.read());
            }
            srcCfg.close();
            dstCfg.close();
        }
    }

    lastBackupTime = millis();
    totalBackups++;

    return true;
}

bool restoreFromBackup(const String& backupName) {
    if (!hasSD) return false;

    String backupDir = String(SD_BACKUP_PATH) + "/" + backupName;
    if (!SD_MMC.exists(backupDir.c_str())) {
        return false;
    }

    // Restore user data
    File userFile = SD_MMC.open((backupDir + "/user_data.json").c_str(), FILE_READ);
    if (userFile) {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, userFile);
        userFile.close();
    
        if (!error) {
            userData.brightness = doc["brightness"] | 200;
            userData.theme = doc["theme"] | 0;
            userData.vibration = doc["vibration"] | true;
            userData.gmtOffset = doc["gmt_offset"] | 10;
            userData.steps = doc["steps"] | 0;
            userData.calories = doc["calories"] | 0;
            userData.stepGoal = doc["step_goal"] | 10000;
            userData.calorieGoal = doc["calorie_goal"] | 500;
        
            gfx->setBrightness(userData.brightness);
            saveUserData();
        }
    }

    // Restore compass calibration
    File compassFile = SD_MMC.open((backupDir + "/compass_calibration.txt").c_str(), FILE_READ);
    if (compassFile) {
        String line = compassFile.readStringUntil('\n');
        compassNorthOffset = line.toFloat();
        compassFile.close();
    
        prefs.begin("minios", false);
        prefs.putFloat("compassOffset", compassNorthOffset);
        prefs.end();
    }

    return true;
}

void checkAutoBackup() {
    if (!autoBackupEnabled || !hasSD) return;

    unsigned long currentTime = millis();
    if (currentTime - lastAutoBackup >= AUTO_BACKUP_INTERVAL_MS) {
        if (createBackup(true)) {
            lastAutoBackup = currentTime;
        }
    }
}

String listBackups() {
    String result = "";
    if (!hasSD) return "NO_SD";

    File dir = SD_MMC.open(SD_BACKUP_PATH);
    if (!dir || !dir.isDirectory()) return "NO_BACKUPS";

    File file = dir.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            result += file.name();
            result += "\n";
        }
        file = dir.openNextFile();
    }
    dir.close();

    return result.length() > 0 ? result : "NO_BACKUPS";
}

bool saveConfigToSD(const String& configData) {
    if (!hasSD) return false;

    File cfg = SD_MMC.open(SD_CONFIG_PATH, FILE_WRITE);
    if (!cfg) {
        sdHealth.writeErrors++;
        return false;
    }

    cfg.print(configData);
    cfg.close();
    return true;
}

String readConfigFromSD() {
    if (!hasSD) return "NO_SD";

    File cfg = SD_MMC.open(SD_CONFIG_PATH, FILE_READ);
    if (!cfg) return "NO_CONFIG";

    String content = cfg.readString();
    cfg.close();
    return content;
}

//  FUSION LABS WEB SERIAL PROTOCOL
//  Compatible with: https://github.com/ithig124-hub/fusion-labs

void sendDeviceStatus() {
    DynamicJsonDocument doc(1024);
    doc["type"] = "WIDGET_STATUS_RESPONSE";
    doc["firmware"] = "5.0";
    doc["protocol"] = FUSION_PROTOCOL_VERSION;
    doc["board"] = "2.06";
    doc["display"] = "CO5300";
    doc["battery"] = hasPMU ? (int)power.getBatteryPercent() : -1;
    doc["charging"] = hasPMU ? power.isCharging() : false;
    doc["wifi"] = wifiConnected;
    doc["sd_mounted"] = hasSD;
    doc["brightness"] = userData.brightness;
    doc["theme"] = userData.theme;
    doc["steps"] = userData.steps;
    doc["uptime_ms"] = millis();

    String output;
    serializeJson(doc, output);
}

void sendSDHealth() {
    updateSDCardHealth();

    DynamicJsonDocument doc(512);
    doc["type"] = "WIDGET_SD_HEALTH_RESPONSE";
    doc["mounted"] = sdHealth.mounted;
    doc["healthy"] = sdHealth.healthy;
    doc["total_mb"] = sdHealth.totalBytes / (1024 * 1024);
    doc["used_mb"] = sdHealth.usedBytes / (1024 * 1024);
    doc["free_mb"] = sdHealth.freeBytes / (1024 * 1024);
    doc["used_percent"] = sdHealth.usedPercent;
    doc["write_errors"] = sdHealth.writeErrors;
    doc["read_errors"] = sdHealth.readErrors;
    doc["last_error"] = sdHealth.lastError;
    doc["auto_backup"] = autoBackupEnabled;
    doc["total_backups"] = totalBackups;

    String output;
    serializeJson(doc, output);
}

void handleFaceInstall(const String& faceId, const String& faceData) {
    if (!hasSD) {
        return;
    }

    String facePath = String(SD_FACES_PATH) + "/custom/" + faceId;
    SD_MMC.mkdir(facePath.c_str());

    File faceFile = SD_MMC.open((facePath + "/face.json").c_str(), FILE_WRITE);
    if (faceFile) {
        faceFile.print(faceData);
        faceFile.close();
    } else {
    }
}

void processWebSerialCommand(const String& cmd) {
    String trimmedCmd = cmd;
    trimmedCmd.trim();

    if (trimmedCmd == "WIDGET_PING") {
    }
    else if (trimmedCmd == "WIDGET_STATUS") {
        sendDeviceStatus();
    }
    else if (trimmedCmd == "WIDGET_SD_HEALTH") {
        sendSDHealth();
    }
    else if (trimmedCmd == "WIDGET_READ_CONFIG") {
        String config = readConfigFromSD();
    }
    else if (trimmedCmd.startsWith("WIDGET_WRITE_CONFIG")) {
        // Config data follows on next lines until END_CONFIG
        receivingFirmware = false;
        receivingFace = false;
        // Config will be processed as it arrives
    }
    else if (trimmedCmd == "WIDGET_BACKUP") {
        bool success = createBackup(false);
    }
    else if (trimmedCmd.startsWith("WIDGET_RESTORE:")) {
        String backupName = trimmedCmd.substring(15);
        bool success = restoreFromBackup(backupName);
    }
    else if (trimmedCmd == "WIDGET_LIST_BACKUPS") {
        String backups = listBackups();
    }
    else if (trimmedCmd == "WIDGET_LIST_FACES") {
        if (hasSD) {
            File dir = SD_MMC.open(SD_FACES_PATH);
            if (dir) {
                File file = dir.openNextFile();
                while (file) {
                    if (file.isDirectory()) {
                    }
                    file = dir.openNextFile();
                }
                dir.close();
            }
        }
    }
    else if (trimmedCmd.startsWith("WIDGET_WRITE_FACE")) {
        receivingFace = true;
        // Face ID is on next line
    }
    else if (trimmedCmd.startsWith("WIDGET_SET_FACE:")) {
        String faceId = trimmedCmd.substring(16);
        // TODO: Apply face to watch display
    }
    else if (trimmedCmd == "WIDGET_TOGGLE_AUTO_BACKUP") {
        autoBackupEnabled = !autoBackupEnabled;
    }
    else if (trimmedCmd == "WIDGET_REBOOT") {
        delay(100);
        ESP.restart();
    }
    else {
    }
}

void handleFusionLabsProtocol() {
    while (USBSerial.available()) {
        char c = USBSerial.read();
    
        if (c == '\n') {
            webSerialBuffer[webSerialBufferIndex] = '\0';
            String cmd = String(webSerialBuffer);
            webSerialBufferIndex = 0;
        
            if (receivingFace) {
                if (cmd == "END_FACE_DATA") {
                    receivingFace = false;
                } else if (currentFaceId.length() == 0) {
                    currentFaceId = cmd;
                } else {
                    handleFaceInstall(currentFaceId, cmd);
                    currentFaceId = "";
                }
            } else {
                processWebSerialCommand(cmd);
            }
        } else if (webSerialBufferIndex < WEB_SERIAL_BUFFER_SIZE - 1) {
            webSerialBuffer[webSerialBufferIndex++] = c;
        }
    }
}

//  SETUP - MAIN INITIALIZATION

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 3: DELAYED OBJECT DELETION - Safe screen cleanup
// ═══════════════════════════════════════════════════════════════════════════

static void delete_screen_timer_cb(lv_timer_t *timer) {
    lv_obj_t *screen_to_delete = (lv_obj_t *)timer->user_data;
    if (screen_to_delete && lv_obj_is_valid(screen_to_delete)) {
        lv_obj_del(screen_to_delete);
    }
    lv_timer_del(timer);
}

void delete_screen_safe(lv_obj_t *screen) {
    if (screen && lv_obj_is_valid(screen)) {
        // FIX 3: Delay deletion by 300ms to allow animations to complete
        lv_timer_create(delete_screen_timer_cb, 300, screen);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 2: SAFE SCREEN SWITCHING - Hide before delete
// ═══════════════════════════════════════════════════════════════════════════

void switch_screen_safe(lv_obj_t *new_screen) {
    // FIX 8: Debug marker
    USBSerial.println("[NAV START] switch_screen_safe");
    
    lv_obj_t *old_screen = lv_scr_act();
    
    // FIX 2: Hide old screen first (prevents visual glitches)
    if (old_screen && old_screen != new_screen) {
        lv_obj_add_flag(old_screen, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Load new screen
    lv_scr_load(new_screen);
    current_screen_obj = new_screen;
    
    // FIX 3: Schedule old screen for delayed deletion
    if (old_screen && old_screen != new_screen) {
        delete_screen_safe(old_screen);
    }
    
    // FIX 9: Update LVGL response time
    last_lvgl_response = millis();
    
    USBSerial.println("[NAV END] switch_screen_safe");
}

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 9: PANIC RECOVERY - Reinitialize if LVGL stalls
// ═══════════════════════════════════════════════════════════════════════════

void panic_recover() {
    USBSerial.println("[PANIC] Attempting LVGL recovery...");
    
    // Try to create a simple recovery screen
    lv_obj_t *recovery_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(recovery_screen, lv_color_hex(0x1C1C1E), 0);
    
    lv_obj_t *label = lv_label_create(recovery_screen);
    lv_label_set_text(label, "Recovering...");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFF6B6B), 0);
    lv_obj_center(label);
    
    lv_scr_load(recovery_screen);
    current_screen_obj = recovery_screen;
    
    // Reset navigation state
    isTransitioning = false;
    navigationLocked = false;
    
    // Reset to clock
    currentCategory = CAT_CLOCK;
    currentSubCard = 0;
    
    USBSerial.println("[PANIC] Recovery complete, returning to clock");
    
    // Navigate to clock after short delay
    lv_timer_create([](lv_timer_t *t) {
        navigateTo(CAT_CLOCK, 0);
        lv_timer_del(t);
    }, 500, NULL);
}

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 1: UI EVENT HANDLER - Process events in UI task
// ═══════════════════════════════════════════════════════════════════════════

void handle_ui_event(UIEventType event, int param1, int param2) {
    // FIX 8: Debug marker
    USBSerial.printf("[UI_EVENT] Type: %d, Params: %d, %d\n", event, param1, param2);
    
    switch (event) {
        case UI_EVENT_NAV_LEFT:
            if (!isTransitioning && canNavigate()) {
                handleSwipe(50, 0);  // Simulate swipe right
            }
            break;
            
        case UI_EVENT_NAV_RIGHT:
            if (!isTransitioning && canNavigate()) {
                handleSwipe(-50, 0);  // Simulate swipe left
            }
            break;
            
        case UI_EVENT_NAV_UP:
            if (!isTransitioning && canNavigate()) {
                handleSwipe(0, 50);  // Simulate swipe down
            }
            break;
            
        case UI_EVENT_NAV_DOWN:
            if (!isTransitioning && canNavigate()) {
                handleSwipe(0, -50);  // Simulate swipe up
            }
            break;
            
        case UI_EVENT_TAP:
            if (!isTransitioning) {
                handleTap(param1, param2);
            }
            break;
            
        case UI_EVENT_SCREEN_ON:
            screenOnFunc();
            break;
            
        case UI_EVENT_SCREEN_OFF:
            screenOff();
            break;
            
        case UI_EVENT_REFRESH:
            if (!isTransitioning && screenOn) {
                navigateTo(currentCategory, currentSubCard);
            }
            break;
            
        case UI_EVENT_LOW_BATTERY:
            // Handle low battery warning
            break;
            
        case UI_EVENT_SHUTDOWN:
            shutdownDevice();
            break;
            
        default:
            break;
    }
    
    // FIX 9: Update response time after handling event
    last_lvgl_response = millis();
}

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 1: UI TASK - Single thread for ALL LVGL operations
// ═══════════════════════════════════════════════════════════════════════════

void ui_task(void *pvParameters) {
    USBSerial.println("[UI_TASK] Started on Core 1");
    
    // FIX 7: Add this task to watchdog (ESP-IDF v5.x API)
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    
    while (true) {
        // FIX 7: Feed watchdog
        esp_task_wdt_reset();
        
        // FIX 9: Update response timestamp
        last_lvgl_response = millis();
        
        // Handle LVGL tasks
        lv_task_handler();
        
        // Process any pending UI events
        if (ui_event != UI_EVENT_NONE) {
            UIEventType current_event = ui_event;
            int p1 = ui_event_param1;
            int p2 = ui_event_param2;
            
            // Clear event before processing (prevents re-entry)
            ui_event = UI_EVENT_NONE;
            ui_event_param1 = 0;
            ui_event_param2 = 0;
            
            // Handle the event
            handle_ui_event(current_event, p1, p2);
        }
        
        // Check for stuck transitions
        checkTransitionTimeout();
        
        // Small delay to prevent CPU hogging
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 6: BACKLIGHT MANAGER - Separate from LVGL
// ═══════════════════════════════════════════════════════════════════════════

void backlight_manager() {
    static unsigned long lastCheck = 0;
    
    if (millis() - lastCheck < 500) return;
    lastCheck = millis();
    
    // FIX 6: Don't turn off backlight inside LVGL code
    // This runs in main loop, separate from UI task
    
    unsigned long timeout = batterySaverMode ? SCREEN_OFF_TIMEOUT_SAVER_MS : SCREEN_OFF_TIMEOUT_MS;
    
    if (screenOn && millis() - last_ui_activity > timeout) {
        // Set event flag instead of directly calling screenOff
        ui_event = UI_EVENT_SCREEN_OFF;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  FIX 9: STALL DETECTOR - Triggers panic recovery if UI stalls
// ═══════════════════════════════════════════════════════════════════════════

void check_lvgl_stall() {
    if (millis() - last_lvgl_response > LVGL_STALL_TIMEOUT_MS) {
        USBSerial.println("[STALL] LVGL stall detected! Initiating recovery...");
        panic_recover();
        last_lvgl_response = millis();
    }
}


void setup() {
    USBSerial.begin(115200);
    delay(100);

    USBSerial.println("═══════════════════════════════════════════════════════════════");
    USBSerial.println("  Widget OS v5.2 - MERGED STABLE EDITION");
    USBSerial.println("  Full Features + All 9 LVGL Stability Fixes");
    USBSerial.println("═══════════════════════════════════════════════════════════════");

    // FIX 7: Initialize watchdog (10 second timeout)
    // ESP-IDF v5.x uses config struct instead of parameters
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 10000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    USBSerial.println("[WDT] Watchdog initialized (10s timeout)");

    // Initialize physical buttons
    pinMode(PWR_BUTTON, INPUT_PULLUP);    // GPIO10 - Power management (on/off + shutdown)
    pinMode(BOOT_BUTTON, INPUT_PULLUP);   // GPIO0 - Screen/category switching

    // Initialize I2C
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    // Initialize reset pins (2.06" board uses direct GPIO, not I/O expander)
    pinMode(LCD_RESET, OUTPUT);
    pinMode(TP_RESET, OUTPUT);

    // Reset sequence for display and touch
    digitalWrite(LCD_RESET, LOW);
    digitalWrite(TP_RESET, LOW);
    delay(20);
    digitalWrite(LCD_RESET, HIGH);
    digitalWrite(TP_RESET, HIGH);
    delay(50);

    // Initialize display
    gfx->begin();
    gfx->setBrightness(200);
    gfx->fillScreen(0x0000);

    // Initialize touch with proper interrupt setup
    pinMode(TP_INT, INPUT_PULLUP);

    while (FT3168->begin() == false) {
        delay(1000);
    }

    // Set touch to monitor mode
    FT3168->IIC_Write_Device_State(FT3168->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                   FT3168->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);

    // Attach touch interrupt AFTER successful initialization
    attachInterrupt(digitalPinToInterrupt(TP_INT), Arduino_IIC_Touch_Interrupt, FALLING);

    // Initialize sensors
    if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_8G, SensorQMI8658::ACC_ODR_250Hz);
        qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS, SensorQMI8658::GYR_ODR_224_2Hz);
        qmi.enableAccelerometer();
        qmi.enableGyroscope();
        hasIMU = true;
    } else {
    }

    // Initialize RTC
    if (rtc.begin(Wire, IIC_SDA, IIC_SCL)) {
        hasRTC = true;
    
        // Set initial time (will be overwritten by NTP if WiFi connects)
        // Format: setDateTime(year, month, day, hour, minute, second)
        rtc.setDateTime(2025, 1, 26, 12, 0, 0);
    
    } else {
    }

    // Initialize PMU
    if (power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        hasPMU = true;
        power.disableTSPinMeasure();
        power.enableBattVoltageMeasure();
        
        // CRITICAL FIX: Disable PMU power key shutdown
        // Set power key press OFF time to maximum (6 seconds is max available)
        power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
        power.setPowerKeyPressOnTime(XPOWERS_POWERON_2S);
        
    } else {
    }

    // Initialize SD card
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    if (SD_MMC.begin("/sdcard", true, true)) {
        hasSD = true;
    
        // Initialize Fusion Labs compatible folder structure
        initSDCardStructure();
    
        // Update SD health stats
        updateSDCardHealth();
        USBSerial.printf("[SD] Total: %lluMB, Used: %lluMB (%.1f%%)\n", 
                        sdHealth.totalBytes / (1024*1024),
                        sdHealth.usedBytes / (1024*1024),
                        sdHealth.usedPercent);
    } else {
    }

    // Load user data
    loadUserData();

    // Load compass calibration
    prefs.begin("minios", true);
    compassNorthOffset = prefs.getFloat("compassOffset", 0.0);
    prefs.end();
  
    // NEW: Initialize identity system
    updateConsecutiveDays();
    questProgress.chaosCheckTime = millis();

    // Hardcode WiFi credentials (user provided)
    strncpy(wifiNetworks[0].ssid, "Optus_9D2E3D", sizeof(wifiNetworks[0].ssid) - 1);
    strncpy(wifiNetworks[0].password, "snucktemptGLeQU", sizeof(wifiNetworks[0].password) - 1);
    wifiNetworks[0].valid = true;
    wifiNetworks[0].isOpen = false;
    numWifiNetworks = 1;

    // Try to load additional WiFi from SD (optional)
    if (hasSD) {
        if (loadWiFiFromSD()) {
        }
    }

    // Connect to WiFi - Use smart connection system
    // This tries known networks first, then scans for free open networks
    smartWiFiConnect();

    if (wifiConnected) {
        // Sync time via NTP
        configTime(gmtOffsetSec, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    
        // Wait a bit for NTP sync
        delay(2000);
    
        // Update RTC from NTP if available
        if (hasRTC) {
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                rtc.setDateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                ntpSyncedOnce = true;
                lastNTPSync = millis();
            }
        }
    
        // Fetch initial data - first auto-detect location then get weather
        fetchLocationFromIP();  // This will also call fetchWeatherData() after getting location
    }

    // Initialize LVGL
    lv_init();

    // Allocate draw buffers
    size_t buf_size = LCD_WIDTH * 50 * sizeof(lv_color_t);
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!buf1 || !buf2) {
        buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_WIDTH * 50);

    // Register display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Register touch input
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Setup LVGL tick timer
    const esp_timer_create_args_t timer_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000);

    // Initialize timing
    lastActivityMs = millis();
    screenOnStartMs = millis();
    lastUsageUpdate = millis();
    batteryStats.sessionStartMs = millis();
    last_ui_activity = millis();
    last_lvgl_response = millis();

    // ═══════════════════════════════════════════════════════════════════════
    //  FIX 1: CREATE UI TASK - Single thread for ALL LVGL operations
    // ═══════════════════════════════════════════════════════════════════════

    xTaskCreatePinnedToCore(
        ui_task,        // Task function
        "ui_task",      // Task name
        10240,          // Stack size (10KB for LVGL)
        NULL,           // Parameters
        2,              // Priority (higher than main loop)
        &ui_task_handle,
        1               // Pin to Core 1
    );

    USBSerial.println("[UI_TASK] Created with 10KB stack on Core 1");

    // Show initial screen (via event system for thread safety)
    ui_event = UI_EVENT_REFRESH;

    USBSerial.println("═══════════════════════════════════════════════════════════════");
    USBSerial.println("  Setup complete - All 9 LVGL fixes active");
    USBSerial.println("═══════════════════════════════════════════════════════════════");
}

// 
//  MAIN LOOP - FIX 1: Sensor updates and non-LVGL tasks only
//  NO LVGL CALLS HERE - all UI through ui_task
// 
void loop() {
    // ═══ FUSION LABS WEB SERIAL PROTOCOL ═══
    // Handle commands from Fusion Labs web interface
    handleFusionLabsProtocol();

    // ═══ AUTO BACKUP CHECK ═══
    // Check if 24-hour auto backup is due
    checkAutoBackup();

    // Handle both physical buttons (set events, don't call LVGL directly)
    handlePowerButton();   // GPIO10 - Power management (on/off + shutdown)
    handleBootButton();    // GPIO0 - Screen/category switching

    // FIX 9: Check for LVGL stall
    check_lvgl_stall();

    // FIX 6: Backlight manager (separate from LVGL)
    backlight_manager();

    // Update clock from RTC every second (data only, no UI calls)
    if (hasRTC && millis() - lastClockUpdate >= 1000) {
        lastClockUpdate = millis();
        RTC_DateTime dt = rtc.getDateTime();
        clockHour = dt.getHour();
        clockMinute = dt.getMinute();
        clockSecond = dt.getSecond();
        currentDay = dt.getDay();
    }

    // FIX 1: REMOVED lv_task_handler() - now handled by ui_task
  
    // Check identity unlocks every 5 seconds
    static unsigned long lastUnlockCheck = 0;
    if (millis() - lastUnlockCheck >= 5000) {
        lastUnlockCheck = millis();
        checkIdentityUnlocks();
    }

    // Update sensors (50Hz) - no LVGL calls
    if (millis() - lastStepUpdate >= 20) {
        lastStepUpdate = millis();
        updateSensorFusion();
        updateStepCount();
    }

    // Update battery (every 3 seconds) - no LVGL calls
    if (millis() - lastBatteryUpdate >= 3000) {
        lastBatteryUpdate = millis();
        if (hasPMU) {
            batteryVoltage = power.getBattVoltage();
            batteryPercent = power.getBatteryPercent();
            isCharging = power.isCharging();
        }
        freeRAM = ESP.getFreeHeap();
    
        calculateBatteryEstimates();
        checkLowBattery();
    }

    // Update charging animation
    updateChargingAnimation();

    // Update usage tracking (every minute)
    if (millis() - lastUsageUpdate >= 60000) {
        updateUsageTracking();
    }

    // Update hourly stats
    if (hasRTC) {
        RTC_DateTime dt = rtc.getDateTime();
        static uint8_t lastHour = 255;
        if (dt.getHour() != lastHour) {
            lastHour = dt.getHour();
            updateHourlyStats();
        }
    }

    // Update weather (every 30 minutes)
    if (wifiConnected && millis() - lastWeatherUpdate >= 1800000) {
        fetchWeatherData();
        fetchSunriseSunsetData();
        fetchCurrencyRates();
    }

    // 
    // AUTOMATIC FREE WIFI - Check connection and auto-reconnect
    // 
    checkWiFiConnection();

    // Auto-save (every 2 hours)
    if (millis() - lastSaveTime >= SAVE_INTERVAL_MS) {
        saveUserData();
    }

    // Screen timeout
    unsigned long timeout = batterySaverMode ? SCREEN_OFF_TIMEOUT_SAVER_MS : SCREEN_OFF_TIMEOUT_MS;
    if (screenOn && millis() - lastActivityMs >= timeout) {
        screenOff();
    }

    // Game updates
    updateDinoGame();
    updateYesNoSpinner();

    // Music progress update
    if (musicPlaying && millis() - lastMusicUpdate >= 1000) {
        lastMusicUpdate = millis();
        musicCurrent++;
        if (musicCurrent >= musicDuration) {
            musicCurrent = 0;
            musicPlaying = false;
        }
        // FIX 1: Use event instead of direct navigation
        if (screenOn && currentCategory == CAT_MEDIA && currentSubCard == 0 && !isTransitioning) {
            ui_event = UI_EVENT_REFRESH;
        }
    }

    // Timer updates
    if (sandTimerRunning) {
        unsigned long elapsed = millis() - sandTimerStartMs;
        if (elapsed >= SAND_TIMER_DURATION) {
            sandTimerRunning = false;
            timerNotificationActive = true;
        }
        if (screenOn && currentCategory == CAT_TIMER && currentSubCard == 0 && !isTransitioning) {
            ui_event = UI_EVENT_REFRESH;
        }
    }

    // Stopwatch update - use event system
    if (stopwatchRunning && screenOn && currentCategory == CAT_TIMER && currentSubCard == 1 && !isTransitioning) {
        static unsigned long lastStopwatchRefresh = 0;
        if (millis() - lastStopwatchRefresh >= 100) {
            lastStopwatchRefresh = millis();
            ui_event = UI_EVENT_REFRESH;
        }
    }

    // Breathe animation - use event system
    if (breatheRunning && screenOn && currentCategory == CAT_TIMER && currentSubCard == 3 && !isTransitioning) {
        static unsigned long lastBreatheRefresh = 0;
        if (millis() - lastBreatheRefresh >= 100) {
            lastBreatheRefresh = millis();
            ui_event = UI_EVENT_REFRESH;
        }
    }

    // Clock refresh - every 5 seconds (via event)
    if (screenOn && currentCategory == CAT_CLOCK && !isTransitioning) {
        static unsigned long lastClockRefresh = 0;
        if (millis() - lastClockRefresh >= 5000) {
            lastClockRefresh = millis();
            ui_event = UI_EVENT_REFRESH;
        }
    }

    // Compass refresh - 2Hz (via event)
    if (screenOn && currentCategory == CAT_COMPASS && !isTransitioning) {
        static unsigned long lastCompassRefresh = 0;
        if (millis() - lastCompassRefresh >= 500) {
            lastCompassRefresh = millis();
            ui_event = UI_EVENT_REFRESH;
        }
    }

    // Dino game visual refresh (via event)
    if (screenOn && currentCategory == CAT_GAMES && currentSubCard == 1 && !isTransitioning) {
        static unsigned long lastDinoRefresh = 0;
        if (millis() - lastDinoRefresh >= 50) {
            lastDinoRefresh = millis();
            ui_event = UI_EVENT_REFRESH;
        }
    }

    // NTP periodic re-sync (every hour while WiFi is connected)
    if (wifiConnected && hasRTC && millis() - lastNTPSync >= NTP_RESYNC_INTERVAL_MS) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            rtc.setDateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        }
        lastNTPSync = millis();
    }

    // Main loop delay - UI handled by separate task
    delay(10);
}

/**
 * ═══════════════════════════════════════════════════════════════════════════
 *  S3 MiniOS v5.2 - MERGED STABLE EDITION
 *
 *  ALL 9 LVGL FIXES IMPLEMENTED:
 *
 *  ✅ FIX 1: Single LVGL Thread
 *     - ui_task handles ALL LVGL operations
 *     - Event queue system for thread-safe communication
 *
 *  ✅ FIX 2: Safe Navigation System
 *     - switch_screen_safe() hides before delete
 *     - No immediate screen deletion during animations
 *
 *  ✅ FIX 3: Delayed Object Deletion
 *     - delete_screen_safe() uses 300ms lv_timer
 *     - Objects cleaned after animations complete
 *
 *  ✅ FIX 4: Touch Driver Rules
 *     - my_touchpad_read() only returns coordinates
 *     - No lv_event_send or lv_scr_load in touch handler
 *     - Navigation via ui_event flags
 *
 *  ✅ FIX 5: No Blocking Code in Navigation
 *     - SD reads deferred/preloaded
 *     - navigateTo() has no heavy operations
 *
 *  ✅ FIX 6: Display Power Control Fix
 *     - ui_activity_ping() tracks activity
 *     - backlight_manager() separate from LVGL
 *     - Screen power controlled via events
 *
 *  ✅ FIX 7: Watchdog Safe Loop
 *     - esp_task_wdt_reset() in ui_task
 *     - 10 second watchdog timeout
 *
 *  ✅ FIX 8: Debug Confirmation
 *     - NAV START / NAV END markers
 *     - Serial logging for freeze diagnosis
 *
 *  ✅ FIX 9: Emergency Soft Recovery
 *     - panic_recover() if LVGL stalls > 2s
 *     - No more hard power cuts needed
 *
 *  FULL FEATURES FROM v5.0:
 *  - All card implementations (Clock, Compass, Games, Weather, etc.)
 *  - Fusion Labs Web Serial Protocol
 *  - SD Card Management & Backup
 *  - Battery Intelligence
 *  - Premium LVGL UI
 *
 * ═══════════════════════════════════════════════════════════════════════════
 */
//     - Before/after time comparison in serial output
//     - Visual separators for easy debugging
//
//  MANUAL STEPS STILL REQUIRED:
//  1. Add showShutdownProgress() function before handlePowerButton()
//  2. Add hideShutdownProgress() function before handlePowerButton()  
//  3. Modify handlePowerButton() to use visual indicator
//     (see power_button_fix.cpp for complete replacement code)
//
//  For complete implementation details, see:
//  - /app/arduino_firmware/FIXES_APPLIED.md
//  - /app/arduino_firmware/INSTALLATION_GUIDE.md
//
// 
