/**
 * Widget OS v2.0 - Premium Edition
 * ESP32-S3 Touch AMOLED 1.8" Smartwatch Firmware
 * 
 * MAJOR IMPROVEMENTS:
 * - Full sensor fusion compass with complementary/Kalman filter
 * - Smooth animated navigation transitions
 * - Apple Watch-style premium widget designs
 * - Enhanced Blackjack with visual cards
 * - Polished game UIs
 * - Tilt-based orientation display
 * - Relative rotation tracking
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
// CONFIGURATION (Defaults - can be overridden by SD card)
// ============================================
char wifiSSID[64] = "Optus_9D2E3D";
char wifiPassword[64] = "snucktemptGLeQU";
char weatherCity[32] = "Perth";  // Default city for weather
char weatherCountry[8] = "AU";   // Country code
const char* NTP_SERVER = "pool.ntp.org";
long gmtOffsetSec = 8 * 3600;    // GMT+8 for Perth
const int DAYLIGHT_OFFSET_SEC = 0;

// API Keys
const char* OPENWEATHER_API = "3795c13a0d3f7e17799d638edda60e3c";
const char* ALPHAVANTAGE_API = "UHLX28BF7GQ4T8J3";
const char* COINAPI_KEY = "11afad22-b6ea-4f18-9056-c7a1d7ed14a1";

// Save interval - 2 hours to reduce lag
#define SAVE_INTERVAL_MS 7200000  // 2 hours

// ============================================
// SD CARD WIFI CONFIG
// ============================================
#define WIFI_CONFIG_PATH "/wifi/config.txt"

bool loadWiFiFromSD() {
  File file = SD_MMC.open(WIFI_CONFIG_PATH, "r");
  if (!file) {
    USBSerial.println("No WiFi config on SD card, using defaults");
    return false;
  }
  
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    
    if (line.startsWith("SSID=")) {
      strncpy(wifiSSID, line.substring(5).c_str(), 63);
      wifiSSID[63] = '\0';
      USBSerial.printf("WiFi SSID from SD: %s\n", wifiSSID);
    }
    else if (line.startsWith("PASSWORD=")) {
      strncpy(wifiPassword, line.substring(9).c_str(), 63);
      wifiPassword[63] = '\0';
      USBSerial.println("WiFi Password loaded from SD");
    }
    else if (line.startsWith("CITY=")) {
      strncpy(weatherCity, line.substring(5).c_str(), 31);
      weatherCity[31] = '\0';
      USBSerial.printf("Weather city from SD: %s\n", weatherCity);
    }
    else if (line.startsWith("COUNTRY=")) {
      strncpy(weatherCountry, line.substring(8).c_str(), 7);
      weatherCountry[7] = '\0';
    }
    else if (line.startsWith("GMT_OFFSET=")) {
      gmtOffsetSec = line.substring(11).toInt() * 3600;
      USBSerial.printf("GMT offset: %ld\n", gmtOffsetSec);
    }
  }
  file.close();
  return true;
}

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
const int maxSubCards[] = {2, 3, 4, 3, 2, 2, 2, 4, 3, 1, 1, 2};  // Timer now has 4 sub-cards (added Breathe)

// Animation state
bool isTransitioning = false;
int transitionDir = 0;  // -1 left, 1 right, -2 up, 2 down
float transitionProgress = 0.0;
unsigned long transitionStartMs = 0;
const unsigned long TRANSITION_DURATION = 200;  // ms

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
  int screenTimeout;
  int themeIndex;
  float totalDistance;
  float totalCalories;
  int compassMode;  // 0=fusion, 1=tilt, 2=gyro
  int wallpaperIndex;  // 0=none, 1+=wallpapers from SD or gradients
} userData = {0, 10000, 7, 0, 0, 0, 200, 1, 0, 0.0, 0.0, 0, 0};

// ============================================
// SD CARD WALLPAPER SYSTEM
// ============================================
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

// Gradient fallbacks for when SD images aren't available
struct WallpaperTheme {
  const char* name;
  lv_color_t top;
  lv_color_t mid1;
  lv_color_t mid2;
  lv_color_t bottom;
};

// Beautiful scenic gradient wallpapers (fallback)
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

// Function to scan SD card for wallpaper images
void scanSDWallpapers() {
  numSDWallpapers = 0;
  sdWallpapersLoaded = true;
  
  // Add "None" as first option
  strcpy(sdWallpapers[0].filename, "");
  strcpy(sdWallpapers[0].displayName, "None");
  sdWallpapers[0].valid = true;
  numSDWallpapers = 1;
  
  File root = SD_MMC.open(WALLPAPER_PATH);
  if (!root || !root.isDirectory()) {
    USBSerial.println("Wallpaper folder not found, using gradients");
    return;
  }
  
  File file = root.openNextFile();
  while (file && numSDWallpapers < MAX_SD_WALLPAPERS) {
    if (!file.isDirectory()) {
      String fname = file.name();
      // Check for supported image formats (BMP, PNG, JPG for LVGL with decoders)
      if (fname.endsWith(".bmp") || fname.endsWith(".BMP") ||
          fname.endsWith(".png") || fname.endsWith(".PNG") ||
          fname.endsWith(".jpg") || fname.endsWith(".JPG") ||
          fname.endsWith(".jpeg") || fname.endsWith(".JPEG")) {
        
        snprintf(sdWallpapers[numSDWallpapers].filename, 64, "S:%s/%s", WALLPAPER_PATH, file.name());
        
        // Create display name from filename (remove extension)
        String dispName = fname;
        int dotIdx = dispName.lastIndexOf('.');
        if (dotIdx > 0) dispName = dispName.substring(0, dotIdx);
        dispName.replace("_", " ");
        dispName.replace("-", " ");
        strncpy(sdWallpapers[numSDWallpapers].displayName, dispName.c_str(), 31);
        sdWallpapers[numSDWallpapers].displayName[31] = '\0';
        
        sdWallpapers[numSDWallpapers].valid = true;
        numSDWallpapers++;
        
        USBSerial.printf("Found wallpaper: %s\n", file.name());
      }
    }
    file = root.openNextFile();
  }
  root.close();
  
  USBSerial.printf("Total wallpapers found: %d\n", numSDWallpapers - 1);
}

// Get total number of available wallpapers
int getTotalWallpapers() {
  if (numSDWallpapers > 1) {
    return numSDWallpapers;  // SD wallpapers available
  }
  return NUM_GRADIENT_WALLPAPERS;  // Fall back to gradients
}

// Get wallpaper name by index
const char* getWallpaperName(int idx) {
  if (numSDWallpapers > 1 && idx < numSDWallpapers) {
    return sdWallpapers[idx].displayName;
  }
  if (idx < NUM_GRADIENT_WALLPAPERS) {
    return gradientWallpapers[idx].name;
  }
  return "Unknown";
}

// ============================================
// RUNTIME STATE
// ============================================
bool wifiConnected = false;
unsigned long lastActivityMs = 0;
bool screenOn = true;

// ============================================
// SENSOR FUSION COMPASS STATE
// ============================================
// Complementary filter coefficients
const float ALPHA = 0.98;  // Gyro trust factor
const float BETA = 0.02;   // Accel trust factor

// Orientation angles (Euler)
float roll = 0.0;
float pitch = 0.0;
float yaw = 0.0;

// Gyroscope integration
float gyroRoll = 0.0;
float gyroPitch = 0.0;
float gyroYaw = 0.0;

// Accelerometer-derived angles
float accelRoll = 0.0;
float accelPitch = 0.0;

// Compass heading (fused)
float compassHeading = 0.0;
float compassHeadingSmooth = 0.0;

// Initial orientation reference
float initialYaw = 0.0;
bool compassCalibrated = false;

// Tilt values
float tiltX = 0.0;
float tiltY = 0.0;

// Last update timestamp
unsigned long lastSensorUpdate = 0;

// ============================================
// WEATHER DATA
// ============================================
float weatherTemp = 24.0;
String weatherDesc = "Sunny";
float weatherHigh = 28.0;
float weatherLow = 18.0;

// Stocks/Crypto
float btcPrice = 0, ethPrice = 0;
float aaplPrice = 0, tslaPrice = 0;

// ============================================
// TIMER STATE
// ============================================
bool sandTimerRunning = false;
unsigned long sandTimerStartMs = 0;
const unsigned long SAND_TIMER_DURATION = 5 * 60 * 1000;
bool timerNotificationActive = false;
unsigned long notificationStartMs = 0;

// Stopwatch
bool stopwatchRunning = false;
unsigned long stopwatchStartMs = 0;
unsigned long stopwatchElapsedMs = 0;

// ============================================
// BLACKJACK STATE
// ============================================
int playerCards[10], dealerCards[10];
int playerCount = 0, dealerCount = 0;
int blackjackBet = 100;
bool blackjackGameActive = false;
bool playerStand = false;

// Yes/No
bool yesNoSpinning = false;
int yesNoAngle = 0;
String yesNoResult = "?";

// Dino game - improved physics
int dinoY = 0;
float dinoVelocity = 0;
bool dinoJumping = false;
int dinoScore = 0;
int obstacleX = 350;
bool dinoGameOver = false;
const float GRAVITY = 4.0;
const float JUMP_FORCE = -22.0;

// Touch
int32_t touchStartX = 0, touchStartY = 0;
int32_t touchCurrentX = 0, touchCurrentY = 0;
bool touchActive = false;
unsigned long touchStartMs = 0;

// ============================================
// PREMIUM GRADIENT THEMES (Apple Watch Style)
// ============================================
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

// ============================================
// FUNCTION PROTOTYPES
// ============================================
void navigateTo(int category, int subCard);
void handleSwipe(int dx, int dy);
void saveUserData();
void loadUserData();
void syncTimeNTP();
void fetchWeatherData();
void fetchCryptoData();
void updateSensorFusion();
void calibrateCompass();

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
// TOUCHPAD READ WITH SMOOTH GESTURE
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
      
      // Smooth swipe detection with velocity
      float velocity = sqrt(dx*dx + dy*dy) / (float)duration;
      if (duration < 400 && velocity > 0.3 && (abs(dx) > 40 || abs(dy) > 40)) {
        handleSwipe(dx, dy);
      }
    }
  }
}

// ============================================
// SMOOTH NAVIGATION WITH TRANSITIONS
// ============================================
void startTransition(int direction) {
  isTransitioning = true;
  transitionDir = direction;
  transitionProgress = 0.0;
  transitionStartMs = millis();
}

void handleSwipe(int dx, int dy) {
  int newCategory = currentCategory;
  int newSubCard = currentSubCard;
  int direction = 0;
  
  if (abs(dx) > abs(dy)) {
    // Horizontal - always change category (infinite loop)
    if (dx > 40) {
      newCategory = currentCategory - 1;
      if (newCategory < 0) newCategory = NUM_CATEGORIES - 1;
      direction = -1;
    } else if (dx < -40) {
      newCategory = (currentCategory + 1) % NUM_CATEGORIES;
      direction = 1;
    }
    newSubCard = 0;  // Reset to main card
  } else {
    // Vertical - navigate within category
    if (dy > 40 && currentSubCard < maxSubCards[currentCategory] - 1) {
      newSubCard = currentSubCard + 1;
      direction = 2;
    } else if (dy < -40 && currentSubCard > 0) {
      newSubCard = 0;  // Return to main
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

// ============================================
// CREATE BACKGROUND (SD Image or Gradient)
// ============================================
static lv_obj_t *bgImage = NULL;

void createGradientBg() {
  int wpIdx = userData.wallpaperIndex;
  
  // Try to load SD card wallpaper first
  if (wpIdx > 0 && numSDWallpapers > 1 && wpIdx < numSDWallpapers) {
    // Load actual image from SD card
    bgImage = lv_img_create(lv_scr_act());
    lv_img_set_src(bgImage, sdWallpapers[wpIdx].filename);
    lv_obj_set_size(bgImage, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(bgImage, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(bgImage);
    
    // Add slight darkening overlay for better text readability
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_20, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(overlay);
    return;
  }
  
  // Fall back to gradient wallpapers
  if (wpIdx > 0 && wpIdx < NUM_GRADIENT_WALLPAPERS) {
    WallpaperTheme &wp = gradientWallpapers[wpIdx];
    
    // Create layered gradient effect for scenic look
    lv_obj_set_style_bg_color(lv_scr_act(), wp.top, 0);
    lv_obj_set_style_bg_grad_color(lv_scr_act(), wp.mid1, 0);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);
    
    // Middle gradient overlay
    lv_obj_t *midLayer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(midLayer, LCD_WIDTH, LCD_HEIGHT / 2);
    lv_obj_align(midLayer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(midLayer, wp.mid2, 0);
    lv_obj_set_style_bg_grad_color(midLayer, wp.bottom, 0);
    lv_obj_set_style_bg_grad_dir(midLayer, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(midLayer, LV_OPA_90, 0);
    lv_obj_set_style_radius(midLayer, 0, 0);
    lv_obj_set_style_border_width(midLayer, 0, 0);
    lv_obj_clear_flag(midLayer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(midLayer);
  } else {
    // Default theme gradient
    GradientTheme &theme = gradientThemes[userData.themeIndex];
    lv_obj_set_style_bg_color(lv_scr_act(), theme.color1, 0);
    lv_obj_set_style_bg_grad_color(lv_scr_act(), theme.color2, 0);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);
  }
}

// ============================================
// CREATE PREMIUM CARD CONTAINER
// ============================================
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

// ============================================
// PREMIUM NAVIGATION DOTS
// ============================================
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
    int h = 8;
    lv_obj_set_size(dot, w, h);
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
      else createSystemCard();
      break;
  }
  
  createNavDots();
  isTransitioning = false;
}
// ============================================
// S3_MiniOS_v2_part2.ino - Clock & Compass Cards
// Append this to S3_MiniOS_v2.ino
// ============================================

// ============================================
// PREMIUM CLOCK CARD
// ============================================
static lv_obj_t *clockLabel = NULL;
static lv_obj_t *dateLabel = NULL;

void createClockCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  bool hasWallpaper = userData.wallpaperIndex > 0;
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Large time display with shadow for visibility on wallpapers
  clockLabel = lv_label_create(card);
  lv_label_set_text(clockLabel, "00:00");
  lv_obj_set_style_text_color(clockLabel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(clockLabel, &lv_font_montserrat_48, 0);
  lv_obj_align(clockLabel, LV_ALIGN_CENTER, 0, -60);
  if (hasWallpaper) {
    lv_obj_set_style_shadow_width(clockLabel, 8, 0);
    lv_obj_set_style_shadow_color(clockLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(clockLabel, LV_OPA_70, 0);
  }
  
  // Seconds
  lv_obj_t *secLbl = lv_label_create(card);
  lv_label_set_text(secLbl, ":00");
  lv_obj_set_style_text_color(secLbl, hasWallpaper ? lv_color_hex(0xFFFFFF) : theme.accent, 0);
  lv_obj_set_style_text_font(secLbl, &lv_font_montserrat_24, 0);
  lv_obj_align(secLbl, LV_ALIGN_CENTER, 85, -55);
  
  // Date
  dateLabel = lv_label_create(card);
  lv_label_set_text(dateLabel, "Saturday");
  lv_obj_set_style_text_color(dateLabel, hasWallpaper ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_opa(dateLabel, hasWallpaper ? LV_OPA_90 : LV_OPA_COVER, 0);
  lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_18, 0);
  lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 0);
  
  // Full date
  lv_obj_t *fullDate = lv_label_create(card);
  lv_label_set_text(fullDate, "January 25, 2026");
  lv_obj_set_style_text_color(fullDate, hasWallpaper ? lv_color_hex(0xFFFFFF) : theme.accent, 0);
  lv_obj_set_style_text_font(fullDate, &lv_font_montserrat_16, 0);
  lv_obj_align(fullDate, LV_ALIGN_CENTER, 0, 30);
  
  // Status bar - semi-transparent for wallpaper visibility
  lv_obj_t *statusBar = lv_obj_create(card);
  lv_obj_set_size(statusBar, LCD_WIDTH - 60, 50);
  lv_obj_align(statusBar, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(statusBar, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(statusBar, hasWallpaper ? LV_OPA_50 : LV_OPA_80, 0);
  lv_obj_set_style_radius(statusBar, 25, 0);
  lv_obj_set_style_border_width(statusBar, 0, 0);
  lv_obj_set_flex_flow(statusBar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(statusBar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  // WiFi icon
  lv_obj_t *wifiIcon = lv_label_create(statusBar);
  lv_label_set_text(wifiIcon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(wifiIcon, wifiConnected ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
  
  // Battery
  int batt = power.getBatteryPercent();
  char battBuf[8];
  snprintf(battBuf, sizeof(battBuf), "%d%%", batt);
  lv_obj_t *battLbl = lv_label_create(statusBar);
  lv_label_set_text(battLbl, battBuf);
  lv_obj_set_style_text_color(battLbl, batt > 20 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
  
  // Steps
  char stepBuf[16];
  snprintf(stepBuf, sizeof(stepBuf), "%lu", (unsigned long)userData.steps);
  lv_obj_t *stepLbl = lv_label_create(statusBar);
  lv_label_set_text(stepLbl, stepBuf);
  lv_obj_set_style_text_color(stepLbl, theme.accent, 0);
}

// ============================================
// PREMIUM ANALOG CLOCK CARD
// ============================================
void createAnalogClockCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("");
  
  // Outer ring
  lv_obj_t *outerRing = lv_obj_create(card);
  lv_obj_set_size(outerRing, 260, 260);
  lv_obj_align(outerRing, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(outerRing, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(outerRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(outerRing, theme.accent, 0);
  lv_obj_set_style_border_width(outerRing, 3, 0);
  lv_obj_set_style_shadow_width(outerRing, 30, 0);
  lv_obj_set_style_shadow_color(outerRing, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(outerRing, LV_OPA_40, 0);
  
  // Inner face
  lv_obj_t *face = lv_obj_create(outerRing);
  lv_obj_set_size(face, 240, 240);
  lv_obj_align(face, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(face, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(face, 0, 0);
  
  // Hour markers
  for (int i = 0; i < 12; i++) {
    float angle = i * 30.0 * 3.14159 / 180.0;
    int len = (i % 3 == 0) ? 15 : 8;
    int r = 105;
    
    lv_obj_t *marker = lv_obj_create(face);
    lv_obj_set_size(marker, (i % 3 == 0) ? 4 : 2, len);
    int x = sin(angle) * r;
    int y = -cos(angle) * r;
    lv_obj_align(marker, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_bg_color(marker, (i % 3 == 0) ? theme.text : lv_color_hex(0x636366), 0);
    lv_obj_set_style_radius(marker, 1, 0);
    lv_obj_set_style_border_width(marker, 0, 0);
  }
  
  // Current time
  RTC_DateTime dt = rtc.getDateTime();
  float hourAngle = (dt.getHour() % 12 + dt.getMinute() / 60.0) * 30.0;
  float minAngle = dt.getMinute() * 6.0;
  
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
}

// ============================================
// PREMIUM COMPASS CARD - FULL SENSOR FUSION
// ============================================
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
  
  // Compass rose background
  lv_obj_t *rose = lv_obj_create(card);
  lv_obj_set_size(rose, 260, 260);
  lv_obj_align(rose, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_style_bg_opa(rose, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(rose, 0, 0);
  
  // Outer degree ring
  lv_obj_t *degRing = lv_obj_create(rose);
  lv_obj_set_size(degRing, 250, 250);
  lv_obj_align(degRing, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(degRing, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(degRing, lv_color_hex(0x3A3A3C), 0);
  lv_obj_set_style_border_width(degRing, 2, 0);
  lv_obj_set_style_radius(degRing, LV_RADIUS_CIRCLE, 0);
  
  // Tick marks every 30 degrees
  for (int i = 0; i < 12; i++) {
    float angle = (i * 30.0 - compassHeadingSmooth) * 3.14159 / 180.0;
    int r = 115;
    int x = sin(angle) * r;
    int y = -cos(angle) * r;
    
    lv_obj_t *tick = lv_obj_create(rose);
    lv_obj_set_size(tick, 3, 12);
    lv_obj_align(tick, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_bg_color(tick, lv_color_hex(0x636366), 0);
    lv_obj_set_style_radius(tick, 1, 0);
    lv_obj_set_style_border_width(tick, 0, 0);
  }
  
  // Cardinal directions with rotation
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
  
  // Intermediate directions (NE, SE, SW, NW)
  const char* intDirs[] = {"NE", "SE", "SW", "NW"};
  for (int i = 0; i < 4; i++) {
    float baseAngle = 45.0 + i * 90.0;
    float angle = (baseAngle - compassHeadingSmooth) * 3.14159 / 180.0;
    int r = 95;
    int x = sin(angle) * r;
    int y = -cos(angle) * r;
    
    lv_obj_t *lbl = lv_label_create(rose);
    lv_label_set_text(lbl, intDirs[i]);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, x, y);
  }
  
  // Compass needle (always points up - represents North direction relative to device)
  // Red tip (North)
  lv_obj_t *needleN = lv_obj_create(rose);
  lv_obj_set_size(needleN, 12, 70);
  lv_obj_align(needleN, LV_ALIGN_CENTER, 0, -35);
  lv_obj_set_style_bg_color(needleN, lv_color_hex(0xFF453A), 0);
  lv_obj_set_style_radius(needleN, 6, 0);
  lv_obj_set_style_border_width(needleN, 0, 0);
  lv_obj_set_style_shadow_width(needleN, 10, 0);
  lv_obj_set_style_shadow_color(needleN, lv_color_hex(0xFF453A), 0);
  lv_obj_set_style_shadow_opa(needleN, LV_OPA_50, 0);
  
  // Blue tip (South)
  lv_obj_t *needleS = lv_obj_create(rose);
  lv_obj_set_size(needleS, 12, 70);
  lv_obj_align(needleS, LV_ALIGN_CENTER, 0, 35);
  lv_obj_set_style_bg_color(needleS, lv_color_hex(0x0A84FF), 0);
  lv_obj_set_style_radius(needleS, 6, 0);
  lv_obj_set_style_border_width(needleS, 0, 0);
  lv_obj_set_style_shadow_width(needleS, 10, 0);
  lv_obj_set_style_shadow_color(needleS, lv_color_hex(0x0A84FF), 0);
  lv_obj_set_style_shadow_opa(needleS, LV_OPA_50, 0);
  
  // Center pivot
  lv_obj_t *pivot = lv_obj_create(rose);
  lv_obj_set_size(pivot, 24, 24);
  lv_obj_align(pivot, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(pivot, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(pivot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(pivot, lv_color_hex(0x3A3A3C), 0);
  lv_obj_set_style_border_width(pivot, 3, 0);
  
  // Heading readout
  char headingBuf[16];
  int headingInt = (int)compassHeadingSmooth;
  if (headingInt < 0) headingInt += 360;
  snprintf(headingBuf, sizeof(headingBuf), "%d", headingInt);
  
  lv_obj_t *headingLbl = lv_label_create(card);
  lv_label_set_text(headingLbl, headingBuf);
  lv_obj_set_style_text_color(headingLbl, theme.text, 0);
  lv_obj_set_style_text_font(headingLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(headingLbl, LV_ALIGN_BOTTOM_MID, -15, -30);
  
  lv_obj_t *degLbl = lv_label_create(card);
  lv_label_set_text(degLbl, "o");
  lv_obj_set_style_text_color(degLbl, theme.accent, 0);
  lv_obj_set_style_text_font(degLbl, &lv_font_montserrat_20, 0);
  lv_obj_align(degLbl, LV_ALIGN_BOTTOM_MID, 35, -45);
  
  // Direction label
  const char* dirLabel;
  if (headingInt >= 337 || headingInt < 23) dirLabel = "N";
  else if (headingInt < 68) dirLabel = "NE";
  else if (headingInt < 113) dirLabel = "E";
  else if (headingInt < 158) dirLabel = "SE";
  else if (headingInt < 203) dirLabel = "S";
  else if (headingInt < 248) dirLabel = "SW";
  else if (headingInt < 293) dirLabel = "W";
  else dirLabel = "NW";
  
  lv_obj_t *dirLbl = lv_label_create(card);
  lv_label_set_text(dirLbl, dirLabel);
  lv_obj_set_style_text_color(dirLbl, theme.accent, 0);
  lv_obj_set_style_text_font(dirLbl, &lv_font_montserrat_18, 0);
  lv_obj_align(dirLbl, LV_ALIGN_BOTTOM_MID, 60, -35);
  
  // Calibration hint
  if (!compassCalibrated) {
    lv_obj_t *calHint = lv_label_create(card);
    lv_label_set_text(calHint, "Rotate device to calibrate");
    lv_obj_set_style_text_color(calHint, lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_text_font(calHint, &lv_font_montserrat_12, 0);
    lv_obj_align(calHint, LV_ALIGN_BOTTOM_MID, 0, -8);
  }
}

// ============================================
// TILT / LEVEL CARD (Horizon Gauge)
// ============================================
void createTiltCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x0C0C0E), 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Title
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "LEVEL");
  lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
  
  // Level bubble container (circle)
  lv_obj_t *levelBg = lv_obj_create(card);
  lv_obj_set_size(levelBg, 220, 220);
  lv_obj_align(levelBg, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(levelBg, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(levelBg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(levelBg, lv_color_hex(0x3A3A3C), 0);
  lv_obj_set_style_border_width(levelBg, 2, 0);
  
  // Cross-hairs
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
  
  // Target ring (center)
  lv_obj_t *targetRing = lv_obj_create(levelBg);
  lv_obj_set_size(targetRing, 40, 40);
  lv_obj_align(targetRing, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(targetRing, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(targetRing, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_border_width(targetRing, 2, 0);
  lv_obj_set_style_radius(targetRing, LV_RADIUS_CIRCLE, 0);
  
  // Bubble position based on tilt
  float maxOffset = 80.0;  // Max pixels from center
  int bubbleX = constrain((int)(tiltX * maxOffset / 45.0), -80, 80);
  int bubbleY = constrain((int)(tiltY * maxOffset / 45.0), -80, 80);
  
  // Calculate if level
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
  
  // Tilt values
  char tiltBuf[32];
  snprintf(tiltBuf, sizeof(tiltBuf), "X: %.1f   Y: %.1f", tiltX, tiltY);
  lv_obj_t *tiltLbl = lv_label_create(card);
  lv_label_set_text(tiltLbl, tiltBuf);
  lv_obj_set_style_text_color(tiltLbl, lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(tiltLbl, &lv_font_montserrat_14, 0);
  lv_obj_align(tiltLbl, LV_ALIGN_BOTTOM_MID, 0, -40);
  
  // Level status
  lv_obj_t *statusLbl = lv_label_create(card);
  lv_label_set_text(statusLbl, isLevel ? "LEVEL" : "TILTED");
  lv_obj_set_style_text_color(statusLbl, isLevel ? lv_color_hex(0x30D158) : lv_color_hex(0xFF9F0A), 0);
  lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_18, 0);
  lv_obj_align(statusLbl, LV_ALIGN_BOTTOM_MID, 0, -15);
}

// ============================================
// GYROSCOPE ROTATION TRACKING CARD
// ============================================
void createGyroCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x0C0C0E), 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Title
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "ROTATION");
  lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
  
  // 3D orientation visualization (simplified)
  lv_obj_t *vizContainer = lv_obj_create(card);
  lv_obj_set_size(vizContainer, 200, 200);
  lv_obj_align(vizContainer, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_bg_color(vizContainer, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(vizContainer, 20, 0);
  lv_obj_set_style_border_width(vizContainer, 0, 0);
  
  // Pitch arc
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
  
  // Roll arc
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
  
  // Yaw arc
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
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, 100, 30);
    lv_obj_align(row, LV_ALIGN_BOTTOM_LEFT, 20 + i * 105, -15);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(dot, colors[i], 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    
    char buf[24];
    snprintf(buf, sizeof(buf), "%s: %.0f", labels[i], values[i]);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xAEAEB2), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 15, 0);
  }
}
// ============================================
// S3_MiniOS_v2_part3.ino - Activity & Premium Games
// Append this to S3_MiniOS_v2.ino
// ============================================

// ============================================
// PREMIUM STEPS CARD (Apple Watch Style)
// ============================================
void createStepsCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
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
  
  // App badge
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
  
  // Large step count
  char stepBuf[16];
  snprintf(stepBuf, sizeof(stepBuf), "%lu", (unsigned long)userData.steps);
  lv_obj_t *stepCount = lv_label_create(card);
  lv_label_set_text(stepCount, stepBuf);
  lv_obj_set_style_text_color(stepCount, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(stepCount, &lv_font_montserrat_48, 0);
  lv_obj_align(stepCount, LV_ALIGN_CENTER, 0, -30);
  
  // Goal progress
  int progress = (userData.steps * 100) / userData.dailyGoal;
  if (progress > 100) progress = 100;
  
  // Progress bar background
  lv_obj_t *progBg = lv_obj_create(card);
  lv_obj_set_size(progBg, LCD_WIDTH - 80, 12);
  lv_obj_align(progBg, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_style_bg_color(progBg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(progBg, LV_OPA_30, 0);
  lv_obj_set_style_radius(progBg, 6, 0);
  lv_obj_set_style_border_width(progBg, 0, 0);
  
  // Progress bar fill
  int fillWidth = ((LCD_WIDTH - 84) * progress) / 100;
  if (fillWidth > 4) {
    lv_obj_t *progFill = lv_obj_create(progBg);
    lv_obj_set_size(progFill, fillWidth, 8);
    lv_obj_align(progFill, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(progFill, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(progFill, 4, 0);
    lv_obj_set_style_border_width(progFill, 0, 0);
  }
  
  // Milestone markers
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
    lv_obj_set_style_text_color(mLbl, reached ? lv_color_hex(0xFFFFFF) : lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(mLbl, reached ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_text_font(mLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(mLbl, LV_ALIGN_TOP_MID, 0, 0);
    
    // Dot indicator
    lv_obj_t *dot = lv_obj_create(milestone);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(dot, reached ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x666688), 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
  }
  
  // Stats row
  lv_obj_t *statsRow = lv_obj_create(card);
  lv_obj_set_size(statsRow, LCD_WIDTH - 60, 50);
  lv_obj_align(statsRow, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_set_style_bg_color(statsRow, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(statsRow, LV_OPA_20, 0);
  lv_obj_set_style_radius(statsRow, 25, 0);
  lv_obj_set_style_border_width(statsRow, 0, 0);
  lv_obj_set_flex_flow(statsRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(statsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  // Distance
  char distBuf[16];
  snprintf(distBuf, sizeof(distBuf), "%.1f km", userData.totalDistance);
  lv_obj_t *distLbl = lv_label_create(statsRow);
  lv_label_set_text(distLbl, distBuf);
  lv_obj_set_style_text_color(distLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(distLbl, &lv_font_montserrat_14, 0);
  
  // Calories
  char calBuf[16];
  snprintf(calBuf, sizeof(calBuf), "%.0f kcal", userData.totalCalories);
  lv_obj_t *calLbl = lv_label_create(statsRow);
  lv_label_set_text(calLbl, calBuf);
  lv_obj_set_style_text_color(calLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(calLbl, &lv_font_montserrat_14, 0);
}

// ============================================
// PREMIUM ACTIVITY RINGS CARD
// ============================================
void createActivityRingsCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("ACTIVITY");
  
  int moveP = (userData.steps * 100) / userData.dailyGoal;
  if (moveP > 100) moveP = 100;
  
  // Move ring (outer - red)
  lv_obj_t *moveArc = lv_arc_create(card);
  lv_obj_set_size(moveArc, 200, 200);
  lv_obj_align(moveArc, LV_ALIGN_CENTER, 0, 10);
  lv_arc_set_rotation(moveArc, 270);
  lv_arc_set_bg_angles(moveArc, 0, 360);
  lv_arc_set_value(moveArc, moveP);
  lv_obj_set_style_arc_color(moveArc, lv_color_hex(0x3A1515), LV_PART_MAIN);
  lv_obj_set_style_arc_color(moveArc, lv_color_hex(0xFF2D55), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(moveArc, 20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(moveArc, 20, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(moveArc, true, LV_PART_INDICATOR);
  lv_obj_remove_style(moveArc, NULL, LV_PART_KNOB);
  
  // Exercise ring (middle - green)
  lv_obj_t *exArc = lv_arc_create(card);
  lv_obj_set_size(exArc, 155, 155);
  lv_obj_align(exArc, LV_ALIGN_CENTER, 0, 10);
  lv_arc_set_rotation(exArc, 270);
  lv_arc_set_bg_angles(exArc, 0, 360);
  lv_arc_set_value(exArc, 65);
  lv_obj_set_style_arc_color(exArc, lv_color_hex(0x152A15), LV_PART_MAIN);
  lv_obj_set_style_arc_color(exArc, lv_color_hex(0x30D158), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(exArc, 20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(exArc, 20, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(exArc, true, LV_PART_INDICATOR);
  lv_obj_remove_style(exArc, NULL, LV_PART_KNOB);
  
  // Stand ring (inner - blue)
  lv_obj_t *stArc = lv_arc_create(card);
  lv_obj_set_size(stArc, 110, 110);
  lv_obj_align(stArc, LV_ALIGN_CENTER, 0, 10);
  lv_arc_set_rotation(stArc, 270);
  lv_arc_set_bg_angles(stArc, 0, 360);
  lv_arc_set_value(stArc, 80);
  lv_obj_set_style_arc_color(stArc, lv_color_hex(0x152030), LV_PART_MAIN);
  lv_obj_set_style_arc_color(stArc, lv_color_hex(0x5AC8FA), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(stArc, 20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(stArc, 20, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(stArc, true, LV_PART_INDICATOR);
  lv_obj_remove_style(stArc, NULL, LV_PART_KNOB);
  
  // Legend
  lv_obj_t *legend = lv_obj_create(card);
  lv_obj_set_size(legend, LCD_WIDTH - 60, 50);
  lv_obj_align(legend, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(legend, 0, 0);
  lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  const char* names[] = {"Move", "Exercise", "Stand"};
  lv_color_t colors[] = {lv_color_hex(0xFF2D55), lv_color_hex(0x30D158), lv_color_hex(0x5AC8FA)};
  int values[] = {moveP, 65, 80};
  
  for (int i = 0; i < 3; i++) {
    lv_obj_t *item = lv_obj_create(legend);
    lv_obj_set_size(item, 80, 40);
    lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(item, 0, 0);
    
    lv_obj_t *dot = lv_obj_create(item);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 0, 5);
    lv_obj_set_style_bg_color(dot, colors[i], 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    
    lv_obj_t *nameLbl = lv_label_create(item);
    lv_label_set_text(nameLbl, names[i]);
    lv_obj_set_style_text_color(nameLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(nameLbl, LV_ALIGN_TOP_LEFT, 15, 2);
    
    char valBuf[8];
    snprintf(valBuf, sizeof(valBuf), "%d%%", values[i]);
    lv_obj_t *valLbl = lv_label_create(item);
    lv_label_set_text(valLbl, valBuf);
    lv_obj_set_style_text_color(valLbl, colors[i], 0);
    lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(valLbl, LV_ALIGN_BOTTOM_LEFT, 15, 0);
  }
}

// ============================================
// WORKOUT CARD
// ============================================
void createWorkoutCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("WORKOUTS");
  
  const char* modes[] = {LV_SYMBOL_LOOP " Run", LV_SYMBOL_REFRESH " Bike", LV_SYMBOL_SHUFFLE " Walk", LV_SYMBOL_CHARGE " Gym"};
  lv_color_t modeColors[] = {lv_color_hex(0x30D158), lv_color_hex(0x5AC8FA), lv_color_hex(0xFF9F0A), lv_color_hex(0xFF2D55)};
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *btn = lv_obj_create(card);
    lv_obj_set_size(btn, LCD_WIDTH - 80, 60);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 35 + i * 70);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    
    // Left accent bar
    lv_obj_t *accent = lv_obj_create(btn);
    lv_obj_set_size(accent, 4, 40);
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(accent, modeColors[i], 0);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, modes[i]);
    lv_obj_set_style_text_color(lbl, theme.text, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 25, 0);
    
    lv_obj_t *arrow = lv_label_create(btn);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, lv_color_hex(0x636366), 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
  }
}

// ============================================
// DISTANCE CARD
// ============================================
void createDistanceCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("DISTANCE");
  
  // Large distance number
  char distBuf[16];
  snprintf(distBuf, sizeof(distBuf), "%.2f", userData.totalDistance);
  lv_obj_t *distLbl = lv_label_create(card);
  lv_label_set_text(distLbl, distBuf);
  lv_obj_set_style_text_color(distLbl, theme.accent, 0);
  lv_obj_set_style_text_font(distLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(distLbl, LV_ALIGN_CENTER, 0, -50);
  
  lv_obj_t *kmLbl = lv_label_create(card);
  lv_label_set_text(kmLbl, "kilometers");
  lv_obj_set_style_text_color(kmLbl, lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(kmLbl, &lv_font_montserrat_16, 0);
  lv_obj_align(kmLbl, LV_ALIGN_CENTER, 0, 0);
  
  // Stats cards
  lv_obj_t *statsRow = lv_obj_create(card);
  lv_obj_set_size(statsRow, LCD_WIDTH - 60, 80);
  lv_obj_align(statsRow, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_opa(statsRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(statsRow, 0, 0);
  lv_obj_set_flex_flow(statsRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(statsRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  // Calories stat
  lv_obj_t *calCard = lv_obj_create(statsRow);
  lv_obj_set_size(calCard, 130, 70);
  lv_obj_set_style_bg_color(calCard, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(calCard, 16, 0);
  lv_obj_set_style_border_width(calCard, 0, 0);
  
  char calBuf[16];
  snprintf(calBuf, sizeof(calBuf), "%.0f", userData.totalCalories);
  lv_obj_t *calVal = lv_label_create(calCard);
  lv_label_set_text(calVal, calBuf);
  lv_obj_set_style_text_color(calVal, lv_color_hex(0xFF9F0A), 0);
  lv_obj_set_style_text_font(calVal, &lv_font_montserrat_24, 0);
  lv_obj_align(calVal, LV_ALIGN_TOP_MID, 0, 10);
  
  lv_obj_t *calUnit = lv_label_create(calCard);
  lv_label_set_text(calUnit, "kcal");
  lv_obj_set_style_text_color(calUnit, lv_color_hex(0x8E8E93), 0);
  lv_obj_align(calUnit, LV_ALIGN_BOTTOM_MID, 0, -10);
  
  // Time stat
  lv_obj_t *timeCard = lv_obj_create(statsRow);
  lv_obj_set_size(timeCard, 130, 70);
  lv_obj_set_style_bg_color(timeCard, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(timeCard, 16, 0);
  lv_obj_set_style_border_width(timeCard, 0, 0);
  
  int mins = (int)(userData.totalDistance / 0.08);  // ~5km/h walking
  lv_obj_t *timeVal = lv_label_create(timeCard);
  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%d", mins);
  lv_label_set_text(timeVal, timeBuf);
  lv_obj_set_style_text_color(timeVal, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_text_font(timeVal, &lv_font_montserrat_24, 0);
  lv_obj_align(timeVal, LV_ALIGN_TOP_MID, 0, 10);
  
  lv_obj_t *timeUnit = lv_label_create(timeCard);
  lv_label_set_text(timeUnit, "min");
  lv_obj_set_style_text_color(timeUnit, lv_color_hex(0x8E8E93), 0);
  lv_obj_align(timeUnit, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ============================================
// PREMIUM BLACKJACK CARD
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

const char* getCardSymbol(int card) {
  static char buf[4];
  int rank = card % 13;
  const char* ranks[] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
  return ranks[rank];
}

const char* getSuitSymbol(int card) {
  int suit = card / 13;
  const char* suits[] = {"H", "D", "C", "S"};  // Hearts, Diamonds, Clubs, Spades
  return suits[suit];
}

lv_color_t getSuitColor(int card) {
  int suit = card / 13;
  return (suit < 2) ? lv_color_hex(0xFF453A) : lv_color_hex(0xFFFFFF);  // Red for hearts/diamonds
}

void drawCard(lv_obj_t* parent, int card, int x, int y, bool faceUp) {
  lv_obj_t *cardObj = lv_obj_create(parent);
  lv_obj_set_size(cardObj, 50, 70);
  lv_obj_align(cardObj, LV_ALIGN_TOP_LEFT, x, y);
  lv_obj_set_style_bg_color(cardObj, faceUp ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x0A84FF), 0);
  lv_obj_set_style_radius(cardObj, 8, 0);
  lv_obj_set_style_border_width(cardObj, 0, 0);
  lv_obj_set_style_shadow_width(cardObj, 8, 0);
  lv_obj_set_style_shadow_color(cardObj, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(cardObj, LV_OPA_30, 0);
  
  if (faceUp) {
    // Rank
    lv_obj_t *rankLbl = lv_label_create(cardObj);
    lv_label_set_text(rankLbl, getCardSymbol(card));
    lv_obj_set_style_text_color(rankLbl, getSuitColor(card), 0);
    lv_obj_set_style_text_font(rankLbl, &lv_font_montserrat_18, 0);
    lv_obj_align(rankLbl, LV_ALIGN_TOP_LEFT, 5, 3);
    
    // Suit
    lv_obj_t *suitLbl = lv_label_create(cardObj);
    lv_label_set_text(suitLbl, getSuitSymbol(card));
    lv_obj_set_style_text_color(suitLbl, getSuitColor(card), 0);
    lv_obj_set_style_text_font(suitLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(suitLbl, LV_ALIGN_BOTTOM_RIGHT, -5, -3);
  } else {
    // Card back pattern
    lv_obj_t *pattern = lv_obj_create(cardObj);
    lv_obj_set_size(pattern, 40, 60);
    lv_obj_align(pattern, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(pattern, lv_color_hex(0x007AFF), 0);
    lv_obj_set_style_radius(pattern, 4, 0);
    lv_obj_set_style_border_color(pattern, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(pattern, 1, 0);
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
  lv_obj_set_style_text_color(title, lv_color_hex(0xD4AF37), 0);  // Gold
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
  
  // Dealer area
  lv_obj_t *dealerLbl = lv_label_create(card);
  lv_label_set_text(dealerLbl, "Dealer");
  lv_obj_set_style_text_color(dealerLbl, lv_color_hex(0xAEAEB2), 0);
  lv_obj_set_style_text_font(dealerLbl, &lv_font_montserrat_12, 0);
  lv_obj_align(dealerLbl, LV_ALIGN_TOP_LEFT, 16, 35);
  
  // Dealer cards
  for (int i = 0; i < dealerCount && i < 5; i++) {
    bool showCard = playerStand || i == 0;
    drawCard(card, dealerCards[i], 16 + i * 40, 50, showCard);
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
  lv_obj_align(divider, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0x52B788), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_50, 0);
  lv_obj_set_style_radius(divider, 1, 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  
  // Player area
  lv_obj_t *playerLbl = lv_label_create(card);
  lv_label_set_text(playerLbl, "You");
  lv_obj_set_style_text_color(playerLbl, lv_color_hex(0xAEAEB2), 0);
  lv_obj_set_style_text_font(playerLbl, &lv_font_montserrat_12, 0);
  lv_obj_align(playerLbl, LV_ALIGN_CENTER, -130, 0);
  
  // Player cards
  for (int i = 0; i < playerCount && i < 5; i++) {
    drawCard(card, playerCards[i], 16 + i * 40, 175, true);
  }
  
  // Player total
  int pTotal = handTotal(playerCards, playerCount);
  char pBuf[16];
  snprintf(pBuf, sizeof(pBuf), "= %d", pTotal);
  lv_obj_t *pTotalLbl = lv_label_create(card);
  lv_label_set_text(pTotalLbl, pBuf);
  lv_obj_set_style_text_color(pTotalLbl, pTotal > 21 ? lv_color_hex(0xFF453A) : lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(pTotalLbl, &lv_font_montserrat_18, 0);
  lv_obj_align(pTotalLbl, LV_ALIGN_CENTER, 120, 25);
  
  // Result or buttons
  if (!blackjackGameActive) {
    // Show result if game ended
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
      lv_obj_align(resultLbl, LV_ALIGN_BOTTOM_MID, 0, -80);
    }
    
    // New game button
    lv_obj_t *newBtn = lv_btn_create(card);
    lv_obj_set_size(newBtn, 160, 50);
    lv_obj_align(newBtn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(newBtn, lv_color_hex(0xD4AF37), 0);
    lv_obj_set_style_radius(newBtn, 25, 0);
    lv_obj_set_style_shadow_width(newBtn, 10, 0);
    lv_obj_set_style_shadow_color(newBtn, lv_color_hex(0xD4AF37), 0);
    lv_obj_set_style_shadow_opa(newBtn, LV_OPA_30, 0);
    lv_obj_add_event_cb(newBtn, blackjackNewCb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *nLbl = lv_label_create(newBtn);
    lv_label_set_text(nLbl, "DEAL");
    lv_obj_set_style_text_color(nLbl, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_text_font(nLbl, &lv_font_montserrat_16, 0);
    lv_obj_center(nLbl);
  } else if (!playerStand && pTotal <= 21) {
    // Hit/Stand buttons
    lv_obj_t *hitBtn = lv_btn_create(card);
    lv_obj_set_size(hitBtn, 110, 45);
    lv_obj_align(hitBtn, LV_ALIGN_BOTTOM_MID, -65, -20);
    lv_obj_set_style_bg_color(hitBtn, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_radius(hitBtn, 22, 0);
    lv_obj_add_event_cb(hitBtn, blackjackHitCb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *hLbl = lv_label_create(hitBtn);
    lv_label_set_text(hLbl, "HIT");
    lv_obj_set_style_text_color(hLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(hLbl, &lv_font_montserrat_16, 0);
    lv_obj_center(hLbl);
    
    lv_obj_t *standBtn = lv_btn_create(card);
    lv_obj_set_size(standBtn, 110, 45);
    lv_obj_align(standBtn, LV_ALIGN_BOTTOM_MID, 65, -20);
    lv_obj_set_style_bg_color(standBtn, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_radius(standBtn, 22, 0);
    lv_obj_add_event_cb(standBtn, blackjackStandCb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *sLbl = lv_label_create(standBtn);
    lv_label_set_text(sLbl, "STAND");
    lv_obj_set_style_text_color(sLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_16, 0);
    lv_obj_center(sLbl);
  }
  
  // Streak badge
  lv_obj_t *streakBadge = lv_obj_create(card);
  lv_obj_set_size(streakBadge, 80, 26);
  lv_obj_align(streakBadge, LV_ALIGN_TOP_RIGHT, -12, 8);
  lv_obj_set_style_bg_color(streakBadge, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(streakBadge, LV_OPA_40, 0);
  lv_obj_set_style_radius(streakBadge, 13, 0);
  lv_obj_set_style_border_width(streakBadge, 0, 0);
  
  char streakBuf[16];
  snprintf(streakBuf, sizeof(streakBuf), LV_SYMBOL_CHARGE " %d", userData.blackjackStreak);
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
// ============================================
// S3_MiniOS_v2_part4.ino - More Games & Weather/Stocks
// Append this to S3_MiniOS_v2.ino
// ============================================

// ============================================
// PREMIUM DINO GAME CARD
// ============================================
void dinoJumpCb(lv_event_t *e) {
  if (dinoGameOver) {
    dinoGameOver = false;
    dinoScore = 0;
    obstacleX = 350;
    dinoY = 0;
    dinoVelocity = 0;
    dinoJumping = false;
  } else if (!dinoJumping && dinoY == 0) {
    dinoJumping = true;
    dinoVelocity = JUMP_FORCE;
  }
}

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
  
  // Score display
  lv_obj_t *scoreBg = lv_obj_create(card);
  lv_obj_set_size(scoreBg, 80, 32);
  lv_obj_align(scoreBg, LV_ALIGN_TOP_RIGHT, -12, 8);
  lv_obj_set_style_bg_color(scoreBg, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_bg_opa(scoreBg, LV_OPA_20, 0);
  lv_obj_set_style_radius(scoreBg, 16, 0);
  lv_obj_set_style_border_width(scoreBg, 0, 0);
  
  char sBuf[16];
  snprintf(sBuf, sizeof(sBuf), "%d", dinoScore);
  lv_obj_t *scoreLbl = lv_label_create(scoreBg);
  lv_label_set_text(scoreLbl, sBuf);
  lv_obj_set_style_text_color(scoreLbl, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_text_font(scoreLbl, &lv_font_montserrat_18, 0);
  lv_obj_center(scoreLbl);
  
  // Game area
  lv_obj_t *gameArea = lv_obj_create(card);
  lv_obj_set_size(gameArea, LCD_WIDTH - 50, 180);
  lv_obj_align(gameArea, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_style_bg_color(gameArea, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(gameArea, 20, 0);
  lv_obj_set_style_border_width(gameArea, 0, 0);
  
  // Ground with texture
  for (int i = 0; i < 20; i++) {
    lv_obj_t *groundDot = lv_obj_create(gameArea);
    lv_obj_set_size(groundDot, random(3, 8), 2);
    lv_obj_align(groundDot, LV_ALIGN_BOTTOM_LEFT, random(0, LCD_WIDTH - 80), -25 - random(0, 5));
    lv_obj_set_style_bg_color(groundDot, lv_color_hex(0x48484A), 0);
    lv_obj_set_style_radius(groundDot, 1, 0);
    lv_obj_set_style_border_width(groundDot, 0, 0);
  }
  
  // Ground line
  lv_obj_t *ground = lv_obj_create(gameArea);
  lv_obj_set_size(ground, LCD_WIDTH - 70, 3);
  lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(ground, lv_color_hex(0x636366), 0);
  lv_obj_set_style_radius(ground, 1, 0);
  lv_obj_set_style_border_width(ground, 0, 0);
  
  // Dino character (pixelated style)
  lv_obj_t *dino = lv_obj_create(gameArea);
  lv_obj_set_size(dino, 35, 50);
  lv_obj_align(dino, LV_ALIGN_BOTTOM_LEFT, 40, -23 + dinoY);
  lv_obj_set_style_bg_color(dino, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_radius(dino, 8, 0);
  lv_obj_set_style_border_width(dino, 0, 0);
  lv_obj_set_style_shadow_width(dino, 10, 0);
  lv_obj_set_style_shadow_color(dino, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_shadow_opa(dino, LV_OPA_40, 0);
  
  // Dino eye
  lv_obj_t *eye = lv_obj_create(dino);
  lv_obj_set_size(eye, 8, 8);
  lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, -5, 8);
  lv_obj_set_style_bg_color(eye, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(eye, 0, 0);
  
  // Obstacle (cactus style)
  lv_obj_t *obs = lv_obj_create(gameArea);
  lv_obj_set_size(obs, 25, 45);
  lv_obj_align(obs, LV_ALIGN_BOTTOM_LEFT, obstacleX, -23);
  lv_obj_set_style_bg_color(obs, lv_color_hex(0xFF453A), 0);
  lv_obj_set_style_radius(obs, 6, 0);
  lv_obj_set_style_border_width(obs, 0, 0);
  lv_obj_set_style_shadow_width(obs, 8, 0);
  lv_obj_set_style_shadow_color(obs, lv_color_hex(0xFF453A), 0);
  lv_obj_set_style_shadow_opa(obs, LV_OPA_40, 0);
  
  // Jump button
  lv_obj_t *jumpBtn = lv_btn_create(card);
  lv_obj_set_size(jumpBtn, 140, 50);
  lv_obj_align(jumpBtn, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_bg_color(jumpBtn, dinoGameOver ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x30D158), 0);
  lv_obj_set_style_radius(jumpBtn, 25, 0);
  lv_obj_set_style_shadow_width(jumpBtn, 10, 0);
  lv_obj_set_style_shadow_color(jumpBtn, dinoGameOver ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x30D158), 0);
  lv_obj_set_style_shadow_opa(jumpBtn, LV_OPA_30, 0);
  lv_obj_add_event_cb(jumpBtn, dinoJumpCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *jLbl = lv_label_create(jumpBtn);
  lv_label_set_text(jLbl, dinoGameOver ? "RESTART" : "JUMP!");
  lv_obj_set_style_text_color(jLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(jLbl, &lv_font_montserrat_16, 0);
  lv_obj_center(jLbl);
  
  // Game over overlay
  if (dinoGameOver) {
    lv_obj_t *overlay = lv_obj_create(gameArea);
    lv_obj_set_size(overlay, LCD_WIDTH - 80, 80);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_radius(overlay, 16, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    
    lv_obj_t *goLbl = lv_label_create(overlay);
    lv_label_set_text(goLbl, "GAME OVER");
    lv_obj_set_style_text_color(goLbl, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(goLbl, &lv_font_montserrat_24, 0);
    lv_obj_align(goLbl, LV_ALIGN_TOP_MID, 0, 10);
    
    char finalBuf[32];
    snprintf(finalBuf, sizeof(finalBuf), "Score: %d", dinoScore);
    lv_obj_t *finalLbl = lv_label_create(overlay);
    lv_label_set_text(finalLbl, finalBuf);
    lv_obj_set_style_text_color(finalLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(finalLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(finalLbl, LV_ALIGN_BOTTOM_MID, 0, -10);
  }
}

// ============================================
// PREMIUM YES/NO SPINNER CARD
// ============================================
void yesNoSpinCb(lv_event_t *e) {
  yesNoSpinning = true;
  yesNoAngle = 0;
}

void createYesNoCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  // Red gradient background
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0xC41E3A), 0);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x9B2335), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Title
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "YES / NO");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(title, LV_OPA_70, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
  
  // Result circle
  lv_obj_t *resultCircle = lv_obj_create(card);
  lv_obj_set_size(resultCircle, 200, 200);
  lv_obj_align(resultCircle, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_color(resultCircle, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(resultCircle, LV_OPA_30, 0);
  lv_obj_set_style_radius(resultCircle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(resultCircle, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(resultCircle, 3, 0);
  lv_obj_set_style_border_opa(resultCircle, LV_OPA_30, 0);
  
  // Result text
  lv_obj_t *resultLbl = lv_label_create(resultCircle);
  lv_label_set_text(resultLbl, yesNoResult.c_str());
  lv_obj_set_style_text_color(resultLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(resultLbl, &lv_font_montserrat_40, 0);
  lv_obj_center(resultLbl);
  
  // Decorative elements
  if (yesNoResult == "Yes" || yesNoResult == "Definitely") {
    lv_obj_t *glow = lv_obj_create(card);
    lv_obj_set_size(glow, 220, 220);
    lv_obj_align(glow, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_opa(glow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(glow, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_border_width(glow, 4, 0);
    lv_obj_set_style_border_opa(glow, LV_OPA_50, 0);
    lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
  } else if (yesNoResult == "No" || yesNoResult == "Never") {
    lv_obj_t *glow = lv_obj_create(card);
    lv_obj_set_size(glow, 220, 220);
    lv_obj_align(glow, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_opa(glow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(glow, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_border_width(glow, 4, 0);
    lv_obj_set_style_border_opa(glow, LV_OPA_50, 0);
    lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
  }
  
  // Spin button
  lv_obj_t *spinBtn = lv_btn_create(card);
  lv_obj_set_size(spinBtn, 160, 50);
  lv_obj_align(spinBtn, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(spinBtn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(spinBtn, 25, 0);
  lv_obj_set_style_shadow_width(spinBtn, 15, 0);
  lv_obj_set_style_shadow_color(spinBtn, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(spinBtn, LV_OPA_30, 0);
  lv_obj_add_event_cb(spinBtn, yesNoSpinCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *sLbl = lv_label_create(spinBtn);
  lv_label_set_text(sLbl, LV_SYMBOL_REFRESH " SPIN");
  lv_obj_set_style_text_color(sLbl, lv_color_hex(0xC41E3A), 0);
  lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_16, 0);
  lv_obj_center(sLbl);
}

// ============================================
// PREMIUM WEATHER CARD
// ============================================
void createWeatherCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  // Weather-themed gradient
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1A759F), 0);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x34A0A4), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Location
  lv_obj_t *locLbl = lv_label_create(card);
  lv_label_set_text(locLbl, "Sydney");
  lv_obj_set_style_text_color(locLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(locLbl, LV_OPA_80, 0);
  lv_obj_set_style_text_font(locLbl, &lv_font_montserrat_16, 0);
  lv_obj_align(locLbl, LV_ALIGN_TOP_MID, 0, 20);
  
  // Weather icon placeholder (sun)
  lv_obj_t *iconBg = lv_obj_create(card);
  lv_obj_set_size(iconBg, 80, 80);
  lv_obj_align(iconBg, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_bg_color(iconBg, lv_color_hex(0xFFD700), 0);
  lv_obj_set_style_radius(iconBg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(iconBg, 0, 0);
  lv_obj_set_style_shadow_width(iconBg, 20, 0);
  lv_obj_set_style_shadow_color(iconBg, lv_color_hex(0xFFD700), 0);
  lv_obj_set_style_shadow_opa(iconBg, LV_OPA_50, 0);
  
  // Temperature
  char tempBuf[16];
  snprintf(tempBuf, sizeof(tempBuf), "%.0f", weatherTemp);
  lv_obj_t *tempLbl = lv_label_create(card);
  lv_label_set_text(tempLbl, tempBuf);
  lv_obj_set_style_text_color(tempLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(tempLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(tempLbl, LV_ALIGN_CENTER, -15, 30);
  
  // Degree symbol
  lv_obj_t *degLbl = lv_label_create(card);
  lv_label_set_text(degLbl, "o");
  lv_obj_set_style_text_color(degLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(degLbl, &lv_font_montserrat_20, 0);
  lv_obj_align(degLbl, LV_ALIGN_CENTER, 35, 15);
  
  // Description
  lv_obj_t *descLbl = lv_label_create(card);
  lv_label_set_text(descLbl, weatherDesc.c_str());
  lv_obj_set_style_text_color(descLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(descLbl, LV_OPA_80, 0);
  lv_obj_set_style_text_font(descLbl, &lv_font_montserrat_18, 0);
  lv_obj_align(descLbl, LV_ALIGN_CENTER, 0, 80);
  
  // High/Low
  lv_obj_t *hlRow = lv_obj_create(card);
  lv_obj_set_size(hlRow, 200, 50);
  lv_obj_align(hlRow, LV_ALIGN_BOTTOM_MID, 0, -25);
  lv_obj_set_style_bg_color(hlRow, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(hlRow, LV_OPA_20, 0);
  lv_obj_set_style_radius(hlRow, 25, 0);
  lv_obj_set_style_border_width(hlRow, 0, 0);
  lv_obj_set_flex_flow(hlRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hlRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  char hBuf[16], lBuf[16];
  snprintf(hBuf, sizeof(hBuf), "H: %.0f", weatherHigh);
  snprintf(lBuf, sizeof(lBuf), "L: %.0f", weatherLow);
  
  lv_obj_t *hLbl = lv_label_create(hlRow);
  lv_label_set_text(hLbl, hBuf);
  lv_obj_set_style_text_color(hLbl, lv_color_hex(0xFFFFFF), 0);
  
  lv_obj_t *lLbl = lv_label_create(hlRow);
  lv_label_set_text(lLbl, lBuf);
  lv_obj_set_style_text_color(lLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(lLbl, LV_OPA_70, 0);
}

// ============================================
// FORECAST CARD
// ============================================
void createForecastCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("3-DAY FORECAST");
  
  const char* days[] = {"SAT", "SUN", "MON"};
  int temps[] = {24, 26, 23};
  int highs[] = {28, 29, 27};
  int lows[] = {18, 19, 17};
  
  for (int i = 0; i < 3; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 70, 75);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 35 + i * 85);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    // Day
    lv_obj_t *dayLbl = lv_label_create(row);
    lv_label_set_text(dayLbl, days[i]);
    lv_obj_set_style_text_color(dayLbl, theme.text, 0);
    lv_obj_set_style_text_font(dayLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(dayLbl, LV_ALIGN_LEFT_MID, 15, 0);
    
    // Weather icon (simplified)
    lv_obj_t *icon = lv_obj_create(row);
    lv_obj_set_size(icon, 30, 30);
    lv_obj_align(icon, LV_ALIGN_CENTER, -20, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    
    // Temp range bar
    lv_obj_t *tempBar = lv_obj_create(row);
    lv_obj_set_size(tempBar, 80, 8);
    lv_obj_align(tempBar, LV_ALIGN_CENTER, 30, 0);
    lv_obj_set_style_bg_color(tempBar, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_bg_grad_color(tempBar, lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_bg_grad_dir(tempBar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_radius(tempBar, 4, 0);
    lv_obj_set_style_border_width(tempBar, 0, 0);
    
    // Current temp
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
  
  const char* syms[] = {"AAPL", "TSLA", "NVDA", "GOOG"};
  float prices[] = {aaplPrice > 0 ? aaplPrice : 178.5f, tslaPrice > 0 ? tslaPrice : 248.2f, 875.3f, 140.2f};
  float changes[] = {2.3, -1.5, 4.2, 0.8};
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 70, 60);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 30 + i * 68);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    // Symbol
    lv_obj_t *symLbl = lv_label_create(row);
    lv_label_set_text(symLbl, syms[i]);
    lv_obj_set_style_text_color(symLbl, theme.text, 0);
    lv_obj_set_style_text_font(symLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(symLbl, LV_ALIGN_LEFT_MID, 15, -8);
    
    // Company abbreviation
    lv_obj_t *compLbl = lv_label_create(row);
    lv_label_set_text(compLbl, i == 0 ? "Apple" : (i == 1 ? "Tesla" : (i == 2 ? "NVIDIA" : "Google")));
    lv_obj_set_style_text_color(compLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(compLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(compLbl, LV_ALIGN_LEFT_MID, 15, 10);
    
    // Price
    char pBuf[16];
    snprintf(pBuf, sizeof(pBuf), "$%.2f", prices[i]);
    lv_obj_t *pLbl = lv_label_create(row);
    lv_label_set_text(pLbl, pBuf);
    lv_obj_set_style_text_color(pLbl, theme.text, 0);
    lv_obj_set_style_text_font(pLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(pLbl, LV_ALIGN_RIGHT_MID, -15, -8);
    
    // Change
    char cBuf[16];
    snprintf(cBuf, sizeof(cBuf), "%+.1f%%", changes[i]);
    lv_obj_t *cLbl = lv_label_create(row);
    lv_label_set_text(cLbl, cBuf);
    lv_obj_set_style_text_color(cLbl, changes[i] >= 0 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(cLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(cLbl, LV_ALIGN_RIGHT_MID, -15, 10);
  }
}

// ============================================
// CRYPTO CARD
// ============================================
void createCryptoCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "CRYPTO");
  lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 12);
  
  const char* syms[] = {"BTC", "ETH", "SOL", "ADA"};
  float prices[] = {btcPrice > 0 ? btcPrice : 67432.5f, ethPrice > 0 ? ethPrice : 3521.8f, 142.3f, 0.58f};
  lv_color_t colors[] = {lv_color_hex(0xF7931A), lv_color_hex(0x627EEA), lv_color_hex(0x00FFA3), lv_color_hex(0x0033AD)};
  float changes[] = {3.2, 2.1, -1.8, 0.5};
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 70, 60);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 35 + i * 68);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    // Color accent
    lv_obj_t *accent = lv_obj_create(row);
    lv_obj_set_size(accent, 4, 40);
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(accent, colors[i], 0);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    
    // Symbol
    lv_obj_t *symLbl = lv_label_create(row);
    lv_label_set_text(symLbl, syms[i]);
    lv_obj_set_style_text_color(symLbl, theme.text, 0);
    lv_obj_set_style_text_font(symLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(symLbl, LV_ALIGN_LEFT_MID, 20, 0);
    
    // Price
    char pBuf[16];
    if (prices[i] >= 1000) snprintf(pBuf, sizeof(pBuf), "$%.0f", prices[i]);
    else snprintf(pBuf, sizeof(pBuf), "$%.2f", prices[i]);
    lv_obj_t *pLbl = lv_label_create(row);
    lv_label_set_text(pLbl, pBuf);
    lv_obj_set_style_text_color(pLbl, theme.text, 0);
    lv_obj_set_style_text_font(pLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(pLbl, LV_ALIGN_RIGHT_MID, -15, -8);
    
    // Change badge
    lv_obj_t *changeBadge = lv_obj_create(row);
    lv_obj_set_size(changeBadge, 55, 22);
    lv_obj_align(changeBadge, LV_ALIGN_RIGHT_MID, -10, 12);
    lv_obj_set_style_bg_color(changeBadge, changes[i] >= 0 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_bg_opa(changeBadge, LV_OPA_20, 0);
    lv_obj_set_style_radius(changeBadge, 11, 0);
    lv_obj_set_style_border_width(changeBadge, 0, 0);
    
    char cBuf[16];
    snprintf(cBuf, sizeof(cBuf), "%+.1f%%", changes[i]);
    lv_obj_t *cLbl = lv_label_create(changeBadge);
    lv_label_set_text(cLbl, cBuf);
    lv_obj_set_style_text_color(cLbl, changes[i] >= 0 ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_text_font(cLbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cLbl);
  }
}
// ============================================
// S3_MiniOS_v2_part5.ino - Media, Timer, Settings, Setup & Loop
// Append this to S3_MiniOS_v2.ino
// ============================================

// ============================================
// MUSIC CARD
// ============================================
void createMusicCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("");
  
  // Album art placeholder
  lv_obj_t *art = lv_obj_create(card);
  lv_obj_set_size(art, 160, 160);
  lv_obj_align(art, LV_ALIGN_CENTER, 0, -40);
  lv_obj_set_style_bg_color(art, theme.accent, 0);
  lv_obj_set_style_bg_grad_color(art, theme.secondary, 0);
  lv_obj_set_style_bg_grad_dir(art, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_radius(art, 20, 0);
  lv_obj_set_style_border_width(art, 0, 0);
  lv_obj_set_style_shadow_width(art, 20, 0);
  lv_obj_set_style_shadow_color(art, theme.accent, 0);
  lv_obj_set_style_shadow_opa(art, LV_OPA_30, 0);
  
  lv_obj_t *icon = lv_label_create(art);
  lv_label_set_text(icon, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
  lv_obj_center(icon);
  
  // Track info
  lv_obj_t *trackLbl = lv_label_create(card);
  lv_label_set_text(trackLbl, "No Track Playing");
  lv_obj_set_style_text_color(trackLbl, theme.text, 0);
  lv_obj_set_style_text_font(trackLbl, &lv_font_montserrat_16, 0);
  lv_obj_align(trackLbl, LV_ALIGN_CENTER, 0, 60);
  
  lv_obj_t *artistLbl = lv_label_create(card);
  lv_label_set_text(artistLbl, "Connect to play");
  lv_obj_set_style_text_color(artistLbl, lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(artistLbl, &lv_font_montserrat_14, 0);
  lv_obj_align(artistLbl, LV_ALIGN_CENTER, 0, 85);
  
  // Controls
  lv_obj_t *ctrl = lv_obj_create(card);
  lv_obj_set_size(ctrl, 220, 60);
  lv_obj_align(ctrl, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_opa(ctrl, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctrl, 0, 0);
  lv_obj_set_flex_flow(ctrl, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctrl, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  const char* icons[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_NEXT};
  int sizes[] = {45, 55, 45};
  
  for (int i = 0; i < 3; i++) {
    lv_obj_t *btn = lv_btn_create(ctrl);
    lv_obj_set_size(btn, sizes[i], sizes[i]);
    lv_obj_set_style_bg_color(btn, i == 1 ? theme.accent : lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    if (i == 1) {
      lv_obj_set_style_shadow_width(btn, 15, 0);
      lv_obj_set_style_shadow_color(btn, theme.accent, 0);
      lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
    }
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, icons[i]);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
  }
}

// ============================================
// WALLPAPER SELECTOR CARD
// ============================================
void wallpaperSelectCb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  userData.wallpaperIndex = idx;
  navigateTo(CAT_MEDIA, 1);
}

void createGalleryCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
  
  // Title with SD indicator
  lv_obj_t *title = lv_label_create(card);
  if (numSDWallpapers > 1) {
    lv_label_set_text(title, "WALLPAPERS (SD)");
  } else {
    lv_label_set_text(title, "WALLPAPERS");
  }
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
  
  // Current wallpaper name
  lv_obj_t *currentLbl = lv_label_create(card);
  lv_label_set_text(currentLbl, getWallpaperName(userData.wallpaperIndex));
  lv_obj_set_style_text_color(currentLbl, theme.accent, 0);
  lv_obj_set_style_text_font(currentLbl, &lv_font_montserrat_14, 0);
  lv_obj_align(currentLbl, LV_ALIGN_TOP_MID, 0, 35);
  
  // Scrollable wallpaper grid
  lv_obj_t *grid = lv_obj_create(card);
  lv_obj_set_size(grid, LCD_WIDTH - 50, 260);
  lv_obj_align(grid, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 5, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(grid, 8, 0);
  lv_obj_set_style_pad_column(grid, 8, 0);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
  
  int totalWP = getTotalWallpapers();
  bool useSDImages = (numSDWallpapers > 1);
  
  for (int i = 0; i < totalWP && i < 12; i++) {
    bool selected = (i == userData.wallpaperIndex);
    
    lv_obj_t *thumb = lv_obj_create(grid);
    lv_obj_set_size(thumb, 85, 60);
    lv_obj_set_style_radius(thumb, 10, 0);
    lv_obj_set_style_pad_all(thumb, 0, 0);
    lv_obj_add_flag(thumb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(thumb, wallpaperSelectCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    
    if (i == 0) {
      // "None" option - dark solid
      lv_obj_set_style_bg_color(thumb, lv_color_hex(0x2C2C2E), 0);
      lv_obj_set_style_border_width(thumb, 0, 0);
      
      lv_obj_t *noneLbl = lv_label_create(thumb);
      lv_label_set_text(noneLbl, "None");
      lv_obj_set_style_text_color(noneLbl, lv_color_hex(0x8E8E93), 0);
      lv_obj_set_style_text_font(noneLbl, &lv_font_montserrat_12, 0);
      lv_obj_center(noneLbl);
    } else if (useSDImages && i < numSDWallpapers) {
      // SD card image thumbnail
      lv_obj_t *imgThumb = lv_img_create(thumb);
      lv_img_set_src(imgThumb, sdWallpapers[i].filename);
      lv_obj_set_size(imgThumb, 85, 60);
      lv_obj_align(imgThumb, LV_ALIGN_CENTER, 0, 0);
      lv_img_set_zoom(imgThumb, 64);  // Scale down to ~25%
      lv_obj_set_style_radius(imgThumb, 10, 0);
      lv_obj_set_style_clip_corner(imgThumb, true, 0);
      
      // Fallback if image doesn't load
      lv_obj_set_style_bg_color(thumb, lv_color_hex(0x3A3A3C), 0);
    } else {
      // Gradient preview (fallback)
      int gradIdx = useSDImages ? 1 : i;  // Use first gradient as fallback
      if (gradIdx >= NUM_GRADIENT_WALLPAPERS) gradIdx = 1;
      WallpaperTheme &wp = gradientWallpapers[gradIdx];
      
      lv_obj_set_style_bg_color(thumb, wp.top, 0);
      lv_obj_set_style_bg_grad_color(thumb, wp.mid1, 0);
      lv_obj_set_style_bg_grad_dir(thumb, LV_GRAD_DIR_VER, 0);
      lv_obj_set_style_border_width(thumb, 0, 0);
      
      // Bottom gradient overlay
      lv_obj_t *bottomHalf = lv_obj_create(thumb);
      lv_obj_set_size(bottomHalf, 85, 30);
      lv_obj_align(bottomHalf, LV_ALIGN_BOTTOM_MID, 0, 0);
      lv_obj_set_style_bg_color(bottomHalf, wp.mid2, 0);
      lv_obj_set_style_bg_grad_color(bottomHalf, wp.bottom, 0);
      lv_obj_set_style_bg_grad_dir(bottomHalf, LV_GRAD_DIR_VER, 0);
      lv_obj_set_style_bg_opa(bottomHalf, LV_OPA_90, 0);
      lv_obj_set_style_radius(bottomHalf, 0, 0);
      lv_obj_set_style_border_width(bottomHalf, 0, 0);
      lv_obj_clear_flag(bottomHalf, LV_OBJ_FLAG_CLICKABLE);
    }
    
    // Selection indicator
    if (selected) {
      lv_obj_set_style_border_color(thumb, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_border_width(thumb, 3, 0);
      lv_obj_set_style_shadow_width(thumb, 12, 0);
      lv_obj_set_style_shadow_color(thumb, theme.accent, 0);
      lv_obj_set_style_shadow_opa(thumb, LV_OPA_50, 0);
      
      // Checkmark
      lv_obj_t *check = lv_obj_create(thumb);
      lv_obj_set_size(check, 22, 22);
      lv_obj_align(check, LV_ALIGN_TOP_RIGHT, -3, 3);
      lv_obj_set_style_bg_color(check, lv_color_hex(0x30D158), 0);
      lv_obj_set_style_radius(check, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(check, 0, 0);
      lv_obj_clear_flag(check, LV_OBJ_FLAG_CLICKABLE);
      
      lv_obj_t *checkIcon = lv_label_create(check);
      lv_label_set_text(checkIcon, LV_SYMBOL_OK);
      lv_obj_set_style_text_color(checkIcon, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_text_font(checkIcon, &lv_font_montserrat_12, 0);
      lv_obj_center(checkIcon);
    }
  }
  
  // Hint
  lv_obj_t *hint = lv_label_create(card);
  lv_label_set_text(hint, "Tap to select wallpaper");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x636366), 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -15);
}

// ============================================
// PREMIUM SAND TIMER CARD
// ============================================
static lv_obj_t *sandTimerLbl = NULL;

void sandTimerStartCb(lv_event_t *e) {
  sandTimerRunning = !sandTimerRunning;
  if (sandTimerRunning) sandTimerStartMs = millis();
  navigateTo(CAT_TIMER, 0);
}

void sandTimerResetCb(lv_event_t *e) {
  sandTimerRunning = false;
  sandTimerStartMs = 0;
  navigateTo(CAT_TIMER, 0);
}

void createSandTimerCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  // Premium orange gradient background
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Title badge
  lv_obj_t *titleBadge = lv_obj_create(card);
  lv_obj_set_size(titleBadge, 100, 30);
  lv_obj_align(titleBadge, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(titleBadge, lv_color_hex(0xFF9500), 0);
  lv_obj_set_style_bg_opa(titleBadge, LV_OPA_20, 0);
  lv_obj_set_style_radius(titleBadge, 15, 0);
  lv_obj_set_style_border_width(titleBadge, 0, 0);
  
  lv_obj_t *title = lv_label_create(titleBadge);
  lv_label_set_text(title, "TIMER");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFF9500), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_center(title);
  
  // Calculate remaining time
  unsigned long remaining = SAND_TIMER_DURATION;
  if (sandTimerRunning) {
    unsigned long elapsed = millis() - sandTimerStartMs;
    remaining = SAND_TIMER_DURATION > elapsed ? SAND_TIMER_DURATION - elapsed : 0;
  }
  float progress = (float)remaining / SAND_TIMER_DURATION;
  int progressPct = (int)(progress * 100);
  
  // Large circular progress ring
  lv_obj_t *progressRing = lv_arc_create(card);
  lv_obj_set_size(progressRing, 200, 200);
  lv_obj_align(progressRing, LV_ALIGN_CENTER, 0, -10);
  lv_arc_set_rotation(progressRing, 270);
  lv_arc_set_bg_angles(progressRing, 0, 360);
  lv_arc_set_value(progressRing, progressPct);
  lv_obj_set_style_arc_color(progressRing, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
  lv_obj_set_style_arc_color(progressRing, lv_color_hex(0xFF9500), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(progressRing, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_width(progressRing, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(progressRing, true, LV_PART_INDICATOR);
  lv_obj_remove_style(progressRing, NULL, LV_PART_KNOB);
  
  // Inner glow effect
  lv_obj_t *innerGlow = lv_obj_create(card);
  lv_obj_set_size(innerGlow, 160, 160);
  lv_obj_align(innerGlow, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_color(innerGlow, lv_color_hex(0xFF9500), 0);
  lv_obj_set_style_bg_opa(innerGlow, sandTimerRunning ? LV_OPA_10 : LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(innerGlow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(innerGlow, 0, 0);
  
  // Time display - large and centered
  int secs = remaining / 1000;
  int mins = secs / 60;
  secs = secs % 60;
  char tBuf[16];
  snprintf(tBuf, sizeof(tBuf), "%d:%02d", mins, secs);
  
  sandTimerLbl = lv_label_create(card);
  lv_label_set_text(sandTimerLbl, tBuf);
  lv_obj_set_style_text_color(sandTimerLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(sandTimerLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(sandTimerLbl, LV_ALIGN_CENTER, 0, -15);
  
  // Status label
  lv_obj_t *statusLbl = lv_label_create(card);
  lv_label_set_text(statusLbl, sandTimerRunning ? "Running" : (remaining < SAND_TIMER_DURATION ? "Paused" : "Ready"));
  lv_obj_set_style_text_color(statusLbl, sandTimerRunning ? lv_color_hex(0xFF9500) : lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_14, 0);
  lv_obj_align(statusLbl, LV_ALIGN_CENTER, 0, 25);
  
  // Button row
  lv_obj_t *btnRow = lv_obj_create(card);
  lv_obj_set_size(btnRow, LCD_WIDTH - 60, 60);
  lv_obj_align(btnRow, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btnRow, 0, 0);
  lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  // Reset button
  lv_obj_t *resetBtn = lv_btn_create(btnRow);
  lv_obj_set_size(resetBtn, 100, 50);
  lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0x3A3A3C), 0);
  lv_obj_set_style_radius(resetBtn, 25, 0);
  lv_obj_add_event_cb(resetBtn, sandTimerResetCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *resetLbl = lv_label_create(resetBtn);
  lv_label_set_text(resetLbl, "Reset");
  lv_obj_set_style_text_color(resetLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(resetLbl);
  
  // Start/Pause button
  lv_obj_t *startBtn = lv_btn_create(btnRow);
  lv_obj_set_size(startBtn, 130, 50);
  lv_obj_set_style_bg_color(startBtn, sandTimerRunning ? lv_color_hex(0xFF9500) : lv_color_hex(0x30D158), 0);
  lv_obj_set_style_radius(startBtn, 25, 0);
  lv_obj_set_style_shadow_width(startBtn, 12, 0);
  lv_obj_set_style_shadow_color(startBtn, sandTimerRunning ? lv_color_hex(0xFF9500) : lv_color_hex(0x30D158), 0);
  lv_obj_set_style_shadow_opa(startBtn, LV_OPA_40, 0);
  lv_obj_add_event_cb(startBtn, sandTimerStartCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *startLbl = lv_label_create(startBtn);
  lv_label_set_text(startLbl, sandTimerRunning ? "Pause" : "Start");
  lv_obj_set_style_text_color(startLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(startLbl, &lv_font_montserrat_16, 0);
  lv_obj_center(startLbl);
}

// ============================================
// STOPWATCH CARD - Premium Design
// ============================================
void stopwatchCb(lv_event_t *e) {
  int action = (int)(intptr_t)lv_event_get_user_data(e);
  if (action == 0) {
    stopwatchRunning = !stopwatchRunning;
    if (stopwatchRunning) stopwatchStartMs = millis() - stopwatchElapsedMs;
    else stopwatchElapsedMs = millis() - stopwatchStartMs;
  } else if (action == 1) {
    stopwatchRunning = false;
    stopwatchElapsedMs = 0;
  }
  navigateTo(CAT_TIMER, 1);
}

void createStopwatchCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  // Dark card with cyan accent
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Title badge
  lv_obj_t *titleBadge = lv_obj_create(card);
  lv_obj_set_size(titleBadge, 120, 30);
  lv_obj_align(titleBadge, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(titleBadge, lv_color_hex(0x5AC8FA), 0);
  lv_obj_set_style_bg_opa(titleBadge, LV_OPA_20, 0);
  lv_obj_set_style_radius(titleBadge, 15, 0);
  lv_obj_set_style_border_width(titleBadge, 0, 0);
  
  lv_obj_t *title = lv_label_create(titleBadge);
  lv_label_set_text(title, "STOPWATCH");
  lv_obj_set_style_text_color(title, lv_color_hex(0x5AC8FA), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_center(title);
  
  unsigned long ms = stopwatchRunning ? (millis() - stopwatchStartMs) : stopwatchElapsedMs;
  int hours = (ms / 3600000);
  int mins = (ms / 60000) % 60;
  int secs = (ms / 1000) % 60;
  int cents = (ms / 10) % 100;
  
  // Main time container
  lv_obj_t *timeContainer = lv_obj_create(card);
  lv_obj_set_size(timeContainer, LCD_WIDTH - 60, 140);
  lv_obj_align(timeContainer, LV_ALIGN_CENTER, 0, -30);
  lv_obj_set_style_bg_color(timeContainer, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(timeContainer, 24, 0);
  lv_obj_set_style_border_width(timeContainer, 0, 0);
  lv_obj_set_style_pad_all(timeContainer, 0, 0);
  
  // Pulsing ring when running
  if (stopwatchRunning) {
    lv_obj_set_style_border_color(timeContainer, lv_color_hex(0x5AC8FA), 0);
    lv_obj_set_style_border_width(timeContainer, 2, 0);
    lv_obj_set_style_shadow_width(timeContainer, 20, 0);
    lv_obj_set_style_shadow_color(timeContainer, lv_color_hex(0x5AC8FA), 0);
    lv_obj_set_style_shadow_opa(timeContainer, LV_OPA_30, 0);
  }
  
  // Hours (if > 0)
  if (hours > 0) {
    char hBuf[8];
    snprintf(hBuf, sizeof(hBuf), "%d:", hours);
    lv_obj_t *hourLbl = lv_label_create(timeContainer);
    lv_label_set_text(hourLbl, hBuf);
    lv_obj_set_style_text_color(hourLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(hourLbl, &lv_font_montserrat_36, 0);
    lv_obj_align(hourLbl, LV_ALIGN_CENTER, -100, -10);
  }
  
  // Minutes : Seconds (large)
  char tBuf[16];
  snprintf(tBuf, sizeof(tBuf), "%02d:%02d", mins, secs);
  lv_obj_t *timeLbl = lv_label_create(timeContainer);
  lv_label_set_text(timeLbl, tBuf);
  lv_obj_set_style_text_color(timeLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(timeLbl, LV_ALIGN_CENTER, 0, -15);
  
  // Centiseconds (smaller, accent color)
  char cBuf[8];
  snprintf(cBuf, sizeof(cBuf), ".%02d", cents);
  lv_obj_t *centLbl = lv_label_create(timeContainer);
  lv_label_set_text(centLbl, cBuf);
  lv_obj_set_style_text_color(centLbl, lv_color_hex(0x5AC8FA), 0);
  lv_obj_set_style_text_font(centLbl, &lv_font_montserrat_36, 0);
  lv_obj_align(centLbl, LV_ALIGN_CENTER, 0, 35);
  
  // Status indicator
  lv_obj_t *statusDot = lv_obj_create(timeContainer);
  lv_obj_set_size(statusDot, 12, 12);
  lv_obj_align(statusDot, LV_ALIGN_TOP_RIGHT, -15, 15);
  lv_obj_set_style_bg_color(statusDot, stopwatchRunning ? lv_color_hex(0x30D158) : lv_color_hex(0x636366), 0);
  lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(statusDot, 0, 0);
  if (stopwatchRunning) {
    lv_obj_set_style_shadow_width(statusDot, 8, 0);
    lv_obj_set_style_shadow_color(statusDot, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_shadow_opa(statusDot, LV_OPA_60, 0);
  }
  
  // Button row
  lv_obj_t *btnRow = lv_obj_create(card);
  lv_obj_set_size(btnRow, LCD_WIDTH - 60, 70);
  lv_obj_align(btnRow, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btnRow, 0, 0);
  lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  // Reset button (left, gray)
  lv_obj_t *resetBtn = lv_btn_create(btnRow);
  lv_obj_set_size(resetBtn, 100, 55);
  lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0x3A3A3C), 0);
  lv_obj_set_style_radius(resetBtn, 27, 0);
  lv_obj_add_event_cb(resetBtn, stopwatchCb, LV_EVENT_CLICKED, (void*)1);
  
  lv_obj_t *resetLbl = lv_label_create(resetBtn);
  lv_label_set_text(resetLbl, "Reset");
  lv_obj_set_style_text_color(resetLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(resetLbl, &lv_font_montserrat_16, 0);
  lv_obj_center(resetLbl);
  
  // Start/Pause button (right, colored)
  lv_obj_t *startBtn = lv_btn_create(btnRow);
  lv_obj_set_size(startBtn, 140, 55);
  lv_obj_set_style_bg_color(startBtn, stopwatchRunning ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x30D158), 0);
  lv_obj_set_style_radius(startBtn, 27, 0);
  lv_obj_set_style_shadow_width(startBtn, 12, 0);
  lv_obj_set_style_shadow_color(startBtn, stopwatchRunning ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x30D158), 0);
  lv_obj_set_style_shadow_opa(startBtn, LV_OPA_40, 0);
  lv_obj_add_event_cb(startBtn, stopwatchCb, LV_EVENT_CLICKED, (void*)0);
  
  lv_obj_t *startLbl = lv_label_create(startBtn);
  lv_label_set_text(startLbl, stopwatchRunning ? "Pause" : "Start");
  lv_obj_set_style_text_color(startLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(startLbl, &lv_font_montserrat_18, 0);
  lv_obj_center(startLbl);
}

// ============================================
// COUNTDOWN CARD - Premium Design
// ============================================
int countdownSelected = 2;  // Default 5 min (index 2)
int countdownTimes[] = {60, 180, 300, 600};  // seconds

void countdownSelectCb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  countdownSelected = idx;
  navigateTo(CAT_TIMER, 2);
}

void createCountdownCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  // Purple gradient background
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Title badge
  lv_obj_t *titleBadge = lv_obj_create(card);
  lv_obj_set_size(titleBadge, 130, 30);
  lv_obj_align(titleBadge, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(titleBadge, lv_color_hex(0xBF5AF2), 0);
  lv_obj_set_style_bg_opa(titleBadge, LV_OPA_20, 0);
  lv_obj_set_style_radius(titleBadge, 15, 0);
  lv_obj_set_style_border_width(titleBadge, 0, 0);
  
  lv_obj_t *title = lv_label_create(titleBadge);
  lv_label_set_text(title, "COUNTDOWN");
  lv_obj_set_style_text_color(title, lv_color_hex(0xBF5AF2), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_center(title);
  
  // Time display with arc background
  lv_obj_t *timeArc = lv_arc_create(card);
  lv_obj_set_size(timeArc, 180, 180);
  lv_obj_align(timeArc, LV_ALIGN_CENTER, 0, -20);
  lv_arc_set_rotation(timeArc, 270);
  lv_arc_set_bg_angles(timeArc, 0, 360);
  lv_arc_set_value(timeArc, 100);
  lv_obj_set_style_arc_color(timeArc, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
  lv_obj_set_style_arc_color(timeArc, lv_color_hex(0xBF5AF2), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(timeArc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(timeArc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(timeArc, true, LV_PART_INDICATOR);
  lv_obj_remove_style(timeArc, NULL, LV_PART_KNOB);
  
  // Time display
  int totalSecs = countdownTimes[countdownSelected];
  int mins = totalSecs / 60;
  int secs = totalSecs % 60;
  char tBuf[16];
  snprintf(tBuf, sizeof(tBuf), "%02d:%02d", mins, secs);
  
  lv_obj_t *timeLbl = lv_label_create(card);
  lv_label_set_text(timeLbl, tBuf);
  lv_obj_set_style_text_color(timeLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(timeLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(timeLbl, LV_ALIGN_CENTER, 0, -20);
  
  // Time preset buttons
  lv_obj_t *presets = lv_obj_create(card);
  lv_obj_set_size(presets, LCD_WIDTH - 50, 55);
  lv_obj_align(presets, LV_ALIGN_CENTER, 0, 75);
  lv_obj_set_style_bg_opa(presets, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(presets, 0, 0);
  lv_obj_set_flex_flow(presets, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(presets, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  const char* times[] = {"1m", "3m", "5m", "10m"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *btn = lv_obj_create(presets);
    lv_obj_set_size(btn, 60, 45);
    bool selected = (i == countdownSelected);
    lv_obj_set_style_bg_color(btn, selected ? lv_color_hex(0xBF5AF2) : lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_radius(btn, 22, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, countdownSelectCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    
    if (selected) {
      lv_obj_set_style_shadow_width(btn, 10, 0);
      lv_obj_set_style_shadow_color(btn, lv_color_hex(0xBF5AF2), 0);
      lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
    }
    
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, times[i]);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
  }
  
  // Start button
  lv_obj_t *startBtn = lv_btn_create(card);
  lv_obj_set_size(startBtn, 180, 55);
  lv_obj_align(startBtn, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_bg_color(startBtn, lv_color_hex(0xBF5AF2), 0);
  lv_obj_set_style_radius(startBtn, 27, 0);
  lv_obj_set_style_shadow_width(startBtn, 15, 0);
  lv_obj_set_style_shadow_color(startBtn, lv_color_hex(0xBF5AF2), 0);
  lv_obj_set_style_shadow_opa(startBtn, LV_OPA_40, 0);
  
  lv_obj_t *startLbl = lv_label_create(startBtn);
  lv_label_set_text(startLbl, "Start Countdown");
  lv_obj_set_style_text_color(startLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(startLbl, &lv_font_montserrat_16, 0);
  lv_obj_center(startLbl);
}

// ============================================
// BREATHE CARD (Apple Watch Style Wellness)
// ============================================
bool breatheRunning = false;
int breathePhase = 0;  // 0=inhale, 1=hold, 2=exhale
unsigned long breatheStartMs = 0;

void breatheStartCb(lv_event_t *e) {
  breatheRunning = !breatheRunning;
  if (breatheRunning) {
    breatheStartMs = millis();
    breathePhase = 0;
  }
  navigateTo(CAT_TIMER, 3);
}

void createBreatheCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  // Calming teal gradient
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x00695C), 0);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x00897B), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "BREATHE");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(title, LV_OPA_80, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
  
  // Calculate animation state
  int circleSize = 120;
  const char* instruction = "Tap to begin";
  
  if (breatheRunning) {
    unsigned long elapsed = (millis() - breatheStartMs) % 12000;  // 12 second cycle
    
    if (elapsed < 4000) {
      // Inhale (4 seconds) - circle expands
      breathePhase = 0;
      circleSize = 100 + (elapsed * 80 / 4000);
      instruction = "Breathe in...";
    } else if (elapsed < 6000) {
      // Hold (2 seconds)
      breathePhase = 1;
      circleSize = 180;
      instruction = "Hold...";
    } else {
      // Exhale (6 seconds) - circle contracts
      breathePhase = 2;
      unsigned long exhaleTime = elapsed - 6000;
      circleSize = 180 - (exhaleTime * 80 / 6000);
      instruction = "Breathe out...";
    }
  }
  
  // Breathing circle (pulsing)
  lv_obj_t *breatheCircle = lv_obj_create(card);
  lv_obj_set_size(breatheCircle, circleSize, circleSize);
  lv_obj_align(breatheCircle, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_color(breatheCircle, lv_color_hex(0x4DB6AC), 0);
  lv_obj_set_style_radius(breatheCircle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(breatheCircle, 0, 0);
  lv_obj_set_style_shadow_width(breatheCircle, 30, 0);
  lv_obj_set_style_shadow_color(breatheCircle, lv_color_hex(0x4DB6AC), 0);
  lv_obj_set_style_shadow_opa(breatheCircle, LV_OPA_60, 0);
  
  // Inner circle
  lv_obj_t *innerCircle = lv_obj_create(breatheCircle);
  int innerSize = circleSize - 30;
  if (innerSize < 40) innerSize = 40;
  lv_obj_set_size(innerCircle, innerSize, innerSize);
  lv_obj_align(innerCircle, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(innerCircle, lv_color_hex(0x80CBC4), 0);
  lv_obj_set_style_radius(innerCircle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(innerCircle, 0, 0);
  
  // Instruction text
  lv_obj_t *instrLbl = lv_label_create(card);
  lv_label_set_text(instrLbl, instruction);
  lv_obj_set_style_text_color(instrLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(instrLbl, &lv_font_montserrat_20, 0);
  lv_obj_align(instrLbl, LV_ALIGN_CENTER, 0, 90);
  
  // Start/Stop button
  lv_obj_t *btn = lv_btn_create(card);
  lv_obj_set_size(btn, 140, 50);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(btn, breatheRunning ? lv_color_hex(0xFF453A) : lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(btn, 25, 0);
  lv_obj_add_event_cb(btn, breatheStartCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *btnLbl = lv_label_create(btn);
  lv_label_set_text(btnLbl, breatheRunning ? "STOP" : "BEGIN");
  lv_obj_set_style_text_color(btnLbl, breatheRunning ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x00695C), 0);
  lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_16, 0);
  lv_obj_center(btnLbl);
}

// ============================================
// STREAK CARDS
// ============================================
void createStepStreakCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0xFF6B00), 0);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(0xFF9500), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "STEP STREAK");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(title, LV_OPA_80, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  
  // Fire icon
  lv_obj_t *fireIcon = lv_label_create(card);
  lv_label_set_text(fireIcon, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_color(fireIcon, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(fireIcon, &lv_font_montserrat_48, 0);
  lv_obj_align(fireIcon, LV_ALIGN_CENTER, 0, -50);
  
  char sBuf[16];
  snprintf(sBuf, sizeof(sBuf), "%d", userData.stepStreak);
  lv_obj_t *numLbl = lv_label_create(card);
  lv_label_set_text(numLbl, sBuf);
  lv_obj_set_style_text_color(numLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(numLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(numLbl, LV_ALIGN_CENTER, 0, 20);
  
  lv_obj_t *daysLbl = lv_label_create(card);
  lv_label_set_text(daysLbl, "days in a row");
  lv_obj_set_style_text_color(daysLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(daysLbl, LV_OPA_80, 0);
  lv_obj_align(daysLbl, LV_ALIGN_CENTER, 0, 70);
  
  // Motivation
  lv_obj_t *motiv = lv_label_create(card);
  lv_label_set_text(motiv, "Keep it going!");
  lv_obj_set_style_text_color(motiv, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(motiv, LV_OPA_60, 0);
  lv_obj_align(motiv, LV_ALIGN_BOTTOM_MID, 0, -30);
}

void createGameStreakCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("GAME STREAK");
  
  char sBuf[16];
  snprintf(sBuf, sizeof(sBuf), "%d", userData.blackjackStreak);
  lv_obj_t *numLbl = lv_label_create(card);
  lv_label_set_text(numLbl, sBuf);
  lv_obj_set_style_text_color(numLbl, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_text_font(numLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(numLbl, LV_ALIGN_CENTER, 0, -40);
  
  lv_obj_t *winsLbl = lv_label_create(card);
  lv_label_set_text(winsLbl, "wins in a row");
  lv_obj_set_style_text_color(winsLbl, theme.text, 0);
  lv_obj_align(winsLbl, LV_ALIGN_CENTER, 0, 10);
  
  // Stats
  lv_obj_t *statsRow = lv_obj_create(card);
  lv_obj_set_size(statsRow, LCD_WIDTH - 70, 60);
  lv_obj_align(statsRow, LV_ALIGN_BOTTOM_MID, 0, -25);
  lv_obj_set_style_bg_color(statsRow, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(statsRow, 16, 0);
  lv_obj_set_style_border_width(statsRow, 0, 0);
  
  char statBuf[32];
  snprintf(statBuf, sizeof(statBuf), "%d / %d", userData.gamesWon, userData.gamesPlayed);
  lv_obj_t *statLbl = lv_label_create(statsRow);
  lv_label_set_text(statLbl, statBuf);
  lv_obj_set_style_text_color(statLbl, theme.accent, 0);
  lv_obj_set_style_text_font(statLbl, &lv_font_montserrat_20, 0);
  lv_obj_align(statLbl, LV_ALIGN_TOP_MID, 0, 8);
  
  lv_obj_t *descLbl = lv_label_create(statsRow);
  lv_label_set_text(descLbl, "games won");
  lv_obj_set_style_text_color(descLbl, lv_color_hex(0x8E8E93), 0);
  lv_obj_align(descLbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void createAchievementsCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("ACHIEVEMENTS");
  
  struct Achievement {
    const char* name;
    const char* desc;
    bool unlocked;
  };
  
  Achievement achs[] = {
    {"10K Steps", "Walk 10,000 steps", userData.steps >= 10000},
    {"Winner", "Win 5 games", userData.gamesWon >= 5},
    {"7 Day Streak", "Maintain 7-day streak", userData.stepStreak >= 7}
  };
  
  for (int i = 0; i < 3; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 70, 70);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 30 + i * 80);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    // Status icon
    lv_obj_t *statusIcon = lv_obj_create(row);
    lv_obj_set_size(statusIcon, 36, 36);
    lv_obj_align(statusIcon, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_bg_color(statusIcon, achs[i].unlocked ? lv_color_hex(0x30D158) : lv_color_hex(0x3A3A3C), 0);
    lv_obj_set_style_radius(statusIcon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(statusIcon, 0, 0);
    
    lv_obj_t *check = lv_label_create(statusIcon);
    lv_label_set_text(check, achs[i].unlocked ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(check, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(check);
    
    // Text
    lv_obj_t *nameLbl = lv_label_create(row);
    lv_label_set_text(nameLbl, achs[i].name);
    lv_obj_set_style_text_color(nameLbl, achs[i].unlocked ? theme.text : lv_color_hex(0x636366), 0);
    lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_16, 0);
    lv_obj_align(nameLbl, LV_ALIGN_LEFT_MID, 60, -10);
    
    lv_obj_t *descLbl = lv_label_create(row);
    lv_label_set_text(descLbl, achs[i].desc);
    lv_obj_set_style_text_color(descLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(descLbl, &lv_font_montserrat_12, 0);
    lv_obj_align(descLbl, LV_ALIGN_LEFT_MID, 60, 10);
  }
}

// ============================================
// CALENDAR CARD
// ============================================
void createCalendarCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("");
  
  lv_obj_t *header = lv_label_create(card);
  lv_label_set_text(header, "January 2026");
  lv_obj_set_style_text_color(header, theme.text, 0);
  lv_obj_set_style_text_font(header, &lv_font_montserrat_20, 0);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 15);
  
  // Weekday headers
  const char* weekdays[] = {"S", "M", "T", "W", "T", "F", "S"};
  for (int i = 0; i < 7; i++) {
    lv_obj_t *wdLbl = lv_label_create(card);
    lv_label_set_text(wdLbl, weekdays[i]);
    lv_obj_set_style_text_color(wdLbl, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_text_font(wdLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(wdLbl, LV_ALIGN_TOP_LEFT, 20 + i * 44, 50);
  }
  
  // Day grid (Jan 2026 starts on Thursday)
  int startDay = 4;  // Thursday
  for (int d = 1; d <= 31; d++) {
    int pos = startDay + d - 1;
    int row = pos / 7;
    int col = pos % 7;
    
    lv_obj_t *day = lv_obj_create(card);
    lv_obj_set_size(day, 38, 38);
    lv_obj_align(day, LV_ALIGN_TOP_LEFT, 12 + col * 44, 75 + row * 44);
    lv_obj_set_style_border_width(day, 0, 0);
    
    if (d == 25) {
      lv_obj_set_style_bg_color(day, theme.accent, 0);
      lv_obj_set_style_radius(day, LV_RADIUS_CIRCLE, 0);
    } else {
      lv_obj_set_style_bg_opa(day, LV_OPA_TRANSP, 0);
    }
    
    char dBuf[4];
    snprintf(dBuf, sizeof(dBuf), "%d", d);
    lv_obj_t *lbl = lv_label_create(day);
    lv_label_set_text(lbl, dBuf);
    lv_obj_set_style_text_color(lbl, d == 25 ? lv_color_hex(0xFFFFFF) : theme.text, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
  }
}

// ============================================
// SETTINGS CARD
// ============================================
void brightnessSliderCb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  userData.brightness = lv_slider_get_value(slider);
  gfx->setBrightness(userData.brightness);
}

void themeChangeCb(lv_event_t *e) {
  userData.themeIndex = (userData.themeIndex + 1) % NUM_THEMES;
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
  navigateTo(CAT_SETTINGS, 0);
}

void resetDataCb(lv_event_t *e) {
  resetAllData();
  navigateTo(CAT_SETTINGS, 0);
}

void createSettingsCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  
  // Scrollable settings card
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 24, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "SETTINGS");
  lv_obj_set_style_text_color(title, lv_color_hex(0x8E8E93), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 10);
  
  // Location/Weather info row
  lv_obj_t *locRow = lv_obj_create(card);
  lv_obj_set_size(locRow, LCD_WIDTH - 70, 45);
  lv_obj_align(locRow, LV_ALIGN_TOP_MID, 0, 35);
  lv_obj_set_style_bg_color(locRow, lv_color_hex(0x007AFF), 0);
  lv_obj_set_style_bg_opa(locRow, LV_OPA_20, 0);
  lv_obj_set_style_radius(locRow, 12, 0);
  lv_obj_set_style_border_width(locRow, 0, 0);
  
  lv_obj_t *locIcon = lv_label_create(locRow);
  lv_label_set_text(locIcon, LV_SYMBOL_GPS);
  lv_obj_set_style_text_color(locIcon, lv_color_hex(0x007AFF), 0);
  lv_obj_align(locIcon, LV_ALIGN_LEFT_MID, 12, 0);
  
  char locBuf[48];
  snprintf(locBuf, sizeof(locBuf), "%s, %s", weatherCity, weatherCountry);
  lv_obj_t *locLbl = lv_label_create(locRow);
  lv_label_set_text(locLbl, locBuf);
  lv_obj_set_style_text_color(locLbl, lv_color_hex(0x007AFF), 0);
  lv_obj_align(locLbl, LV_ALIGN_LEFT_MID, 35, 0);
  
  // Theme preview strip
  lv_obj_t *themePreview = lv_obj_create(card);
  lv_obj_set_size(themePreview, LCD_WIDTH - 70, 45);
  lv_obj_align(themePreview, LV_ALIGN_TOP_MID, 0, 90);
  lv_obj_set_style_bg_color(themePreview, theme.color1, 0);
  lv_obj_set_style_bg_grad_color(themePreview, theme.color2, 0);
  lv_obj_set_style_bg_grad_dir(themePreview, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_radius(themePreview, 12, 0);
  lv_obj_set_style_border_width(themePreview, 0, 0);
  lv_obj_add_flag(themePreview, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(themePreview, themeChangeCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *thNameLbl = lv_label_create(themePreview);
  lv_label_set_text(thNameLbl, gradientThemes[userData.themeIndex].name);
  lv_obj_set_style_text_color(thNameLbl, theme.text, 0);
  lv_obj_set_style_text_font(thNameLbl, &lv_font_montserrat_14, 0);
  lv_obj_align(thNameLbl, LV_ALIGN_LEFT_MID, 15, 0);
  
  lv_obj_t *thArrow = lv_label_create(themePreview);
  lv_label_set_text(thArrow, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(thArrow, theme.text, 0);
  lv_obj_align(thArrow, LV_ALIGN_RIGHT_MID, -10, 0);
  
  // Brightness
  lv_obj_t *brRow = lv_obj_create(card);
  lv_obj_set_size(brRow, LCD_WIDTH - 70, 65);
  lv_obj_align(brRow, LV_ALIGN_TOP_MID, 0, 145);
  lv_obj_set_style_bg_color(brRow, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(brRow, 12, 0);
  lv_obj_set_style_border_width(brRow, 0, 0);
  
  lv_obj_t *brIcon = lv_label_create(brRow);
  lv_label_set_text(brIcon, LV_SYMBOL_IMAGE);
  lv_obj_set_style_text_color(brIcon, lv_color_hex(0xFFD60A), 0);
  lv_obj_align(brIcon, LV_ALIGN_TOP_LEFT, 12, 8);
  
  lv_obj_t *brLbl = lv_label_create(brRow);
  lv_label_set_text(brLbl, "Brightness");
  lv_obj_set_style_text_color(brLbl, theme.text, 0);
  lv_obj_align(brLbl, LV_ALIGN_TOP_LEFT, 35, 8);
  
  lv_obj_t *brSlider = lv_slider_create(brRow);
  lv_obj_set_size(brSlider, LCD_WIDTH - 110, 10);
  lv_obj_align(brSlider, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_slider_set_range(brSlider, 20, 255);
  lv_slider_set_value(brSlider, userData.brightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(brSlider, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
  lv_obj_set_style_bg_color(brSlider, theme.accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brSlider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_add_event_cb(brSlider, brightnessSliderCb, LV_EVENT_VALUE_CHANGED, NULL);
  
  // Screen Timeout
  lv_obj_t *toRow = lv_obj_create(card);
  lv_obj_set_size(toRow, LCD_WIDTH - 70, 45);
  lv_obj_align(toRow, LV_ALIGN_TOP_MID, 0, 220);
  lv_obj_set_style_bg_color(toRow, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(toRow, 12, 0);
  lv_obj_set_style_border_width(toRow, 0, 0);
  lv_obj_add_flag(toRow, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(toRow, timeoutChangeCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *toIcon = lv_label_create(toRow);
  lv_label_set_text(toIcon, LV_SYMBOL_EYE_CLOSE);
  lv_obj_set_style_text_color(toIcon, lv_color_hex(0x8E8E93), 0);
  lv_obj_align(toIcon, LV_ALIGN_LEFT_MID, 12, 0);
  
  lv_obj_t *toLbl = lv_label_create(toRow);
  lv_label_set_text(toLbl, "Screen Timeout");
  lv_obj_set_style_text_color(toLbl, theme.text, 0);
  lv_obj_align(toLbl, LV_ALIGN_LEFT_MID, 35, 0);
  
  char tBuf[8];
  snprintf(tBuf, sizeof(tBuf), "%d min", userData.screenTimeout);
  lv_obj_t *toVal = lv_label_create(toRow);
  lv_label_set_text(toVal, tBuf);
  lv_obj_set_style_text_color(toVal, theme.accent, 0);
  lv_obj_align(toVal, LV_ALIGN_RIGHT_MID, -12, 0);
  
  // WiFi Status
  lv_obj_t *wifiRow = lv_obj_create(card);
  lv_obj_set_size(wifiRow, LCD_WIDTH - 70, 45);
  lv_obj_align(wifiRow, LV_ALIGN_TOP_MID, 0, 275);
  lv_obj_set_style_bg_color(wifiRow, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(wifiRow, 12, 0);
  lv_obj_set_style_border_width(wifiRow, 0, 0);
  
  lv_obj_t *wifiIcon = lv_label_create(wifiRow);
  lv_label_set_text(wifiIcon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(wifiIcon, wifiConnected ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
  lv_obj_align(wifiIcon, LV_ALIGN_LEFT_MID, 12, 0);
  
  lv_obj_t *wifiLbl = lv_label_create(wifiRow);
  lv_label_set_text(wifiLbl, "WiFi");
  lv_obj_set_style_text_color(wifiLbl, theme.text, 0);
  lv_obj_align(wifiLbl, LV_ALIGN_LEFT_MID, 35, 0);
  
  lv_obj_t *wifiVal = lv_label_create(wifiRow);
  lv_label_set_text(wifiVal, wifiConnected ? wifiSSID : "Not Connected");
  lv_obj_set_style_text_color(wifiVal, wifiConnected ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
  lv_obj_set_style_text_font(wifiVal, &lv_font_montserrat_12, 0);
  lv_obj_align(wifiVal, LV_ALIGN_RIGHT_MID, -12, 0);
  
  // RESET ALL DATA button (danger zone)
  lv_obj_t *resetBtn = lv_btn_create(card);
  lv_obj_set_size(resetBtn, LCD_WIDTH - 70, 50);
  lv_obj_align(resetBtn, LV_ALIGN_TOP_MID, 0, 340);
  lv_obj_set_style_bg_color(resetBtn, lv_color_hex(0xFF453A), 0);
  lv_obj_set_style_radius(resetBtn, 12, 0);
  lv_obj_add_event_cb(resetBtn, resetDataCb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *resetLbl = lv_label_create(resetBtn);
  lv_label_set_text(resetLbl, "Reset All Data");
  lv_obj_set_style_text_color(resetLbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(resetLbl, &lv_font_montserrat_16, 0);
  lv_obj_center(resetLbl);
  
  // Version info
  lv_obj_t *verLbl = lv_label_create(card);
  lv_label_set_text(verLbl, "Widget OS v2.2");
  lv_obj_set_style_text_color(verLbl, lv_color_hex(0x636366), 0);
  lv_obj_set_style_text_font(verLbl, &lv_font_montserrat_12, 0);
  lv_obj_align(verLbl, LV_ALIGN_TOP_MID, 0, 405);
}

// ============================================
// BATTERY CARD
// ============================================
void createBatteryCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("BATTERY");
  
  int batt = power.getBatteryPercent();
  bool charging = power.isCharging();
  
  // Large arc
  lv_obj_t *arc = lv_arc_create(card);
  lv_obj_set_size(arc, 180, 180);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, -20);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_value(arc, batt);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 15, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 15, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  
  lv_color_t arcCol;
  if (batt > 60) arcCol = lv_color_hex(0x30D158);
  else if (batt > 20) arcCol = lv_color_hex(0xFF9F0A);
  else arcCol = lv_color_hex(0xFF453A);
  lv_obj_set_style_arc_color(arc, arcCol, LV_PART_INDICATOR);
  
  // Percentage
  char bBuf[16];
  snprintf(bBuf, sizeof(bBuf), "%d%%", batt);
  lv_obj_t *battLbl = lv_label_create(card);
  lv_label_set_text(battLbl, bBuf);
  lv_obj_set_style_text_color(battLbl, theme.text, 0);
  lv_obj_set_style_text_font(battLbl, &lv_font_montserrat_36, 0);
  lv_obj_align(battLbl, LV_ALIGN_CENTER, 0, -20);
  
  // Charging status
  lv_obj_t *statusRow = lv_obj_create(card);
  lv_obj_set_size(statusRow, 160, 40);
  lv_obj_align(statusRow, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_set_style_bg_color(statusRow, charging ? lv_color_hex(0x30D158) : lv_color_hex(0x3A3A3C), 0);
  lv_obj_set_style_bg_opa(statusRow, LV_OPA_30, 0);
  lv_obj_set_style_radius(statusRow, 20, 0);
  lv_obj_set_style_border_width(statusRow, 0, 0);
  
  lv_obj_t *chLbl = lv_label_create(statusRow);
  lv_label_set_text(chLbl, charging ? LV_SYMBOL_CHARGE " Charging" : "On Battery");
  lv_obj_set_style_text_color(chLbl, charging ? lv_color_hex(0x30D158) : lv_color_hex(0x8E8E93), 0);
  lv_obj_center(chLbl);
}

// ============================================
// SYSTEM CARD
// ============================================
void createSystemCard() {
  GradientTheme &theme = gradientThemes[userData.themeIndex];
  lv_obj_t *card = createCard("SYSTEM");
  
  // CPU bar
  lv_obj_t *cpuRow = lv_obj_create(card);
  lv_obj_set_size(cpuRow, LCD_WIDTH - 70, 50);
  lv_obj_align(cpuRow, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_set_style_bg_color(cpuRow, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(cpuRow, 12, 0);
  lv_obj_set_style_border_width(cpuRow, 0, 0);
  
  lv_obj_t *cpuLbl = lv_label_create(cpuRow);
  lv_label_set_text(cpuLbl, "CPU");
  lv_obj_set_style_text_color(cpuLbl, lv_color_hex(0x8E8E93), 0);
  lv_obj_align(cpuLbl, LV_ALIGN_LEFT_MID, 15, 0);
  
  lv_obj_t *cpuBar = lv_bar_create(cpuRow);
  lv_obj_set_size(cpuBar, 120, 8);
  lv_obj_align(cpuBar, LV_ALIGN_RIGHT_MID, -15, 0);
  lv_bar_set_value(cpuBar, random(20, 50), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(cpuBar, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
  lv_obj_set_style_bg_color(cpuBar, theme.accent, LV_PART_INDICATOR);
  lv_obj_set_style_radius(cpuBar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(cpuBar, 4, LV_PART_INDICATOR);
  
  // RAM bar
  lv_obj_t *ramRow = lv_obj_create(card);
  lv_obj_set_size(ramRow, LCD_WIDTH - 70, 50);
  lv_obj_align(ramRow, LV_ALIGN_TOP_MID, 0, 90);
  lv_obj_set_style_bg_color(ramRow, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(ramRow, 12, 0);
  lv_obj_set_style_border_width(ramRow, 0, 0);
  
  lv_obj_t *ramLbl = lv_label_create(ramRow);
  lv_label_set_text(ramLbl, "RAM");
  lv_obj_set_style_text_color(ramLbl, lv_color_hex(0x8E8E93), 0);
  lv_obj_align(ramLbl, LV_ALIGN_LEFT_MID, 15, 0);
  
  uint32_t freeH = ESP.getFreeHeap() / 1024;
  uint32_t totalH = ESP.getHeapSize() / 1024;
  int ramP = 100 - (freeH * 100 / totalH);
  
  lv_obj_t *ramBar = lv_bar_create(ramRow);
  lv_obj_set_size(ramBar, 120, 8);
  lv_obj_align(ramBar, LV_ALIGN_RIGHT_MID, -15, 0);
  lv_bar_set_value(ramBar, ramP, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0x30D158), LV_PART_INDICATOR);
  lv_obj_set_style_radius(ramBar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(ramBar, 4, LV_PART_INDICATOR);
  
  // WiFi status
  lv_obj_t *wifiRow = lv_obj_create(card);
  lv_obj_set_size(wifiRow, LCD_WIDTH - 70, 50);
  lv_obj_align(wifiRow, LV_ALIGN_TOP_MID, 0, 150);
  lv_obj_set_style_bg_color(wifiRow, lv_color_hex(0x2C2C2E), 0);
  lv_obj_set_style_radius(wifiRow, 12, 0);
  lv_obj_set_style_border_width(wifiRow, 0, 0);
  
  lv_obj_t *wifiLbl = lv_label_create(wifiRow);
  lv_label_set_text(wifiLbl, "WiFi");
  lv_obj_set_style_text_color(wifiLbl, lv_color_hex(0x8E8E93), 0);
  lv_obj_align(wifiLbl, LV_ALIGN_LEFT_MID, 15, 0);
  
  lv_obj_t *wifiSt = lv_label_create(wifiRow);
  lv_label_set_text(wifiSt, wifiConnected ? "Connected" : "Disconnected");
  lv_obj_set_style_text_color(wifiSt, wifiConnected ? lv_color_hex(0x30D158) : lv_color_hex(0xFF453A), 0);
  lv_obj_align(wifiSt, LV_ALIGN_RIGHT_MID, -15, 0);
  
  // Version
  lv_obj_t *verLbl = lv_label_create(card);
  lv_label_set_text(verLbl, "Widget OS v2.0");
  lv_obj_set_style_text_color(verLbl, lv_color_hex(0x636366), 0);
  lv_obj_align(verLbl, LV_ALIGN_BOTTOM_MID, 0, -25);
}
// ============================================
// S3_MiniOS_v2_part6.ino - Sensor Fusion & Main Loop
// Append this to S3_MiniOS_v2.ino
// ============================================

// ============================================
// SENSOR FUSION - COMPLEMENTARY FILTER
// ============================================
void updateSensorFusion() {
  if (!qmi.getDataReady()) return;
  
  // Get sensor data
  qmi.getAccelerometer(acc.x, acc.y, acc.z);
  qmi.getGyroscope(gyr.x, gyr.y, gyr.z);
  
  // Calculate time delta
  unsigned long now = millis();
  float dt = (now - lastSensorUpdate) / 1000.0;
  if (dt > 0.1) dt = 0.1;  // Cap at 100ms
  lastSensorUpdate = now;
  
  // =========================================
  // ACCELEROMETER-BASED ANGLES
  // =========================================
  // Calculate roll and pitch from accelerometer
  accelRoll = atan2(acc.y, acc.z) * 180.0 / PI;
  accelPitch = atan2(-acc.x, sqrt(acc.y * acc.y + acc.z * acc.z)) * 180.0 / PI;
  
  // =========================================
  // GYROSCOPE INTEGRATION
  // =========================================
  // Convert gyro rates (deg/s) to angle changes
  gyroRoll += gyr.x * dt;
  gyroPitch += gyr.y * dt;
  gyroYaw += gyr.z * dt;
  
  // Wrap gyro yaw
  while (gyroYaw > 180) gyroYaw -= 360;
  while (gyroYaw < -180) gyroYaw += 360;
  
  // =========================================
  // COMPLEMENTARY FILTER FUSION
  // =========================================
  // Fuse accelerometer and gyroscope for roll and pitch
  // High-pass gyro (short-term accuracy) + Low-pass accel (long-term accuracy)
  roll = ALPHA * (roll + gyr.x * dt) + BETA * accelRoll;
  pitch = ALPHA * (pitch + gyr.y * dt) + BETA * accelPitch;
  
  // For yaw, we only have gyro (no magnetometer reference)
  // Integrate gyro with drift compensation
  yaw += gyr.z * dt;
  
  // Apply slow drift correction based on acceleration magnitude
  float accMag = sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
  if (abs(accMag - 1.0) < 0.1) {
    // Device is relatively stationary, apply drift correction
    yaw *= 0.995;  // Slowly decay drift
  }
  
  // Wrap angles
  while (roll > 180) roll -= 360;
  while (roll < -180) roll += 360;
  while (pitch > 90) pitch = 180 - pitch;
  while (pitch < -90) pitch = -180 - pitch;
  while (yaw > 180) yaw -= 360;
  while (yaw < -180) yaw += 360;
  
  // =========================================
  // TILT VALUES (for level card)
  // =========================================
  tiltX = roll;
  tiltY = pitch;
  
  // =========================================
  // COMPASS HEADING CALCULATION
  // =========================================
  // Since we don't have magnetometer, use relative yaw
  // Reference: Initial orientation is "North"
  if (!compassCalibrated && abs(roll) < 5 && abs(pitch) < 5) {
    // Device is level - calibrate
    initialYaw = yaw;
    compassCalibrated = true;
  }
  
  // Relative heading from initial position
  compassHeading = yaw - initialYaw;
  while (compassHeading < 0) compassHeading += 360;
  while (compassHeading >= 360) compassHeading -= 360;
  
  // Smooth the compass heading for display
  float diff = compassHeading - compassHeadingSmooth;
  if (diff > 180) diff -= 360;
  if (diff < -180) diff += 360;
  compassHeadingSmooth += diff * 0.15;  // Smoothing factor
  
  while (compassHeadingSmooth < 0) compassHeadingSmooth += 360;
  while (compassHeadingSmooth >= 360) compassHeadingSmooth -= 360;
}

// ============================================
// COMPASS CALIBRATION
// ============================================
void calibrateCompass() {
  initialYaw = yaw;
  compassCalibrated = true;
  compassHeading = 0;
  compassHeadingSmooth = 0;
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

// Reset all user data to defaults
void resetAllData() {
  userData.steps = 0;
  userData.dailyGoal = 10000;
  userData.stepStreak = 0;
  userData.blackjackStreak = 0;
  userData.gamesWon = 0;
  userData.gamesPlayed = 0;
  userData.brightness = 200;
  userData.screenTimeout = 1;
  userData.themeIndex = 0;
  userData.totalDistance = 0.0;
  userData.totalCalories = 0.0;
  userData.compassMode = 0;
  userData.wallpaperIndex = 0;
  
  // Delete saved data file
  if (SPIFFS.begin(true)) {
    SPIFFS.remove("/userdata.bin");
  }
  
  // Reset compass calibration
  compassCalibrated = false;
  initialYaw = 0;
  roll = pitch = yaw = 0;
  
  USBSerial.println("All data reset to defaults");
}

// ============================================
// NTP TIME SYNC
// ============================================
void syncTimeNTP() {
  if (WiFi.status() != WL_CONNECTED) return;
  configTime(gmtOffsetSec, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
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
  String url = "http://api.openweathermap.org/data/2.5/weather?q=";
  url += weatherCity;
  url += ",";
  url += weatherCountry;
  url += "&units=metric&appid=";
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
// NOTIFICATION FLASH
// ============================================
void showTimerNotification() {
  static int flashCount = 0;
  static unsigned long lastFlash = 0;
  lv_color_t colors[] = {lv_color_hex(0xFF453A), lv_color_hex(0x30D158), lv_color_hex(0x0A84FF)};
  
  if (millis() - lastFlash > 150) {
    lastFlash = millis();
    lv_obj_set_style_bg_color(lv_scr_act(), colors[flashCount % 3], 0);
    flashCount++;
    if (flashCount > 20) {
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
  
  // IMU - Enable both accelerometer AND gyroscope
  if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_500Hz, SensorQMI8658::LPF_MODE_0);
    qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS, SensorQMI8658::GYR_ODR_896_8Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();
    qmi.enableGyroscope();
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
  
  // SD Card - Initialize FIRST to load WiFi config
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
  if (SD_MMC.begin("/sdcard", true)) {
    USBSerial.println("SD Card mounted successfully");
    loadWiFiFromSD();      // Load WiFi credentials from /wifi/config.txt
    scanSDWallpapers();    // Scan /wallpaper folder for images
  } else {
    USBSerial.println("SD Card mount failed - using defaults");
  }
  
  // WiFi - Connect using credentials (from SD or defaults)
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPassword);
  USBSerial.printf("Connecting to WiFi: %s\n", wifiSSID);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) {
    USBSerial.println("WiFi Connected!");
  } else {
    USBSerial.println("WiFi Connection Failed");
  }
  
  // Sync time and fetch data if connected
  if (wifiConnected) {
    syncTimeNTP();
    fetchWeatherData();
    fetchCryptoData();
  }
  
  lastActivityMs = millis();
  lastSensorUpdate = millis();
  navigateTo(CAT_CLOCK, 0);
}

// ============================================
// MAIN LOOP
// ============================================
unsigned long lastUpdate = 0;
unsigned long lastStepCheck = 0;
unsigned long lastDinoUpdate = 0;
unsigned long lastCompassUpdate = 0;
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
  
  // =========================================
  // SENSOR FUSION UPDATE (every 20ms)
  // =========================================
  if (millis() - lastCompassUpdate > 20) {
    lastCompassUpdate = millis();
    updateSensorFusion();
    
    // Update compass card if visible - reduced frequency for performance
    if (currentCategory == CAT_COMPASS && currentSubCard == 0) {
      static unsigned long lastCompassRedraw = 0;
      if (millis() - lastCompassRedraw > 150) {  // Redraw every 150ms (smoother)
        lastCompassRedraw = millis();
        navigateTo(CAT_COMPASS, 0);
      }
    }
    
    // Update tilt card if visible
    if (currentCategory == CAT_COMPASS && currentSubCard == 1) {
      static unsigned long lastTiltRedraw = 0;
      if (millis() - lastTiltRedraw > 150) {
        lastTiltRedraw = millis();
        navigateTo(CAT_COMPASS, 1);
      }
    }
    
    // Update gyro card if visible
    if (currentCategory == CAT_COMPASS && currentSubCard == 2) {
      static unsigned long lastGyroRedraw = 0;
      if (millis() - lastGyroRedraw > 150) {
        lastGyroRedraw = millis();
        navigateTo(CAT_COMPASS, 2);
      }
    }
  }
  
  // =========================================
  // UI UPDATES (every second)
  // =========================================
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
      
      // Update timer display if visible
      if (currentCategory == CAT_TIMER && currentSubCard == 0) {
        navigateTo(CAT_TIMER, 0);
      }
    }
    
    // Stopwatch update
    if (stopwatchRunning && currentCategory == CAT_TIMER && currentSubCard == 1) {
      navigateTo(CAT_TIMER, 1);
    }
    
    // Breathe animation update
    if (breatheRunning && currentCategory == CAT_TIMER && currentSubCard == 3) {
      navigateTo(CAT_TIMER, 3);
    }
    
    // Yes/No spin animation
    if (yesNoSpinning) {
      yesNoAngle += 45;
      if (yesNoAngle > 360) {
        yesNoSpinning = false;
        const char* answers[] = {"Yes", "No", "Maybe", "Ask again", "Definitely", "Never"};
        yesNoResult = answers[random(0, 6)];
        navigateTo(CAT_GAMES, 2);
      }
    }
  }
  
  // =========================================
  // STEP COUNTING (every 50ms)
  // =========================================
  if (millis() - lastStepCheck > 50) {
    lastStepCheck = millis();
    
    float mag = sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
    float delta = abs(mag - lastAccMag);
    
    if (delta > 0.5 && delta < 3.0) {
      userData.steps++;
      userData.totalDistance = userData.steps * 0.0007;
      userData.totalCalories = userData.steps * 0.04;
    }
    lastAccMag = mag;
  }
  
  // =========================================
  // DINO GAME LOOP (every 30ms for smoother animation)
  // =========================================
  if (currentCategory == CAT_GAMES && currentSubCard == 1 && !dinoGameOver) {
    if (millis() - lastDinoUpdate > 30) {
      lastDinoUpdate = millis();
      
      // Move obstacle
      obstacleX -= 10;
      if (obstacleX < -30) {
        obstacleX = 320;
        dinoScore++;
      }
      
      // Physics-based jump
      if (dinoJumping) {
        dinoVelocity += GRAVITY;
        dinoY += (int)dinoVelocity;
        
        // Land on ground
        if (dinoY >= 0) {
          dinoY = 0;
          dinoVelocity = 0;
          dinoJumping = false;
        }
      }
      
      // Collision detection (dino is at x=40, width=35, obstacle width=25)
      // dinoY is negative when jumping (higher = more negative)
      if (obstacleX < 75 && obstacleX > 5 && dinoY > -45) {
        dinoGameOver = true;
      }
      
      navigateTo(CAT_GAMES, 1);
    }
  }
  
  // =========================================
  // =========================================
  // SAVE DATA (every 2 hours to reduce lag)
  // =========================================
  static unsigned long lastSave = 0;
  if (millis() - lastSave > SAVE_INTERVAL_MS) {
    lastSave = millis();
    saveUserData();
  }
  
  // =========================================
  // TRANSITION ANIMATION
  // =========================================
  if (isTransitioning) {
    transitionProgress = (millis() - transitionStartMs) / (float)TRANSITION_DURATION;
    if (transitionProgress >= 1.0) {
      transitionProgress = 1.0;
      isTransitioning = false;
    }
  }
}
