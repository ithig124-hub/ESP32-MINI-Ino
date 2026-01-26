/**
 * ═══════════════════════════════════════════════════════════════════════════════
 *  S3 MiniOS v4.3.1 - ENHANCED UI EDITION (2.06 inch improvements ported)
 *  ESP32-S3-Touch-AMOLED-1.8" Smartwatch Firmware
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 *  NAVIGATION MODEL:
 *  
 *  State: mainIndex (category), subIndex (depth within category)
 *  
 *  ↔️ HORIZONTAL (Left/Right):
 *    • ALWAYS changes category
 *    • ALWAYS resets subIndex to 0 (main card)
 *    • Works from ANY screen (main or sub-card)
 *    • Infinite loop (wraps around)
 *  
 *  ↕️ VERTICAL (Up/Down):
 *    • DOWN: Go deeper (subIndex + 1)
 *    • UP: Go back (subIndex - 1)
 *    • Bounce at boundaries (no wrap)
 *  
 *  STATE RESET RULE:
 *    When leaving a category horizontally, subIndex MUST reset to 0.
 *    Re-entering a category ALWAYS opens the main card.
 *
 *  POWER BUTTON:
 *    • TAP: Toggle screen on/off
 *    • HOLD 5s: Shutdown (screen turns ON to show progress)
 *
 *
 *  === IMPROVEMENTS FROM 2.06 INCH VERSION ===
 *  • Apple-style shutdown progress UI with real-time percentage
 *  • Enhanced navigation lock system (prevents conflicts)
 *  • Better button debouncing with consecutive reading checks
 *  • Transition timeout protection (auto-recovery)
 *  • Improved code documentation and safety checks
 *
 *  Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.8
 *    • Display: SH8601 QSPI AMOLED 368x448
 *    • Touch: FT3168
 *    • IMU: QMI8658
 *    • RTC: PCF85063
 *    • PMU: AXP2101
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
//  BOARD-SPECIFIC CONFIGURATION (1.8" SH8601)
// ═══════════════════════════════════════════════════════════════════════════════
#define LCD_WIDTH       368
#define LCD_HEIGHT      448

// ═══════════════════════════════════════════════════════════════════════════════
//  WIFI CONFIGURATION - HARDCODED CREDENTIALS
// ═══════════════════════════════════════════════════════════════════════════════
#define WIFI_SSID "Optus_9D2E3D"
#define WIFI_PASSWORD "snucktemptGLeQU"

#define MAX_WIFI_NETWORKS 5
#define WIFI_CONFIG_PATH "/wifi/config.txt"

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
const char* NTP_SERVER = "pool.ntp.org";
const int DAYLIGHT_OFFSET_SEC = 0;

// API Keys
const char* OPENWEATHER_API = "3795c13a0d3f7e17799d638edda60e3c";
const char* ALPHAVANTAGE_API = "UHLX28BF7GQ4T8J3";
const char* COINAPI_KEY = "11afad22-b6ea-4f18-9056-c7a1d7ed14a1";

bool wifiConnected = false;
bool wifiConfigFromSD = false;

// NTP Time Sync Tracking
bool timeInitiallySynced = false;
unsigned long lastNTPSyncMs = 0;
const unsigned long NTP_RESYNC_INTERVAL_MS = 3600000;  // Re-sync every 1 hour

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY INTELLIGENCE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
#define SAVE_INTERVAL_MS 7200000UL
#define SCREEN_OFF_TIMEOUT_MS 30000
#define SCREEN_OFF_TIMEOUT_SAVER_MS 10000

#define BATTERY_CAPACITY_MAH 500
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
Adafruit_XCA9554 expander;
SensorQMI8658 qmi;
SensorPCF85063 rtc;
XPowersPMU power;
IMUdata acc, gyr;
Preferences prefs;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_SH8601 *gfx = new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

// ═══════════════════════════════════════════════════════════════════════════════
//  NAVIGATION SYSTEM - PERFECT IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════════
//
//  🔷 NAVIGATION RULES:
//  
//  State Model:
//    mainIndex  = category index (0 to NUM_CATEGORIES-1)
//    subIndex   = vertical index within category (0 = main card)
//  
//  Horizontal (Left/Right):
//    • ALWAYS changes category (mainIndex)
//    • ALWAYS resets subIndex to 0
//    • Works from ANY screen (main or sub-card)
//    • Infinite loop (wraps around)
//  
//  Vertical (Up/Down):
//    • DOWN from main card → first sub-card (subIndex 1)
//    • DOWN from sub-card → next sub-card (subIndex + 1)
//    • UP from sub-card → previous sub-card (subIndex - 1)
//    • UP from first sub-card → main card (subIndex 0)
//    • UP from main card → bounce (no movement)
//    • DOWN from last sub-card → bounce (no movement)
//
// ═══════════════════════════════════════════════════════════════════════════════

#define NUM_CATEGORIES 12
enum Category {
  CAT_CLOCK = 0, CAT_COMPASS, CAT_ACTIVITY, CAT_GAMES,
  CAT_WEATHER, CAT_STOCKS, CAT_MEDIA, CAT_TIMER,
  CAT_STREAK, CAT_CALENDAR, CAT_SETTINGS, CAT_SYSTEM
};

// Navigation state - THE ONLY STATE VARIABLES NEEDED
int mainIndex = 0;      // Category index (0 to NUM_CATEGORIES-1)
int subIndex = 0;       // Vertical index (0 = main card, 1+ = sub-cards)

// Legacy aliases for compatibility
#define currentCategory mainIndex
#define currentSubCard subIndex

// Number of sub-cards per category (index 0 = main card, so total cards = value)
// Example: {2} means main card + 1 sub-card = 2 total cards
const int maxSubCards[] = {2, 3, 4, 3, 2, 2, 2, 4, 3, 1, 1, 3};

// Animation state
bool isTransitioning = false;
int transitionDir = 0;
float transitionProgress = 0.0;
unsigned long transitionStartMs = 0;
const unsigned long TRANSITION_DURATION = 200;

// Navigation lock - prevents conflicting UI updates
volatile bool navigationLocked = false;
unsigned long lastNavigationMs = 0;
const unsigned long NAVIGATION_COOLDOWN_MS = 150;  // Minimum time between navigations


// ═══════════════════════════════════════════════════════════════════════════════
//  TOUCH STATE - IMPROVED FOR RESPONSIVE NAVIGATION
// ═══════════════════════════════════════════════════════════════════════════════
int32_t touchStartX = 0, touchStartY = 0;
int32_t touchCurrentX = 0, touchCurrentY = 0;
int32_t touchLastX = 0, touchLastY = 0;
bool touchActive = false;
unsigned long touchStartMs = 0;

// Touch gesture thresholds - MORE RESPONSIVE
const int SWIPE_THRESHOLD_MIN = 25;        // Minimum pixels for swipe (lowered)
const int SWIPE_THRESHOLD_MAX = 350;       // Maximum swipe distance (increased)
const float SWIPE_VELOCITY_MIN = 0.08f;    // Minimum velocity (lowered for easier swipes)
const unsigned long SWIPE_MAX_DURATION = 800;  // Max time for swipe (increased)

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
} userData = {0, 10000, 7, 0.0, 0.0, {0}, 0, 0, 0, 0, 200, 1, 0, 0, 0};

// ═══════════════════════════════════════════════════════════════════════════════
//  RUNTIME STATE
// ═══════════════════════════════════════════════════════════════════════════════
bool screenOn = true;
unsigned long lastActivityMs = 0;
unsigned long screenOnStartMs = 0;
unsigned long screenOffStartMs = 0;

// Power button handling - ENHANCED: Apple-style progress, better debouncing
// Improvements from 2.06 inch: consecutive reading checks, visual feedback
bool powerButtonPressed = false;
unsigned long powerButtonPressStartMs = 0;
const unsigned long POWER_BUTTON_LONG_PRESS_MS = 5000;  // 5 seconds for shutdown
const unsigned long POWER_BUTTON_DEBOUNCE_MS = 50;
bool powerButtonLongPressTriggered = false;  // Prevent multiple triggers

// Visual feedback for long press
bool showingShutdownProgress = false;
unsigned long shutdownProgressStartMs = 0;

// Shutdown progress visual indicator
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

// Stocks/Crypto
float btcPrice = 0, ethPrice = 0;
float aaplPrice = 0, tslaPrice = 0;

// Battery
uint16_t batteryVoltage = 4100;
uint8_t batteryPercent = 85;
bool isCharging = false;
uint32_t freeRAM = 234567;

// Hardware flags
bool hasIMU = false, hasRTC = false, hasPMU = false, hasSD = false;

// ═══════════════════════════════════════════════════════════════════════════════
//  SENSOR FUSION COMPASS STATE
// ═══════════════════════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════════════════════
//  TIMER STATE
// ═══════════════════════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════════════════════
//  GAME STATE
// ═══════════════════════════════════════════════════════════════════════════════
int playerCards[10], dealerCards[10];
int playerCount = 0, dealerCount = 0;
int blackjackBet = 100;
bool blackjackGameActive = false;
bool playerStand = false;

bool yesNoSpinning = false;
int yesNoAngle = 0;
String yesNoResult = "?";

int dinoY = 0;
float dinoVelocity = 0;
bool dinoJumping = false;
int dinoScore = 0;
int obstacleX = 350;
bool dinoGameOver = false;
const float GRAVITY = 4.0;
const float JUMP_FORCE = -22.0;

bool musicPlaying = false;
uint16_t musicDuration = 245;
uint16_t musicCurrent = 86;
const char* musicTitle = "Night Drive";
const char* musicArtist = "Synthwave FM";

// ═══════════════════════════════════════════════════════════════════════════════
//  TIMING VARIABLES
// ═══════════════════════════════════════════════════════════════════════════════
unsigned long lastClockUpdate = 0;
unsigned long lastStepUpdate = 0;
unsigned long lastBatteryUpdate = 0;
unsigned long lastMusicUpdate = 0;
unsigned long lastSaveTime = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastUsageUpdate = 0;
unsigned long lastHourlyUpdate = 0;

// ═══════════════════════════════════════════════════════════════════════════════
//  PREMIUM GRADIENT THEMES - COMPASS STYLE
// ═══════════════════════════════════════════════════════════════════════════════
struct GradientTheme {
  const char* name;
  lv_color_t color1;
  lv_color_t color2;
  lv_color_t text;
  lv_color_t accent;
  lv_color_t secondary;
};

GradientTheme gradientThemes[] = {
  {"Midnight", lv_color_hex(0x0A0A0C), lv_color_hex(0x1C1C1E), lv_color_hex(0xFFFFFF), lv_color_hex(0x0A84FF), lv_color_hex(0xFF453A)},
  {"Ocean", lv_color_hex(0x0D3B66), lv_color_hex(0x1A759F), lv_color_hex(0xFFFFFF), lv_color_hex(0x52B2CF), lv_color_hex(0x99E1D9)},
  {"Sunset", lv_color_hex(0xFF6B35), lv_color_hex(0xF7931E), lv_color_hex(0xFFFFFF), lv_color_hex(0xFFD166), lv_color_hex(0xFFA62F)},
  {"Aurora", lv_color_hex(0x7B2CBF), lv_color_hex(0x9D4EDD), lv_color_hex(0xFFFFFF), lv_color_hex(0xC77DFF), lv_color_hex(0xE0AAFF)},
  {"Forest", lv_color_hex(0x1B4332), lv_color_hex(0x2D6A4F), lv_color_hex(0xFFFFFF), lv_color_hex(0x52B788), lv_color_hex(0x95D5B2)},
  {"Ruby", lv_color_hex(0x9B2335), lv_color_hex(0xC41E3A), lv_color_hex(0xFFFFFF), lv_color_hex(0xFF6B6B), lv_color_hex(0xFFA07A)},
  {"Graphite", lv_color_hex(0x0A0A0C), lv_color_hex(0x2C2C2E), lv_color_hex(0xFFFFFF), lv_color_hex(0x8E8E93), lv_color_hex(0xAEAEB2)},
  {"Mint", lv_color_hex(0x00A896), lv_color_hex(0x02C39A), lv_color_hex(0x1C1C1E), lv_color_hex(0x00F5D4), lv_color_hex(0xB5FFE1)}
};
#define NUM_THEMES 8

// ═══════════════════════════════════════════════════════════════════════════════
//  SD CARD WALLPAPER SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════
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

struct WallpaperTheme {
  const char* name;
  lv_color_t top;
  lv_color_t mid1;
  lv_color_t mid2;
  lv_color_t bottom;
};

WallpaperTheme gradientWallpapers[] = {
  {"None", lv_color_hex(0x0A0A0C), lv_color_hex(0x0A0A0C), lv_color_hex(0x0A0A0C), lv_color_hex(0x0A0A0C)},
  {"Mountain Sunset", lv_color_hex(0x4A90D9), lv_color_hex(0xF4A460), lv_color_hex(0xE07020), lv_color_hex(0x8B4513)},
  {"Golden Peaks", lv_color_hex(0xFF8C00), lv_color_hex(0xFFD700), lv_color_hex(0xDC6B00), lv_color_hex(0x2F1810)},
  {"Canyon Dawn", lv_color_hex(0x87CEEB), lv_color_hex(0xFFB347), lv_color_hex(0xCD5C5C), lv_color_hex(0x8B4513)},
  {"Island Paradise", lv_color_hex(0xE6B3CC), lv_color_hex(0x9370DB), lv_color_hex(0x4169E1), lv_color_hex(0x006994)},
  {"Alpine Meadow", lv_color_hex(0xFFD700), lv_color_hex(0xF0E68C), lv_color_hex(0x90EE90), lv_color_hex(0x228B22)},
  {"Twilight Ocean", lv_color_hex(0x191970), lv_color_hex(0x483D8B), lv_color_hex(0x4682B4), lv_color_hex(0x008B8B)}
};
#define NUM_GRADIENT_WALLPAPERS 7

// ═══════════════════════════════════════════════════════════════════════════════
//  FUNCTION PROTOTYPES
// ═══════════════════════════════════════════════════════════════════════════════
void navigateTo(int category, int subCard);
void handleSwipe(int dx, int dy);
void saveUserData();
void loadUserData();
void syncTimeNTP();
void fetchWeatherData();
void fetchCryptoData();
void updateSensorFusion();
void calibrateCompass();

void updateUsageTracking();
void updateHourlyStats();
void updateDailyStats();
void calculateBatteryEstimates();
void checkLowBattery();
void toggleBatterySaver();

void screenOff();
void screenOnFunc();
void shutdownDevice();

void createClockCard();
void createAnalogClockCard();
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
void createStocksCard();
void createCryptoCard();
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
void createFactoryResetCard();

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
//  TOUCHPAD READ - REWRITTEN FOR RELIABLE NAVIGATION
// ═══════════════════════════════════════════════════════════════════════════════
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  // Read touch coordinates
  int32_t touchX = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
  int32_t touchY = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
  uint8_t touchCount = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);

  // Check if finger is touching
  bool isTouching = (touchCount > 0) && (touchX >= 0) && (touchX <= LCD_WIDTH) && 
                    (touchY >= 0) && (touchY <= LCD_HEIGHT);

  // Clear interrupt flag
  if (FT3168->IIC_Interrupt_Flag) {
    FT3168->IIC_Interrupt_Flag = false;
  }

  if (isTouching) {
    // FINGER DOWN
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
    lastActivityMs = millis();

    // Wake screen on touch (consume this touch)
    if (!screenOn) {
      screenOnFunc();
      touchActive = false;
      data->state = LV_INDEV_STATE_REL;
      return;
    }

    // Don't process touch during transitions
    if (isTransitioning) {
      return;
    }

    // Touch START
    if (!touchActive) {
      touchActive = true;
      touchStartX = touchX;
      touchStartY = touchY;
      touchCurrentX = touchX;
      touchCurrentY = touchY;
      touchStartMs = millis();
      USBSerial.printf("[TOUCH] Start: x=%d y=%d\n", touchX, touchY);
    } else {
      // Touch MOVE - update current position
      touchCurrentX = touchX;
      touchCurrentY = touchY;
    }

  } else {
    // FINGER UP
    data->state = LV_INDEV_STATE_REL;
    
    // Process gesture on release
    if (touchActive) {
      touchActive = false;
      
      int dx = touchCurrentX - touchStartX;
      int dy = touchCurrentY - touchStartY;
      unsigned long duration = millis() - touchStartMs;
      
      // Calculate distance
      float distance = sqrt((float)(dx*dx + dy*dy));
      
      USBSerial.printf("[TOUCH] End: dx=%d dy=%d dist=%.1f dur=%lu\n", 
                       dx, dy, distance, duration);
      
      // Check if this is a valid swipe
      // Only require: minimum distance AND reasonable duration
      if (distance >= SWIPE_THRESHOLD_MIN && duration <= SWIPE_MAX_DURATION && duration > 30) {
        USBSerial.println("[TOUCH] >>> SWIPE DETECTED <<<");
        handleSwipe(dx, dy);
      } else if (duration < 250 && distance < SWIPE_THRESHOLD_MIN) {
        // Short touch with small movement = TAP
        USBSerial.printf("[TOUCH] Tap at x=%d y=%d\n", touchCurrentX, touchCurrentY);
      }
    }
  }
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
    USBSerial.println("[SCREEN] Off");
}

void screenOnFunc() {
    if (screenOn) return;

    batteryStats.screenOffTimeMs += (millis() - screenOffStartMs);
    screenOnStartMs = millis();

    screenOn = true;
    lastActivityMs = millis();
    gfx->setBrightness(batterySaverMode ? 100 : userData.brightness);
    navigateTo(mainIndex, subIndex);
    USBSerial.println("[SCREEN] On");
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

// ═══════════════════════════════════════════════════════════════════════════════
//  NAVIGATION - PERFECT IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════════
void startTransition(int direction) {
  isTransitioning = true;
  transitionDir = direction;
  transitionProgress = 0.0;
  transitionStartMs = millis();
}


// Check if navigation is allowed (prevents conflicts)
bool canNavigate() {
    if (navigationLocked) return false;
    if (isTransitioning) return false;
    if (millis() - lastNavigationMs < NAVIGATION_COOLDOWN_MS) return false;
    return true;
}

// Auto-recover from stuck transitions
void checkTransitionTimeout() {
    if (isTransitioning && (millis() - transitionStartMs > TRANSITION_DURATION + 200)) {
        // Force end transition if it's stuck
        isTransitioning = false;
        navigationLocked = false;
        USBSerial.println("[NAV] Transition timeout - unlocked");
    }
    // Also unlock navigation after cooldown
    if (navigationLocked && !isTransitioning && (millis() - lastNavigationMs > NAVIGATION_COOLDOWN_MS)) {
        navigationLocked = false;
    }
}

void handleSwipe(int dx, int dy) {
  // SAFETY: Don't process swipes if navigation is locked
  if (navigationLocked || isTransitioning) {
    USBSerial.println("[NAV] Swipe ignored - navigation locked");
    return;
  }

  // Dismiss low battery popup first
  if (showingLowBatteryPopup) {
    showingLowBatteryPopup = false;
    if (canNavigate()) {
      navigationLocked = true;
      navigateTo(mainIndex, subIndex);
      lastNavigationMs = millis();
    }
    return;
  }

  int newMainIndex = mainIndex;
  int newSubIndex = subIndex;
  int direction = 0;

  // ═══════════════════════════════════════════════════════════════════════════
  //  GESTURE DIRECTION LOCK
  //  Determine if this is horizontal or vertical based on dominant axis
  // ═══════════════════════════════════════════════════════════════════════════
  bool isHorizontal = abs(dx) > abs(dy);
  
  if (isHorizontal && abs(dx) >= SWIPE_THRESHOLD_MIN) {
    // ═══════════════════════════════════════════════════════════════════════
    //  HORIZONTAL SWIPE → CHANGE CATEGORY
    //  
    //  Rules:
    //  • Always works (from main card OR sub-card)
    //  • Always resets subIndex to 0
    //  • Infinite loop (wraps around)
    // ═══════════════════════════════════════════════════════════════════════
    
    if (dx > 0) {
      // Swipe RIGHT → previous category
      newMainIndex = mainIndex - 1;
      if (newMainIndex < 0) {
        newMainIndex = NUM_CATEGORIES - 1;  // Wrap to last
      }
      direction = -1;  // Slide right animation
    } else {
      // Swipe LEFT → next category
      newMainIndex = mainIndex + 1;
      if (newMainIndex >= NUM_CATEGORIES) {
        newMainIndex = 0;  // Wrap to first
      }
      direction = 1;  // Slide left animation
    }
    
    // CRITICAL: Always reset to main card when changing category
    newSubIndex = 0;
    
    USBSerial.printf("[NAV] HORIZONTAL: cat %d→%d (subIndex reset to 0)\n", 
                     mainIndex, newMainIndex);
    
  } else if (!isHorizontal && abs(dy) >= SWIPE_THRESHOLD_MIN) {
    // ═══════════════════════════════════════════════════════════════════════
    //  VERTICAL SWIPE → NAVIGATE WITHIN CATEGORY STACK
    //  
    //  Rules:
    //  • DOWN: go deeper (subIndex + 1)
    //  • UP: go back (subIndex - 1)
    //  • Bounce at boundaries (no wrap)
    // ═══════════════════════════════════════════════════════════════════════
    
    int maxSub = maxSubCards[mainIndex] - 1;  // Max subIndex (0-based)
    
    if (dy > 0) {
      // Swipe DOWN → go deeper into sub-cards
      if (subIndex < maxSub) {
        newSubIndex = subIndex + 1;
        direction = 2;  // Slide up animation (content moves up)
        USBSerial.printf("[NAV] DOWN: sub %d→%d (max=%d)\n", subIndex, newSubIndex, maxSub);
      } else {
        // Already at last sub-card → bounce
        USBSerial.printf("[NAV] DOWN: BOUNCE (already at sub %d, max=%d)\n", subIndex, maxSub);
      }
    } else {
      // Swipe UP → go back
      if (subIndex > 0) {
        newSubIndex = subIndex - 1;
        direction = -2;  // Slide down animation (content moves down)
        USBSerial.printf("[NAV] UP: sub %d→%d\n", subIndex, newSubIndex);
      } else {
        // Already at main card → bounce
        USBSerial.println("[NAV] UP: BOUNCE (already at main card)");
      }
    }
    
    // Category stays the same for vertical navigation
    newMainIndex = mainIndex;
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  APPLY NAVIGATION
  // ═══════════════════════════════════════════════════════════════════════════
  if (direction != 0) {
    USBSerial.printf("[NAV] NAVIGATE: main=%d sub=%d → main=%d sub=%d\n",
                     mainIndex, subIndex, newMainIndex, newSubIndex);
    
    mainIndex = newMainIndex;
    subIndex = newSubIndex;
    startTransition(direction);
    navigateTo(mainIndex, subIndex);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY INTELLIGENCE FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════════
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

    float currentDraw = batterySaverMode ? SAVER_MODE_CURRENT_MA :
                        (screenOn ? SCREEN_ON_CURRENT_MA : SCREEN_OFF_CURRENT_MA);
    batteryStats.simpleEstimateMins = (uint32_t)((remainingCapacity / currentDraw) * 60.0f);

    if (batteryStats.weightedDrainRate > 0.1f) {
        batteryStats.weightedEstimateMins = (uint32_t)((batteryPercent / batteryStats.weightedDrainRate) * 60.0f);
    } else {
        batteryStats.weightedEstimateMins = batteryStats.simpleEstimateMins;
    }

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

        if (!batterySaverMode) {
            batterySaverMode = true;
            batterySaverAutoEnabled = true;
            gfx->setBrightness(100);
        }
    }
    else if (batteryPercent <= LOW_BATTERY_WARNING && !lowBatteryWarningShown) {
        lowBatteryWarningShown = true;
        showingLowBatteryPopup = true;
        lowBatteryPopupTime = millis();
    }

    if (showingLowBatteryPopup && millis() - lowBatteryPopupTime > 5000) {
        showingLowBatteryPopup = false;
        navigateTo(currentCategory, currentSubCard);
    }
}

void toggleBatterySaver() {
    batterySaverMode = !batterySaverMode;
    batterySaverAutoEnabled = false;
    gfx->setBrightness(batterySaverMode ? 100 : userData.brightness);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DATA PERSISTENCE
// ═══════════════════════════════════════════════════════════════════════════════
void saveUserData() {
    prefs.begin("minios", false);

    prefs.putUInt("steps", userData.steps);
    prefs.putUInt("goal", userData.dailyGoal);
    prefs.putInt("streak", userData.stepStreak);
    prefs.putFloat("dist", userData.totalDistance);
    prefs.putFloat("cal", userData.totalCalories);

    prefs.putInt("bjstreak", userData.blackjackStreak);
    prefs.putInt("won", userData.gamesWon);
    prefs.putInt("played", userData.gamesPlayed);
    prefs.putUInt("clicker", userData.clickerScore);

    prefs.putInt("bright", userData.brightness);
    prefs.putInt("timeout", userData.screenTimeout);
    prefs.putInt("theme", userData.themeIndex);
    prefs.putInt("compass", userData.compassMode);
    prefs.putInt("wallpaper", userData.wallpaperIndex);

    prefs.putBool("saver", batterySaverMode);
    prefs.putFloat("avgDrain", batteryStats.avgDrainPerHour);
    prefs.putFloat("wDrain", batteryStats.weightedDrainRate);

    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist%d", i);
        prefs.putUInt(key, userData.stepHistory[i]);
        snprintf(key, sizeof(key), "dOn%d", i);
        prefs.putFloat(key, batteryStats.dailyAvgScreenOnHours[i]);
        snprintf(key, sizeof(key), "dDr%d", i);
        prefs.putFloat(key, batteryStats.dailyAvgDrainRate[i]);
    }

    for (int i = 0; i < CARD_USAGE_SLOTS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "card%d", i);
        prefs.putUInt(key, batteryStats.cardUsageTime[i]);
    }

    prefs.end();
    lastSaveTime = millis();
    USBSerial.println("[OK] Data saved");
}

void loadUserData() {
    prefs.begin("minios", true);

    userData.steps = prefs.getUInt("steps", 0);
    userData.dailyGoal = prefs.getUInt("goal", 10000);
    userData.stepStreak = prefs.getInt("streak", 0);
    userData.totalDistance = prefs.getFloat("dist", 0);
    userData.totalCalories = prefs.getFloat("cal", 0);

    userData.blackjackStreak = prefs.getInt("bjstreak", 0);
    userData.gamesWon = prefs.getInt("won", 0);
    userData.gamesPlayed = prefs.getInt("played", 0);
    userData.clickerScore = prefs.getUInt("clicker", 0);

    userData.brightness = prefs.getInt("bright", 200);
    userData.screenTimeout = prefs.getInt("timeout", 1);
    userData.themeIndex = prefs.getInt("theme", 0);
    userData.compassMode = prefs.getInt("compass", 0);
    userData.wallpaperIndex = prefs.getInt("wallpaper", 0);

    batterySaverMode = prefs.getBool("saver", false);
    batteryStats.avgDrainPerHour = prefs.getFloat("avgDrain", 5.0f);
    batteryStats.weightedDrainRate = prefs.getFloat("wDrain", 5.0f);

    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist%d", i);
        userData.stepHistory[i] = prefs.getUInt(key, 0);
        snprintf(key, sizeof(key), "dOn%d", i);
        batteryStats.dailyAvgScreenOnHours[i] = prefs.getFloat(key, 0);
        snprintf(key, sizeof(key), "dDr%d", i);
        batteryStats.dailyAvgDrainRate[i] = prefs.getFloat(key, 0);
    }

    for (int i = 0; i < CARD_USAGE_SLOTS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "card%d", i);
        batteryStats.cardUsageTime[i] = prefs.getUInt(key, 0);
    }

    prefs.end();
    USBSerial.println("[OK] Data loaded");
}

void factoryReset() {
    USBSerial.println("[WARN] FACTORY RESET");
    prefs.begin("minios", false);
    prefs.clear();
    prefs.end();
    delay(500);
    ESP.restart();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WIFI - HARDCODED CREDENTIALS + SD FALLBACK
// ═══════════════════════════════════════════════════════════════════════════════
bool loadWiFiFromSD() {
    if (!hasSD) return false;

    File file = SD_MMC.open(WIFI_CONFIG_PATH, "r");
    if (!file) {
        USBSerial.println("No WiFi config on SD card");
        return false;
    }

    USBSerial.println("[INFO] Loading WiFi from SD...");
    // Parse SD config...
    file.close();
    return false;
}

void connectWiFi() {
    USBSerial.println("[INFO] Connecting to WiFi (hardcoded credentials)");
    USBSerial.printf("  SSID: %s\n", WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        USBSerial.print(".");
        attempts++;
    }
    USBSerial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        connectedNetworkIndex = 0;
        
        // Store in wifiNetworks for display
        strncpy(wifiNetworks[0].ssid, WIFI_SSID, 63);
        strncpy(wifiNetworks[0].password, WIFI_PASSWORD, 63);
        wifiNetworks[0].valid = true;
        numWifiNetworks = 1;
        
        USBSerial.printf("[OK] Connected to: %s\n", WIFI_SSID);
        USBSerial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        USBSerial.println("[WARN] Could not connect to WiFi");
        wifiConnected = false;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  NTP TIME SYNC - SYNC ONCE + PERIODIC RE-SYNC
// ═══════════════════════════════════════════════════════════════════════════════
void syncTimeNTP() {
    if (!wifiConnected) {
        USBSerial.println("[NTP] No WiFi, skipping sync");
        return;
    }

    USBSerial.println("[NTP] Syncing time...");
    
    configTime(gmtOffsetSec, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    
    // Wait for time to sync
    int attempts = 0;
    time_t now = 0;
    struct tm timeinfo = {0};
    while (now < 1000000000 && attempts < 20) {
        delay(250);
        time(&now);
        localtime_r(&now, &timeinfo);
        attempts++;
    }
    
    if (now > 1000000000) {
        USBSerial.printf("[NTP] Time synced: %02d:%02d:%02d %02d/%02d/%d\n",
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                         timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        
        // Update RTC with synced time
        if (hasRTC) {
            rtc.setDateTime(timeinfo.tm_year + 1900 - 2000,  // Year offset
                           timeinfo.tm_mon + 1,
                           timeinfo.tm_mday,
                           timeinfo.tm_hour,
                           timeinfo.tm_min,
                           timeinfo.tm_sec);
            USBSerial.println("[NTP] RTC updated");
        }
        
        timeInitiallySynced = true;
        lastNTPSyncMs = millis();
    } else {
        USBSerial.println("[NTP] Sync failed");
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

void fetchCryptoData() {
    if (!wifiConnected) return;

    HTTPClient http;
    http.begin("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum&vs_currencies=usd");
    http.setTimeout(5000);

    if (http.GET() == HTTP_CODE_OK) {
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
            btcPrice = doc["bitcoin"]["usd"].as<float>();
            ethPrice = doc["ethereum"]["usd"].as<float>();
        }
    }
    http.end();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SENSOR FUSION
// ═══════════════════════════════════════════════════════════════════════════════
void updateSensorFusion() {
    if (!hasIMU) return;

    unsigned long now = millis();
    float dt = (now - lastSensorUpdate) / 1000.0;
    if (dt <= 0 || dt > 1.0) dt = 0.01;
    lastSensorUpdate = now;

    if (qmi.getDataReady()) {
        qmi.getAccelerometer(acc.x, acc.y, acc.z);
        qmi.getGyroscope(gyr.x, gyr.y, gyr.z);

        accelRoll = atan2(acc.y, acc.z) * 180.0 / PI;
        accelPitch = atan2(-acc.x, sqrt(acc.y * acc.y + acc.z * acc.z)) * 180.0 / PI;

        gyroRoll += gyr.x * dt;
        gyroPitch += gyr.y * dt;
        gyroYaw += gyr.z * dt;

        roll = ALPHA * (roll + gyr.x * dt) + BETA * accelRoll;
        pitch = ALPHA * (pitch + gyr.y * dt) + BETA * accelPitch;
        yaw = gyroYaw;

        compassHeading = yaw - initialYaw;
        while (compassHeading < 0) compassHeading += 360;
        while (compassHeading >= 360) compassHeading -= 360;

        float diff = compassHeading - compassHeadingSmooth;
        if (diff > 180) diff -= 360;
        if (diff < -180) diff += 360;
        compassHeadingSmooth += diff * 0.1;

        tiltX = roll;
        tiltY = pitch;
    }
}

void calibrateCompass() {
    initialYaw = gyroYaw;
    compassCalibrated = true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  UI HELPERS - COMPASS STYLE
// ═══════════════════════════════════════════════════════════════════════════════
void createGradientBg() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_set_style_bg_color(lv_scr_act(), theme.color1, 0);
    lv_obj_set_style_bg_grad_color(lv_scr_act(), theme.color2, 0);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);
}

lv_obj_t* createCard(const char* title, bool fullBg = false) {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);

    if (fullBg) {
        lv_obj_set_style_bg_color(card, theme.color1, 0);
        lv_obj_set_style_bg_grad_color(card, theme.color2, 0);
        lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
    }

    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);

    if (strlen(title) > 0) {
        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_color(label, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 4, 0);
    }
    return card;
}

void createNavDots() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    // Bottom category dots (horizontal navigation indicator)
    lv_obj_t *container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(container, LCD_WIDTH, 24);
    lv_obj_align(container, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container, 8, 0);

    // Show 5 dots centered around current category
    int start = mainIndex - 2;
    if (start < 0) start = 0;
    if (start > NUM_CATEGORIES - 5) start = NUM_CATEGORIES - 5;

    for (int i = start; i < start + 5 && i < NUM_CATEGORIES; i++) {
        lv_obj_t *dot = lv_obj_create(container);
        bool active = (i == mainIndex);
        int w = active ? 20 : 8;
        lv_obj_set_size(dot, w, 8);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_bg_color(dot, active ? theme.accent : lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    }

    // Right side sub-card dots (vertical navigation indicator)
    // Only show if category has sub-cards
    if (maxSubCards[mainIndex] > 1) {
        lv_obj_t *subContainer = lv_obj_create(lv_scr_act());
        lv_obj_set_size(subContainer, 16, 80);
        lv_obj_align(subContainer, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_set_style_bg_opa(subContainer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(subContainer, 0, 0);
        lv_obj_set_flex_flow(subContainer, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(subContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(subContainer, 8, 0);

        for (int i = 0; i < maxSubCards[mainIndex]; i++) {
            lv_obj_t *dot = lv_obj_create(subContainer);
            bool active = (i == subIndex);
            int h = active ? 20 : 8;
            lv_obj_set_size(dot, 8, h);
            lv_obj_set_style_radius(dot, 4, 0);
            lv_obj_set_style_bg_color(dot, active ? theme.accent : lv_color_hex(0x3A3A3C), 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        }
    }
}

void createMiniStatusBar(lv_obj_t* parent) {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *statusBar = lv_obj_create(parent);
    lv_obj_set_size(statusBar, LCD_WIDTH - 50, 28);
    lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, -12);
    lv_obj_set_style_bg_color(statusBar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(statusBar, LV_OPA_50, 0);
    lv_obj_set_style_radius(statusBar, 14, 0);
    lv_obj_set_style_border_width(statusBar, 0, 0);

    lv_obj_t *wifiIcon = lv_label_create(statusBar);
    lv_label_set_text(wifiIcon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifiIcon, wifiConnected ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(wifiIcon, &lv_font_montserrat_12, 0);
    lv_obj_align(wifiIcon, LV_ALIGN_LEFT_MID, 8, 0);

    if (batterySaverMode) {
        lv_obj_t *saverIcon = lv_label_create(statusBar);
        lv_label_set_text(saverIcon, "S");
        lv_obj_set_style_text_color(saverIcon, lv_color_hex(0xFF9F0A), 0);
        lv_obj_set_style_text_font(saverIcon, &lv_font_montserrat_12, 0);
        lv_obj_align(saverIcon, LV_ALIGN_LEFT_MID, 28, 0);
    }

    calculateBatteryEstimates();
    lv_obj_t *estLabel = lv_label_create(statusBar);

    if (isCharging) {
        lv_label_set_text(estLabel, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_color(estLabel, lv_color_hex(0x30D158), 0);
    } else {
        char estBuf[16];
        uint32_t hrs = batteryStats.combinedEstimateMins / 60;
        uint32_t mins = batteryStats.combinedEstimateMins % 60;
        if (hrs > 0) snprintf(estBuf, sizeof(estBuf), "~%luh", hrs);
        else snprintf(estBuf, sizeof(estBuf), "~%lum", mins);
        lv_label_set_text(estLabel, estBuf);
        lv_obj_set_style_text_color(estLabel, lv_color_hex(0x636366), 0);
    }
    lv_obj_set_style_text_font(estLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(estLabel, LV_ALIGN_CENTER, 0, 0);

    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d%%", batteryPercent);
    lv_obj_t *battLabel = lv_label_create(statusBar);
    lv_label_set_text(battLabel, battBuf);
    lv_obj_set_style_text_color(battLabel, batteryPercent > 20 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(battLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(battLabel, LV_ALIGN_RIGHT_MID, -8, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  LOW BATTERY POPUP
// ═══════════════════════════════════════════════════════════════════════════════
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
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x0A0A0C), 0);
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
    lv_obj_set_style_text_color(hint, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SHUTDOWN PROGRESS INDICATOR
// ═══════════════════════════════════════════════════════════════════════════════
// Show Apple-style shutdown progress UI
void showShutdownProgressUI() {
    if (!screenOn || !showingShutdownProgress) return;

    // Calculate progress
    unsigned long elapsed = millis() - powerButtonPressStartMs;
    int progress = (elapsed * 100) / POWER_BUTTON_LONG_PRESS_MS;
    if (progress > 100) progress = 100;

    // Create Apple-style popup on first call
    if (shutdownPopup == NULL) {
        // Full screen dark overlay
        shutdownPopup = lv_obj_create(lv_scr_act());
        lv_obj_set_size(shutdownPopup, LCD_WIDTH, LCD_HEIGHT);
        lv_obj_set_pos(shutdownPopup, 0, 0);
        lv_obj_set_style_bg_color(shutdownPopup, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(shutdownPopup, LV_OPA_80, 0);
        lv_obj_set_style_border_width(shutdownPopup, 0, 0);
        lv_obj_set_style_pad_all(shutdownPopup, 0, 0);
        lv_obj_clear_flag(shutdownPopup, LV_OBJ_FLAG_SCROLLABLE);

        // Apple-style power icon container
        lv_obj_t *iconContainer = lv_obj_create(shutdownPopup);
        lv_obj_set_size(iconContainer, 70, 70);
        lv_obj_align(iconContainer, LV_ALIGN_TOP_MID, 0, 70);
        lv_obj_set_style_bg_color(iconContainer, lv_color_hex(0xFF3B30), 0);
        lv_obj_set_style_bg_opa(iconContainer, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(iconContainer, 35, 0);
        lv_obj_set_style_border_width(iconContainer, 0, 0);
        lv_obj_set_style_shadow_width(iconContainer, 25, 0);
        lv_obj_set_style_shadow_color(iconContainer, lv_color_hex(0xFF3B30), 0);
        lv_obj_set_style_shadow_opa(iconContainer, LV_OPA_50, 0);
        lv_obj_clear_flag(iconContainer, LV_OBJ_FLAG_SCROLLABLE);

        // Power icon symbol
        lv_obj_t *powerIcon = lv_label_create(iconContainer);
        lv_label_set_text(powerIcon, LV_SYMBOL_POWER);
        lv_obj_set_style_text_color(powerIcon, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(powerIcon, &lv_font_montserrat_28, 0);
        lv_obj_center(powerIcon);

        // "Hold to power off" text
        lv_obj_t *slideText = lv_label_create(shutdownPopup);
        lv_label_set_text(slideText, "Hold to Power Off");
        lv_obj_set_style_text_color(slideText, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(slideText, &lv_font_montserrat_16, 0);
        lv_obj_align(slideText, LV_ALIGN_TOP_MID, 0, 160);

        // Progress bar container (pill shape)
        lv_obj_t *sliderBg = lv_obj_create(shutdownPopup);
        lv_obj_set_size(sliderBg, 240, 50);
        lv_obj_align(sliderBg, LV_ALIGN_CENTER, 0, 35);
        lv_obj_set_style_bg_color(sliderBg, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_bg_opa(sliderBg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(sliderBg, 25, 0);
        lv_obj_set_style_border_width(sliderBg, 1, 0);
        lv_obj_set_style_border_color(sliderBg, lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_pad_all(sliderBg, 4, 0);
        lv_obj_clear_flag(sliderBg, LV_OBJ_FLAG_SCROLLABLE);

        // Progress fill
        shutdownProgressArc = lv_obj_create(sliderBg);
        lv_obj_set_height(shutdownProgressArc, 42);
        lv_obj_set_width(shutdownProgressArc, 0);
        lv_obj_align(shutdownProgressArc, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(shutdownProgressArc, lv_color_hex(0xFF3B30), 0);
        lv_obj_set_style_bg_opa(shutdownProgressArc, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(shutdownProgressArc, 21, 0);
        lv_obj_set_style_border_width(shutdownProgressArc, 0, 0);

        // Percentage label
        shutdownProgressLabel = lv_label_create(sliderBg);
        lv_obj_set_style_text_color(shutdownProgressLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(shutdownProgressLabel, &lv_font_montserrat_18, 0);
        lv_obj_center(shutdownProgressLabel);

        // Cancel hint
        lv_obj_t *cancelHint = lv_label_create(shutdownPopup);
        lv_label_set_text(cancelHint, "Release to cancel");
        lv_obj_set_style_text_color(cancelHint, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(cancelHint, &lv_font_montserrat_12, 0);
        lv_obj_align(cancelHint, LV_ALIGN_BOTTOM_MID, 0, -50);
    }

    // Safety check
    if (shutdownProgressArc == NULL || shutdownProgressLabel == NULL) {
        return;
    }

    // Update progress bar width
    int barWidth = (progress * 228) / 100;
    if (barWidth < 42) barWidth = 42;
    lv_obj_set_width(shutdownProgressArc, barWidth);

    // Update percentage text
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", progress);
    lv_label_set_text(shutdownProgressLabel, buf);

    lv_task_handler();
}

void hideShutdownProgressUI() {
    showingShutdownProgress = false;

    if (shutdownPopup != NULL) {
        lv_obj_del(shutdownPopup);
    }
    shutdownPopup = NULL;
    shutdownProgressArc = NULL;
    shutdownProgressLabel = NULL;

    lv_task_handler();
}

// Legacy function for compatibility
void drawShutdownProgress(float progress) {
    showShutdownProgressUI();
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MAIN NAVIGATION - CARD RENDERER
// ═══════════════════════════════════════════════════════════════════════════════
void navigateTo(int category, int sub) {
    // Clear transition state after navigation completes
    isTransitioning = false;
    
    // Validate bounds
    if (category < 0 || category >= NUM_CATEGORIES) {
        category = 0;  // Default to first category
    }
    if (sub < 0 || sub >= maxSubCards[category]) {
        sub = 0;  // Default to main card
    }
    
    // Update state (in case called directly)
    mainIndex = category;
    subIndex = sub;

    // Clear screen and create background
    lv_obj_clean(lv_scr_act());
    createGradientBg();

    // Update usage tracking
    updateUsageTracking();
    
    // Debug output
    USBSerial.printf("[RENDER] Category %d, SubCard %d\n", category, sub);

    // Render the appropriate card
    switch (category) {
        case CAT_CLOCK:
            if (sub == 0) createClockCard();
            else createAnalogClockCard();
            break;
        case CAT_COMPASS:
            if (sub == 0) createCompassCard();
            else if (sub == 1) createTiltCard();
            else createGyroCard();
            break;
        case CAT_ACTIVITY:
            if (sub == 0) createStepsCard();
            else if (sub == 1) createActivityRingsCard();
            else if (sub == 2) createWorkoutCard();
            else createDistanceCard();
            break;
        case CAT_GAMES:
            if (sub == 0) createBlackjackCard();
            else if (sub == 1) createDinoCard();
            else createYesNoCard();
            break;
        case CAT_WEATHER:
            if (sub == 0) createWeatherCard();
            else createForecastCard();
            break;
        case CAT_STOCKS:
            if (sub == 0) createStocksCard();
            else createCryptoCard();
            break;
        case CAT_MEDIA:
            if (sub == 0) createMusicCard();
            else createGalleryCard();
            break;
        case CAT_TIMER:
            if (sub == 0) createSandTimerCard();
            else if (sub == 1) createStopwatchCard();
            else if (sub == 2) createCountdownCard();
            else createBreatheCard();
            break;
        case CAT_STREAK:
            if (sub == 0) createStepStreakCard();
            else if (sub == 1) createGameStreakCard();
            else createAchievementsCard();
            break;
        case CAT_CALENDAR:
            createCalendarCard();
            break;
        case CAT_SETTINGS:
            createSettingsCard();
            break;
        case CAT_SYSTEM:
            if (sub == 0) createBatteryCard();
            else if (sub == 1) createBatteryStatsCard();
            else createUsagePatternsCard();
            break;
    }

    // Draw navigation dots
    createNavDots();

    // Show low battery popup if needed
    if (showingLowBatteryPopup) {
        drawLowBatteryPopup();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  COMPASS CARD - PREMIUM STYLE (REFERENCE FOR OTHER CARDS)
// ═══════════════════════════════════════════════════════════════════════════════
void createCompassCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    // Full dark background like compass reference
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);

    // Compass tick marks around the edge
    int centerX = (LCD_WIDTH - 24) / 2;
    int centerY = (LCD_HEIGHT - 60) / 2;
    int outerRadius = min(centerX, centerY) - 20;

    // Draw tick marks
    for (int i = 0; i < 36; i++) {
        float angle = i * 10 * PI / 180.0;
        int tickLen = (i % 3 == 0) ? 10 : 5;
        
        int x1 = centerX + (int)((outerRadius - tickLen) * sin(angle));
        int y1 = centerY - (int)((outerRadius - tickLen) * cos(angle));
        int x2 = centerX + (int)(outerRadius * sin(angle));
        int y2 = centerY - (int)(outerRadius * cos(angle));

        lv_obj_t *tick = lv_obj_create(card);
        lv_obj_set_size(tick, 2, tickLen);
        lv_obj_align(tick, LV_ALIGN_TOP_LEFT, x1 - 1, y1);
        lv_obj_set_style_bg_color(tick, lv_color_hex(0x3A3A3C), 0);
        lv_obj_set_style_border_width(tick, 0, 0);
        lv_obj_set_style_radius(tick, 1, 0);
    }

    // Cardinal directions
    const char* cardinals[] = {"N", "E", "S", "W"};
    int cardinalOffsets[][2] = {{0, -outerRadius + 25}, {outerRadius - 25, 0}, {0, outerRadius - 25}, {-outerRadius + 25, 0}};
    lv_color_t cardinalColors[] = {lv_color_hex(0xFFFFFF), lv_color_hex(0x8E8E93), lv_color_hex(0xFFFFFF), lv_color_hex(0x8E8E93)};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, cardinals[i]);
        lv_obj_set_style_text_color(label, cardinalColors[i], 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, cardinalOffsets[i][0], cardinalOffsets[i][1]);
    }

    // Compass needle - blue/red like reference
    float needleAngle = compassHeadingSmooth * PI / 180.0;
    int needleLen = outerRadius - 50;

    // Red needle (North)
    lv_obj_t *redNeedle = lv_obj_create(card);
    lv_obj_set_size(redNeedle, 8, needleLen);
    lv_obj_align(redNeedle, LV_ALIGN_CENTER, 0, -needleLen/2);
    lv_obj_set_style_bg_color(redNeedle, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(redNeedle, 4, 0);
    lv_obj_set_style_border_width(redNeedle, 0, 0);
    lv_obj_set_style_transform_pivot_x(redNeedle, 4, 0);
    lv_obj_set_style_transform_pivot_y(redNeedle, needleLen, 0);
    lv_obj_set_style_transform_angle(redNeedle, (int)(-compassHeadingSmooth * 10), 0);

    // Blue needle (South)
    lv_obj_t *blueNeedle = lv_obj_create(card);
    lv_obj_set_size(blueNeedle, 8, needleLen);
    lv_obj_align(blueNeedle, LV_ALIGN_CENTER, 0, needleLen/2);
    lv_obj_set_style_bg_color(blueNeedle, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_radius(blueNeedle, 4, 0);
    lv_obj_set_style_border_width(blueNeedle, 0, 0);
    lv_obj_set_style_transform_pivot_x(blueNeedle, 4, 0);
    lv_obj_set_style_transform_pivot_y(blueNeedle, 0, 0);
    lv_obj_set_style_transform_angle(blueNeedle, (int)(-compassHeadingSmooth * 10), 0);

    // Center dot
    lv_obj_t *centerDot = lv_obj_create(card);
    lv_obj_set_size(centerDot, 16, 16);
    lv_obj_align(centerDot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(centerDot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(centerDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(centerDot, 0, 0);

    // Sunrise time (top)
    lv_obj_t *sunriseLabel = lv_label_create(card);
    lv_label_set_text(sunriseLabel, "SUNRISE");
    lv_obj_set_style_text_color(sunriseLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(sunriseLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(sunriseLabel, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *sunriseTime = lv_label_create(card);
    lv_label_set_text(sunriseTime, "05:39");
    lv_obj_set_style_text_color(sunriseTime, lv_color_hex(0xFFD166), 0);
    lv_obj_set_style_text_font(sunriseTime, &lv_font_montserrat_24, 0);
    lv_obj_align(sunriseTime, LV_ALIGN_CENTER, 0, -45);

    // Sunset time (bottom)
    lv_obj_t *sunsetTime = lv_label_create(card);
    lv_label_set_text(sunsetTime, "19:05");
    lv_obj_set_style_text_color(sunsetTime, lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_text_font(sunsetTime, &lv_font_montserrat_24, 0);
    lv_obj_align(sunsetTime, LV_ALIGN_CENTER, 0, 45);

    lv_obj_t *sunsetLabel = lv_label_create(card);
    lv_label_set_text(sunsetLabel, "SUNSET");
    lv_obj_set_style_text_color(sunsetLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(sunsetLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(sunsetLabel, LV_ALIGN_CENTER, 0, 70);

    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  CLOCK CARD - COMPASS STYLE
// ═══════════════════════════════════════════════════════════════════════════════
void createClockCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];

    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0A0A0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);

    // Get time from RTC
    RTC_DateTime dt = rtc.getDateTime();
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", dt.getHour(), dt.getMinute());

    // Large time display
    lv_obj_t *timeLabel = lv_label_create(card);
    lv_label_set_text(timeLabel, timeBuf);
    lv_obj_set_style_text_color(timeLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -40);

    // Seconds
    char secBuf[4];
    snprintf(secBuf, sizeof(secBuf), ":%02d", dt.getSecond());
    lv_obj_t *secLabel = lv_label_create(card);
    lv_label_set_text(secLabel, secBuf);
    lv_obj_set_style_text_color(secLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(secLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(secLabel, LV_ALIGN_CENTER, 85, -40);

    // Date
    const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char dateBuf[32];
    snprintf(dateBuf, sizeof(dateBuf), "%s, %s %d", 
             dayNames[dt.getWeek()], monthNames[dt.getMonth()-1], dt.getDay());

    lv_obj_t *dateLabel = lv_label_create(card);
    lv_label_set_text(dateLabel, dateBuf);
    lv_obj_set_style_text_color(dateLabel, theme.accent, 0);
    lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 20);

    // Steps summary
    char stepsBuf[32];
    snprintf(stepsBuf, sizeof(stepsBuf), "%lu steps", userData.steps);
    lv_obj_t *stepsLabel = lv_label_create(card);
    lv_label_set_text(stepsLabel, stepsBuf);
    lv_obj_set_style_text_color(stepsLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(stepsLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(stepsLabel, LV_ALIGN_CENTER, 0, 60);

    // Weather
    char weatherBuf[32];
    snprintf(weatherBuf, sizeof(weatherBuf), "%.0f°C %s", weatherTemp, weatherDesc.c_str());
    lv_obj_t *weatherLabel = lv_label_create(card);
    lv_label_set_text(weatherLabel, weatherBuf);
    lv_obj_set_style_text_color(weatherLabel, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(weatherLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(weatherLabel, LV_ALIGN_CENTER, 0, 85);

    createMiniStatusBar(card);
}

// Stub implementations for remaining cards (to be filled with compass-style design)
void createAnalogClockCard() { createCard("ANALOG CLOCK"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createTiltCard() { createCard("TILT"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createGyroCard() { createCard("GYRO"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createStepsCard() { 
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("STEPS");
    
    char stepsBuf[16];
    snprintf(stepsBuf, sizeof(stepsBuf), "%lu", userData.steps);
    
    lv_obj_t *stepsLabel = lv_label_create(card);
    lv_label_set_text(stepsLabel, stepsBuf);
    lv_obj_set_style_text_color(stepsLabel, theme.accent, 0);
    lv_obj_set_style_text_font(stepsLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(stepsLabel, LV_ALIGN_CENTER, 0, -20);
    
    char goalBuf[32];
    snprintf(goalBuf, sizeof(goalBuf), "Goal: %lu", userData.dailyGoal);
    lv_obj_t *goalLabel = lv_label_create(card);
    lv_label_set_text(goalLabel, goalBuf);
    lv_obj_set_style_text_color(goalLabel, lv_color_hex(0x636366), 0);
    lv_obj_align(goalLabel, LV_ALIGN_CENTER, 0, 30);
    
    createMiniStatusBar(card);
}
void createActivityRingsCard() { createCard("ACTIVITY"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createWorkoutCard() { createCard("WORKOUT"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createDistanceCard() { createCard("DISTANCE"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createBlackjackCard() { createCard("BLACKJACK"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createDinoCard() { createCard("DINO GAME"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createYesNoCard() { createCard("YES/NO"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createWeatherCard() { 
    lv_obj_t *card = createCard("WEATHER");
    
    char tempBuf[16];
    snprintf(tempBuf, sizeof(tempBuf), "%.0f°", weatherTemp);
    lv_obj_t *tempLabel = lv_label_create(card);
    lv_label_set_text(tempLabel, tempBuf);
    lv_obj_set_style_text_color(tempLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(tempLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(tempLabel, LV_ALIGN_CENTER, 0, -20);
    
    lv_obj_t *descLabel = lv_label_create(card);
    lv_label_set_text(descLabel, weatherDesc.c_str());
    lv_obj_set_style_text_color(descLabel, lv_color_hex(0x636366), 0);
    lv_obj_align(descLabel, LV_ALIGN_CENTER, 0, 30);
    
    createMiniStatusBar(card);
}
void createForecastCard() { createCard("FORECAST"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createStocksCard() { createCard("STOCKS"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createCryptoCard() { 
    lv_obj_t *card = createCard("CRYPTO");
    
    char btcBuf[24];
    snprintf(btcBuf, sizeof(btcBuf), "BTC: $%.0f", btcPrice);
    lv_obj_t *btcLabel = lv_label_create(card);
    lv_label_set_text(btcLabel, btcBuf);
    lv_obj_set_style_text_color(btcLabel, lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_text_font(btcLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(btcLabel, LV_ALIGN_CENTER, 0, -20);
    
    char ethBuf[24];
    snprintf(ethBuf, sizeof(ethBuf), "ETH: $%.0f", ethPrice);
    lv_obj_t *ethLabel = lv_label_create(card);
    lv_label_set_text(ethLabel, ethBuf);
    lv_obj_set_style_text_color(ethLabel, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_text_font(ethLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(ethLabel, LV_ALIGN_CENTER, 0, 20);
    
    createMiniStatusBar(card);
}
void createMusicCard() { createCard("MUSIC"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createGalleryCard() { createCard("GALLERY"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createSandTimerCard() { createCard("TIMER"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createStopwatchCard() { createCard("STOPWATCH"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createCountdownCard() { createCard("COUNTDOWN"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createBreatheCard() { createCard("BREATHE"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createStepStreakCard() { createCard("STREAK"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createGameStreakCard() { createCard("GAME STREAK"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createAchievementsCard() { createCard("ACHIEVEMENTS"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createCalendarCard() { createCard("CALENDAR"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createSettingsCard() { createCard("SETTINGS"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createBatteryCard() { 
    lv_obj_t *card = createCard("BATTERY");
    
    char percBuf[8];
    snprintf(percBuf, sizeof(percBuf), "%d%%", batteryPercent);
    lv_obj_t *percLabel = lv_label_create(card);
    lv_label_set_text(percLabel, percBuf);
    lv_obj_set_style_text_color(percLabel, batteryPercent > 20 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(percLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(percLabel, LV_ALIGN_CENTER, 0, -20);
    
    lv_obj_t *statusLabel = lv_label_create(card);
    lv_label_set_text(statusLabel, isCharging ? "Charging" : "Discharging");
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x636366), 0);
    lv_obj_align(statusLabel, LV_ALIGN_CENTER, 0, 30);
    
    createMiniStatusBar(card);
}
void createBatteryStatsCard() { createCard("BATTERY STATS"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createUsagePatternsCard() { createCard("USAGE"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }
void createFactoryResetCard() { createCard("RESET"); createMiniStatusBar(lv_obj_get_child(lv_scr_act(), 0)); }

// ═══════════════════════════════════════════════════════════════════════════════
//  STEP DETECTION
// ═══════════════════════════════════════════════════════════════════════════════
float prevMag = 1.0, prevPrevMag = 1.0;
unsigned long lastStepTime = 0;

void updateStepCount() {
    if (!hasIMU) return;

    float currentMag = sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);

    if (prevMag > prevPrevMag && prevMag > currentMag && prevMag > 1.3) {
        if (millis() - lastStepTime > 300) {
            userData.steps++;
            userData.totalDistance += 0.0007;
            userData.totalCalories += 0.04;
            lastStepTime = millis();

            RTC_DateTime dt = rtc.getDateTime();
            userData.stepHistory[dt.getWeek()] = userData.steps;
        }
    }

    prevPrevMag = prevMag;
    prevMag = currentMag;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  POWER BUTTON HANDLER - 5 SECONDS FOR SHUTDOWN
// ═══════════════════════════════════════════════════════════════════════════════
void handlePowerButton() {
    static bool lastButtonState = HIGH;
    static unsigned long lastDebounceTime = 0;

    bool buttonState = digitalRead(BOOT_BUTTON);

    // Debounce
    if (buttonState != lastButtonState) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > POWER_BUTTON_DEBOUNCE_MS) {
        // Button pressed (LOW = pressed)
        if (buttonState == LOW) {
            if (!powerButtonPressed) {
                powerButtonPressed = true;
                powerButtonPressStartMs = millis();
                powerButtonLongPressTriggered = false;
                showingShutdownProgress = false;
                USBSerial.println("[POWER] Button pressed");
            }
            
            // Check for long press (5 seconds)
            unsigned long pressDuration = millis() - powerButtonPressStartMs;
            
            // Show progress after 1 second of holding
            if (pressDuration > 1000 && !powerButtonLongPressTriggered) {
                float progress = (float)(pressDuration - 1000) / (float)(POWER_BUTTON_LONG_PRESS_MS - 1000);
                if (progress > 1.0) progress = 1.0;
                
                // TURN ON SCREEN if it's off - user needs to see shutdown progress!
                if (!screenOn) {
                    screenOn = true;
                    gfx->setBrightness(userData.brightness);
                    USBSerial.println("[POWER] Screen turned ON for shutdown display");
                }
                
                if (!showingShutdownProgress) {
                    showingShutdownProgress = true;
                }
                
                // Update progress display
                if (showingShutdownProgress) {
                    lv_obj_clean(lv_scr_act());
                    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
                    drawShutdownProgress(progress);
                    lv_task_handler();
                }
                
                // Trigger shutdown at 5 seconds
                if (pressDuration >= POWER_BUTTON_LONG_PRESS_MS) {
                    powerButtonLongPressTriggered = true;
                    USBSerial.println("[POWER] Long press (5s) - Shutting down");
                    shutdownDevice();
                }
            }
        }
        // Button released
        else if (buttonState == HIGH && powerButtonPressed) {
            unsigned long pressDuration = millis() - powerButtonPressStartMs;
            powerButtonPressed = false;
            showingShutdownProgress = false;

            // If long press wasn't triggered, this was a tap
            if (!powerButtonLongPressTriggered && pressDuration >= POWER_BUTTON_DEBOUNCE_MS) {
                USBSerial.println("[POWER] Short press - Toggle screen");
                if (screenOn) {
                    screenOff();
                } else {
                    screenOnFunc();
                }
            } else if (powerButtonLongPressTriggered == false && showingShutdownProgress) {
                // Cancelled long press, restore screen
                navigateTo(mainIndex, subIndex);
            }
            
            powerButtonLongPressTriggered = false;
        }
    }

    lastButtonState = buttonState;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
    USBSerial.begin(115200);
    delay(100);
    USBSerial.println("\n═══════════════════════════════════════════════════════");
    USBSerial.println("   S3 MiniOS v4.3.1 - ENHANCED UI EDITION (2.06 inch improvements ported)");
    USBSerial.println("═══════════════════════════════════════════════════════");
    USBSerial.println("");
    USBSerial.println("  NAVIGATION:");
    USBSerial.println("  • Swipe Left/Right = Change category (infinite)");
    USBSerial.println("  • Swipe Down = Go to sub-card");
    USBSerial.println("  • Swipe Up = Go back one level");
    USBSerial.println("");
    USBSerial.println("  POWER BUTTON:");
    USBSerial.println("  • TAP = Screen on/off");
    USBSerial.println("  • HOLD 5s = Shutdown (shows progress)");
    USBSerial.println("");
    USBSerial.println("═══════════════════════════════════════════════════════\n");

    // Initialize power button
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    USBSerial.println("[INIT] Power button configured (GPIO0) - 5s for shutdown");

    // Initialize I2C
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    // Initialize I/O expander
    if (expander.begin(0x20, &Wire)) {
        USBSerial.println("[OK] XCA9554 I/O Expander");
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

    // Initialize display
    gfx->begin();
    gfx->setBrightness(200);
    gfx->fillScreen(0x0000);
    USBSerial.println("[OK] Display (SH8601 368x448)");

    // Initialize touch
    if (FT3168->begin() == true) {
        USBSerial.println("[OK] Touch (FT3168)");
        FT3168->IIC_Write_Device_State(FT3168->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                       FT3168->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
    } else {
        USBSerial.println("[FAIL] Touch init");
    }

    // Initialize sensors
    if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_8G, SensorQMI8658::ACC_ODR_250Hz);
        qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS, SensorQMI8658::GYR_ODR_224_2Hz);
        qmi.enableAccelerometer();
        qmi.enableGyroscope();
        hasIMU = true;
        USBSerial.println("[OK] IMU (QMI8658)");
    }

    // Initialize RTC
    if (rtc.begin(Wire, IIC_SDA, IIC_SCL)) {
        hasRTC = true;
        USBSerial.println("[OK] RTC (PCF85063)");
    }

    // Initialize PMU
    if (power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        hasPMU = true;
        power.disableTSPinMeasure();
        power.enableBattVoltageMeasure();
        USBSerial.println("[OK] PMU (AXP2101)");
    }

    // Initialize SD card
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    if (SD_MMC.begin("/sdcard", true, true)) {
        hasSD = true;
        USBSerial.println("[OK] SD Card");
    }

    // Load user data
    loadUserData();

    // Connect WiFi with hardcoded credentials
    connectWiFi();

    if (wifiConnected) {
        // Sync time via NTP
        syncTimeNTP();

        // Fetch initial data
        fetchWeatherData();
        fetchCryptoData();
    }

    // Initialize LVGL
    lv_init();

    size_t buf_size = LCD_WIDTH * 50 * sizeof(lv_color_t);
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!buf1 || !buf2) {
        buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_WIDTH * 50);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    const esp_timer_create_args_t timer_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);
    esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000);

    lastActivityMs = millis();
    screenOnStartMs = millis();
    lastUsageUpdate = millis();
    batteryStats.sessionStartMs = millis();

    navigateTo(CAT_CLOCK, 0);

    USBSerial.println("\n[READY] S3 MiniOS v4.1 initialized!");
    USBSerial.printf("[INFO] WiFi: %s\n", WIFI_SSID);
    USBSerial.println("[INFO] Power button: TAP = screen toggle, HOLD 5s = shutdown\n");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
    // Handle power button first
    handlePowerButton();

    // Check for stuck transitions (auto-recovery)
    checkTransitionTimeout();

    // Handle LVGL tasks
    lv_task_handler();

    // Update sensors (50Hz)
    if (millis() - lastStepUpdate >= 20) {
        lastStepUpdate = millis();
        updateSensorFusion();
        updateStepCount();
    }

    // Update battery (every 3 seconds)
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

        if (screenOn && mainIndex == CAT_SYSTEM) {
            navigateTo(CAT_SYSTEM, subIndex);
        }
    }

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

    // Periodic NTP re-sync (every hour)
    if (wifiConnected && timeInitiallySynced && 
        millis() - lastNTPSyncMs >= NTP_RESYNC_INTERVAL_MS) {
        USBSerial.println("[NTP] Periodic re-sync");
        syncTimeNTP();
    }

    // Update weather (every 30 minutes)
    if (wifiConnected && millis() - lastWeatherUpdate >= 1800000) {
        fetchWeatherData();
        fetchCryptoData();
    }

    // Auto-save (every 2 hours)
    if (millis() - lastSaveTime >= SAVE_INTERVAL_MS) {
        saveUserData();
    }

    // Screen timeout
    unsigned long timeout = batterySaverMode ? SCREEN_OFF_TIMEOUT_SAVER_MS : SCREEN_OFF_TIMEOUT_MS;
    if (screenOn && !powerButtonPressed && millis() - lastActivityMs >= timeout) {
        screenOff();
    }

    // Clock refresh (every second when visible)
    if (screenOn && mainIndex == CAT_CLOCK) {
        static unsigned long lastClockRefresh = 0;
        if (millis() - lastClockRefresh >= 1000) {
            lastClockRefresh = millis();
            navigateTo(CAT_CLOCK, subIndex);
        }
    }

    // Compass refresh (100ms when visible)
    if (screenOn && mainIndex == CAT_COMPASS) {
        static unsigned long lastCompassRefresh = 0;
        if (millis() - lastCompassRefresh >= 100) {
            lastCompassRefresh = millis();
            navigateTo(CAT_COMPASS, subIndex);
        }
    }

    delay(5);
}
