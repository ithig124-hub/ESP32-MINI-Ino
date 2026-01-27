/**
 * ═══════════════════════════════════════════════════════════════════════════════
 *  S3 MiniOS v4.0 - ULTIMATE PREMIUM EDITION
 *  ESP32-S3-Touch-AMOLED-2.06" Smartwatch Firmware
 * ═══════════════════════════════════════════════════════════════════════════════
 * 
 *  MERGED FEATURES FROM v2.0 + v3.1:
 * 
 *  === PREMIUM LVGL UI (from v2.0) ===
 *  - Apple Watch-style gradient themes (8 themes)
 *  - Full sensor fusion compass with Kalman filter
 *  - Smooth animated navigation transitions
 *  - Premium Blackjack with visual cards
 *  - Dino runner game with physics
 *  - Yes/No spinner decision maker
 *  - Activity rings & workout tracking
 *  - Stocks & Crypto live prices
 *  - Step streak & achievements
 *  - SD card wallpaper support
 *  - Breathe wellness app
 * 
 *  === BATTERY INTELLIGENCE (from v3.1) ===
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
 *  Hardware: Waveshare ESP32-S3-Touch-AMOLED-2.06
 *    • Display: CO5300 QSPI AMOLED 412x412 (Round)
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
//  Fix for 'USBSerial' not declared error - handles different board configs
// ═══════════════════════════════════════════════════════════════════════════════
#if ARDUINO_USB_CDC_ON_BOOT
  // USB CDC is enabled at boot - USBSerial maps to Serial
  #define USBSerial Serial
#else
  // USB CDC not enabled at boot - create HWCDC instance
  #if !defined(USBSerial)
    HWCDC USBSerial;
  #endif
#endif

// ═══════════════════════════════════════════════════════════════════════════════
//  BOARD-SPECIFIC CONFIGURATION (2.06" CO5300 - Round Display)
//  Official Waveshare ESP32-S3-Touch-AMOLED-2.06 Board
//  Resolution: 410x502 (visible area, with rounded corners)
// ═══════════════════════════════════════════════════════════════════════════════
#define LCD_WIDTH       410
#define LCD_HEIGHT      502

// ═══════════════════════════════════════════════════════════════════════════════
//  WIFI & API CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY INTELLIGENCE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════════
#define SAVE_INTERVAL_MS 7200000UL  // 2 hours
#define SCREEN_OFF_TIMEOUT_MS 30000
#define SCREEN_OFF_TIMEOUT_SAVER_MS 10000

// Battery specs
#define BATTERY_CAPACITY_MAH 500
#define SCREEN_ON_CURRENT_MA 80
#define SCREEN_OFF_CURRENT_MA 15
#define SAVER_MODE_CURRENT_MA 40

// Low battery thresholds
#define LOW_BATTERY_WARNING 20
#define CRITICAL_BATTERY_WARNING 10

// Usage tracking
#define USAGE_HISTORY_SIZE 24
#define CARD_USAGE_SLOTS 12  // All categories

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
// Note: USBSerial is defined above with compatibility fix
Adafruit_XCA9554 expander;
SensorQMI8658 qmi;
SensorPCF85063 rtc;
XPowersPMU power;
IMUdata acc, gyr;
Preferences prefs;

// 2.06" CO5300 Round Display Driver
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

// ═══════════════════════════════════════════════════════════════════════════════
//  NAVIGATION SYSTEM
// ═══════════════════════════════════════════════════════════════════════════════
#define NUM_CATEGORIES 12
enum Category {
  CAT_CLOCK = 0, CAT_COMPASS, CAT_ACTIVITY, CAT_GAMES,
  CAT_WEATHER, CAT_STOCKS, CAT_MEDIA, CAT_TIMER,
  CAT_STREAK, CAT_CALENDAR, CAT_SETTINGS, CAT_SYSTEM
};

int currentCategory = CAT_CLOCK;
int currentSubCard = 0;
const int maxSubCards[] = {2, 3, 4, 3, 2, 2, 2, 4, 3, 1, 1, 3};  // System now has 3 sub-cards (battery stats, usage, reset)

// Animation state
bool isTransitioning = false;
int transitionDir = 0;
float transitionProgress = 0.0;
unsigned long transitionStartMs = 0;
const unsigned long TRANSITION_DURATION = 200;

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY INTELLIGENCE DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════════════════════
//  USER DATA (Persistent) - Combined from v2.0 + v3.1
// ═══════════════════════════════════════════════════════════════════════════════
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
} userData = {0, 10000, 7, 0.0, 0.0, {0}, 0, 0, 0, 0, 200, 1, 0, 0, 0};

// ═══════════════════════════════════════════════════════════════════════════════
//  RUNTIME STATE
// ═══════════════════════════════════════════════════════════════════════════════
bool screenOn = true;
unsigned long lastActivityMs = 0;
unsigned long screenOnStartMs = 0;
unsigned long screenOffStartMs = 0;

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

// ═══════════════════════════════════════════════════════════════════════════════
//  TOUCH STATE
// ═══════════════════════════════════════════════════════════════════════════════
int32_t touchStartX = 0, touchStartY = 0;
int32_t touchCurrentX = 0, touchCurrentY = 0;
bool touchActive = false;
unsigned long touchStartMs = 0;

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
//  PREMIUM GRADIENT THEMES (Apple Watch Style)
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
  {"None", lv_color_hex(0x1C1C1E), lv_color_hex(0x1C1C1E), lv_color_hex(0x1C1C1E), lv_color_hex(0x1C1C1E)},
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

// Battery Intelligence
void updateUsageTracking();
void updateHourlyStats();
void updateDailyStats();
void calculateBatteryEstimates();
void checkLowBattery();
void toggleBatterySaver();

// Screen control
void screenOff();
void screenOnFunc();
void shutdownDevice();

// Card creators
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
//  TOUCHPAD READ WITH SMOOTH GESTURE
// ═══════════════════════════════════════════════════════════════════════════════
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  int32_t touchX = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
  int32_t touchY = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

  if (FT3168->IIC_Interrupt_Flag) {
    FT3168->IIC_Interrupt_Flag = false;
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
    lastActivityMs = millis();
    
    if (!screenOn) {
      screenOnFunc();
      return;
    }

    if (!touchActive && !isTransitioning) {
      touchActive = true;
      touchStartX = touchX;
      touchStartY = touchY;
      touchStartMs = millis();
    }
    touchCurrentX = touchX;
    touchCurrentY = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
    if (touchActive && !isTransitioning) {
      touchActive = false;
      int dx = touchCurrentX - touchStartX;
      int dy = touchCurrentY - touchStartY;
      unsigned long duration = millis() - touchStartMs;
      
      float velocity = sqrt(dx*dx + dy*dy) / (float)duration;
      if (duration < 400 && velocity > 0.3 && (abs(dx) > 40 || abs(dy) > 40)) {
        handleSwipe(dx, dy);
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
//  NAVIGATION
// ═══════════════════════════════════════════════════════════════════════════════
void startTransition(int direction) {
  isTransitioning = true;
  transitionDir = direction;
  transitionProgress = 0.0;
  transitionStartMs = millis();
}

void handleSwipe(int dx, int dy) {
  // Dismiss low battery popup first
  if (showingLowBatteryPopup) {
    showingLowBatteryPopup = false;
    navigateTo(currentCategory, currentSubCard);
    return;
  }
  
  int newCategory = currentCategory;
  int newSubCard = currentSubCard;
  int direction = 0;
  
  if (abs(dx) > abs(dy)) {
    if (dx > 40) {
      newCategory = currentCategory - 1;
      if (newCategory < 0) newCategory = NUM_CATEGORIES - 1;
      direction = -1;
    } else if (dx < -40) {
      newCategory = (currentCategory + 1) % NUM_CATEGORIES;
      direction = 1;
    }
    newSubCard = 0;
  } else {
    if (dy > 40 && currentSubCard < maxSubCards[currentCategory] - 1) {
      newSubCard = currentSubCard + 1;
      direction = 2;
    } else if (dy < -40 && currentSubCard > 0) {
      newSubCard = 0;
      direction = -2;
    }
    newCategory = currentCategory;
  }
  
  if (direction != 0) {
    currentCategory = newCategory;
    currentSubCard = newSubCard;
    startTransition(direction);
    navigateTo(currentCategory, currentSubCard);
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
    USBSerial.println("[OK] Data saved");
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
//  SD CARD & WIFI
// ═══════════════════════════════════════════════════════════════════════════════
bool loadWiFiFromSD() {
    if (!hasSD) return false;
    
    File file = SD_MMC.open(WIFI_CONFIG_PATH, "r");
    if (!file) {
        USBSerial.println("No WiFi config on SD card");
        return false;
    }
    
    USBSerial.println("[INFO] Loading WiFi from SD...");
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
            USBSerial.printf("  Network %d: %s\n", numWifiNetworks + 1, tempSSID[i]);
            numWifiNetworks++;
        }
    }
    
    wifiConfigFromSD = (numWifiNetworks > 0);
    return wifiConfigFromSD;
}

void connectWiFi() {
    if (numWifiNetworks == 0) {
        USBSerial.println("[WARN] No WiFi networks configured");
        return;
    }
    
    WiFi.mode(WIFI_STA);
    
    for (int i = 0; i < numWifiNetworks; i++) {
        if (!wifiNetworks[i].valid) continue;
        
        USBSerial.printf("[INFO] Trying WiFi %d/%d: %s\n", i + 1, numWifiNetworks, wifiNetworks[i].ssid);
        WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 15) {
            delay(500);
            USBSerial.print(".");
            attempts++;
        }
        USBSerial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            connectedNetworkIndex = i;
            USBSerial.printf("[OK] Connected to: %s\n", wifiNetworks[i].ssid);
            return;
        } else {
            WiFi.disconnect();
            delay(100);
        }
    }
    
    USBSerial.println("[WARN] Could not connect to any WiFi");
    wifiConnected = false;
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

// ═══════════════════════════════════════════════════════════════════════════════
//  UI HELPERS
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
        lv_obj_set_style_bg_opa(card, LV_OPA_80, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
    }
    
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    
    if (strlen(title) > 0) {
        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_color(label, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 4, 0);
    }
    return card;
}

void createNavDots() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    // Bottom category dots
    lv_obj_t *container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(container, LCD_WIDTH, 24);
    lv_obj_align(container, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container, 8, 0);
    
    int start = currentCategory - 2;
    if (start < 0) start = 0;
    if (start > NUM_CATEGORIES - 5) start = NUM_CATEGORIES - 5;
    
    for (int i = start; i < start + 5 && i < NUM_CATEGORIES; i++) {
        lv_obj_t *dot = lv_obj_create(container);
        bool active = (i == currentCategory);
        int w = active ? 20 : 8;
        lv_obj_set_size(dot, w, 8);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_bg_color(dot, active ? theme.accent : lv_color_hex(0x48484A), 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    }
    
    // Right side sub-card dots
    if (maxSubCards[currentCategory] > 1) {
        lv_obj_t *subContainer = lv_obj_create(lv_scr_act());
        lv_obj_set_size(subContainer, 16, 80);
        lv_obj_align(subContainer, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_set_style_bg_opa(subContainer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(subContainer, 0, 0);
        lv_obj_set_flex_flow(subContainer, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(subContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(subContainer, 8, 0);
        
        for (int i = 0; i < maxSubCards[currentCategory]; i++) {
            lv_obj_t *dot = lv_obj_create(subContainer);
            bool active = (i == currentSubCard);
            int h = active ? 20 : 8;
            lv_obj_set_size(dot, 8, h);
            lv_obj_set_style_radius(dot, 4, 0);
            lv_obj_set_style_bg_color(dot, active ? theme.accent : lv_color_hex(0x48484A), 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        }
    }
}

void createMiniStatusBar(lv_obj_t* parent) {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    // Status bar container
    lv_obj_t *statusBar = lv_obj_create(parent);
    lv_obj_set_size(statusBar, LCD_WIDTH - 50, 28);
    lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, -12);
    lv_obj_set_style_bg_color(statusBar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(statusBar, LV_OPA_40, 0);
    lv_obj_set_style_radius(statusBar, 14, 0);
    lv_obj_set_style_border_width(statusBar, 0, 0);
    
    // WiFi indicator
    lv_obj_t *wifiIcon = lv_label_create(statusBar);
    lv_label_set_text(wifiIcon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifiIcon, wifiConnected ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(wifiIcon, &lv_font_montserrat_12, 0);
    lv_obj_align(wifiIcon, LV_ALIGN_LEFT_MID, 8, 0);
    
    // Battery saver indicator
    if (batterySaverMode) {
        lv_obj_t *saverIcon = lv_label_create(statusBar);
        lv_label_set_text(saverIcon, "S");
        lv_obj_set_style_text_color(saverIcon, lv_color_hex(0xFF9F0A), 0);
        lv_obj_set_style_text_font(saverIcon, &lv_font_montserrat_12, 0);
        lv_obj_align(saverIcon, LV_ALIGN_LEFT_MID, 28, 0);
    }
    
    // Battery estimate
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
        lv_obj_set_style_text_color(estLabel, lv_color_hex(0x8E8E93), 0);
    }
    lv_obj_set_style_text_font(estLabel, &lv_font_montserrat_12, 0);
    lv_obj_align(estLabel, LV_ALIGN_CENTER, 0, 0);
    
    // Battery percentage
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

// ═══════════════════════════════════════════════════════════════════════════════
//  MAIN NAVIGATION
// ═══════════════════════════════════════════════════════════════════════════════
void navigateTo(int category, int subCard) {
    lv_obj_clean(lv_scr_act());
    createGradientBg();
    
    // Update usage tracking
    updateUsageTracking();
    
    switch (category) {
        case CAT_CLOCK:
            if (subCard == 0) createClockCard();
            else createAnalogClockCard();
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
        case CAT_STOCKS:
            if (subCard == 0) createStocksCard();
            else createCryptoCard();
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
        case CAT_SETTINGS:
            createSettingsCard();
            break;
        case CAT_SYSTEM:
            if (subCard == 0) createBatteryCard();
            else if (subCard == 1) createBatteryStatsCard();
            else createUsagePatternsCard();
            break;
    }
    
    createNavDots();
    
    // Show low battery popup if needed
    if (showingLowBatteryPopup) {
        drawLowBatteryPopup();
    }
    
    isTransitioning = false;
}

// Due to file size limits, the card creation functions are in Part 2
// Include S3_MiniOS_Part2.ino in the same folder

// ═══════════════════════════════════════════════════════════════════════════════
//  S3 MiniOS v4.0 - PART 2: Card UI Functions
//  Place this file in the same folder as S3_MiniOS.ino
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
//  PREMIUM CLOCK CARD
// ═══════════════════════════════════════════════════════════════════════════════
void createClockCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
    // Time display
    RTC_DateTime dt = rtc.getDateTime();
    char timeBuf[10];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", dt.getHour(), dt.getMinute());
    
    lv_obj_t *clockLabel = lv_label_create(card);
    lv_label_set_text(clockLabel, timeBuf);
    lv_obj_set_style_text_color(clockLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(clockLabel, LV_ALIGN_CENTER, 0, -60);
    
    // Seconds
    char secBuf[8];
    snprintf(secBuf, sizeof(secBuf), ":%02d", dt.getSecond());
    lv_obj_t *secLabel = lv_label_create(card);
    lv_label_set_text(secLabel, secBuf);
    lv_obj_set_style_text_color(secLabel, theme.accent, 0);
    lv_obj_set_style_text_font(secLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(secLabel, LV_ALIGN_CENTER, 85, -55);
    
    // Day name
    const char* dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    lv_obj_t *dayLabel = lv_label_create(card);
    lv_label_set_text(dayLabel, dayNames[dt.getWeekday()]);
    lv_obj_set_style_text_color(dayLabel, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(dayLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(dayLabel, LV_ALIGN_CENTER, 0, 0);
    
    // Full date
    char dateBuf[32];
    const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    snprintf(dateBuf, sizeof(dateBuf), "%s %d, 20%02d", monthNames[dt.getMonth()-1], dt.getDay(), dt.getYear());
    lv_obj_t *dateLabel = lv_label_create(card);
    lv_label_set_text(dateLabel, dateBuf);
    lv_obj_set_style_text_color(dateLabel, theme.accent, 0);
    lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 30);
    
    // Status bar with battery estimate
    lv_obj_t *statusBar = lv_obj_create(card);
    lv_obj_set_size(statusBar, LCD_WIDTH - 60, 50);
    lv_obj_align(statusBar, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(statusBar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(statusBar, LV_OPA_50, 0);
    lv_obj_set_style_radius(statusBar, 25, 0);
    lv_obj_set_style_border_width(statusBar, 0, 0);
    lv_obj_set_flex_flow(statusBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(statusBar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // WiFi
    lv_obj_t *wifiIcon = lv_label_create(statusBar);
    lv_label_set_text(wifiIcon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifiIcon, wifiConnected ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    
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
    
    // Battery
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d%%", batteryPercent);
    lv_obj_t *battLabel = lv_label_create(statusBar);
    lv_label_set_text(battLabel, battBuf);
    lv_obj_set_style_text_color(battLabel, batteryPercent > 20 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    
    // Steps
    char stepBuf[16];
    snprintf(stepBuf, sizeof(stepBuf), "%lu", (unsigned long)userData.steps);
    lv_obj_t *stepLabel = lv_label_create(statusBar);
    lv_label_set_text(stepLabel, stepBuf);
    lv_obj_set_style_text_color(stepLabel, theme.accent, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  ANALOG CLOCK CARD
// ═══════════════════════════════════════════════════════════════════════════════
void createAnalogClockCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("");
    
    // Clock face
    lv_obj_t *face = lv_obj_create(card);
    lv_obj_set_size(face, 260, 260);
    lv_obj_align(face, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(face, theme.accent, 0);
    lv_obj_set_style_border_width(face, 3, 0);
    lv_obj_set_style_shadow_width(face, 30, 0);
    lv_obj_set_style_shadow_color(face, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(face, LV_OPA_40, 0);
    
    // Hour markers
    for (int i = 0; i < 12; i++) {
        float angle = i * 30.0 * 3.14159 / 180.0;
        int len = (i % 3 == 0) ? 15 : 8;
        int r = 115;
        
        lv_obj_t *marker = lv_obj_create(face);
        lv_obj_set_size(marker, (i % 3 == 0) ? 4 : 2, len);
        int x = sin(angle) * r;
        int y = -cos(angle) * r;
        lv_obj_align(marker, LV_ALIGN_CENTER, x, y);
        lv_obj_set_style_bg_color(marker, (i % 3 == 0) ? theme.text : lv_color_hex(0x636366), 0);
        lv_obj_set_style_radius(marker, 1, 0);
        lv_obj_set_style_border_width(marker, 0, 0);
    }
    
    RTC_DateTime dt = rtc.getDateTime();
    
    // Hour hand
    lv_obj_t *hHand = lv_obj_create(face);
    lv_obj_set_size(hHand, 8, 65);
    lv_obj_align(hHand, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_style_bg_color(hHand, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(hHand, 4, 0);
    lv_obj_set_style_border_width(hHand, 0, 0);
    
    // Minute hand
    lv_obj_t *mHand = lv_obj_create(face);
    lv_obj_set_size(mHand, 6, 90);
    lv_obj_align(mHand, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_color(mHand, theme.text, 0);
    lv_obj_set_style_radius(mHand, 3, 0);
    lv_obj_set_style_border_width(mHand, 0, 0);
    
    // Center cap
    lv_obj_t *center = lv_obj_create(face);
    lv_obj_set_size(center, 18, 18);
    lv_obj_align(center, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(center, theme.accent, 0);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  COMPASS CARD (Sensor Fusion)
// ═══════════════════════════════════════════════════════════════════════════════
void createCompassCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0C0C0E), 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "COMPASS");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    
    // Compass rose
    lv_obj_t *rose = lv_obj_create(card);
    lv_obj_set_size(rose, 260, 260);
    lv_obj_align(rose, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_opa(rose, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rose, 0, 0);
    
    // Outer ring
    lv_obj_t *ring = lv_obj_create(rose);
    lv_obj_set_size(ring, 250, 250);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    
    // Cardinal directions
    const char* dirs[] = {"N", "E", "S", "W"};
    lv_color_t dirColors[] = {lv_color_hex(0xFF453A), lv_color_hex(0xFFFFFF), lv_color_hex(0xFFFFFF), lv_color_hex(0xFFFFFF)};
    
    for (int i = 0; i < 4; i++) {
        float baseAngle = i * 90.0;
        float angle = (baseAngle - compassHeadingSmooth) * 3.14159 / 180.0;
        int r = 95;
        int x = sin(angle) * r;
        int y = -cos(angle) * r;
        
        lv_obj_t *lbl = lv_label_create(rose);
        lv_label_set_text(lbl, dirs[i]);
        lv_obj_set_style_text_color(lbl, dirColors[i], 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, x, y);
    }
    
    // Needle - North (red)
    lv_obj_t *needleN = lv_obj_create(rose);
    lv_obj_set_size(needleN, 12, 70);
    lv_obj_align(needleN, LV_ALIGN_CENTER, 0, -35);
    lv_obj_set_style_bg_color(needleN, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(needleN, 6, 0);
    lv_obj_set_style_border_width(needleN, 0, 0);
    lv_obj_set_style_shadow_width(needleN, 10, 0);
    lv_obj_set_style_shadow_color(needleN, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_shadow_opa(needleN, LV_OPA_50, 0);
    
    // Needle - South (blue)
    lv_obj_t *needleS = lv_obj_create(rose);
    lv_obj_set_size(needleS, 12, 70);
    lv_obj_align(needleS, LV_ALIGN_CENTER, 0, 35);
    lv_obj_set_style_bg_color(needleS, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_radius(needleS, 6, 0);
    lv_obj_set_style_border_width(needleS, 0, 0);
    
    // Center pivot
    lv_obj_t *pivot = lv_obj_create(rose);
    lv_obj_set_size(pivot, 24, 24);
    lv_obj_align(pivot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(pivot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(pivot, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_border_width(pivot, 3, 0);
    
    // Heading display
    int headingInt = (int)compassHeadingSmooth;
    if (headingInt < 0) headingInt += 360;
    
    char headingBuf[16];
    snprintf(headingBuf, sizeof(headingBuf), "%d°", headingInt);
    lv_obj_t *headingLbl = lv_label_create(card);
    lv_label_set_text(headingLbl, headingBuf);
    lv_obj_set_style_text_color(headingLbl, theme.text, 0);
    lv_obj_set_style_text_font(headingLbl, &lv_font_montserrat_36, 0);
    lv_obj_align(headingLbl, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  TILT/LEVEL CARD
// ═══════════════════════════════════════════════════════════════════════════════
void createTiltCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0C0C0E), 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "LEVEL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    
    // Level bubble container
    lv_obj_t *levelBg = lv_obj_create(card);
    lv_obj_set_size(levelBg, 220, 220);
    lv_obj_align(levelBg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(levelBg, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(levelBg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(levelBg, lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_border_width(levelBg, 2, 0);
    
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
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  GYRO ROTATION CARD
// ═══════════════════════════════════════════════════════════════════════════════
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
    
    // Legend
    const char* labels[] = {"Pitch", "Roll", "Yaw"};
    lv_color_t colors[] = {lv_color_hex(0xFF453A), lv_color_hex(0x30D158), lv_color_hex(0x0A84FF)};
    float values[] = {pitch, roll, yaw};
    
    for (int i = 0; i < 3; i++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%s: %.0f°", labels[i], values[i]);
        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, colors[i], 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 20 + i * 100, -20);
    }
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  STEPS CARD (Premium)
// ═══════════════════════════════════════════════════════════════════════════════
void createStepsCard() {
    // Full gradient card
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x5856D6), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x007AFF), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x5856D6), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    
    // Badge
    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_set_size(badge, 100, 28);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
    lv_obj_set_style_radius(badge, 14, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    
    lv_obj_t *badgeLbl = lv_label_create(badge);
    lv_label_set_text(badgeLbl, LV_SYMBOL_CHARGE " Activity");
    lv_obj_set_style_text_color(badgeLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(badgeLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(badgeLbl);
    
    // Steps label
    lv_obj_t *stepsLabel = lv_label_create(card);
    lv_label_set_text(stepsLabel, "Steps:");
    lv_obj_set_style_text_color(stepsLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(stepsLabel, LV_OPA_70, 0);
    lv_obj_set_style_text_font(stepsLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(stepsLabel, LV_ALIGN_CENTER, 0, -80);
    
    // Step count
    char stepBuf[16];
    snprintf(stepBuf, sizeof(stepBuf), "%lu", (unsigned long)userData.steps);
    lv_obj_t *stepCount = lv_label_create(card);
    lv_label_set_text(stepCount, stepBuf);
    lv_obj_set_style_text_color(stepCount, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(stepCount, &lv_font_montserrat_48, 0);
    lv_obj_align(stepCount, LV_ALIGN_CENTER, 0, -30);
    
    // Progress bar
    int progress = (userData.steps * 100) / userData.dailyGoal;
    if (progress > 100) progress = 100;
    
    lv_obj_t *progBg = lv_obj_create(card);
    lv_obj_set_size(progBg, LCD_WIDTH - 80, 12);
    lv_obj_align(progBg, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(progBg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(progBg, LV_OPA_30, 0);
    lv_obj_set_style_radius(progBg, 6, 0);
    lv_obj_set_style_border_width(progBg, 0, 0);
    
    int fillWidth = ((LCD_WIDTH - 84) * progress) / 100;
    if (fillWidth > 4) {
        lv_obj_t *progFill = lv_obj_create(progBg);
        lv_obj_set_size(progFill, fillWidth, 8);
        lv_obj_align(progFill, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_set_style_bg_color(progFill, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(progFill, 4, 0);
        lv_obj_set_style_border_width(progFill, 0, 0);
    }
    
    // Milestones
    int milestones[] = {2000, 4000, 6000, 8000};
    lv_obj_t *milestoneContainer = lv_obj_create(card);
    lv_obj_set_size(milestoneContainer, LCD_WIDTH - 60, 40);
    lv_obj_align(milestoneContainer, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_bg_opa(milestoneContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(milestoneContainer, 0, 0);
    lv_obj_set_flex_flow(milestoneContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(milestoneContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    for (int i = 0; i < 4; i++) {
        lv_obj_t *milestone = lv_obj_create(milestoneContainer);
        lv_obj_set_size(milestone, 60, 36);
        lv_obj_set_style_bg_opa(milestone, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(milestone, 0, 0);
        
        char mBuf[8];
        snprintf(mBuf, sizeof(mBuf), "%d", milestones[i]);
        lv_obj_t *mLbl = lv_label_create(milestone);
        lv_label_set_text(mLbl, mBuf);
        bool reached = userData.steps >= (uint32_t)milestones[i];
        lv_obj_set_style_text_color(mLbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_opa(mLbl, reached ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_set_style_text_font(mLbl, &lv_font_montserrat_14, 0);
        lv_obj_align(mLbl, LV_ALIGN_TOP_MID, 0, 0);
        
        lv_obj_t *dot = lv_obj_create(milestone);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(dot, reached ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x666688), 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    }
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  ACTIVITY RINGS CARD
// ═══════════════════════════════════════════════════════════════════════════════
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
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WORKOUT & DISTANCE CARDS (Placeholders)
// ═══════════════════════════════════════════════════════════════════════════════
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
    
    createMiniStatusBar(card);
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
    
    createMiniStatusBar(card);
}

// Continue in Part 3...
// ═══════════════════════════════════════════════════════════════════════════════
//  S3 MiniOS v4.0 - PART 3: Games, Weather, Media, Timer Cards
//  Place this file in the same folder as S3_MiniOS.ino
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
//  BLACKJACK CARD (Premium)
// ═══════════════════════════════════════════════════════════════════════════════
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
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1B4332), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x2D6A4F), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
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

// ═══════════════════════════════════════════════════════════════════════════════
//  DINO GAME CARD
// ═══════════════════════════════════════════════════════════════════════════════
void dinoJumpCb(lv_event_t *e);

void createDinoCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "DINO RUN");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 12);
    
    // Score
    char sBuf[16];
    snprintf(sBuf, sizeof(sBuf), "%d", dinoScore);
    lv_obj_t *scoreBg = lv_obj_create(card);
    lv_obj_set_size(scoreBg, 70, 28);
    lv_obj_align(scoreBg, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_obj_set_style_bg_color(scoreBg, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_bg_opa(scoreBg, LV_OPA_20, 0);
    lv_obj_set_style_radius(scoreBg, 14, 0);
    lv_obj_set_style_border_width(scoreBg, 0, 0);
    
    lv_obj_t *scoreLbl = lv_label_create(scoreBg);
    lv_label_set_text(scoreLbl, sBuf);
    lv_obj_set_style_text_color(scoreLbl, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_text_font(scoreLbl, &lv_font_montserrat_16, 0);
    lv_obj_center(scoreLbl);
    
    // Game area
    lv_obj_t *gameArea = lv_obj_create(card);
    lv_obj_set_size(gameArea, LCD_WIDTH - 50, 170);
    lv_obj_align(gameArea, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(gameArea, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(gameArea, 20, 0);
    lv_obj_set_style_border_width(gameArea, 0, 0);
    
    // Ground
    lv_obj_t *ground = lv_obj_create(gameArea);
    lv_obj_set_size(ground, LCD_WIDTH - 70, 3);
    lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(ground, lv_color_hex(0x636366), 0);
    lv_obj_set_style_radius(ground, 1, 0);
    lv_obj_set_style_border_width(ground, 0, 0);
    
    // Dino
    lv_obj_t *dino = lv_obj_create(gameArea);
    lv_obj_set_size(dino, 32, 45);
    lv_obj_align(dino, LV_ALIGN_BOTTOM_LEFT, 35, -23 + dinoY);
    lv_obj_set_style_bg_color(dino, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_radius(dino, 8, 0);
    lv_obj_set_style_border_width(dino, 0, 0);
    lv_obj_set_style_shadow_width(dino, 10, 0);
    lv_obj_set_style_shadow_color(dino, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_shadow_opa(dino, LV_OPA_40, 0);
    
    // Obstacle
    lv_obj_t *obs = lv_obj_create(gameArea);
    lv_obj_set_size(obs, 22, 40);
    lv_obj_align(obs, LV_ALIGN_BOTTOM_LEFT, obstacleX, -23);
    lv_obj_set_style_bg_color(obs, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(obs, 6, 0);
    lv_obj_set_style_border_width(obs, 0, 0);
    
    // Jump button
    lv_obj_t *jumpBtn = lv_btn_create(card);
    lv_obj_set_size(jumpBtn, 130, 45);
    lv_obj_align(jumpBtn, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_color(jumpBtn, dinoGameOver ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x30D158), 0);
    lv_obj_set_style_radius(jumpBtn, 22, 0);
    lv_obj_add_event_cb(jumpBtn, dinoJumpCb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *jLbl = lv_label_create(jumpBtn);
    lv_label_set_text(jLbl, dinoGameOver ? "RESTART" : "JUMP!");
    lv_obj_set_style_text_color(jLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(jLbl);
    
    if (dinoGameOver) {
        lv_obj_t *overlay = lv_obj_create(gameArea);
        lv_obj_set_size(overlay, LCD_WIDTH - 80, 70);
        lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
        lv_obj_set_style_radius(overlay, 16, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        
        lv_obj_t *goLbl = lv_label_create(overlay);
        lv_label_set_text(goLbl, "GAME OVER");
        lv_obj_set_style_text_color(goLbl, lv_color_hex(0xFF453A), 0);
        lv_obj_set_style_text_font(goLbl, &lv_font_montserrat_20, 0);
        lv_obj_center(goLbl);
    }
}

void dinoJumpCb(lv_event_t *e) {
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

// ═══════════════════════════════════════════════════════════════════════════════
//  YES/NO SPINNER CARD
// ═══════════════════════════════════════════════════════════════════════════════
void yesNoSpinCb(lv_event_t *e);

void createYesNoCard() {
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xC41E3A), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x9B2335), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
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

// ═══════════════════════════════════════════════════════════════════════════════
//  WEATHER CARD
// ═══════════════════════════════════════════════════════════════════════════════
void createWeatherCard() {
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A759F), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x34A0A4), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
    // Location
    char locBuf[32];
    snprintf(locBuf, sizeof(locBuf), "%s, %s", weatherCity, weatherCountry);
    lv_obj_t *locLbl = lv_label_create(card);
    lv_label_set_text(locLbl, locBuf);
    lv_obj_set_style_text_color(locLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(locLbl, LV_OPA_80, 0);
    lv_obj_set_style_text_font(locLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(locLbl, LV_ALIGN_TOP_MID, 0, 20);
    
    // Sun icon
    lv_obj_t *iconBg = lv_obj_create(card);
    lv_obj_set_size(iconBg, 70, 70);
    lv_obj_align(iconBg, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(iconBg, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_radius(iconBg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(iconBg, 0, 0);
    lv_obj_set_style_shadow_width(iconBg, 20, 0);
    lv_obj_set_style_shadow_color(iconBg, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_shadow_opa(iconBg, LV_OPA_50, 0);
    
    // Temperature
    char tempBuf[16];
    snprintf(tempBuf, sizeof(tempBuf), "%.0f°", weatherTemp);
    lv_obj_t *tempLbl = lv_label_create(card);
    lv_label_set_text(tempLbl, tempBuf);
    lv_obj_set_style_text_color(tempLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(tempLbl, &lv_font_montserrat_48, 0);
    lv_obj_align(tempLbl, LV_ALIGN_CENTER, 0, 40);
    
    // Description
    lv_obj_t *descLbl = lv_label_create(card);
    lv_label_set_text(descLbl, weatherDesc.c_str());
    lv_obj_set_style_text_color(descLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(descLbl, LV_OPA_80, 0);
    lv_obj_set_style_text_font(tempLbl, &lv_font_montserrat_18, 0);
    lv_obj_align(descLbl, LV_ALIGN_CENTER, 0, 90);
    
    // High/Low
    char hlBuf[32];
    snprintf(hlBuf, sizeof(hlBuf), "H: %.0f°   L: %.0f°", weatherHigh, weatherLow);
    lv_obj_t *hlLbl = lv_label_create(card);
    lv_label_set_text(hlLbl, hlBuf);
    lv_obj_set_style_text_color(hlLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(hlLbl, LV_OPA_70, 0);
    lv_obj_align(hlLbl, LV_ALIGN_BOTTOM_MID, 0, -30);
    
    createMiniStatusBar(card);
}

void createForecastCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("3-DAY FORECAST");
    
    const char* days[] = {"SAT", "SUN", "MON"};
    int temps[] = {24, 26, 23};
    
    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, LCD_WIDTH - 70, 70);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 35 + i * 80);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        
        lv_obj_t *dayLbl = lv_label_create(row);
        lv_label_set_text(dayLbl, days[i]);
        lv_obj_set_style_text_color(dayLbl, theme.text, 0);
        lv_obj_set_style_text_font(dayLbl, &lv_font_montserrat_16, 0);
        lv_obj_align(dayLbl, LV_ALIGN_LEFT_MID, 15, 0);
        
        char tBuf[8];
        snprintf(tBuf, sizeof(tBuf), "%d°", temps[i]);
        lv_obj_t *tLbl = lv_label_create(row);
        lv_label_set_text(tLbl, tBuf);
        lv_obj_set_style_text_color(tLbl, theme.accent, 0);
        lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_24, 0);
        lv_obj_align(tLbl, LV_ALIGN_RIGHT_MID, -15, 0);
    }
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  STOCKS & CRYPTO CARDS
// ═══════════════════════════════════════════════════════════════════════════════
void createStocksCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("STOCKS");
    
    const char* syms[] = {"AAPL", "TSLA", "NVDA"};
    float prices[] = {aaplPrice > 0 ? aaplPrice : 178.5f, tslaPrice > 0 ? tslaPrice : 248.2f, 875.3f};
    float changes[] = {2.3, -1.5, 4.2};
    
    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, LCD_WIDTH - 70, 65);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 30 + i * 75);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        
        lv_obj_t *symLbl = lv_label_create(row);
        lv_label_set_text(symLbl, syms[i]);
        lv_obj_set_style_text_color(symLbl, theme.text, 0);
        lv_obj_set_style_text_font(symLbl, &lv_font_montserrat_16, 0);
        lv_obj_align(symLbl, LV_ALIGN_LEFT_MID, 15, 0);
        
        char pBuf[16];
        snprintf(pBuf, sizeof(pBuf), "$%.2f", prices[i]);
        lv_obj_t *pLbl = lv_label_create(row);
        lv_label_set_text(pLbl, pBuf);
        lv_obj_set_style_text_color(pLbl, theme.text, 0);
        lv_obj_align(pLbl, LV_ALIGN_RIGHT_MID, -70, 0);
        
        char cBuf[16];
        snprintf(cBuf, sizeof(cBuf), "%+.1f%%", changes[i]);
        lv_obj_t *cLbl = lv_label_create(row);
        lv_label_set_text(cLbl, cBuf);
        lv_obj_set_style_text_color(cLbl, changes[i] >= 0 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
        lv_obj_align(cLbl, LV_ALIGN_RIGHT_MID, -10, 0);
    }
    
    createMiniStatusBar(card);
}

void createCryptoCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("CRYPTO");
    
    const char* syms[] = {"BTC", "ETH", "SOL"};
    float prices[] = {btcPrice > 0 ? btcPrice : 67432.5f, ethPrice > 0 ? ethPrice : 3521.8f, 142.3f};
    lv_color_t colors[] = {lv_color_hex(0xF7931A), lv_color_hex(0x627EEA), lv_color_hex(0x00FFA3)};
    
    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, LCD_WIDTH - 70, 65);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 30 + i * 75);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        
        lv_obj_t *accent = lv_obj_create(row);
        lv_obj_set_size(accent, 4, 40);
        lv_obj_align(accent, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_set_style_bg_color(accent, colors[i], 0);
        lv_obj_set_style_radius(accent, 2, 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        
        lv_obj_t *symLbl = lv_label_create(row);
        lv_label_set_text(symLbl, syms[i]);
        lv_obj_set_style_text_color(symLbl, theme.text, 0);
        lv_obj_set_style_text_font(symLbl, &lv_font_montserrat_16, 0);
        lv_obj_align(symLbl, LV_ALIGN_LEFT_MID, 20, 0);
        
        char pBuf[16];
        if (prices[i] >= 1000) snprintf(pBuf, sizeof(pBuf), "$%.0f", prices[i]);
        else snprintf(pBuf, sizeof(pBuf), "$%.2f", prices[i]);
        lv_obj_t *pLbl = lv_label_create(row);
        lv_label_set_text(pLbl, pBuf);
        lv_obj_set_style_text_color(pLbl, theme.text, 0);
        lv_obj_align(pLbl, LV_ALIGN_RIGHT_MID, -15, 0);
    }
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MUSIC & GALLERY CARDS
// ═══════════════════════════════════════════════════════════════════════════════
void musicPlayCb(lv_event_t *e);

void createMusicCard() {
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF81F), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x5856D6), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
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
    
    createMiniStatusBar(card);
}

void musicPlayCb(lv_event_t *e) {
    musicPlaying = !musicPlaying;
    navigateTo(CAT_MEDIA, 0);
}

void createGalleryCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("GALLERY");
    
    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_color(icon, theme.accent, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -20);
    
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, "No photos yet");
    lv_obj_set_style_text_color(label, lv_color_hex(0x8E8E93), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);
    
    createMiniStatusBar(card);
}

// Continue in Part 4...
// ═══════════════════════════════════════════════════════════════════════════════
//  S3 MiniOS v4.0 - PART 4: Timer, Streak, Calendar, System, Battery Stats Cards
//  Place this file in the same folder as S3_MiniOS.ino
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
//  TIMER CARDS
// ═══════════════════════════════════════════════════════════════════════════════
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
    
    createMiniStatusBar(card);
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
    
    createMiniStatusBar(card);
}

void stopwatchToggleCb(lv_event_t *e) {
    if (stopwatchRunning) {
        stopwatchElapsedMs += millis() - stopwatchStartMs;
        stopwatchRunning = false;
    } else {
        stopwatchRunning = true;
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
    
    createMiniStatusBar(card);
}

void createBreatheCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x00A896), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x02C39A), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
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
    
    createMiniStatusBar(card);
}

void breatheStartCb(lv_event_t *e) {
    breatheRunning = !breatheRunning;
    if (breatheRunning) breatheStartMs = millis();
    navigateTo(CAT_TIMER, 3);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  STREAK & ACHIEVEMENTS CARDS
// ═══════════════════════════════════════════════════════════════════════════════
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
    
    createMiniStatusBar(card);
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
    
    createMiniStatusBar(card);
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
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  CALENDAR CARD
// ═══════════════════════════════════════════════════════════════════════════════
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
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETTINGS CARD
// ═══════════════════════════════════════════════════════════════════════════════
void themeChangeCb(lv_event_t *e);
void brightnessChangeCb(lv_event_t *e);

void createSettingsCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("SETTINGS");
    
    // Theme selector
    lv_obj_t *themeRow = lv_obj_create(card);
    lv_obj_set_size(themeRow, LCD_WIDTH - 70, 60);
    lv_obj_align(themeRow, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(themeRow, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(themeRow, 14, 0);
    lv_obj_set_style_border_width(themeRow, 0, 0);
    
    lv_obj_t *themeLbl = lv_label_create(themeRow);
    lv_label_set_text(themeLbl, "Theme");
    lv_obj_set_style_text_color(themeLbl, theme.text, 0);
    lv_obj_align(themeLbl, LV_ALIGN_LEFT_MID, 15, 0);
    
    lv_obj_t *themeValLbl = lv_label_create(themeRow);
    lv_label_set_text(themeValLbl, gradientThemes[userData.themeIndex].name);
    lv_obj_set_style_text_color(themeValLbl, theme.accent, 0);
    lv_obj_align(themeValLbl, LV_ALIGN_RIGHT_MID, -15, 0);
    
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
    
    createMiniStatusBar(card);
}

void brightnessChangeCb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    userData.brightness = lv_slider_get_value(slider);
    if (!batterySaverMode) gfx->setBrightness(userData.brightness);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY CARD (Main System Screen)
// ═══════════════════════════════════════════════════════════════════════════════
void batterySaverToggleCb(lv_event_t *e);

void createBatteryCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
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
    
    // WiFi status
    char wifiBuf[48];
    if (wifiConnected && connectedNetworkIndex >= 0) {
        snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %s", wifiNetworks[connectedNetworkIndex].ssid);
    } else {
        snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: Disconnected");
    }
    lv_obj_t *wifiLbl = lv_label_create(infoRow);
    lv_label_set_text(wifiLbl, wifiBuf);
    lv_obj_set_style_text_color(wifiLbl, wifiConnected ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
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

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY STATS SUB-CARD (24h graphs, estimates)
// ═══════════════════════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════════════════════
//  USAGE PATTERNS SUB-CARD
// ═══════════════════════════════════════════════════════════════════════════════
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
        
        bool isToday = (i == dt.getWeekday());
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

// ═══════════════════════════════════════════════════════════════════════════════
//  Continue to Part 5 (Setup & Loop)...
// ═══════════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════════
//  S3 MiniOS v4.0 - PART 5: Setup & Loop
//  Place this file in the same folder as S3_MiniOS.ino
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
//  STEP DETECTION
// ═══════════════════════════════════════════════════════════════════════════════
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
            userData.stepHistory[dt.getWeekday()] = userData.steps;
        }
    }
    
    prevPrevMag = prevMag;
    prevMag = currentMag;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DINO GAME UPDATE
// ═══════════════════════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════════════════════
//  YES/NO SPINNER UPDATE
// ═══════════════════════════════════════════════════════════════════════════════
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

// ═══════════════════════════════════════════════════════════════════════════════
//  CHARGING ANIMATION
// ═══════════════════════════════════════════════════════════════════════════════
void updateChargingAnimation() {
    if (!isCharging) return;
    
    if (millis() - lastChargingAnimMs > 500) {
        chargingAnimFrame = (chargingAnimFrame + 1) % 5;
        lastChargingAnimMs = millis();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
    USBSerial.begin(115200);
    delay(100);
    USBSerial.println("\n═══════════════════════════════════════════════════════");
    USBSerial.println("   S3 MiniOS v4.0 - ULTIMATE PREMIUM EDITION");
    USBSerial.println("   LVGL UI + Battery Intelligence (2.06\" Round)");
    USBSerial.println("═══════════════════════════════════════════════════════\n");
    
    // Initialize I2C
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);
    
    // Initialize I/O expander (2.06" board)
    if (expander.begin(0x20, &Wire)) {
        USBSerial.println("[OK] XCA9554 I/O Expander");
        expander.pinMode(0, OUTPUT);  // Touch reset
        expander.pinMode(1, OUTPUT);  // LCD reset
        expander.pinMode(2, OUTPUT);  // Audio PA
        
        // Reset sequence
        expander.digitalWrite(0, LOW);
        expander.digitalWrite(1, LOW);
        expander.digitalWrite(2, LOW);
        delay(20);
        expander.digitalWrite(0, HIGH);
        expander.digitalWrite(1, HIGH);
        expander.digitalWrite(2, HIGH);
        delay(50);
    } else {
        USBSerial.println("[WARN] XCA9554 not found");
    }
    
    // Initialize display
    gfx->begin();
    gfx->setBrightness(200);
    gfx->fillScreen(0x0000);
    USBSerial.println("[OK] Display (CO5300 412x412 Round)");
    
    // Initialize touch
    if (FT3168->IIC_Device_Initialization() == true) {
        USBSerial.println("[OK] Touch (FT3168)");
        FT3168->IIC_Write_Device_State(FT3168->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                       FT3168->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
    } else {
        USBSerial.println("[FAIL] Touch init");
    }
    
    // Initialize sensors
    if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_8G, SensorQMI8658::ACC_ODR_250Hz);
        qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS, SensorQMI8658::GYR_ODR_250Hz);
        qmi.enableAccelerometer();
        qmi.enableGyroscope();
        hasIMU = true;
        USBSerial.println("[OK] IMU (QMI8658)");
    } else {
        USBSerial.println("[WARN] IMU not found");
    }
    
    // Initialize RTC
    if (rtc.begin(Wire, PCF85063_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        hasRTC = true;
        USBSerial.println("[OK] RTC (PCF85063)");
    } else {
        USBSerial.println("[WARN] RTC not found");
    }
    
    // Initialize PMU
    if (power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        hasPMU = true;
        power.disableTSPinMeasure();
        power.enableBattVoltageMeasure();
        USBSerial.println("[OK] PMU (AXP2101)");
    } else {
        USBSerial.println("[WARN] PMU not found");
    }
    
    // Initialize SD card
    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    if (SD_MMC.begin("/sdcard", true, true)) {
        hasSD = true;
        USBSerial.println("[OK] SD Card");
    } else {
        USBSerial.println("[WARN] SD Card not found");
    }
    
    // Load user data
    loadUserData();
    
    // Load WiFi from SD and connect
    if (loadWiFiFromSD()) {
        USBSerial.printf("[INFO] Loaded %d WiFi networks\n", numWifiNetworks);
        connectWiFi();
        
        if (wifiConnected) {
            // Sync time
            configTime(gmtOffsetSec, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
            
            // Fetch initial data
            fetchWeatherData();
            fetchCryptoData();
        }
    }
    
    // Initialize LVGL
    lv_init();
    
    // Allocate draw buffers
    size_t buf_size = LCD_WIDTH * 50 * sizeof(lv_color_t);
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!buf1 || !buf2) {
        USBSerial.println("[WARN] PSRAM alloc failed, using internal");
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
    
    // Show initial screen
    navigateTo(CAT_CLOCK, 0);
    
    USBSerial.println("\n[READY] S3 MiniOS v4.0 initialized!\n");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
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
        
        // Refresh system card if visible
        if (screenOn && currentCategory == CAT_SYSTEM) {
            navigateTo(CAT_SYSTEM, currentSubCard);
        }
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
        fetchCryptoData();
    }
    
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
    
    // Music progress
    if (musicPlaying && millis() - lastMusicUpdate >= 1000) {
        lastMusicUpdate = millis();
        musicCurrent++;
        if (musicCurrent >= musicDuration) {
            musicCurrent = 0;
            musicPlaying = false;
        }
        if (screenOn && currentCategory == CAT_MEDIA && currentSubCard == 0) {
            navigateTo(CAT_MEDIA, 0);
        }
    }
    
    // Timer updates
    if (sandTimerRunning) {
        unsigned long elapsed = millis() - sandTimerStartMs;
        if (elapsed >= SAND_TIMER_DURATION) {
            sandTimerRunning = false;
            timerNotificationActive = true;
        }
        if (screenOn && currentCategory == CAT_TIMER && currentSubCard == 0) {
            navigateTo(CAT_TIMER, 0);
        }
    }
    
    // Stopwatch update
    if (stopwatchRunning && screenOn && currentCategory == CAT_TIMER && currentSubCard == 1) {
        static unsigned long lastStopwatchRefresh = 0;
        if (millis() - lastStopwatchRefresh >= 100) {
            lastStopwatchRefresh = millis();
            navigateTo(CAT_TIMER, 1);
        }
    }
    
    // Breathe animation
    if (breatheRunning && screenOn && currentCategory == CAT_TIMER && currentSubCard == 3) {
        static unsigned long lastBreatheRefresh = 0;
        if (millis() - lastBreatheRefresh >= 100) {
            lastBreatheRefresh = millis();
            navigateTo(CAT_TIMER, 3);
        }
    }
    
    // Clock refresh
    if (screenOn && currentCategory == CAT_CLOCK) {
        static unsigned long lastClockRefresh = 0;
        if (millis() - lastClockRefresh >= 1000) {
            lastClockRefresh = millis();
            navigateTo(CAT_CLOCK, currentSubCard);
        }
    }
    
    // Compass refresh
    if (screenOn && currentCategory == CAT_COMPASS) {
        static unsigned long lastCompassRefresh = 0;
        if (millis() - lastCompassRefresh >= 100) {
            lastCompassRefresh = millis();
            navigateTo(CAT_COMPASS, currentSubCard);
        }
    }
    
    delay(5);
}
