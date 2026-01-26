/**
 * ═══════════════════════════════════════════════════════════════════════════════
 *  S3 MiniOS v4.0 - ULTIMATE PREMIUM EDITION
 *  *** ADAPTED FOR ESP32-S3-Touch-AMOLED-2.06" ***
 *  
 *  CHANGES FROM ORIGINAL (1.8" version):
 *  - Display driver: CO5300 (was SH8601)
 *  - Resolution: 410×502 (was 368×448)
 *  - Added col_offset for CO5300 driver
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
 *  Hardware: Waveshare ESP32-S3-Touch-AMOLED-2.06"
 *    • Display: CO5300 QSPI AMOLED 410x502
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
//  BOARD-SPECIFIC CONFIGURATION (2.06" CO5300)
//  *** UPDATED FOR 2.06" DISPLAY ***
// ═══════════════════════════════════════════════════════════════════════════════
// Resolution is now defined in pin_config.h as 410x502
// Using LCD_WIDTH and LCD_HEIGHT from pin_config.h

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
//  *** UPDATED: Using CO5300 driver for 2.06" display ***
// ═══════════════════════════════════════════════════════════════════════════════
SensorQMI8658 qmi;
SensorPCF85063 rtc;
XPowersPMU power;
IMUdata acc, gyr;
Preferences prefs;

// QSPI Data Bus (same for both displays)
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS,     // CS
    LCD_SCLK,   // SCK
    LCD_SDIO0,  // SDIO0
    LCD_SDIO1,  // SDIO1
    LCD_SDIO2,  // SDIO2
    LCD_SDIO3   // SDIO3
);

// *** CRITICAL CHANGE: CO5300 driver for 2.06" display ***
// The 1.8" uses Arduino_SH8601, but 2.06" uses Arduino_CO5300
Arduino_GFX *gfx = new Arduino_CO5300(
    bus, 
    LCD_RESET,      // RST
    0,              // rotation
    LCD_WIDTH,      // width = 410
    LCD_HEIGHT,     // height = 502
    LCD_COL_OFFSET1,// col_offset1 = 22 (important for CO5300!)
    LCD_ROW_OFFSET1,// row_offset1 = 0
    LCD_COL_OFFSET2,// col_offset2 = 0
    LCD_ROW_OFFSET2 // row_offset2 = 0
);

// Touch controller (same for both displays)
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus = 
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(
    IIC_Bus, 
    FT3168_DEVICE_ADDRESS, 
    DRIVEBUS_DEFAULT_VALUE, 
    TP_INT, 
    Arduino_IIC_Touch_Interrupt
));

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
const int maxSubCards[] = {2, 3, 4, 3, 2, 2, 2, 4, 3, 1, 1, 3};

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
int obstacleX = 400;  // Adjusted for wider screen
bool dinoGameOver = false;
const float GRAVITY = 4.0;
const float JUMP_FORCE = -22.0;

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
//  PREMIUM GRADIENT THEMES
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

// Card creators (adapted for 410x502 resolution)
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
//  DISPLAY FLUSH - Works the same for CO5300
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
//  TOUCHPAD READ
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
//  BATTERY INTELLIGENCE
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
    prefs.putBool("saver", batterySaverMode);
    prefs.putFloat("avgDrain", batteryStats.avgDrainPerHour);
    prefs.putFloat("wDrain", batteryStats.weightedDrainRate);

    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist%d", i);
        prefs.putUInt(key, userData.stepHistory[i]);
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
    batterySaverMode = prefs.getBool("saver", false);
    batteryStats.avgDrainPerHour = prefs.getFloat("avgDrain", 5.0f);
    batteryStats.weightedDrainRate = prefs.getFloat("wDrain", 5.0f);

    for (int i = 0; i < 7; i++) {
        char key[16];
        snprintf(key, sizeof(key), "hist%d", i);
        userData.stepHistory[i] = prefs.getUInt(key, 0);
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
//  SENSOR FUSION (placeholder - implement full version as needed)
// ═══════════════════════════════════════════════════════════════════════════════
void updateSensorFusion() {
    if (!hasIMU) return;
    if (qmi.getDataReady()) {
        qmi.getAccelerometer(acc.x, acc.y, acc.z);
        qmi.getGyroscope(gyr.x, gyr.y, gyr.z);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  NAVIGATION TO CARDS
// ═══════════════════════════════════════════════════════════════════════════════
void navigateTo(int category, int subCard) {
    lv_obj_clean(lv_scr_act());
    
    // Set background
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_set_style_bg_color(lv_scr_act(), theme.color1, 0);
    
    // Call appropriate card creator
    switch(category) {
        case CAT_CLOCK:
            if (subCard == 0) createClockCard();
            else createAnalogClockCard();
            break;
        case CAT_SYSTEM:
            if (subCard == 0) createBatteryCard();
            else if (subCard == 1) createBatteryStatsCard();
            else createFactoryResetCard();
            break;
        // Add other cases as needed...
        default:
            createClockCard();
            break;
    }
    
    isTransitioning = false;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  HELPER: Create Card Container (adapted for 410x502)
// ═══════════════════════════════════════════════════════════════════════════════
lv_obj_t* createCard(const char* title) {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);  // 386x442 for 2.06"
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(card, theme.color2, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    
    if (strlen(title) > 0) {
        lv_obj_t *titleLbl = lv_label_create(card);
        lv_label_set_text(titleLbl, title);
        lv_obj_set_style_text_color(titleLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(titleLbl, LV_ALIGN_TOP_LEFT, 16, 12);
    }
    
    return card;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MINI STATUS BAR (adapted for wider screen)
// ═══════════════════════════════════════════════════════════════════════════════
void createMiniStatusBar(lv_obj_t* parent) {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LCD_WIDTH - 50, 20);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    
    // WiFi indicator
    lv_obj_t *wifiLbl = lv_label_create(bar);
    lv_label_set_text(wifiLbl, wifiConnected ? LV_SYMBOL_WIFI : "");
    lv_obj_set_style_text_color(wifiLbl, lv_color_hex(0x636366), 0);
    lv_obj_align(wifiLbl, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Battery estimate
    calculateBatteryEstimates();
    char estBuf[16];
    if (isCharging) {
        snprintf(estBuf, sizeof(estBuf), "Charging");
    } else {
        uint32_t hrs = batteryStats.combinedEstimateMins / 60;
        uint32_t mins = batteryStats.combinedEstimateMins % 60;
        snprintf(estBuf, sizeof(estBuf), "~%luh %lum", hrs, mins);
    }
    
    lv_obj_t *estLbl = lv_label_create(bar);
    lv_label_set_text(estLbl, estBuf);
    lv_obj_set_style_text_color(estLbl, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(estLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(estLbl, LV_ALIGN_CENTER, 0, 0);
    
    // Battery percentage
    char batBuf[8];
    snprintf(batBuf, sizeof(batBuf), "%d%%", batteryPercent);
    lv_obj_t *batLbl = lv_label_create(bar);
    lv_label_set_text(batLbl, batBuf);
    lv_obj_set_style_text_color(batLbl, batteryPercent > 20 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(batLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(batLbl, LV_ALIGN_RIGHT_MID, 0, 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  CLOCK CARD (digital) - Adapted for 410x502
// ═══════════════════════════════════════════════════════════════════════════════
void createClockCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("");
    
    // Get time from RTC
    if (hasRTC) {
        RTC_DateTime dt = rtc.getDateTime();
        clockHour = dt.getHour();
        clockMinute = dt.getMinute();
        clockSecond = dt.getSecond();
    }
    
    // Large time display
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", clockHour, clockMinute);
    
    lv_obj_t *timeLbl = lv_label_create(card);
    lv_label_set_text(timeLbl, timeBuf);
    lv_obj_set_style_text_color(timeLbl, theme.text, 0);
    lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_48, 0);
    lv_obj_align(timeLbl, LV_ALIGN_CENTER, 0, -60);
    
    // Seconds
    char secBuf[4];
    snprintf(secBuf, sizeof(secBuf), ":%02d", clockSecond);
    
    lv_obj_t *secLbl = lv_label_create(card);
    lv_label_set_text(secLbl, secBuf);
    lv_obj_set_style_text_color(secLbl, theme.accent, 0);
    lv_obj_set_style_text_font(secLbl, &lv_font_montserrat_24, 0);
    lv_obj_align(secLbl, LV_ALIGN_CENTER, 85, -55);
    
    // Date
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    
    RTC_DateTime dt = rtc.getDateTime();
    char dateBuf[32];
    snprintf(dateBuf, sizeof(dateBuf), "%s, %s %d", 
             days[dt.getWeekday()], months[dt.getMonth()-1], dt.getDay());
    
    lv_obj_t *dateLbl = lv_label_create(card);
    lv_label_set_text(dateLbl, dateBuf);
    lv_obj_set_style_text_color(dateLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(dateLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(dateLbl, LV_ALIGN_CENTER, 0, 0);
    
    // Location
    char locBuf[64];
    snprintf(locBuf, sizeof(locBuf), "%s, %s", weatherCity, weatherCountry);
    
    lv_obj_t *locLbl = lv_label_create(card);
    lv_label_set_text(locLbl, locBuf);
    lv_obj_set_style_text_color(locLbl, theme.accent, 0);
    lv_obj_set_style_text_font(locLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(locLbl, LV_ALIGN_CENTER, 0, 30);
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  ANALOG CLOCK CARD - Adapted for 410x502
// ═══════════════════════════════════════════════════════════════════════════════
void createAnalogClockCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("");
    
    if (hasRTC) {
        RTC_DateTime dt = rtc.getDateTime();
        clockHour = dt.getHour();
        clockMinute = dt.getMinute();
        clockSecond = dt.getSecond();
    }
    
    // Clock face - larger for 2.06"
    int centerX = (LCD_WIDTH - 24) / 2;
    int centerY = (LCD_HEIGHT - 60) / 2 - 20;
    int radius = 150;  // Larger radius for bigger screen
    
    // Draw hour markers
    for (int i = 0; i < 12; i++) {
        float angle = (i * 30 - 90) * PI / 180.0;
        int x1 = centerX + cos(angle) * (radius - 15);
        int y1 = centerY + sin(angle) * (radius - 15);
        int x2 = centerX + cos(angle) * radius;
        int y2 = centerY + sin(angle) * radius;
        
        lv_obj_t *marker = lv_line_create(card);
        static lv_point_t points[2];
        points[0] = {(lv_coord_t)x1, (lv_coord_t)y1};
        points[1] = {(lv_coord_t)x2, (lv_coord_t)y2};
        lv_line_set_points(marker, points, 2);
        lv_obj_set_style_line_color(marker, theme.text, 0);
        lv_obj_set_style_line_width(marker, i % 3 == 0 ? 3 : 1, 0);
    }
    
    // Hour hand
    float hourAngle = ((clockHour % 12) * 30 + clockMinute * 0.5 - 90) * PI / 180.0;
    int hx = centerX + cos(hourAngle) * (radius * 0.5);
    int hy = centerY + sin(hourAngle) * (radius * 0.5);
    
    lv_obj_t *hourHand = lv_line_create(card);
    static lv_point_t hPoints[2];
    hPoints[0] = {(lv_coord_t)centerX, (lv_coord_t)centerY};
    hPoints[1] = {(lv_coord_t)hx, (lv_coord_t)hy};
    lv_line_set_points(hourHand, hPoints, 2);
    lv_obj_set_style_line_color(hourHand, theme.text, 0);
    lv_obj_set_style_line_width(hourHand, 6, 0);
    lv_obj_set_style_line_rounded(hourHand, true, 0);
    
    // Minute hand
    float minAngle = (clockMinute * 6 - 90) * PI / 180.0;
    int mx = centerX + cos(minAngle) * (radius * 0.75);
    int my = centerY + sin(minAngle) * (radius * 0.75);
    
    lv_obj_t *minHand = lv_line_create(card);
    static lv_point_t mPoints[2];
    mPoints[0] = {(lv_coord_t)centerX, (lv_coord_t)centerY};
    mPoints[1] = {(lv_coord_t)mx, (lv_coord_t)my};
    lv_line_set_points(minHand, mPoints, 2);
    lv_obj_set_style_line_color(minHand, theme.text, 0);
    lv_obj_set_style_line_width(minHand, 4, 0);
    lv_obj_set_style_line_rounded(minHand, true, 0);
    
    // Second hand
    float secAngle = (clockSecond * 6 - 90) * PI / 180.0;
    int sx = centerX + cos(secAngle) * (radius * 0.85);
    int sy = centerY + sin(secAngle) * (radius * 0.85);
    
    lv_obj_t *secHand = lv_line_create(card);
    static lv_point_t sPoints[2];
    sPoints[0] = {(lv_coord_t)centerX, (lv_coord_t)centerY};
    sPoints[1] = {(lv_coord_t)sx, (lv_coord_t)sy};
    lv_line_set_points(secHand, sPoints, 2);
    lv_obj_set_style_line_color(secHand, theme.accent, 0);
    lv_obj_set_style_line_width(secHand, 2, 0);
    
    // Center dot
    lv_obj_t *centerDot = lv_obj_create(card);
    lv_obj_set_size(centerDot, 12, 12);
    lv_obj_set_pos(centerDot, centerX - 6, centerY - 6);
    lv_obj_set_style_bg_color(centerDot, theme.accent, 0);
    lv_obj_set_style_radius(centerDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(centerDot, 0, 0);
    
    createMiniStatusBar(card);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY CARD - Adapted for 410x502
// ═══════════════════════════════════════════════════════════════════════════════
void createBatteryCard() {
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_t *card = createCard("SYSTEM");
    
    // Large percentage
    char percBuf[8];
    snprintf(percBuf, sizeof(percBuf), "%d%%", batteryPercent);
    lv_obj_t *percLbl = lv_label_create(card);
    lv_label_set_text(percLbl, percBuf);
    lv_obj_set_style_text_color(percLbl, batteryPercent > 20 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(percLbl, &lv_font_montserrat_48, 0);
    lv_obj_align(percLbl, LV_ALIGN_TOP_MID, 0, 50);
    
    // Status
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
    lv_obj_align(statusLbl, LV_ALIGN_TOP_MID, 0, 110);
    
    // Voltage
    char voltBuf[16];
    snprintf(voltBuf, sizeof(voltBuf), "%dmV", batteryVoltage);
    lv_obj_t *voltLbl = lv_label_create(card);
    lv_label_set_text(voltLbl, voltBuf);
    lv_obj_set_style_text_color(voltLbl, lv_color_hex(0x636366), 0);
    lv_obj_align(voltLbl, LV_ALIGN_TOP_MID, 0, 135);
    
    // Battery saver toggle
    lv_obj_t *saverRow = lv_obj_create(card);
    lv_obj_set_size(saverRow, LCD_WIDTH - 70, 55);
    lv_obj_align(saverRow, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(saverRow, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(saverRow, 14, 0);
    lv_obj_set_style_border_width(saverRow, 0, 0);
    
    lv_obj_t *saverLbl = lv_label_create(saverRow);
    lv_label_set_text(saverLbl, "Battery Saver");
    lv_obj_set_style_text_color(saverLbl, theme.text, 0);
    lv_obj_align(saverLbl, LV_ALIGN_LEFT_MID, 15, 0);
    
    lv_obj_t *saverValLbl = lv_label_create(saverRow);
    lv_label_set_text(saverValLbl, batterySaverMode ? "ON" : "OFF");
    lv_obj_set_style_text_color(saverValLbl, batterySaverMode ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x636366), 0);
    lv_obj_align(saverValLbl, LV_ALIGN_RIGHT_MID, -15, 0);
    
    // System info
    lv_obj_t *infoRow = lv_obj_create(card);
    lv_obj_set_size(infoRow, LCD_WIDTH - 70, 100);
    lv_obj_align(infoRow, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(infoRow, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(infoRow, 14, 0);
    lv_obj_set_style_border_width(infoRow, 0, 0);
    
    // WiFi
    char wifiBuf[48];
    snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %s", wifiConnected ? "Connected" : "Disconnected");
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
    lv_obj_align(ramLbl, LV_ALIGN_TOP_LEFT, 10, 35);
    
    // SD
    char sdBuf[24];
    snprintf(sdBuf, sizeof(sdBuf), "SD Card: %s", hasSD ? "OK" : "None");
    lv_obj_t *sdLbl = lv_label_create(infoRow);
    lv_label_set_text(sdLbl, sdBuf);
    lv_obj_set_style_text_color(sdLbl, hasSD ? lv_color_hex(0x30D158) : lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(sdLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(sdLbl, LV_ALIGN_TOP_LEFT, 10, 60);
    
    // Swipe hint
    lv_obj_t *hintLbl = lv_label_create(card);
    lv_label_set_text(hintLbl, "Swipe down for stats");
    lv_obj_set_style_text_color(hintLbl, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(hintLbl, &lv_font_montserrat_10, 0);
    lv_obj_align(hintLbl, LV_ALIGN_TOP_RIGHT, -15, 12);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  BATTERY STATS CARD - Adapted for 410x502
// ═══════════════════════════════════════════════════════════════════════════════
void createBatteryStatsCard() {
    lv_obj_t *card = createCard("BATTERY STATS");
    
    // Graph title
    lv_obj_t *graphTitle = lv_label_create(card);
    lv_label_set_text(graphTitle, "Screen Time (24h)");
    lv_obj_set_style_text_color(graphTitle, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(graphTitle, &lv_font_montserrat_12, 0);
    lv_obj_align(graphTitle, LV_ALIGN_TOP_LEFT, 5, 30);
    
    // Graph container - larger for 2.06"
    lv_obj_t *graphBg = lv_obj_create(card);
    lv_obj_set_size(graphBg, LCD_WIDTH - 70, 100);
    lv_obj_align(graphBg, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(graphBg, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(graphBg, 12, 0);
    lv_obj_set_style_border_width(graphBg, 0, 0);
    
    // Find max for scaling
    uint16_t maxMins = 1;
    for (int i = 0; i < USAGE_HISTORY_SIZE; i++) {
        if (batteryStats.hourlyScreenOnMins[i] > maxMins)
            maxMins = batteryStats.hourlyScreenOnMins[i];
    }
    
    // Draw bars - more space for wider screen
    int barWidth = (LCD_WIDTH - 100) / USAGE_HISTORY_SIZE;
    for (int i = 0; i < USAGE_HISTORY_SIZE; i++) {
        int barHeight = (batteryStats.hourlyScreenOnMins[i] * 70) / maxMins;
        if (barHeight < 2) barHeight = 2;
        
        lv_obj_t *bar = lv_obj_create(graphBg);
        lv_obj_set_size(bar, barWidth - 2, barHeight);
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
    lv_obj_align(estTitle, LV_ALIGN_TOP_LEFT, 5, 160);
    
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
        lv_obj_align(estLbl, LV_ALIGN_TOP_LEFT, 5 + (i % 2) * 180, 180 + (i / 2) * 20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  FACTORY RESET CARD - Adapted for 410x502
// ═══════════════════════════════════════════════════════════════════════════════
void createFactoryResetCard() {
    lv_obj_t *card = createCard("FACTORY RESET");
    
    lv_obj_t *warningLbl = lv_label_create(card);
    lv_label_set_text(warningLbl, "WARNING: This will clear:");
    lv_obj_set_style_text_color(warningLbl, lv_color_hex(0xFF9F0A), 0);
    lv_obj_align(warningLbl, LV_ALIGN_TOP_LEFT, 10, 40);
    
    const char* items[] = {"- Steps, games, all settings", "- Battery learning data", "- Usage patterns"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *itemLbl = lv_label_create(card);
        lv_label_set_text(itemLbl, items[i]);
        lv_obj_set_style_text_color(itemLbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(itemLbl, LV_ALIGN_TOP_LEFT, 10, 70 + i * 25);
    }
    
    lv_obj_t *preservedLbl = lv_label_create(card);
    lv_label_set_text(preservedLbl, "PRESERVED: SD card, firmware");
    lv_obj_set_style_text_color(preservedLbl, lv_color_hex(0x30D158), 0);
    lv_obj_align(preservedLbl, LV_ALIGN_TOP_LEFT, 10, 160);
    
    // Reset button - larger for 2.06"
    lv_obj_t *resetBtn = lv_btn_create(card);
    lv_obj_set_size(resetBtn, 200, 60);
    lv_obj_align(resetBtn, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(resetBtn, 16, 0);
    
    lv_obj_t *resetLbl = lv_label_create(resetBtn);
    lv_label_set_text(resetLbl, "RESET ALL");
    lv_obj_set_style_text_color(resetLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(resetLbl, &lv_font_montserrat_18, 0);
    lv_obj_center(resetLbl);
}

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

            if (hasRTC) {
                RTC_DateTime dt = rtc.getDateTime();
                userData.stepHistory[dt.getWeekday()] = userData.steps;
            }
        }
    }

    prevPrevMag = prevMag;
    prevMag = currentMag;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WIFI (placeholder - implement full version)
// ═══════════════════════════════════════════════════════════════════════════════
void connectWiFi() {
    // Implement WiFi connection logic
    // This is a placeholder
}

bool loadWiFiFromSD() {
    // Implement SD card WiFi loading
    return false;
}

void fetchWeatherData() {
    // Implement weather fetching
}

void fetchCryptoData() {
    // Implement crypto fetching
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
    USBSerial.begin(115200);
    delay(100);
    USBSerial.println("\n═══════════════════════════════════════════════════════");
    USBSerial.println("   S3 MiniOS v4.0 - FOR 2.06\" DISPLAY");
    USBSerial.println("   Resolution: 410x502 | Driver: CO5300");
    USBSerial.println("═══════════════════════════════════════════════════════\n");

    // Initialize I2C
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    // Initialize display (CO5300 for 2.06")
    if (!gfx->begin()) {
        USBSerial.println("[FAIL] Display init failed!");
    } else {
        gfx->setBrightness(200);
        gfx->fillScreen(0x0000);
        USBSerial.printf("[OK] Display (CO5300 %dx%d)\n", LCD_WIDTH, LCD_HEIGHT);
    }

    // Initialize touch
    if (FT3168->begin() == true) {
        USBSerial.println("[OK] Touch (FT3168)");
        FT3168->IIC_Write_Device_State(FT3168->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                       FT3168->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
    } else {
        USBSerial.println("[FAIL] Touch init");
    }

    // Initialize IMU
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

    // Initialize LVGL
    lv_init();

    // Allocate draw buffers (adjusted for 410x502)
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

    USBSerial.println("\n[READY] S3 MiniOS initialized for 2.06\" display!\n");
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

        if (screenOn && currentCategory == CAT_SYSTEM) {
            navigateTo(CAT_SYSTEM, currentSubCard);
        }
    }

    // Update usage tracking (every minute)
    if (millis() - lastUsageUpdate >= 60000) {
        updateUsageTracking();
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

    // Clock refresh
    if (screenOn && currentCategory == CAT_CLOCK) {
        static unsigned long lastClockRefresh = 0;
        if (millis() - lastClockRefresh >= 1000) {
            lastClockRefresh = millis();
            navigateTo(CAT_CLOCK, currentSubCard);
        }
    }

    delay(5);
}
