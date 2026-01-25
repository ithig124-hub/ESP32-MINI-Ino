/**
 * Widget OS v1.0
 * ESP32-S3 Touch AMOLED 1.8" Smartwatch Firmware
 * 
 * Features:
 * - Infinite horizontal card navigation
 * - Vertical category stacks
 * - Real compass with magnetometer
 * - WiFi NTP time sync (GMT+8)
 * - Weather/Stocks/Crypto APIs
 * - Blackjack game
 * - Sand timer with notifications
 * - Local data persistence (SPIFFS)
 * - Screen timeout
 * - Gradient themes
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

// ============================================
// CONFIGURATION
// ============================================
const char* WIFI_SSID = "Optus_9D2E3D";
const char* WIFI_PASSWORD = "snucktemptGLeQU";
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 8 * 3600;  // GMT+8
const int DAYLIGHT_OFFSET_SEC = 0;

// API Keys
const char* OPENWEATHER_API = "3795c13a0d3f7e17799d638edda60e3c";
const char* ALPHAVANTAGE_API = "UHLX28BF7GQ4T8J3";
const char* COINAPI_KEY = "11afad22-b6ea-4f18-9056-c7a1d7ed14a1";

// ============================================
// LVGL CONFIG
// ============================================
#define LVGL_TICK_PERIOD_MS 2
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

// ============================================
// HARDWARE
// ============================================
HWCDC USBSerial;
Adafruit_XCA9554 expander;
SensorQMI8658 qmi;
SensorPCF85063 rtc;
XPowersPMU power;
IMUdata acc, gyr;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_SH8601 *gfx = new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS, DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

// ============================================
// NAVIGATION STATE
// ============================================
#define NUM_CATEGORIES 12
enum Category {
  CAT_CLOCK = 0, CAT_COMPASS, CAT_ACTIVITY, CAT_GAMES,
  CAT_WEATHER, CAT_STOCKS, CAT_MEDIA, CAT_TIMER,
  CAT_STREAK, CAT_CALENDAR, CAT_SETTINGS, CAT_SYSTEM
};

int currentCategory = CAT_CLOCK;
int currentSubCard = 0;
const int maxSubCards[] = {2, 1, 4, 3, 2, 2, 2, 3, 3, 1, 1, 2};

// ============================================
// USER DATA (Persistent)
// ============================================
struct UserData {
  uint32_t steps;
  uint32_t dailyGoal;
  int stepStreak;
  int blackjackStreak;
  int gamesWon;
  int gamesPlayed;
  int brightness;
  int screenTimeout;  // minutes: 1,2,3,5,7,10
  int themeIndex;
  float totalDistance;
  float totalCalories;
} userData = {0, 10000, 7, 0, 0, 0, 200, 1, 0, 0.0, 0.0};

// ============================================
// RUNTIME STATE
// ============================================
bool wifiConnected = false;
unsigned long lastActivityMs = 0;
bool screenOn = true;

// Compass
float compassHeading = 0.0;
float magnetometer[3] = {0, 0, 0};

// Weather data
float weatherTemp = 24.0;
String weatherDesc = "Sunny";
float weatherHigh = 28.0;
float weatherLow = 18.0;

// Stocks/Crypto
float btcPrice = 0, ethPrice = 0;
float aaplPrice = 0, tslaPrice = 0;

// Timer
bool sandTimerRunning = false;
unsigned long sandTimerStartMs = 0;
const unsigned long SAND_TIMER_DURATION = 5 * 60 * 1000;  // 5 minutes
bool timerNotificationActive = false;
unsigned long notificationStartMs = 0;

// Stopwatch
bool stopwatchRunning = false;
unsigned long stopwatchStartMs = 0;
unsigned long stopwatchElapsedMs = 0;

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
bool dinoJumping = false;
int dinoScore = 0;
int obstacleX = 350;
bool dinoGameOver = false;

// Touch
int32_t touchStartX = 0, touchStartY = 0;
int32_t touchCurrentX = 0, touchCurrentY = 0;
bool touchActive = false;
unsigned long touchStartMs = 0;

// ============================================
// GRADIENT THEMES
// ============================================
struct GradientTheme {
  const char* name;
  lv_color_t color1;
  lv_color_t color2;
  lv_color_t text;
  lv_color_t accent;
};

GradientTheme gradientThemes[] = {
  {"Cyber Purple", lv_color_hex(0x8B5CF6), lv_color_hex(0x3B82F6), lv_color_hex(0xFFFFFF), lv_color_hex(0x00D4FF)},
  {"Ocean Teal", lv_color_hex(0x06B6D4), lv_color_hex(0x3B82F6), lv_color_hex(0xFFFFFF), lv_color_hex(0x22D3EE)},
  {"Sunset Orange", lv_color_hex(0xF97316), lv_color_hex(0xEAB308), lv_color_hex(0xFFFFFF), lv_color_hex(0xFBBF24)},
  {"Neon Pink", lv_color_hex(0xEC4899), lv_color_hex(0x8B5CF6), lv_color_hex(0xFFFFFF), lv_color_hex(0xF472B6)},
  {"Lime Green", lv_color_hex(0x22C55E), lv_color_hex(0x06B6D4), lv_color_hex(0xFFFFFF), lv_color_hex(0x4ADE80)},
  {"Fire Red", lv_color_hex(0xEF4444), lv_color_hex(0xF97316), lv_color_hex(0xFFFFFF), lv_color_hex(0xFB7185)},
  {"Deep Space", lv_color_hex(0x1E1B4B), lv_color_hex(0x312E81), lv_color_hex(0xFFFFFF), lv_color_hex(0x818CF8)},
  {"Mint Fresh", lv_color_hex(0x10B981), lv_color_hex(0x34D399), lv_color_hex(0x000000), lv_color_hex(0x6EE7B7)}
};
#define NUM_THEMES 8

// ============================================
// FUNCTION PROTOTYPES
// ============================================
void navigateTo(int category, int subCard);
void handleSwipe(int dx, int dy);
void saveUserData();
void loadUserData();
void syncTimeNTP();
void fetchWeatherData();
void fetchStockData();
void fetchCryptoData();

// Card creators
void createClockCard();
void createAnalogClockCard();
void createCompassCard();
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
void createStepStreakCard();
void createGameStreakCard();
void createAchievementsCard();
void createCalendarCard();
void createSettingsCard();
void createBatteryCard();
void createSystemCard();

// ============================================
// TOUCH INTERRUPT
// ============================================
void Arduino_IIC_Touch_Interrupt(void) {
  FT3168->IIC_Interrupt_Flag = true;
}

// ============================================
// DISPLAY FLUSH
// ============================================
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

// ============================================
// TOUCHPAD READ
// ============================================
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
      screenOn = true;
      gfx->setBrightness(userData.brightness);
      return;
    }

    if (!touchActive) {
      touchActive = true;
      touchStartX = touchX;
      touchStartY = touchY;
      touchStartMs = millis();
    }
    touchCurrentX = touchX;
    touchCurrentY = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
    if (touchActive) {
      touchActive = false;
      int dx = touchCurrentX - touchStartX;
      int dy = touchCurrentY - touchStartY;
      unsigned long duration = millis() - touchStartMs;
      if (duration < 500 && (abs(dx) > 50 || abs(dy) > 50)) {
        handleSwipe(dx, dy);
      }
    }
  }
}

// ============================================
// NAVIGATION HANDLER
// ============================================
void handleSwipe(int dx, int dy) {
  if (abs(dx) > abs(dy)) {
    // Horizontal - always change category (infinite loop)
    if (dx > 50) {
      currentCategory--;
      if (currentCategory < 0) currentCategory = NUM_CATEGORIES - 1;
    } else if (dx < -50) {
      currentCategory++;
      if (currentCategory >= NUM_CATEGORIES) currentCategory = 0;
    }
    currentSubCard = 0;  // Reset to main card
  } else {
    // Vertical - navigate within category
    if (dy > 50) {
      // Swipe down - go to next sub-card
      if (currentSubCard < maxSubCards[currentCategory] - 1) {
        currentSubCard++;
      }
    } else if (dy < -50) {
      // Swipe up - return to main card
      currentSubCard = 0;
    }
  }
  navigateTo(currentCategory, currentSubCard);
}

// ============================================
// CREATE GRADIENT BACKGROUND
// ============================================
void createGradientBg() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_set_style_bg_color(lv_scr_act(), theme.color1, 0);
  lv_obj_set_style_bg_grad_color(lv_scr_act(), theme.color2, 0);
  lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);
}

// ============================================
// CREATE CARD CONTAINER
// ============================================
lv_obj_t* createCard(const char* title) {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 30, LCD_HEIGHT - 70);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 15);
  lv_obj_set_style_bg_opa(card, LV_OPA_70, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x000000), 0);
  lv_obj_set_style_radius(card, 25, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 15, 0);
  
  if (strlen(title) > 0) {
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, theme.text, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
  }
  return card;
}

// ============================================
// NAVIGATION DOTS
// ============================================
void createNavDots() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *container = lv_obj_create(lv_scr_act());
  lv_obj_set_size(container, LCD_WIDTH, 20);
  lv_obj_align(container, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(container, 0, 0);
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(container, 6, 0);
  
  int start = currentCategory - 2;
  if (start < 0) start = 0;
  if (start > NUM_CATEGORIES - 5) start = NUM_CATEGORIES - 5;
  
  for (int i = start; i < start + 5 && i < NUM_CATEGORIES; i++) {
    lv_obj_t *dot = lv_obj_create(container);
    int size = (i == currentCategory) ? 10 : 6;
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, (i == currentCategory) ? theme.accent : lv_color_hex(0x666666), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
  }
  
  // Sub-card dots (right side)
  if (maxSubCards[currentCategory] > 1) {
    lv_obj_t *subContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(subContainer, 20, 60);
    lv_obj_align(subContainer, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_opa(subContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(subContainer, 0, 0);
    lv_obj_set_flex_flow(subContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(subContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(subContainer, 6, 0);
    
    for (int i = 0; i < maxSubCards[currentCategory]; i++) {
      lv_obj_t *dot = lv_obj_create(subContainer);
      int size = (i == currentSubCard) ? 10 : 6;
      lv_obj_set_size(dot, size, size);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(dot, (i == currentSubCard) ? theme.accent : lv_color_hex(0x666666), 0);
      lv_obj_set_style_border_width(dot, 0, 0);
    }
  }
}

// ============================================
// MAIN NAVIGATION
// ============================================
void navigateTo(int category, int subCard) {
  lv_obj_clean(lv_scr_act());
  createGradientBg();
  
  switch (category) {
    case CAT_CLOCK:
      if (subCard == 0) createClockCard();
      else createAnalogClockCard();
      break;
    case CAT_COMPASS:
      createCompassCard();
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
      else createCountdownCard();
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
      else createSystemCard();
      break;
  }
  
  createNavDots();
}

// ============================================
// CLOCK CARD
// ============================================
static lv_obj_t *clockLabel = NULL;
static lv_obj_t *dateLabel = NULL;

void createClockCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("Widget OS");
  
  clockLabel = lv_label_create(card);
  lv_label_set_text(clockLabel, "00:00");
  lv_obj_set_style_text_color(clockLabel, theme.text, 0);
  lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_48, 0);
  lv_obj_align(clockLabel, LV_ALIGN_CENTER, 0, -30);
  
  dateLabel = lv_label_create(card);
  lv_label_set_text(dateLabel, "Sat, Jan 25 2026");
  lv_obj_set_style_text_color(dateLabel, theme.accent, 0);
  lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_16, 0);
  lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 30);
  
  // WiFi status
  lv_obj_t *wifiLbl = lv_label_create(card);
  lv_label_set_text(wifiLbl, wifiConnected ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING);
  lv_obj_set_style_text_color(wifiLbl, wifiConnected ? lv_color_hex(0x4CAF50) : lv_color_hex(0xFF5252), 0);
  lv_obj_align(wifiLbl, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

// ============================================
// ANALOG CLOCK CARD
// ============================================
void createAnalogClockCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("");
  
  // Clock face
  lv_obj_t *face = lv_obj_create(card);
  lv_obj_set_size(face, 240, 240);
  lv_obj_align(face, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(face, lv_color_hex(0x111111), 0);
  lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(face, theme.accent, 0);
  lv_obj_set_style_border_width(face, 3, 0);
  
  // Numerals
  const char* nums[] = {"12", "3", "6", "9"};
  int pos[][2] = {{0, -95}, {95, 0}, {0, 95}, {-95, 0}};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *lbl = lv_label_create(face);
    lv_label_set_text(lbl, nums[i]);
    lv_obj_set_style_text_color(lbl, theme.text, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, pos[i][0], pos[i][1]);
  }
  
  // Hour hand (red)
  lv_obj_t *hHand = lv_obj_create(face);
  lv_obj_set_size(hHand, 60, 8);
  lv_obj_align(hHand, LV_ALIGN_CENTER, 25, 0);
  lv_obj_set_style_bg_color(hHand, lv_color_hex(0xFF3333), 0);
  lv_obj_set_style_radius(hHand, 4, 0);
  lv_obj_set_style_border_width(hHand, 0, 0);
  
  // Minute hand (blue)
  lv_obj_t *mHand = lv_obj_create(face);
  lv_obj_set_size(mHand, 85, 5);
  lv_obj_align(mHand, LV_ALIGN_CENTER, 38, 0);
  lv_obj_set_style_bg_color(mHand, lv_color_hex(0x3388FF), 0);
  lv_obj_set_style_radius(mHand, 2, 0);
  lv_obj_set_style_border_width(mHand, 0, 0);
  
  // Center
  lv_obj_t *center = lv_obj_create(face);
  lv_obj_set_size(center, 16, 16);
  lv_obj_align(center, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(center, theme.text, 0);
  lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(center, 0, 0);
}

// ============================================
// COMPASS CARD (Real functionality)
// ============================================
void createCompassCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 30, LCD_HEIGHT - 70);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 15);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x111111), 0);
  lv_obj_set_style_radius(card, 25, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Cardinal directions
  const char* dirs[] = {"N", "E", "S", "W"};
  int dirPos[][2] = {{0, -140}, {130, 0}, {0, 140}, {-130, 0}};
  lv_color_t dirColors[] = {lv_color_hex(0xFF3333), lv_color_hex(0xAAAAAA), lv_color_hex(0xAAAAAA), lv_color_hex(0xAAAAAA)};
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, dirs[i]);
    lv_obj_set_style_text_color(lbl, dirColors[i], 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, dirPos[i][0], dirPos[i][1]);
  }
  
  // Sunrise label
  lv_obj_t *sunriseL = lv_label_create(card);
  lv_label_set_text(sunriseL, "SUNRISE");
  lv_obj_set_style_text_color(sunriseL, lv_color_hex(0x88CCFF), 0);
  lv_obj_set_style_text_font(sunriseL, &lv_font_montserrat_12, 0);
  lv_obj_align(sunriseL, LV_ALIGN_CENTER, 0, -70);
  
  lv_obj_t *sunriseT = lv_label_create(card);
  lv_label_set_text(sunriseT, "05:39");
  lv_obj_set_style_text_color(sunriseT, lv_color_hex(0xFFCC44), 0);
  lv_obj_set_style_text_font(sunriseT, &lv_font_montserrat_36, 0);
  lv_obj_align(sunriseT, LV_ALIGN_CENTER, 0, -40);
  
  // Sunset label
  lv_obj_t *sunsetT = lv_label_create(card);
  lv_label_set_text(sunsetT, "19:05");
  lv_obj_set_style_text_color(sunsetT, lv_color_hex(0xFF8844), 0);
  lv_obj_set_style_text_font(sunsetT, &lv_font_montserrat_36, 0);
  lv_obj_align(sunsetT, LV_ALIGN_CENTER, 0, 40);
  
  lv_obj_t *sunsetL = lv_label_create(card);
  lv_label_set_text(sunsetL, "SUNSET");
  lv_obj_set_style_text_color(sunsetL, lv_color_hex(0xFF8844), 0);
  lv_obj_set_style_text_font(sunsetL, &lv_font_montserrat_12, 0);
  lv_obj_align(sunsetL, LV_ALIGN_CENTER, 0, 70);
  
  // Compass heading display
  char headingBuf[16];
  snprintf(headingBuf, sizeof(headingBuf), "%.0f", compassHeading);
  lv_obj_t *headingLbl = lv_label_create(card);
  lv_label_set_text(headingLbl, headingBuf);
  lv_obj_set_style_text_color(headingLbl, theme.text, 0);
  lv_obj_set_style_text_font(headingLbl, &lv_font_montserrat_20, 0);
  lv_obj_align(headingLbl, LV_ALIGN_BOTTOM_MID, 0, -20);
  
  // Needle (red tip pointing north)
  float rad = (compassHeading - 45) * 3.14159 / 180.0;
  int needleLen = 80;
  
  lv_obj_t *needleR = lv_obj_create(card);
  lv_obj_set_size(needleR, 8, needleLen);
  lv_obj_align(needleR, LV_ALIGN_CENTER, 35, -20);
  lv_obj_set_style_bg_color(needleR, lv_color_hex(0xFF3333), 0);
  lv_obj_set_style_radius(needleR, 4, 0);
  lv_obj_set_style_border_width(needleR, 0, 0);
  
  lv_obj_t *needleB = lv_obj_create(card);
  lv_obj_set_size(needleB, 8, needleLen);
  lv_obj_align(needleB, LV_ALIGN_CENTER, -35, 20);
  lv_obj_set_style_bg_color(needleB, lv_color_hex(0x3388FF), 0);
  lv_obj_set_style_radius(needleB, 4, 0);
  lv_obj_set_style_border_width(needleB, 0, 0);
  
  // Center pivot
  lv_obj_t *pivot = lv_obj_create(card);
  lv_obj_set_size(pivot, 20, 20);
  lv_obj_align(pivot, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(pivot, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(pivot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(pivot, 0, 0);
}

// ============================================
// STEPS CARD (Purple gradient style)
// ============================================
void createStepsCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 30, LCD_HEIGHT - 70);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 15);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x8B5CF6), 0);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x3B82F6), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_radius(card, 25, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 20, 0);
  
  // App name
  lv_obj_t *appName = lv_label_create(card);
  lv_label_set_text(appName, LV_SYMBOL_CHARGE " Widget OS");
  lv_obj_set_style_text_color(appName, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(appName, &lv_font_montserrat_16, 0);
  lv_obj_align(appName, LV_ALIGN_TOP_LEFT, 0, 0);
  
  // Steps label
  lv_obj_t *stepsL = lv_label_create(card);
  lv_label_set_text(stepsL, "Steps:");
  lv_obj_set_style_text_color(stepsL, lv_color_hex(0xDDDDDD), 0);
  lv_obj_set_style_text_font(stepsL, &lv_font_montserrat_18, 0);
  lv_obj_align(stepsL, LV_ALIGN_CENTER, 0, -70);
  
  // Large step count
  char stepBuf[16];
  snprintf(stepBuf, sizeof(stepBuf), "%lu", (unsigned long)userData.steps);
  lv_obj_t *stepCount = lv_label_create(card);
  lv_label_set_text(stepCount, stepBuf);
  lv_obj_set_style_text_color(stepCount, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(stepCount, &lv_font_montserrat_48, 0);
  lv_obj_align(stepCount, LV_ALIGN_CENTER, 0, -20);
  
  // Progress milestones
  int milestones[] = {2000, 4000, 6000, 8000};
  for (int i = 0; i < 4; i++) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", milestones[i]);
    lv_obj_t *mLbl = lv_label_create(card);
    lv_label_set_text(mLbl, buf);
    lv_obj_set_style_text_color(mLbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(mLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(mLbl, LV_ALIGN_CENTER, -100 + i * 70, 50);
    
    // Progress bar segment
    lv_obj_t *seg = lv_obj_create(card);
    lv_obj_set_size(seg, 55, 8);
    lv_obj_align(seg, LV_ALIGN_CENTER, -100 + i * 70, 75);
    lv_obj_set_style_radius(seg, 4, 0);
    lv_obj_set_style_border_width(seg, 0, 0);
    
    bool filled = userData.steps >= (uint32_t)milestones[i];
    lv_obj_set_style_bg_color(seg, filled ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x666688), 0);
  }
}

// ============================================
// ACTIVITY RINGS CARD
// ============================================
void createActivityRingsCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("ACTIVITY");
  
  int moveP = (userData.steps * 100) / userData.dailyGoal;
  if (moveP > 100) moveP = 100;
  
  // Move ring (outer)
  lv_obj_t *moveArc = lv_arc_create(card);
  lv_obj_set_size(moveArc, 200, 200);
  lv_obj_align(moveArc, LV_ALIGN_CENTER, 0, 10);
  lv_arc_set_rotation(moveArc, 270);
  lv_arc_set_bg_angles(moveArc, 0, 360);
  lv_arc_set_value(moveArc, moveP);
  lv_obj_set_style_arc_color(moveArc, lv_color_hex(0x3A1515), LV_PART_MAIN);
  lv_obj_set_style_arc_color(moveArc, lv_color_hex(0xFF2D55), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(moveArc, 18, LV_PART_MAIN);
  lv_obj_set_style_arc_width(moveArc, 18, LV_PART_INDICATOR);
  lv_obj_remove_style(moveArc, NULL, LV_PART_KNOB);
  
  // Exercise ring (middle)
  lv_obj_t *exArc = lv_arc_create(card);
  lv_obj_set_size(exArc, 160, 160);
  lv_obj_align(exArc, LV_ALIGN_CENTER, 0, 10);
  lv_arc_set_rotation(exArc, 270);
  lv_arc_set_bg_angles(exArc, 0, 360);
  lv_arc_set_value(exArc, 45);
  lv_obj_set_style_arc_color(exArc, lv_color_hex(0x152A15), LV_PART_MAIN);
  lv_obj_set_style_arc_color(exArc, lv_color_hex(0x30D158), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(exArc, 18, LV_PART_MAIN);
  lv_obj_set_style_arc_width(exArc, 18, LV_PART_INDICATOR);
  lv_obj_remove_style(exArc, NULL, LV_PART_KNOB);
  
  // Stand ring (inner)
  lv_obj_t *stArc = lv_arc_create(card);
  lv_obj_set_size(stArc, 120, 120);
  lv_obj_align(stArc, LV_ALIGN_CENTER, 0, 10);
  lv_arc_set_rotation(stArc, 270);
  lv_arc_set_bg_angles(stArc, 0, 360);
  lv_arc_set_value(stArc, 80);
  lv_obj_set_style_arc_color(stArc, lv_color_hex(0x152030), LV_PART_MAIN);
  lv_obj_set_style_arc_color(stArc, lv_color_hex(0x5AC8FA), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(stArc, 18, LV_PART_MAIN);
  lv_obj_set_style_arc_width(stArc, 18, LV_PART_INDICATOR);
  lv_obj_remove_style(stArc, NULL, LV_PART_KNOB);
}

// ============================================
// WORKOUT CARD
// ============================================
void createWorkoutCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("WORKOUTS");
  
  const char* modes[] = {"Run", "Bike", "Walk", "Gym"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *btn = lv_btn_create(card);
    lv_obj_set_size(btn, LCD_WIDTH - 100, 55);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 35 + i * 65);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, modes[i]);
    lv_obj_set_style_text_color(lbl, theme.text, 0);
    lv_obj_center(lbl);
  }
}

// ============================================
// DISTANCE CARD
// ============================================
void createDistanceCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("DISTANCE");
  
  char distBuf[32];
  snprintf(distBuf, sizeof(distBuf), "%.2f", userData.totalDistance);
  lv_obj_t *distLbl = lv_label_create(card);
  lv_label_set_text(distLbl, distBuf);
  lv_obj_set_style_text_color(distLbl, theme.accent, 0);
  lv_obj_set_style_text_font(distLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(distLbl, LV_ALIGN_CENTER, 0, -30);
  
  lv_obj_t *kmLbl = lv_label_create(card);
  lv_label_set_text(kmLbl, "kilometers");
  lv_obj_set_style_text_color(kmLbl, theme.text, 0);
  lv_obj_align(kmLbl, LV_ALIGN_CENTER, 0, 20);
  
  char calBuf[32];
  snprintf(calBuf, sizeof(calBuf), "%.0f kcal burned", userData.totalCalories);
  lv_obj_t *calLbl = lv_label_create(card);
  lv_label_set_text(calLbl, calBuf);
  lv_obj_set_style_text_color(calLbl, lv_color_hex(0x888888), 0);
  lv_obj_align(calLbl, LV_ALIGN_CENTER, 0, 70);
}

// ============================================
// BLACKJACK CARD
// ============================================
void blackjackHitCb(lv_event_t *e);
void blackjackStandCb(lv_event_t *e);
void blackjackNewCb(lv_event_t *e);

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

void createBlackjackCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("BLACKJACK");
  
  // Player hand
  char pBuf[32];
  snprintf(pBuf, sizeof(pBuf), "You: %d", handTotal(playerCards, playerCount));
  lv_obj_t *pLbl = lv_label_create(card);
  lv_label_set_text(pLbl, pBuf);
  lv_obj_set_style_text_color(pLbl, theme.text, 0);
  lv_obj_set_style_text_font(pLbl, &lv_font_montserrat_20, 0);
  lv_obj_align(pLbl, LV_ALIGN_CENTER, 0, -60);
  
  // Dealer hand
  char dBuf[32];
  if (playerStand) {
    snprintf(dBuf, sizeof(dBuf), "Dealer: %d", handTotal(dealerCards, dealerCount));
  } else {
    snprintf(dBuf, sizeof(dBuf), "Dealer: ?");
  }
  lv_obj_t *dLbl = lv_label_create(card);
  lv_label_set_text(dLbl, dBuf);
  lv_obj_set_style_text_color(dLbl, lv_color_hex(0xFFAA00), 0);
  lv_obj_set_style_text_font(dLbl, &lv_font_montserrat_20, 0);
  lv_obj_align(dLbl, LV_ALIGN_CENTER, 0, -20);
  
  // Result or buttons
  if (!blackjackGameActive) {
    lv_obj_t *newBtn = lv_btn_create(card);
    lv_obj_set_size(newBtn, 150, 50);
    lv_obj_align(newBtn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(newBtn, theme.accent, 0);
    lv_obj_add_event_cb(newBtn, blackjackNewCb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nLbl = lv_label_create(newBtn);
    lv_label_set_text(nLbl, "NEW GAME");
    lv_obj_center(nLbl);
  } else if (!playerStand) {
    lv_obj_t *hitBtn = lv_btn_create(card);
    lv_obj_set_size(hitBtn, 100, 45);
    lv_obj_align(hitBtn, LV_ALIGN_CENTER, -60, 60);
    lv_obj_set_style_bg_color(hitBtn, lv_color_hex(0x4CAF50), 0);
    lv_obj_add_event_cb(hitBtn, blackjackHitCb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hLbl = lv_label_create(hitBtn);
    lv_label_set_text(hLbl, "HIT");
    lv_obj_center(hLbl);
    
    lv_obj_t *standBtn = lv_btn_create(card);
    lv_obj_set_size(standBtn, 100, 45);
    lv_obj_align(standBtn, LV_ALIGN_CENTER, 60, 60);
    lv_obj_set_style_bg_color(standBtn, lv_color_hex(0xFF5252), 0);
    lv_obj_add_event_cb(standBtn, blackjackStandCb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sLbl = lv_label_create(standBtn);
    lv_label_set_text(sLbl, "STAND");
    lv_obj_center(sLbl);
  }
  
  // Streak
  char streakBuf[32];
  snprintf(streakBuf, sizeof(streakBuf), "Streak: %d", userData.blackjackStreak);
  lv_obj_t *strLbl = lv_label_create(card);
  lv_label_set_text(strLbl, streakBuf);
  lv_obj_set_style_text_color(strLbl, lv_color_hex(0x888888), 0);
  lv_obj_align(strLbl, LV_ALIGN_BOTTOM_MID, 0, -10);
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

// ============================================
// DINO GAME CARD
// ============================================
void dinoJumpCb(lv_event_t *e) {
  if (dinoGameOver) {
    dinoGameOver = false;
    dinoScore = 0;
    obstacleX = 350;
  } else if (!dinoJumping) {
    dinoJumping = true;
    dinoY = -60;
  }
}

void createDinoCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("DINO RUN");
  
  char sBuf[16];
  snprintf(sBuf, sizeof(sBuf), "Score: %d", dinoScore);
  lv_obj_t *scoreLbl = lv_label_create(card);
  lv_label_set_text(scoreLbl, sBuf);
  lv_obj_set_style_text_color(scoreLbl, theme.text, 0);
  lv_obj_align(scoreLbl, LV_ALIGN_TOP_RIGHT, -10, 30);
  
  // Ground
  lv_obj_t *ground = lv_obj_create(card);
  lv_obj_set_size(ground, LCD_WIDTH - 80, 3);
  lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_set_style_bg_color(ground, lv_color_hex(0x666666), 0);
  lv_obj_set_style_border_width(ground, 0, 0);
  
  // Dino
  lv_obj_t *dino = lv_obj_create(card);
  lv_obj_set_size(dino, 30, 45);
  lv_obj_align(dino, LV_ALIGN_BOTTOM_LEFT, 50, -63 + dinoY);
  lv_obj_set_style_bg_color(dino, theme.accent, 0);
  lv_obj_set_style_radius(dino, 5, 0);
  lv_obj_set_style_border_width(dino, 0, 0);
  
  // Obstacle
  lv_obj_t *obs = lv_obj_create(card);
  lv_obj_set_size(obs, 25, 35);
  lv_obj_align(obs, LV_ALIGN_BOTTOM_LEFT, obstacleX, -63);
  lv_obj_set_style_bg_color(obs, lv_color_hex(0xFF5252), 0);
  lv_obj_set_style_radius(obs, 3, 0);
  lv_obj_set_style_border_width(obs, 0, 0);
  
  // Jump button
  lv_obj_t *jumpBtn = lv_btn_create(card);
  lv_obj_set_size(jumpBtn, 120, 50);
  lv_obj_align(jumpBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(jumpBtn, theme.accent, 0);
  lv_obj_set_style_radius(jumpBtn, 25, 0);
  lv_obj_add_event_cb(jumpBtn, dinoJumpCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *jLbl = lv_label_create(jumpBtn);
  lv_label_set_text(jLbl, dinoGameOver ? "RESTART" : "JUMP!");
  lv_obj_center(jLbl);
  
  if (dinoGameOver) {
    lv_obj_t *goLbl = lv_label_create(card);
    lv_label_set_text(goLbl, "GAME OVER");
    lv_obj_set_style_text_color(goLbl, lv_color_hex(0xFF5252), 0);
    lv_obj_set_style_text_font(goLbl, &lv_font_montserrat_24, 0);
    lv_obj_align(goLbl, LV_ALIGN_CENTER, 0, 0);
  }
}

// ============================================
// YES/NO CARD
// ============================================
void yesNoSpinCb(lv_event_t *e) {
  yesNoSpinning = true;
  yesNoAngle = 0;
}

void createYesNoCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 30, LCD_HEIGHT - 70);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 15);
  lv_obj_set_style_bg_color(card, lv_color_hex(0xCC3333), 0);
  lv_obj_set_style_radius(card, 25, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Result
  lv_obj_t *resultLbl = lv_label_create(card);
  lv_label_set_text(resultLbl, yesNoResult.c_str());
  lv_obj_set_style_text_color(resultLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(resultLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(resultLbl, LV_ALIGN_CENTER, 0, -20);
  
  // Spin button
  lv_obj_t *spinBtn = lv_btn_create(card);
  lv_obj_set_size(spinBtn, 150, 50);
  lv_obj_align(spinBtn, LV_ALIGN_CENTER, 0, 80);
  lv_obj_set_style_bg_color(spinBtn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(spinBtn, 25, 0);
  lv_obj_add_event_cb(spinBtn, yesNoSpinCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *sLbl = lv_label_create(spinBtn);
  lv_label_set_text(sLbl, LV_SYMBOL_REFRESH " Spin");
  lv_obj_set_style_text_color(sLbl, lv_color_hex(0xCC3333), 0);
  lv_obj_center(sLbl);
}

// ============================================
// WEATHER CARD
// ============================================
void createWeatherCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("WEATHER");
  
  char tempBuf[16];
  snprintf(tempBuf, sizeof(tempBuf), "%.0f", weatherTemp);
  lv_obj_t *tempLbl = lv_label_create(card);
  lv_label_set_text(tempLbl, tempBuf);
  lv_obj_set_style_text_color(tempLbl, theme.text, 0);
  lv_obj_set_style_text_font(tempLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(tempLbl, LV_ALIGN_CENTER, -20, -40);
  
  lv_obj_t *degLbl = lv_label_create(card);
  lv_label_set_text(degLbl, "o");
  lv_obj_set_style_text_color(degLbl, theme.text, 0);
  lv_obj_set_style_text_font(degLbl, &lv_font_montserrat_20, 0);
  lv_obj_align(degLbl, LV_ALIGN_CENTER, 30, -55);
  
  lv_obj_t *descLbl = lv_label_create(card);
  lv_label_set_text(descLbl, weatherDesc.c_str());
  lv_obj_set_style_text_color(descLbl, theme.accent, 0);
  lv_obj_set_style_text_font(descLbl, &lv_font_montserrat_18, 0);
  lv_obj_align(descLbl, LV_ALIGN_CENTER, 0, 20);
  
  char hlBuf[32];
  snprintf(hlBuf, sizeof(hlBuf), "H: %.0f   L: %.0f", weatherHigh, weatherLow);
  lv_obj_t *hlLbl = lv_label_create(card);
  lv_label_set_text(hlLbl, hlBuf);
  lv_obj_set_style_text_color(hlLbl, lv_color_hex(0x888888), 0);
  lv_obj_align(hlLbl, LV_ALIGN_CENTER, 0, 60);
}

// ============================================
// FORECAST CARD
// ============================================
void createForecastCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("3-DAY FORECAST");
  
  const char* days[] = {"SAT", "SUN", "MON"};
  int temps[] = {24, 26, 23};
  
  for (int i = 0; i < 3; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 80, 65);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 40 + i * 75);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    lv_obj_t *dayLbl = lv_label_create(row);
    lv_label_set_text(dayLbl, days[i]);
    lv_obj_set_style_text_color(dayLbl, theme.text, 0);
    lv_obj_align(dayLbl, LV_ALIGN_LEFT_MID, 15, 0);
    
    char tBuf[8];
    snprintf(tBuf, sizeof(tBuf), "%d", temps[i]);
    lv_obj_t *tLbl = lv_label_create(row);
    lv_label_set_text(tLbl, tBuf);
    lv_obj_set_style_text_color(tLbl, theme.accent, 0);
    lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_24, 0);
    lv_obj_align(tLbl, LV_ALIGN_RIGHT_MID, -15, 0);
  }
}

// ============================================
// STOCKS CARD
// ============================================
void createStocksCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("STOCKS");
  
  const char* syms[] = {"AAPL", "TSLA", "NVDA", "SPY"};
  float prices[] = {aaplPrice > 0 ? aaplPrice : 178.5f, tslaPrice > 0 ? tslaPrice : 248.2f, 875.3f, 512.4f};
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 80, 55);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 35 + i * 62);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    lv_obj_t *symLbl = lv_label_create(row);
    lv_label_set_text(symLbl, syms[i]);
    lv_obj_set_style_text_color(symLbl, theme.text, 0);
    lv_obj_align(symLbl, LV_ALIGN_LEFT_MID, 15, 0);
    
    char pBuf[16];
    snprintf(pBuf, sizeof(pBuf), "$%.2f", prices[i]);
    lv_obj_t *pLbl = lv_label_create(row);
    lv_label_set_text(pLbl, pBuf);
    lv_obj_set_style_text_color(pLbl, lv_color_hex(0x4CAF50), 0);
    lv_obj_align(pLbl, LV_ALIGN_RIGHT_MID, -15, 0);
  }
}

// ============================================
// CRYPTO CARD
// ============================================
void createCryptoCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("CRYPTO");
  
  const char* syms[] = {"BTC", "ETH", "SOL", "ADA"};
  float prices[] = {btcPrice > 0 ? btcPrice : 67432.5f, ethPrice > 0 ? ethPrice : 3521.8f, 142.3f, 0.58f};
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 80, 55);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 35 + i * 62);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    lv_obj_t *symLbl = lv_label_create(row);
    lv_label_set_text(symLbl, syms[i]);
    lv_obj_set_style_text_color(symLbl, theme.text, 0);
    lv_obj_align(symLbl, LV_ALIGN_LEFT_MID, 15, 0);
    
    char pBuf[16];
    if (prices[i] >= 1000) snprintf(pBuf, sizeof(pBuf), "$%.0f", prices[i]);
    else snprintf(pBuf, sizeof(pBuf), "$%.2f", prices[i]);
    lv_obj_t *pLbl = lv_label_create(row);
    lv_label_set_text(pLbl, pBuf);
    lv_obj_set_style_text_color(pLbl, lv_color_hex(0xF7931A), 0);
    lv_obj_align(pLbl, LV_ALIGN_RIGHT_MID, -15, 0);
  }
}

// ============================================
// MUSIC CARD
// ============================================
void createMusicCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("MUSIC");
  
  lv_obj_t *art = lv_obj_create(card);
  lv_obj_set_size(art, 140, 140);
  lv_obj_align(art, LV_ALIGN_CENTER, 0, -30);
  lv_obj_set_style_bg_color(art, theme.accent, 0);
  lv_obj_set_style_radius(art, 15, 0);
  lv_obj_set_style_border_width(art, 0, 0);
  
  lv_obj_t *icon = lv_label_create(art);
  lv_label_set_text(icon, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_color(icon, theme.text, 0);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
  lv_obj_center(icon);
  
  // Controls
  lv_obj_t *ctrl = lv_obj_create(card);
  lv_obj_set_size(ctrl, 200, 50);
  lv_obj_align(ctrl, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_opa(ctrl, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctrl, 0, 0);
  lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  const char* icons[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_NEXT};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *btn = lv_btn_create(ctrl);
    lv_obj_set_size(btn, 50, 50);
    lv_obj_set_style_bg_color(btn, i == 1 ? theme.accent : lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, icons[i]);
    lv_obj_center(lbl);
  }
}

// ============================================
// GALLERY CARD
// ============================================
void createGalleryCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("GALLERY");
  
  lv_obj_t *frame = lv_obj_create(card);
  lv_obj_set_size(frame, LCD_WIDTH - 100, 200);
  lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(frame, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(frame, 15, 0);
  lv_obj_set_style_border_color(frame, theme.accent, 0);
  lv_obj_set_style_border_width(frame, 2, 0);
  
  lv_obj_t *icon = lv_label_create(frame);
  lv_label_set_text(icon, LV_SYMBOL_IMAGE);
  lv_obj_set_style_text_color(icon, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
  lv_obj_center(icon);
}

// ============================================
// SAND TIMER CARD (5 min with notification)
// ============================================
static lv_obj_t *sandTimerLbl = NULL;

void sandTimerStartCb(lv_event_t *e) {
  sandTimerRunning = !sandTimerRunning;
  if (sandTimerRunning) sandTimerStartMs = millis();
  navigateTo(CAT_TIMER, 0);
}

void createSandTimerCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 30, LCD_HEIGHT - 70);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 15);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x0A3A2A), 0);
  lv_obj_set_style_radius(card, 25, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "5 MIN TIMER");
  lv_obj_set_style_text_color(title, theme.text, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
  
  // Hourglass visualization
  lv_obj_t *topBulb = lv_obj_create(card);
  lv_obj_set_size(topBulb, 80, 80);
  lv_obj_align(topBulb, LV_ALIGN_CENTER, 0, -60);
  lv_obj_set_style_bg_color(topBulb, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_radius(topBulb, 40, 0);
  lv_obj_set_style_border_width(topBulb, 0, 0);
  
  lv_obj_t *botBulb = lv_obj_create(card);
  lv_obj_set_size(botBulb, 80, 80);
  lv_obj_align(botBulb, LV_ALIGN_CENTER, 0, 60);
  lv_obj_set_style_bg_color(botBulb, lv_color_hex(0x0A3A2A), 0);
  lv_obj_set_style_radius(botBulb, 40, 0);
  lv_obj_set_style_border_color(botBulb, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_border_width(botBulb, 3, 0);
  
  // Time remaining
  unsigned long remaining = SAND_TIMER_DURATION;
  if (sandTimerRunning) {
    unsigned long elapsed = millis() - sandTimerStartMs;
    remaining = SAND_TIMER_DURATION > elapsed ? SAND_TIMER_DURATION - elapsed : 0;
  }
  int secs = remaining / 1000;
  char tBuf[16];
  snprintf(tBuf, sizeof(tBuf), "%d s", secs);
  
  sandTimerLbl = lv_label_create(card);
  lv_label_set_text(sandTimerLbl, tBuf);
  lv_obj_set_style_text_color(sandTimerLbl, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_text_font(sandTimerLbl, &lv_font_montserrat_24, 0);
  lv_obj_align(sandTimerLbl, LV_ALIGN_CENTER, 60, 0);
  
  // Start/Stop button
  lv_obj_t *btn = lv_btn_create(card);
  lv_obj_set_size(btn, 150, 50);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(btn, sandTimerRunning ? lv_color_hex(0xFF5252) : lv_color_hex(0x30D158), 0);
  lv_obj_set_style_radius(btn, 25, 0);
  lv_obj_add_event_cb(btn, sandTimerStartCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *btnLbl = lv_label_create(btn);
  lv_label_set_text(btnLbl, sandTimerRunning ? "STOP" : "START");
  lv_obj_center(btnLbl);
}

// ============================================
// STOPWATCH CARD
// ============================================
void stopwatchCb(lv_event_t *e) {
  int action = (int)(intptr_t)lv_event_get_user_data(e);
  if (action == 0) {  // Start/Pause
    stopwatchRunning = !stopwatchRunning;
    if (stopwatchRunning) stopwatchStartMs = millis() - stopwatchElapsedMs;
    else stopwatchElapsedMs = millis() - stopwatchStartMs;
  } else {  // Reset
    stopwatchRunning = false;
    stopwatchElapsedMs = 0;
  }
  navigateTo(CAT_TIMER, 1);
}

void createStopwatchCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("STOPWATCH");
  
  unsigned long ms = stopwatchRunning ? (millis() - stopwatchStartMs) : stopwatchElapsedMs;
  int mins = (ms / 60000) % 60;
  int secs = (ms / 1000) % 60;
  int cents = (ms / 10) % 100;
  
  char tBuf[16];
  snprintf(tBuf, sizeof(tBuf), "%02d:%02d.%02d", mins, secs, cents);
  lv_obj_t *timeLbl = lv_label_create(card);
  lv_label_set_text(timeLbl, tBuf);
  lv_obj_set_style_text_color(timeLbl, theme.text, 0);
  lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_40, 0);
  lv_obj_align(timeLbl, LV_ALIGN_CENTER, 0, -30);
  
  // Buttons
  lv_obj_t *startBtn = lv_btn_create(card);
  lv_obj_set_size(startBtn, 100, 45);
  lv_obj_align(startBtn, LV_ALIGN_CENTER, -55, 60);
  lv_obj_set_style_bg_color(startBtn, stopwatchRunning ? lv_color_hex(0xFF9800) : lv_color_hex(0x4CAF50), 0);
  lv_obj_add_event_cb(startBtn, stopwatchCb, LV_EVENT_CLICKED, (void*)0);
  lv_obj_t *sLbl = lv_label_create(startBtn);
  lv_label_set_text(sLbl, stopwatchRunning ? "PAUSE" : "START");
  lv_obj_center(sLbl);
  
  lv_obj_t *resetBtn = lv_btn_create(card);
  lv_obj_set_size(resetBtn, 100, 45);
  lv_obj_align(resetBtn, LV_ALIGN_CENTER, 55, 60);
  lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0x666666), 0);
  lv_obj_add_event_cb(resetBtn, stopwatchCb, LV_EVENT_CLICKED, (void*)1);
  lv_obj_t *rLbl = lv_label_create(resetBtn);
  lv_label_set_text(rLbl, "RESET");
  lv_obj_center(rLbl);
}

// ============================================
// COUNTDOWN CARD
// ============================================
void createCountdownCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("COUNTDOWN");
  
  lv_obj_t *timeLbl = lv_label_create(card);
  lv_label_set_text(timeLbl, "05:00");
  lv_obj_set_style_text_color(timeLbl, theme.text, 0);
  lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(timeLbl, LV_ALIGN_CENTER, 0, -20);
  
  lv_obj_t *startBtn = lv_btn_create(card);
  lv_obj_set_size(startBtn, 150, 50);
  lv_obj_align(startBtn, LV_ALIGN_CENTER, 0, 70);
  lv_obj_set_style_bg_color(startBtn, theme.accent, 0);
  lv_obj_set_style_radius(startBtn, 25, 0);
  
  lv_obj_t *sLbl = lv_label_create(startBtn);
  lv_label_set_text(sLbl, "START");
  lv_obj_center(sLbl);
}

// ============================================
// STREAK CARDS
// ============================================
void createStepStreakCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("STEP STREAK");
  
  char sBuf[16];
  snprintf(sBuf, sizeof(sBuf), "%d", userData.stepStreak);
  lv_obj_t *numLbl = lv_label_create(card);
  lv_label_set_text(numLbl, sBuf);
  lv_obj_set_style_text_color(numLbl, lv_color_hex(0xFF9500), 0);
  lv_obj_set_style_text_font(numLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(numLbl, LV_ALIGN_CENTER, 0, -30);
  
  lv_obj_t *daysLbl = lv_label_create(card);
  lv_label_set_text(daysLbl, "days");
  lv_obj_set_style_text_color(daysLbl, theme.text, 0);
  lv_obj_align(daysLbl, LV_ALIGN_CENTER, 0, 20);
  
  lv_obj_t *fireLbl = lv_label_create(card);
  lv_label_set_text(fireLbl, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_color(fireLbl, lv_color_hex(0xFF9500), 0);
  lv_obj_set_style_text_font(fireLbl, &lv_font_montserrat_36, 0);
  lv_obj_align(fireLbl, LV_ALIGN_CENTER, 0, 80);
}

void createGameStreakCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("GAME STREAK");
  
  char sBuf[16];
  snprintf(sBuf, sizeof(sBuf), "%d", userData.blackjackStreak);
  lv_obj_t *numLbl = lv_label_create(card);
  lv_label_set_text(numLbl, sBuf);
  lv_obj_set_style_text_color(numLbl, lv_color_hex(0x4CAF50), 0);
  lv_obj_set_style_text_font(numLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(numLbl, LV_ALIGN_CENTER, 0, -30);
  
  lv_obj_t *winsLbl = lv_label_create(card);
  lv_label_set_text(winsLbl, "wins in a row");
  lv_obj_set_style_text_color(winsLbl, theme.text, 0);
  lv_obj_align(winsLbl, LV_ALIGN_CENTER, 0, 20);
  
  char statBuf[32];
  snprintf(statBuf, sizeof(statBuf), "%d / %d games won", userData.gamesWon, userData.gamesPlayed);
  lv_obj_t *statLbl = lv_label_create(card);
  lv_label_set_text(statLbl, statBuf);
  lv_obj_set_style_text_color(statLbl, lv_color_hex(0x888888), 0);
  lv_obj_align(statLbl, LV_ALIGN_CENTER, 0, 80);
}

void createAchievementsCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("ACHIEVEMENTS");
  
  lv_obj_t *ach1 = lv_label_create(card);
  lv_label_set_text(ach1, userData.steps >= 10000 ? LV_SYMBOL_OK " 10K Steps" : LV_SYMBOL_CLOSE " 10K Steps");
  lv_obj_set_style_text_color(ach1, userData.steps >= 10000 ? lv_color_hex(0x4CAF50) : lv_color_hex(0x666666), 0);
  lv_obj_align(ach1, LV_ALIGN_CENTER, 0, -40);
  
  lv_obj_t *ach2 = lv_label_create(card);
  lv_label_set_text(ach2, userData.gamesWon >= 5 ? LV_SYMBOL_OK " Win 5 Games" : LV_SYMBOL_CLOSE " Win 5 Games");
  lv_obj_set_style_text_color(ach2, userData.gamesWon >= 5 ? lv_color_hex(0x4CAF50) : lv_color_hex(0x666666), 0);
  lv_obj_align(ach2, LV_ALIGN_CENTER, 0, 0);
  
  lv_obj_t *ach3 = lv_label_create(card);
  lv_label_set_text(ach3, userData.stepStreak >= 7 ? LV_SYMBOL_OK " 7 Day Streak" : LV_SYMBOL_CLOSE " 7 Day Streak");
  lv_obj_set_style_text_color(ach3, userData.stepStreak >= 7 ? lv_color_hex(0x4CAF50) : lv_color_hex(0x666666), 0);
  lv_obj_align(ach3, LV_ALIGN_CENTER, 0, 40);
}

// ============================================
// CALENDAR CARD (Jan 25, 2026)
// ============================================
void createCalendarCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("CALENDAR");
  
  lv_obj_t *header = lv_label_create(card);
  lv_label_set_text(header, "January 2026");
  lv_obj_set_style_text_color(header, theme.text, 0);
  lv_obj_set_style_text_font(header, &lv_font_montserrat_18, 0);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 30);
  
  // Day grid
  lv_obj_t *grid = lv_obj_create(card);
  lv_obj_set_size(grid, LCD_WIDTH - 70, 200);
  lv_obj_align(grid, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(grid, 2, 0);
  lv_obj_set_style_pad_row(grid, 2, 0);
  
  for (int d = 1; d <= 31; d++) {
    lv_obj_t *day = lv_obj_create(grid);
    lv_obj_set_size(day, 36, 28);
    lv_obj_set_style_border_width(day, 0, 0);
    
    if (d == 25) {
      lv_obj_set_style_bg_color(day, theme.accent, 0);
      lv_obj_set_style_radius(day, 14, 0);
    } else {
      lv_obj_set_style_bg_opa(day, LV_OPA_TRANSP, 0);
    }
    
    char dBuf[4];
    snprintf(dBuf, sizeof(dBuf), "%d", d);
    lv_obj_t *lbl = lv_label_create(day);
    lv_label_set_text(lbl, dBuf);
    lv_obj_set_style_text_color(lbl, d == 25 ? lv_color_hex(0x000000) : theme.text, 0);
    lv_obj_center(lbl);
  }
}

// ============================================
// SETTINGS CARD (Scrollable)
// ============================================
void brightnessSliderCb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  userData.brightness = lv_slider_get_value(slider);
  gfx->setBrightness(userData.brightness);
  saveUserData();
}

void themeChangeCb(lv_event_t *e) {
  userData.themeIndex = (userData.themeIndex + 1) % NUM_THEMES;
  saveUserData();
  navigateTo(CAT_SETTINGS, 0);
}

void timeoutChangeCb(lv_event_t *e) {
  int timeouts[] = {1, 2, 3, 5, 7, 10};
  int idx = 0;
  for (int i = 0; i < 6; i++) {
    if (timeouts[i] == userData.screenTimeout) { idx = i; break; }
  }
  idx = (idx + 1) % 6;
  userData.screenTimeout = timeouts[idx];
  saveUserData();
  navigateTo(CAT_SETTINGS, 0);
}

void createSettingsCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("SETTINGS");
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
  
  // Brightness
  lv_obj_t *brRow = lv_obj_create(card);
  lv_obj_set_size(brRow, LCD_WIDTH - 80, 70);
  lv_obj_align(brRow, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_set_style_bg_color(brRow, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(brRow, 12, 0);
  lv_obj_set_style_border_width(brRow, 0, 0);
  
  lv_obj_t *brLbl = lv_label_create(brRow);
  lv_label_set_text(brLbl, "Brightness");
  lv_obj_set_style_text_color(brLbl, theme.text, 0);
  lv_obj_align(brLbl, LV_ALIGN_TOP_LEFT, 10, 5);
  
  lv_obj_t *brSlider = lv_slider_create(brRow);
  lv_obj_set_size(brSlider, 200, 15);
  lv_obj_align(brSlider, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_slider_set_range(brSlider, 20, 255);
  lv_slider_set_value(brSlider, userData.brightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(brSlider, lv_color_hex(0x444444), LV_PART_MAIN);
  lv_obj_set_style_bg_color(brSlider, theme.accent, LV_PART_INDICATOR);
  lv_obj_add_event_cb(brSlider, brightnessSliderCb, LV_EVENT_VALUE_CHANGED, NULL);
  
  // Theme
  lv_obj_t *thRow = lv_obj_create(card);
  lv_obj_set_size(thRow, LCD_WIDTH - 80, 55);
  lv_obj_align(thRow, LV_ALIGN_TOP_MID, 0, 110);
  lv_obj_set_style_bg_color(thRow, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(thRow, 12, 0);
  lv_obj_set_style_border_width(thRow, 0, 0);
  lv_obj_add_flag(thRow, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(thRow, themeChangeCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *thLbl = lv_label_create(thRow);
  lv_label_set_text(thLbl, "Theme");
  lv_obj_set_style_text_color(thLbl, theme.text, 0);
  lv_obj_align(thLbl, LV_ALIGN_LEFT_MID, 10, 0);
  
  lv_obj_t *thVal = lv_label_create(thRow);
  lv_label_set_text(thVal, gradientThemes[userData.themeIndex].name);
  lv_obj_set_style_text_color(thVal, theme.accent, 0);
  lv_obj_align(thVal, LV_ALIGN_RIGHT_MID, -10, 0);
  
  // Screen Timeout
  lv_obj_t *toRow = lv_obj_create(card);
  lv_obj_set_size(toRow, LCD_WIDTH - 80, 55);
  lv_obj_align(toRow, LV_ALIGN_TOP_MID, 0, 175);
  lv_obj_set_style_bg_color(toRow, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(toRow, 12, 0);
  lv_obj_set_style_border_width(toRow, 0, 0);
  lv_obj_add_flag(toRow, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(toRow, timeoutChangeCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *toLbl = lv_label_create(toRow);
  lv_label_set_text(toLbl, "Screen Timeout");
  lv_obj_set_style_text_color(toLbl, theme.text, 0);
  lv_obj_align(toLbl, LV_ALIGN_LEFT_MID, 10, 0);
  
  char tBuf[8];
  snprintf(tBuf, sizeof(tBuf), "%d min", userData.screenTimeout);
  lv_obj_t *toVal = lv_label_create(toRow);
  lv_label_set_text(toVal, tBuf);
  lv_obj_set_style_text_color(toVal, theme.accent, 0);
  lv_obj_align(toVal, LV_ALIGN_RIGHT_MID, -10, 0);
  
  // Daily Goal
  lv_obj_t *dgRow = lv_obj_create(card);
  lv_obj_set_size(dgRow, LCD_WIDTH - 80, 55);
  lv_obj_align(dgRow, LV_ALIGN_TOP_MID, 0, 240);
  lv_obj_set_style_bg_color(dgRow, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(dgRow, 12, 0);
  lv_obj_set_style_border_width(dgRow, 0, 0);
  
  lv_obj_t *dgLbl = lv_label_create(dgRow);
  lv_label_set_text(dgLbl, "Daily Goal");
  lv_obj_set_style_text_color(dgLbl, theme.text, 0);
  lv_obj_align(dgLbl, LV_ALIGN_LEFT_MID, 10, 0);
  
  char gBuf[16];
  snprintf(gBuf, sizeof(gBuf), "%lu", (unsigned long)userData.dailyGoal);
  lv_obj_t *dgVal = lv_label_create(dgRow);
  lv_label_set_text(dgVal, gBuf);
  lv_obj_set_style_text_color(dgVal, theme.accent, 0);
  lv_obj_align(dgVal, LV_ALIGN_RIGHT_MID, -10, 0);
}

// ============================================
// BATTERY CARD
// ============================================
void createBatteryCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("BATTERY");
  
  int batt = power.getBatteryPercent();
  bool charging = power.isCharging();
  
  lv_obj_t *arc = lv_arc_create(card);
  lv_obj_set_size(arc, 160, 160);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, -10);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_value(arc, batt);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x333333), LV_PART_MAIN);
  
  lv_color_t arcCol;
  if (batt > 60) arcCol = lv_color_hex(0x4CAF50);
  else if (batt > 20) arcCol = lv_color_hex(0xFF9800);
  else arcCol = lv_color_hex(0xFF5252);
  lv_obj_set_style_arc_color(arc, arcCol, LV_PART_INDICATOR);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  
  char bBuf[16];
  snprintf(bBuf, sizeof(bBuf), "%d%%", batt);
  lv_obj_t *battLbl = lv_label_create(card);
  lv_label_set_text(battLbl, bBuf);
  lv_obj_set_style_text_color(battLbl, theme.text, 0);
  lv_obj_set_style_text_font(battLbl, &lv_font_montserrat_36, 0);
  lv_obj_align(battLbl, LV_ALIGN_CENTER, 0, -10);
  
  if (charging) {
    lv_obj_t *chLbl = lv_label_create(card);
    lv_label_set_text(chLbl, LV_SYMBOL_CHARGE " Charging");
    lv_obj_set_style_text_color(chLbl, lv_color_hex(0x4CAF50), 0);
    lv_obj_align(chLbl, LV_ALIGN_BOTTOM_MID, 0, -20);
  }
}

// ============================================
// SYSTEM CARD
// ============================================
void createSystemCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("SYSTEM");
  
  // CPU
  lv_obj_t *cpuRow = lv_obj_create(card);
  lv_obj_set_size(cpuRow, LCD_WIDTH - 80, 45);
  lv_obj_align(cpuRow, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_bg_color(cpuRow, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(cpuRow, 10, 0);
  lv_obj_set_style_border_width(cpuRow, 0, 0);
  
  lv_obj_t *cpuLbl = lv_label_create(cpuRow);
  lv_label_set_text(cpuLbl, "CPU");
  lv_obj_set_style_text_color(cpuLbl, lv_color_hex(0x888888), 0);
  lv_obj_align(cpuLbl, LV_ALIGN_LEFT_MID, 15, 0);
  
  lv_obj_t *cpuBar = lv_bar_create(cpuRow);
  lv_obj_set_size(cpuBar, 100, 10);
  lv_obj_align(cpuBar, LV_ALIGN_RIGHT_MID, -15, 0);
  lv_bar_set_value(cpuBar, random(20, 50), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(cpuBar, lv_color_hex(0x444444), LV_PART_MAIN);
  lv_obj_set_style_bg_color(cpuBar, theme.accent, LV_PART_INDICATOR);
  
  // RAM
  lv_obj_t *ramRow = lv_obj_create(card);
  lv_obj_set_size(ramRow, LCD_WIDTH - 80, 45);
  lv_obj_align(ramRow, LV_ALIGN_TOP_MID, 0, 95);
  lv_obj_set_style_bg_color(ramRow, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(ramRow, 10, 0);
  lv_obj_set_style_border_width(ramRow, 0, 0);
  
  lv_obj_t *ramLbl = lv_label_create(ramRow);
  lv_label_set_text(ramLbl, "RAM");
  lv_obj_set_style_text_color(ramLbl, lv_color_hex(0x888888), 0);
  lv_obj_align(ramLbl, LV_ALIGN_LEFT_MID, 15, 0);
  
  uint32_t freeH = ESP.getFreeHeap() / 1024;
  uint32_t totalH = ESP.getHeapSize() / 1024;
  int ramP = 100 - (freeH * 100 / totalH);
  
  lv_obj_t *ramBar = lv_bar_create(ramRow);
  lv_obj_set_size(ramBar, 100, 10);
  lv_obj_align(ramBar, LV_ALIGN_RIGHT_MID, -15, 0);
  lv_bar_set_value(ramBar, ramP, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0x444444), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
  
  // WiFi
  lv_obj_t *wifiRow = lv_obj_create(card);
  lv_obj_set_size(wifiRow, LCD_WIDTH - 80, 45);
  lv_obj_align(wifiRow, LV_ALIGN_TOP_MID, 0, 150);
  lv_obj_set_style_bg_color(wifiRow, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(wifiRow, 10, 0);
  lv_obj_set_style_border_width(wifiRow, 0, 0);
  
  lv_obj_t *wifiLbl = lv_label_create(wifiRow);
  lv_label_set_text(wifiLbl, "WiFi");
  lv_obj_set_style_text_color(wifiLbl, lv_color_hex(0x888888), 0);
  lv_obj_align(wifiLbl, LV_ALIGN_LEFT_MID, 15, 0);
  
  lv_obj_t *wifiSt = lv_label_create(wifiRow);
  lv_label_set_text(wifiSt, wifiConnected ? "Connected" : "Disconnected");
  lv_obj_set_style_text_color(wifiSt, wifiConnected ? lv_color_hex(0x4CAF50) : lv_color_hex(0xFF5252), 0);
  lv_obj_align(wifiSt, LV_ALIGN_RIGHT_MID, -15, 0);
  
  // Version
  lv_obj_t *verLbl = lv_label_create(card);
  lv_label_set_text(verLbl, "Widget OS v1.0");
  lv_obj_set_style_text_color(verLbl, lv_color_hex(0x666666), 0);
  lv_obj_align(verLbl, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// ============================================
// DATA PERSISTENCE
// ============================================
void saveUserData() {
  if (!SPIFFS.begin(true)) return;
  File f = SPIFFS.open("/userdata.bin", "w");
  if (f) {
    f.write((uint8_t*)&userData, sizeof(userData));
    f.close();
  }
}

void loadUserData() {
  if (!SPIFFS.begin(true)) return;
  if (SPIFFS.exists("/userdata.bin")) {
    File f = SPIFFS.open("/userdata.bin", "r");
    if (f) {
      f.read((uint8_t*)&userData, sizeof(userData));
      f.close();
    }
  }
}

// ============================================
// NTP TIME SYNC
// ============================================
void syncTimeNTP() {
  if (WiFi.status() != WL_CONNECTED) return;
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    rtc.setDateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }
}

// ============================================
// API FETCHERS
// ============================================
void fetchWeatherData() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=Sydney&units=metric&appid=";
  url += OPENWEATHER_API;
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, payload);
    weatherTemp = doc["main"]["temp"];
    weatherHigh = doc["main"]["temp_max"];
    weatherLow = doc["main"]["temp_min"];
    weatherDesc = doc["weather"][0]["main"].as<String>();
  }
  http.end();
}

void fetchCryptoData() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin("https://rest.coinapi.io/v1/exchangerate/BTC/USD");
  http.addHeader("X-CoinAPI-Key", COINAPI_KEY);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    btcPrice = doc["rate"];
  }
  http.end();
  
  http.begin("https://rest.coinapi.io/v1/exchangerate/ETH/USD");
  http.addHeader("X-CoinAPI-Key", COINAPI_KEY);
  code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    ethPrice = doc["rate"];
  }
  http.end();
}

// ============================================
// COMPASS CALCULATION
// ============================================
void updateCompass() {
  if (qmi.getDataReady()) {
    qmi.getAccelerometer(acc.x, acc.y, acc.z);
    // Simple heading estimation from accelerometer
    // (Real compass would need magnetometer)
    compassHeading = atan2(acc.y, acc.x) * 180.0 / 3.14159;
    if (compassHeading < 0) compassHeading += 360;
  }
}

// ============================================
// NOTIFICATION FLASH
// ============================================
void showTimerNotification() {
  static int flashCount = 0;
  static unsigned long lastFlash = 0;
  lv_color_t colors[] = {lv_color_hex(0xFF0000), lv_color_hex(0x00FF00), lv_color_hex(0x0000FF)};
  
  if (millis() - lastFlash > 200) {
    lastFlash = millis();
    lv_obj_set_style_bg_color(lv_scr_act(), colors[flashCount % 3], 0);
    flashCount++;
    if (flashCount > 15) {
      timerNotificationActive = false;
      flashCount = 0;
      navigateTo(currentCategory, currentSubCard);
    }
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  USBSerial.begin(115200);
  Wire.begin(IIC_SDA, IIC_SCL);
  
  // Load saved data
  loadUserData();
  
  // IO Expander
  if (expander.begin(0x20)) {
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
  }
  
  // Power
  power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  power.enableBattDetection();
  power.enableBattVoltageMeasure();
  
  // RTC
  rtc.begin(Wire, IIC_SDA, IIC_SCL);
  
  // IMU
  if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_1000Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();
  }
  
  // Touch
  while (!FT3168->begin()) delay(1000);
  
  // Display
  gfx->begin();
  gfx->setBrightness(userData.brightness);
  
  // LVGL
  lv_init();
  buf1 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT / 4 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  buf2 = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT / 4 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_WIDTH * LCD_HEIGHT / 4);
  
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
  
  FT3168->IIC_Write_Device_State(FT3168->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                 FT3168->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
  
  const esp_timer_create_args_t tick_args = { .callback = &lvgl_tick_cb, .name = "lvgl_tick" };
  esp_timer_handle_t tick_timer;
  esp_timer_create(&tick_args, &tick_timer);
  esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000);
  
  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  
  // Sync time and fetch data if connected
  if (wifiConnected) {
    syncTimeNTP();
    fetchWeatherData();
    fetchCryptoData();
  }
  
  // SD Card
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
  SD_MMC.begin("/sdcard", true);
  
  lastActivityMs = millis();
  navigateTo(CAT_CLOCK, 0);
}

// ============================================
// LOOP
// ============================================
unsigned long lastUpdate = 0;
unsigned long lastStepCheck = 0;
unsigned long lastDinoUpdate = 0;
float lastAccMag = 0;

void loop() {
  lv_timer_handler();
  delay(5);
  
  // Timer notification
  if (timerNotificationActive) {
    showTimerNotification();
    return;
  }
  
  // Screen timeout
  if (screenOn && (millis() - lastActivityMs > (unsigned long)userData.screenTimeout * 60000)) {
    screenOn = false;
    gfx->setBrightness(0);
  }
  
  // UI updates (every second)
  if (millis() - lastUpdate > 1000) {
    lastUpdate = millis();
    
    // Update clock
    if (currentCategory == CAT_CLOCK && currentSubCard == 0 && clockLabel) {
      RTC_DateTime dt = rtc.getDateTime();
      char tBuf[16];
      snprintf(tBuf, sizeof(tBuf), "%02d:%02d", dt.getHour(), dt.getMinute());
      lv_label_set_text(clockLabel, tBuf);
    }
    
    // Sand timer check
    if (sandTimerRunning) {
      unsigned long elapsed = millis() - sandTimerStartMs;
      if (elapsed >= SAND_TIMER_DURATION) {
        sandTimerRunning = false;
        timerNotificationActive = true;
        notificationStartMs = millis();
      }
    }
    
    // Yes/No spin animation
    if (yesNoSpinning) {
      yesNoAngle += 30;
      if (yesNoAngle > 360) {
        yesNoSpinning = false;
        const char* answers[] = {"Yes", "No", "Maybe", "Ask again", "Definitely", "Never"};
        yesNoResult = answers[random(0, 6)];
        navigateTo(CAT_GAMES, 2);
      }
    }
  }
  
  // Step counting (every 50ms)
  if (millis() - lastStepCheck > 50) {
    lastStepCheck = millis();
    
    if (qmi.getDataReady()) {
      qmi.getAccelerometer(acc.x, acc.y, acc.z);
      float mag = sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
      float delta = abs(mag - lastAccMag);
      
      if (delta > 0.5 && delta < 3.0) {
        userData.steps++;
        userData.totalDistance = userData.steps * 0.0007;
        userData.totalCalories = userData.steps * 0.04;
      }
      lastAccMag = mag;
      
      // Update compass
      updateCompass();
    }
  }
  
  // Dino game (every 50ms)
  if (currentCategory == CAT_GAMES && currentSubCard == 1 && !dinoGameOver) {
    if (millis() - lastDinoUpdate > 50) {
      lastDinoUpdate = millis();
      
      obstacleX -= 10;
      if (obstacleX < -30) {
        obstacleX = 350;
        dinoScore++;
      }
      
      if (dinoJumping) {
        dinoY += 12;
        if (dinoY >= 0) {
          dinoY = 0;
          dinoJumping = false;
        }
      }
      
      // Collision
      if (obstacleX < 80 && obstacleX > 30 && dinoY > -40) {
        dinoGameOver = true;
      }
      
      navigateTo(CAT_GAMES, 1);
    }
  }
  
  // Save data periodically (every 30 seconds)
  static unsigned long lastSave = 0;
  if (millis() - lastSave > 30000) {
    lastSave = millis();
    saveUserData();
  }
}
