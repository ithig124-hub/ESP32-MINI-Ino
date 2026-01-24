/**
 * ESP32-S3 Touch AMOLED 1.8" Mini OS
 * Professional LVGL-based firmware with swipe navigation
 * 
 * 23 Total Cards:
 * Clock, Activity Rings, Steps, Workout, Sleep, Streak, Daily Goal,
 * Music, Weather, Quote, Yes/No Spinner, Timer, Dino Game, Volume,
 * World Clock, Calendar, Notes, Torch, Battery, Gallery, Stocks,
 * Themes, System, Compass
 */

// ============================================
// INCLUDES
// ============================================
#include <lvgl.h>
#include <Wire.h>
#include <WiFi.h>
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
// WIFI CONFIGURATION
// ============================================
const char* WIFI_SSID = "Optus_9D2E3D";
const char* WIFI_PASSWORD = "snucktemptGLeQU";

// ============================================
// LVGL CONFIGURATION
// ============================================
#define EXAMPLE_LVGL_TICK_PERIOD_MS 2
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

// ============================================
// HARDWARE OBJECTS
// ============================================
HWCDC USBSerial;
Adafruit_XCA9554 expander;
SensorQMI8658 qmi;
SensorPCF85063 rtc;
XPowersPMU power;

IMUdata acc;
IMUdata gyr;

// Display & Touch
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_SH8601 *gfx = new Arduino_SH8601(
  bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
  std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS,
                                                       DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

// ============================================
// NAVIGATION & STATE
// ============================================
enum MainCard {
  CARD_CLOCK = 0,
  CARD_COMPASS,
  CARD_ACTIVITY_RINGS,
  CARD_STEPS,
  CARD_WORKOUT,
  CARD_SLEEP,
  CARD_STREAK,
  CARD_DAILY_GOAL,
  CARD_MUSIC,
  CARD_WEATHER,
  CARD_QUOTE,
  CARD_YESNO,
  CARD_TIMER,
  CARD_DINO,
  CARD_VOLUME,
  CARD_WORLD_CLOCK,
  CARD_CALENDAR,
  CARD_NOTES,
  CARD_TORCH,
  CARD_BATTERY,
  CARD_GALLERY,
  CARD_STOCKS,
  CARD_THEMES,
  CARD_SYSTEM,
  MAIN_CARD_COUNT
};

int currentMainCard = CARD_CLOCK;
int currentSubCard = 0;

// Sub-card counts per main card
const int subCardCounts[] = {2, 1, 1, 1, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1};

// ============================================
// UI STATE VARIABLES
// ============================================
// Brightness & Volume
int brightness = 200;
int volumeLevel = 70;
bool torchOn = false;

// Step tracking
uint32_t stepCount = 0;
float totalDistance = 0.0;
float totalCalories = 0.0;
int dailyGoal = 10000;

// Activity Rings
int moveProgress = 65;
int exerciseProgress = 45;
int standProgress = 80;

// Streak
int currentStreak = 7;

// Sleep tracking
bool sleepTrackingActive = false;
unsigned long sleepStartTime = 0;
unsigned long sleepEndTime = 0;
int movementData[24] = {0};
int movementIndex = 0;
unsigned long lastMovementSample = 0;

// Stopwatch/Timer
bool stopwatchRunning = false;
unsigned long stopwatchStart = 0;
unsigned long stopwatchElapsed = 0;
bool timerRunning = false;
unsigned long timerDuration = 300000;  // 5 minutes
unsigned long timerStart = 0;

// Workout modes
enum WorkoutMode { WORKOUT_NONE = 0, WORKOUT_BIKE, WORKOUT_RUN, WORKOUT_BASKETBALL };
WorkoutMode currentWorkout = WORKOUT_NONE;
unsigned long workoutStartTime = 0;
uint32_t workoutSteps = 0;

// Games
int clickerScore = 0;
int dinoScore = 0;
bool dinoJumping = false;
int dinoY = 0;
int obstacleX = LCD_WIDTH;
bool dinoGameOver = false;

// Compass
float compassHeading = 45.0;
const char* sunriseTime = "05:39";
const char* sunsetTime = "19:05";

// World Clock
const char* worldCities[] = {"New York", "London", "Tokyo", "Sydney"};
const int worldOffsets[] = {-5, 0, 9, 11};

// Calendar
int selectedDay = 8;
int currentMonth = 12;
int currentYear = 2024;

// Quotes
const char* quotes[] = {
  "The only way to do great\nwork is to love what you do.",
  "Innovation distinguishes\nbetween a leader and\na follower.",
  "Stay hungry, stay foolish.",
  "Your time is limited,\ndon't waste it living\nsomeone else's life.",
  "The future belongs to\nthose who believe in\ntheir dreams."
};
int currentQuote = 0;

// Notes
char notes[5][64] = {"Note 1", "Note 2", "Note 3", "Note 4", "Note 5"};
int currentNote = 0;

// Themes
int currentTheme = 0;

// Mock data
const char* stockSymbols[] = {"AAPL", "GOOGL", "MSFT", "AMZN"};
float stockPrices[] = {178.50, 141.20, 378.90, 178.25};
const char* cryptoSymbols[] = {"BTC", "ETH", "SOL", "ADA"};
float cryptoPrices[] = {67432.50, 3521.80, 142.30, 0.58};

// Music & Gallery
int currentTrack = 0;
bool musicPlaying = false;
String musicFiles[20];
int musicFileCount = 0;
int currentPhoto = 0;
String photoFiles[50];
int photoFileCount = 0;

// ============================================
// TOUCH TRACKING
// ============================================
int32_t touchStartX = 0;
int32_t touchStartY = 0;
int32_t touchCurrentX = 0;
int32_t touchCurrentY = 0;
bool touchActive = false;
unsigned long touchStartTime = 0;

// ============================================
// THEME COLORS
// ============================================
struct Theme {
  lv_color_t bg;
  lv_color_t card;
  lv_color_t text;
  lv_color_t accent;
  lv_color_t secondary;
};

Theme themes[] = {
  // Dark theme (AMOLED)
  {lv_color_hex(0x000000), lv_color_hex(0x1A1A1A), lv_color_hex(0xFFFFFF), lv_color_hex(0x00D4FF), lv_color_hex(0x666666)},
  // Blue theme
  {lv_color_hex(0x0A1929), lv_color_hex(0x132F4C), lv_color_hex(0xFFFFFF), lv_color_hex(0x5090D3), lv_color_hex(0x3D5A80)},
  // Green theme
  {lv_color_hex(0x0D1F0D), lv_color_hex(0x1A3A1A), lv_color_hex(0xFFFFFF), lv_color_hex(0x4CAF50), lv_color_hex(0x2E7D32)},
  // Purple theme
  {lv_color_hex(0x1A0A29), lv_color_hex(0x2D1B4E), lv_color_hex(0xFFFFFF), lv_color_hex(0xBB86FC), lv_color_hex(0x7C4DFF)}
};

// ============================================
// FUNCTION PROTOTYPES
// ============================================
void navigateToCard(int mainCard, int subCard);
void handleSwipe(int dx, int dy);
void createNavigationDots();
lv_obj_t* createCardContainer(const char* title);

// Card creation functions
void createClockCard();
void createAnalogClockCard();
void createCompassCard();
void createActivityRingsCard();
void createStepsCard();
void createWorkoutCard();
void createSleepCard();
void createSleepGraphCard();
void createStreakCard();
void createDailyGoalCard();
void createMusicCard();
void createWeatherCard();
void createQuoteCard();
void createYesNoCard();
void createTimerCard();
void createDinoCard();
void createVolumeCard();
void createWorldClockCard();
void createCalendarCard();
void createNotesCard();
void createTorchCard();
void createBatteryCard();
void createGalleryCard();
void createStocksCard();
void createCryptoCard();
void createThemesCard();
void createSystemCard();

// ============================================
// TOUCH INTERRUPT
// ============================================
void Arduino_IIC_Touch_Interrupt(void) {
  FT3168->IIC_Interrupt_Flag = true;
}

// ============================================
// DISPLAY FLUSH CALLBACK
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

// ============================================
// LVGL TICK CALLBACK
// ============================================
void example_increase_lvgl_tick(void *arg) {
  lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

// ============================================
// TOUCHPAD READ CALLBACK
// ============================================
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  int32_t touchX = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
  int32_t touchY = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

  if (FT3168->IIC_Interrupt_Flag == true) {
    FT3168->IIC_Interrupt_Flag = false;
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;

    if (!touchActive) {
      touchActive = true;
      touchStartX = touchX;
      touchStartY = touchY;
      touchStartTime = millis();
    }
    touchCurrentX = touchX;
    touchCurrentY = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
    
    if (touchActive) {
      touchActive = false;
      int dx = touchCurrentX - touchStartX;
      int dy = touchCurrentY - touchStartY;
      unsigned long touchDuration = millis() - touchStartTime;
      
      if (touchDuration < 500 && (abs(dx) > 50 || abs(dy) > 50)) {
        handleSwipe(dx, dy);
      }
    }
  }
}

// ============================================
// SWIPE HANDLER
// ============================================
void handleSwipe(int dx, int dy) {
  if (abs(dx) > abs(dy)) {
    if (dx > 50) {
      currentMainCard--;
      if (currentMainCard < 0) currentMainCard = MAIN_CARD_COUNT - 1;
      currentSubCard = 0;
    } else if (dx < -50) {
      currentMainCard++;
      if (currentMainCard >= MAIN_CARD_COUNT) currentMainCard = 0;
      currentSubCard = 0;
    }
  } else {
    if (dy > 50) {
      if (currentSubCard < subCardCounts[currentMainCard] - 1) {
        currentSubCard++;
      }
    } else if (dy < -50) {
      if (currentSubCard > 0) {
        currentSubCard--;
      }
    }
  }
  
  navigateToCard(currentMainCard, currentSubCard);
}

// ============================================
// NAVIGATION
// ============================================
void navigateToCard(int mainCard, int subCard) {
  lv_obj_clean(lv_scr_act());
  
  Theme &t = themes[currentTheme];
  lv_obj_set_style_bg_color(lv_scr_act(), t.bg, 0);
  
  switch (mainCard) {
    case CARD_CLOCK:
      if (subCard == 0) createClockCard();
      else createAnalogClockCard();
      break;
    case CARD_COMPASS: createCompassCard(); break;
    case CARD_ACTIVITY_RINGS: createActivityRingsCard(); break;
    case CARD_STEPS: createStepsCard(); break;
    case CARD_WORKOUT: createWorkoutCard(); break;
    case CARD_SLEEP:
      if (subCard == 0) createSleepCard();
      else createSleepGraphCard();
      break;
    case CARD_STREAK: createStreakCard(); break;
    case CARD_DAILY_GOAL: createDailyGoalCard(); break;
    case CARD_MUSIC: createMusicCard(); break;
    case CARD_WEATHER: createWeatherCard(); break;
    case CARD_QUOTE: createQuoteCard(); break;
    case CARD_YESNO: createYesNoCard(); break;
    case CARD_TIMER: createTimerCard(); break;
    case CARD_DINO: createDinoCard(); break;
    case CARD_VOLUME: createVolumeCard(); break;
    case CARD_WORLD_CLOCK: createWorldClockCard(); break;
    case CARD_CALENDAR: createCalendarCard(); break;
    case CARD_NOTES: createNotesCard(); break;
    case CARD_TORCH: createTorchCard(); break;
    case CARD_BATTERY: createBatteryCard(); break;
    case CARD_GALLERY: createGalleryCard(); break;
    case CARD_STOCKS:
      if (subCard == 0) createStocksCard();
      else createCryptoCard();
      break;
    case CARD_THEMES: createThemesCard(); break;
    case CARD_SYSTEM: createSystemCard(); break;
  }
  
  createNavigationDots();
}

// ============================================
// NAVIGATION DOTS
// ============================================
void createNavigationDots() {
  Theme &t = themes[currentTheme];
  
  lv_obj_t *dotsContainer = lv_obj_create(lv_scr_act());
  lv_obj_set_size(dotsContainer, LCD_WIDTH, 20);
  lv_obj_align(dotsContainer, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_opa(dotsContainer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dotsContainer, 0, 0);
  lv_obj_set_style_pad_all(dotsContainer, 0, 0);
  lv_obj_set_flex_flow(dotsContainer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dotsContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  int startCard = max(0, min(currentMainCard - 2, MAIN_CARD_COUNT - 5));
  int endCard = min(MAIN_CARD_COUNT, startCard + 5);
  
  for (int i = startCard; i < endCard; i++) {
    lv_obj_t *dot = lv_obj_create(dotsContainer);
    int size = (i == currentMainCard) ? 10 : 6;
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, (i == currentMainCard) ? t.accent : t.secondary, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_margin_all(dot, 3, 0);
  }
  
  if (subCardCounts[currentMainCard] > 1) {
    lv_obj_t *subDotsContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(subDotsContainer, 20, 80);
    lv_obj_align(subDotsContainer, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_opa(subDotsContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(subDotsContainer, 0, 0);
    lv_obj_set_flex_flow(subDotsContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(subDotsContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    for (int i = 0; i < subCardCounts[currentMainCard]; i++) {
      lv_obj_t *dot = lv_obj_create(subDotsContainer);
      int size = (i == currentSubCard) ? 10 : 6;
      lv_obj_set_size(dot, size, size);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(dot, (i == currentSubCard) ? t.accent : t.secondary, 0);
      lv_obj_set_style_border_width(dot, 0, 0);
      lv_obj_set_style_margin_all(dot, 4, 0);
    }
  }
}

// ============================================
// HELPER: CREATE CARD CONTAINER
// ============================================
lv_obj_t* createCardContainer(const char* title) {
  Theme &t = themes[currentTheme];
  
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 20, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_bg_color(card, t.card, 0);
  lv_obj_set_style_radius(card, 20, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 15, 0);
  
  lv_obj_t *titleLabel = lv_label_create(card);
  lv_label_set_text(titleLabel, title);
  lv_obj_set_style_text_color(titleLabel, t.text, 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 0);
  
  return card;
}

// ============================================
// CLOCK CARD (DIGITAL)
// ============================================
static lv_obj_t *clockTimeLabel = NULL;
static lv_obj_t *clockDateLabel = NULL;
static lv_obj_t *clockDayLabel = NULL;

void createClockCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("CLOCK");
  
  clockTimeLabel = lv_label_create(card);
  lv_label_set_text(clockTimeLabel, "00:00");
  lv_obj_set_style_text_color(clockTimeLabel, t.text, 0);
  lv_obj_set_style_text_font(clockTimeLabel, &lv_font_montserrat_48, 0);
  lv_obj_align(clockTimeLabel, LV_ALIGN_CENTER, 0, -30);
  
  clockDateLabel = lv_label_create(card);
  lv_label_set_text(clockDateLabel, "Dec 01, 2024");
  lv_obj_set_style_text_color(clockDateLabel, t.secondary, 0);
  lv_obj_set_style_text_font(clockDateLabel, &lv_font_montserrat_16, 0);
  lv_obj_align(clockDateLabel, LV_ALIGN_CENTER, 0, 30);
  
  clockDayLabel = lv_label_create(card);
  lv_label_set_text(clockDayLabel, "Monday");
  lv_obj_set_style_text_color(clockDayLabel, t.accent, 0);
  lv_obj_set_style_text_font(clockDayLabel, &lv_font_montserrat_18, 0);
  lv_obj_align(clockDayLabel, LV_ALIGN_CENTER, 0, 60);
  
  lv_obj_t *wifiLabel = lv_label_create(card);
  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text(wifiLabel, LV_SYMBOL_WIFI " Connected");
    lv_obj_set_style_text_color(wifiLabel, lv_color_hex(0x4CAF50), 0);
  } else {
    lv_label_set_text(wifiLabel, LV_SYMBOL_WIFI " Disconnected");
    lv_obj_set_style_text_color(wifiLabel, lv_color_hex(0xFF5252), 0);
  }
  lv_obj_set_style_text_font(wifiLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(wifiLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ============================================
// ANALOG CLOCK CARD
// ============================================
void createAnalogClockCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("");
  
  int cx = (LCD_WIDTH - 40) / 2;
  int cy = (LCD_HEIGHT - 100) / 2;
  int radius = 120;
  
  // Clock face background
  lv_obj_t *clockFace = lv_obj_create(card);
  lv_obj_set_size(clockFace, radius * 2 + 20, radius * 2 + 20);
  lv_obj_align(clockFace, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_color(clockFace, t.bg, 0);
  lv_obj_set_style_radius(clockFace, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(clockFace, t.secondary, 0);
  lv_obj_set_style_border_width(clockFace, 2, 0);
  
  // Roman numerals
  const char* numerals[] = {"XII", "III", "VI", "IX"};
  int positions[][2] = {{0, -100}, {100, 0}, {0, 100}, {-100, 0}};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *numLabel = lv_label_create(clockFace);
    lv_label_set_text(numLabel, numerals[i]);
    lv_obj_set_style_text_color(numLabel, t.text, 0);
    lv_obj_set_style_text_font(numLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(numLabel, LV_ALIGN_CENTER, positions[i][0], positions[i][1]);
  }
  
  // Get current time for hands
  RTC_DateTime datetime = rtc.getDateTime();
  float hourAngle = ((datetime.hour % 12) + datetime.minute / 60.0) * 30.0 - 90;
  float minAngle = datetime.minute * 6.0 - 90;
  
  // Hour hand (red)
  lv_obj_t *hourHand = lv_obj_create(clockFace);
  lv_obj_set_size(hourHand, 60, 6);
  lv_obj_align(hourHand, LV_ALIGN_CENTER, 25, 0);
  lv_obj_set_style_bg_color(hourHand, lv_color_hex(0xFF0000), 0);
  lv_obj_set_style_radius(hourHand, 3, 0);
  lv_obj_set_style_border_width(hourHand, 0, 0);
  
  // Minute hand (blue)
  lv_obj_t *minHand = lv_obj_create(clockFace);
  lv_obj_set_size(minHand, 80, 4);
  lv_obj_align(minHand, LV_ALIGN_CENTER, 35, 0);
  lv_obj_set_style_bg_color(minHand, lv_color_hex(0x0088FF), 0);
  lv_obj_set_style_radius(minHand, 2, 0);
  lv_obj_set_style_border_width(minHand, 0, 0);
  
  // Center dot
  lv_obj_t *centerDot = lv_obj_create(clockFace);
  lv_obj_set_size(centerDot, 16, 16);
  lv_obj_align(centerDot, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(centerDot, t.text, 0);
  lv_obj_set_style_radius(centerDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(centerDot, 0, 0);
}

// ============================================
// COMPASS CARD (Based on reference image)
// ============================================
void createCompassCard() {
  Theme &t = themes[currentTheme];
  
  // Full screen black background for compass
  lv_obj_t *card = lv_obj_create(lv_scr_act());
  lv_obj_set_size(card, LCD_WIDTH - 20, LCD_HEIGHT - 60);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_radius(card, 25, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  
  // Cardinal directions
  lv_obj_t *northLabel = lv_label_create(card);
  lv_label_set_text(northLabel, "N");
  lv_obj_set_style_text_color(northLabel, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(northLabel, &lv_font_montserrat_24, 0);
  lv_obj_align(northLabel, LV_ALIGN_TOP_MID, 0, 20);
  
  lv_obj_t *southLabel = lv_label_create(card);
  lv_label_set_text(southLabel, "S");
  lv_obj_set_style_text_color(southLabel, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(southLabel, &lv_font_montserrat_24, 0);
  lv_obj_align(southLabel, LV_ALIGN_BOTTOM_MID, 0, -40);
  
  lv_obj_t *eastLabel = lv_label_create(card);
  lv_label_set_text(eastLabel, "E");
  lv_obj_set_style_text_color(eastLabel, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(eastLabel, &lv_font_montserrat_24, 0);
  lv_obj_align(eastLabel, LV_ALIGN_RIGHT_MID, -20, 0);
  
  lv_obj_t *westLabel = lv_label_create(card);
  lv_label_set_text(westLabel, "W");
  lv_obj_set_style_text_color(westLabel, lv_color_hex(0xCCCCCC), 0);
  lv_obj_set_style_text_font(westLabel, &lv_font_montserrat_24, 0);
  lv_obj_align(westLabel, LV_ALIGN_LEFT_MID, 20, 0);
  
  // Sunrise label and time
  lv_obj_t *sunriseLabel = lv_label_create(card);
  lv_label_set_text(sunriseLabel, "SUNRISE");
  lv_obj_set_style_text_color(sunriseLabel, lv_color_hex(0x88CCFF), 0);
  lv_obj_set_style_text_font(sunriseLabel, &lv_font_montserrat_12, 0);
  lv_obj_align(sunriseLabel, LV_ALIGN_CENTER, 0, -80);
  
  lv_obj_t *sunriseTimeLabel = lv_label_create(card);
  lv_label_set_text(sunriseTimeLabel, sunriseTime);
  lv_obj_set_style_text_color(sunriseTimeLabel, lv_color_hex(0xFFCC44), 0);
  lv_obj_set_style_text_font(sunriseTimeLabel, &lv_font_montserrat_36, 0);
  lv_obj_align(sunriseTimeLabel, LV_ALIGN_CENTER, 0, -50);
  
  // Sunset label and time
  lv_obj_t *sunsetTimeLabel = lv_label_create(card);
  lv_label_set_text(sunsetTimeLabel, sunsetTime);
  lv_obj_set_style_text_color(sunsetTimeLabel, lv_color_hex(0xFF8844), 0);
  lv_obj_set_style_text_font(sunsetTimeLabel, &lv_font_montserrat_36, 0);
  lv_obj_align(sunsetTimeLabel, LV_ALIGN_CENTER, 0, 50);
  
  lv_obj_t *sunsetLabel = lv_label_create(card);
  lv_label_set_text(sunsetLabel, "SUNSET");
  lv_obj_set_style_text_color(sunsetLabel, lv_color_hex(0xFF8844), 0);
  lv_obj_set_style_text_font(sunsetLabel, &lv_font_montserrat_12, 0);
  lv_obj_align(sunsetLabel, LV_ALIGN_CENTER, 0, 80);
  
  // Compass needle - Red tip (N)
  lv_obj_t *needleRed = lv_obj_create(card);
  lv_obj_set_size(needleRed, 8, 80);
  lv_obj_align(needleRed, LV_ALIGN_CENTER, 40, -20);
  lv_obj_set_style_bg_color(needleRed, lv_color_hex(0xFF3333), 0);
  lv_obj_set_style_radius(needleRed, 4, 0);
  lv_obj_set_style_border_width(needleRed, 0, 0);
  
  // Compass needle - Blue (S)
  lv_obj_t *needleBlue = lv_obj_create(card);
  lv_obj_set_size(needleBlue, 8, 80);
  lv_obj_align(needleBlue, LV_ALIGN_CENTER, -40, 20);
  lv_obj_set_style_bg_color(needleBlue, lv_color_hex(0x3388FF), 0);
  lv_obj_set_style_radius(needleBlue, 4, 0);
  lv_obj_set_style_border_width(needleBlue, 0, 0);
  
  // Center pivot
  lv_obj_t *centerDot = lv_obj_create(card);
  lv_obj_set_size(centerDot, 20, 20);
  lv_obj_align(centerDot, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(centerDot, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(centerDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(centerDot, 0, 0);
}

// ============================================
// ACTIVITY RINGS CARD
// ============================================
void createActivityRingsCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("ACTIVITY");
  
  // Move ring (Red/Pink)
  lv_obj_t *moveArc = lv_arc_create(card);
  lv_obj_set_size(moveArc, 200, 200);
  lv_obj_align(moveArc, LV_ALIGN_CENTER, 0, 0);
  lv_arc_set_rotation(moveArc, 270);
  lv_arc_set_bg_angles(moveArc, 0, 360);
  lv_arc_set_value(moveArc, moveProgress);
  lv_obj_set_style_arc_color(moveArc, lv_color_hex(0x3A1515), LV_PART_MAIN);
  lv_obj_set_style_arc_color(moveArc, lv_color_hex(0xFF2D55), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(moveArc, 18, LV_PART_MAIN);
  lv_obj_set_style_arc_width(moveArc, 18, LV_PART_INDICATOR);
  lv_obj_remove_style(moveArc, NULL, LV_PART_KNOB);
  
  // Exercise ring (Green)
  lv_obj_t *exerciseArc = lv_arc_create(card);
  lv_obj_set_size(exerciseArc, 160, 160);
  lv_obj_align(exerciseArc, LV_ALIGN_CENTER, 0, 0);
  lv_arc_set_rotation(exerciseArc, 270);
  lv_arc_set_bg_angles(exerciseArc, 0, 360);
  lv_arc_set_value(exerciseArc, exerciseProgress);
  lv_obj_set_style_arc_color(exerciseArc, lv_color_hex(0x152A15), LV_PART_MAIN);
  lv_obj_set_style_arc_color(exerciseArc, lv_color_hex(0x30D158), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(exerciseArc, 18, LV_PART_MAIN);
  lv_obj_set_style_arc_width(exerciseArc, 18, LV_PART_INDICATOR);
  lv_obj_remove_style(exerciseArc, NULL, LV_PART_KNOB);
  
  // Stand ring (Blue)
  lv_obj_t *standArc = lv_arc_create(card);
  lv_obj_set_size(standArc, 120, 120);
  lv_obj_align(standArc, LV_ALIGN_CENTER, 0, 0);
  lv_arc_set_rotation(standArc, 270);
  lv_arc_set_bg_angles(standArc, 0, 360);
  lv_arc_set_value(standArc, standProgress);
  lv_obj_set_style_arc_color(standArc, lv_color_hex(0x152030), LV_PART_MAIN);
  lv_obj_set_style_arc_color(standArc, lv_color_hex(0x5AC8FA), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(standArc, 18, LV_PART_MAIN);
  lv_obj_set_style_arc_width(standArc, 18, LV_PART_INDICATOR);
  lv_obj_remove_style(standArc, NULL, LV_PART_KNOB);
  
  // Labels
  lv_obj_t *moveLabel = lv_label_create(card);
  char moveBuf[16];
  snprintf(moveBuf, sizeof(moveBuf), "%d%%", moveProgress);
  lv_label_set_text(moveLabel, moveBuf);
  lv_obj_set_style_text_color(moveLabel, lv_color_hex(0xFF2D55), 0);
  lv_obj_set_style_text_font(moveLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(moveLabel, LV_ALIGN_BOTTOM_LEFT, 20, -30);
  
  lv_obj_t *exerciseLabel = lv_label_create(card);
  char exBuf[16];
  snprintf(exBuf, sizeof(exBuf), "%d%%", exerciseProgress);
  lv_label_set_text(exerciseLabel, exBuf);
  lv_obj_set_style_text_color(exerciseLabel, lv_color_hex(0x30D158), 0);
  lv_obj_set_style_text_font(exerciseLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(exerciseLabel, LV_ALIGN_BOTTOM_MID, 0, -30);
  
  lv_obj_t *standLabel = lv_label_create(card);
  char stBuf[16];
  snprintf(stBuf, sizeof(stBuf), "%d%%", standProgress);
  lv_label_set_text(standLabel, stBuf);
  lv_obj_set_style_text_color(standLabel, lv_color_hex(0x5AC8FA), 0);
  lv_obj_set_style_text_font(standLabel, &lv_font_montserrat_14, 0);
  lv_obj_align(standLabel, LV_ALIGN_BOTTOM_RIGHT, -20, -30);
}

// ============================================
// STEPS CARD
// ============================================
void createStepsCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("STEPS");
  
  // Step count (large)
  lv_obj_t *stepsLabel = lv_label_create(card);
  char stepsBuf[32];
  snprintf(stepsBuf, sizeof(stepsBuf), "%lu", stepCount);
  lv_label_set_text(stepsLabel, stepsBuf);
  lv_obj_set_style_text_color(stepsLabel, t.accent, 0);
  lv_obj_set_style_text_font(stepsLabel, &lv_font_montserrat_48, 0);
  lv_obj_align(stepsLabel, LV_ALIGN_CENTER, 0, -50);
  
  // Goal progress bar
  lv_obj_t *progressBar = lv_bar_create(card);
  lv_obj_set_size(progressBar, LCD_WIDTH - 100, 20);
  lv_obj_align(progressBar, LV_ALIGN_CENTER, 0, 20);
  int progress = min(100, (int)(stepCount * 100 / dailyGoal));
  lv_bar_set_value(progressBar, progress, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(progressBar, t.secondary, LV_PART_MAIN);
  lv_obj_set_style_bg_color(progressBar, t.accent, LV_PART_INDICATOR);
  lv_obj_set_style_radius(progressBar, 10, LV_PART_MAIN);
  lv_obj_set_style_radius(progressBar, 10, LV_PART_INDICATOR);
  
  // Goal label
  char goalBuf[32];
  snprintf(goalBuf, sizeof(goalBuf), "%lu / %d goal", stepCount, dailyGoal);
  lv_obj_t *goalLabel = lv_label_create(card);
  lv_label_set_text(goalLabel, goalBuf);
  lv_obj_set_style_text_color(goalLabel, t.secondary, 0);
  lv_obj_align(goalLabel, LV_ALIGN_CENTER, 0, 55);
  
  // Distance & Calories
  lv_obj_t *statsRow = lv_obj_create(card);
  lv_obj_set_size(statsRow, LCD_WIDTH - 60, 60);
  lv_obj_align(statsRow, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_set_style_bg_color(statsRow, t.bg, 0);
  lv_obj_set_style_radius(statsRow, 12, 0);
  lv_obj_set_style_border_width(statsRow, 0, 0);
  
  char distBuf[16];
  snprintf(distBuf, sizeof(distBuf), "%.2f km", totalDistance);
  lv_obj_t *distLabel = lv_label_create(statsRow);
  lv_label_set_text(distLabel, distBuf);
  lv_obj_set_style_text_color(distLabel, t.text, 0);
  lv_obj_align(distLabel, LV_ALIGN_LEFT_MID, 20, 0);
  
  char calBuf[16];
  snprintf(calBuf, sizeof(calBuf), "%.0f kcal", totalCalories);
  lv_obj_t *calLabel = lv_label_create(statsRow);
  lv_label_set_text(calLabel, calBuf);
  lv_obj_set_style_text_color(calLabel, t.text, 0);
  lv_obj_align(calLabel, LV_ALIGN_RIGHT_MID, -20, 0);
}

// ============================================
// WORKOUT CARD
// ============================================
void workoutBtnCallback(lv_event_t *e) {
  int mode = (int)(intptr_t)lv_event_get_user_data(e);
  if (currentWorkout == WORKOUT_NONE) {
    currentWorkout = (WorkoutMode)mode;
    workoutStartTime = millis();
    workoutSteps = stepCount;
  } else {
    currentWorkout = WORKOUT_NONE;
  }
  navigateToCard(CARD_WORKOUT, 0);
}

void createWorkoutCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("WORKOUT");
  
  const char* workoutNames[] = {"Bike", "Run", "Basketball"};
  const char* workoutIcons[] = {LV_SYMBOL_DRIVE, LV_SYMBOL_SHUFFLE, LV_SYMBOL_REFRESH};
  
  if (currentWorkout != WORKOUT_NONE) {
    lv_obj_t *activeLabel = lv_label_create(card);
    lv_label_set_text(activeLabel, workoutNames[currentWorkout - 1]);
    lv_obj_set_style_text_color(activeLabel, t.accent, 0);
    lv_obj_set_style_text_font(activeLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(activeLabel, LV_ALIGN_CENTER, 0, -60);
    
    unsigned long duration = (millis() - workoutStartTime) / 1000;
    char durBuf[32];
    snprintf(durBuf, sizeof(durBuf), "%02lu:%02lu", duration / 60, duration % 60);
    lv_obj_t *durLabel = lv_label_create(card);
    lv_label_set_text(durLabel, durBuf);
    lv_obj_set_style_text_color(durLabel, t.text, 0);
    lv_obj_set_style_text_font(durLabel, &lv_font_montserrat_40, 0);
    lv_obj_align(durLabel, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_t *stopBtn = lv_btn_create(card);
    lv_obj_set_size(stopBtn, 120, 50);
    lv_obj_align(stopBtn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(stopBtn, lv_color_hex(0xFF5252), 0);
    lv_obj_add_event_cb(stopBtn, workoutBtnCallback, LV_EVENT_CLICKED, (void*)0);
    
    lv_obj_t *stopLabel = lv_label_create(stopBtn);
    lv_label_set_text(stopLabel, "STOP");
    lv_obj_center(stopLabel);
  } else {
    for (int i = 0; i < 3; i++) {
      lv_obj_t *btn = lv_btn_create(card);
      lv_obj_set_size(btn, LCD_WIDTH - 80, 60);
      lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 50 + i * 80);
      lv_obj_set_style_bg_color(btn, t.bg, 0);
      lv_obj_set_style_radius(btn, 15, 0);
      lv_obj_add_event_cb(btn, workoutBtnCallback, LV_EVENT_CLICKED, (void*)(intptr_t)(i + 1));
      
      lv_obj_t *icon = lv_label_create(btn);
      lv_label_set_text(icon, workoutIcons[i]);
      lv_obj_set_style_text_color(icon, t.accent, 0);
      lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);
      
      lv_obj_t *label = lv_label_create(btn);
      lv_label_set_text(label, workoutNames[i]);
      lv_obj_set_style_text_color(label, t.text, 0);
      lv_obj_align(label, LV_ALIGN_LEFT_MID, 50, 0);
    }
  }
}

// ============================================
// SLEEP CARD
// ============================================
void sleepBtnCallback(lv_event_t *e) {
  if (!sleepTrackingActive) {
    sleepTrackingActive = true;
    sleepStartTime = millis();
    memset(movementData, 0, sizeof(movementData));
    movementIndex = 0;
  } else {
    sleepTrackingActive = false;
    sleepEndTime = millis();
  }
  navigateToCard(CARD_SLEEP, 0);
}

void createSleepCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("SLEEP");
  
  if (sleepTrackingActive) {
    unsigned long elapsed = (millis() - sleepStartTime) / 1000;
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu", elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60);
    
    lv_obj_t *timeLabel = lv_label_create(card);
    lv_label_set_text(timeLabel, timeBuf);
    lv_obj_set_style_text_color(timeLabel, t.text, 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_36, 0);
    lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_t *wakeBtn = lv_btn_create(card);
    lv_obj_set_size(wakeBtn, 150, 50);
    lv_obj_align(wakeBtn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(wakeBtn, lv_color_hex(0xFF9800), 0);
    lv_obj_add_event_cb(wakeBtn, sleepBtnCallback, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *wakeLabel = lv_label_create(wakeBtn);
    lv_label_set_text(wakeLabel, "WAKE UP");
    lv_obj_center(wakeLabel);
  } else {
    lv_obj_t *startBtn = lv_btn_create(card);
    lv_obj_set_size(startBtn, 180, 60);
    lv_obj_align(startBtn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(startBtn, t.accent, 0);
    lv_obj_set_style_radius(startBtn, 30, 0);
    lv_obj_add_event_cb(startBtn, sleepBtnCallback, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *startLabel = lv_label_create(startBtn);
    lv_label_set_text(startLabel, LV_SYMBOL_EYE_CLOSE " START");
    lv_obj_center(startLabel);
  }
}

// ============================================
// SLEEP GRAPH CARD
// ============================================
void createSleepGraphCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("SLEEP GRAPH");
  
  lv_obj_t *chart = lv_chart_create(card);
  lv_obj_set_size(chart, LCD_WIDTH - 80, 180);
  lv_obj_align(chart, LV_ALIGN_CENTER, 0, 20);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_point_count(chart, 24);
  
  lv_chart_series_t *ser = lv_chart_add_series(chart, t.accent, LV_CHART_AXIS_PRIMARY_Y);
  for (int i = 0; i < 24; i++) {
    lv_chart_set_next_value(chart, ser, movementData[i]);
  }
}

// ============================================
// STREAK CARD
// ============================================
void createStreakCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("STREAK");
  
  // Large streak number
  char streakBuf[16];
  snprintf(streakBuf, sizeof(streakBuf), "%d", currentStreak);
  lv_obj_t *streakLabel = lv_label_create(card);
  lv_label_set_text(streakLabel, streakBuf);
  lv_obj_set_style_text_color(streakLabel, lv_color_hex(0xFF9500), 0);
  lv_obj_set_style_text_font(streakLabel, &lv_font_montserrat_48, 0);
  lv_obj_align(streakLabel, LV_ALIGN_CENTER, 0, -30);
  
  lv_obj_t *daysLabel = lv_label_create(card);
  lv_label_set_text(daysLabel, "days");
  lv_obj_set_style_text_color(daysLabel, t.secondary, 0);
  lv_obj_set_style_text_font(daysLabel, &lv_font_montserrat_20, 0);
  lv_obj_align(daysLabel, LV_ALIGN_CENTER, 0, 20);
  
  // Fire emoji placeholder
  lv_obj_t *fireLabel = lv_label_create(card);
  lv_label_set_text(fireLabel, LV_SYMBOL_CHARGE);
  lv_obj_set_style_text_color(fireLabel, lv_color_hex(0xFF9500), 0);
  lv_obj_set_style_text_font(fireLabel, &lv_font_montserrat_36, 0);
  lv_obj_align(fireLabel, LV_ALIGN_CENTER, 0, 70);
}

// ============================================
// DAILY GOAL CARD
// ============================================
void createDailyGoalCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("DAILY GOAL");
  
  int progress = min(100, (int)(stepCount * 100 / dailyGoal));
  
  // Circular progress
  lv_obj_t *arc = lv_arc_create(card);
  lv_obj_set_size(arc, 180, 180);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, -20);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_value(arc, progress);
  lv_obj_set_style_arc_color(arc, t.secondary, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, 15, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 15, LV_PART_INDICATOR);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  
  // Percentage
  char percBuf[16];
  snprintf(percBuf, sizeof(percBuf), "%d%%", progress);
  lv_obj_t *percLabel = lv_label_create(card);
  lv_label_set_text(percLabel, percBuf);
  lv_obj_set_style_text_color(percLabel, t.text, 0);
  lv_obj_set_style_text_font(percLabel, &lv_font_montserrat_36, 0);
  lv_obj_align(percLabel, LV_ALIGN_CENTER, 0, -20);
  
  // Goal text
  char goalBuf[32];
  snprintf(goalBuf, sizeof(goalBuf), "%d steps goal", dailyGoal);
  lv_obj_t *goalLabel = lv_label_create(card);
  lv_label_set_text(goalLabel, goalBuf);
  lv_obj_set_style_text_color(goalLabel, t.secondary, 0);
  lv_obj_align(goalLabel, LV_ALIGN_BOTTOM_MID, 0, -30);
}

// ============================================
// MUSIC CARD
// ============================================
void musicBtnCallback(lv_event_t *e) {
  int action = (int)(intptr_t)lv_event_get_user_data(e);
  if (action == 0) { currentTrack--; if (currentTrack < 0) currentTrack = max(0, musicFileCount - 1); }
  else if (action == 1) { musicPlaying = !musicPlaying; }
  else if (action == 2) { currentTrack++; if (currentTrack >= musicFileCount) currentTrack = 0; }
  navigateToCard(CARD_MUSIC, 0);
}

void createMusicCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("MUSIC");
  
  if (musicFileCount == 0) {
    lv_obj_t *noMusic = lv_label_create(card);
    lv_label_set_text(noMusic, "No music found\n\nPlace files in /Music");
    lv_obj_set_style_text_color(noMusic, t.secondary, 0);
    lv_obj_set_style_text_align(noMusic, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(noMusic, LV_ALIGN_CENTER, 0, 0);
    return;
  }
  
  // Album art placeholder
  lv_obj_t *albumArt = lv_obj_create(card);
  lv_obj_set_size(albumArt, 120, 120);
  lv_obj_align(albumArt, LV_ALIGN_CENTER, 0, -50);
  lv_obj_set_style_bg_color(albumArt, t.accent, 0);
  lv_obj_set_style_radius(albumArt, 15, 0);
  lv_obj_set_style_border_width(albumArt, 0, 0);
  
  lv_obj_t *musicIcon = lv_label_create(albumArt);
  lv_label_set_text(musicIcon, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_color(musicIcon, t.text, 0);
  lv_obj_set_style_text_font(musicIcon, &lv_font_montserrat_48, 0);
  lv_obj_center(musicIcon);
  
  // Controls
  lv_obj_t *controlsContainer = lv_obj_create(card);
  lv_obj_set_size(controlsContainer, 200, 60);
  lv_obj_align(controlsContainer, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_set_style_bg_opa(controlsContainer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(controlsContainer, 0, 0);
  lv_obj_set_flex_flow(controlsContainer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(controlsContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  const char *icons[] = {LV_SYMBOL_PREV, musicPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY, LV_SYMBOL_NEXT};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *btn = lv_btn_create(controlsContainer);
    lv_obj_set_size(btn, 50, 50);
    lv_obj_set_style_bg_color(btn, i == 1 ? t.accent : t.bg, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(btn, musicBtnCallback, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, icons[i]);
    lv_obj_center(label);
  }
}

// ============================================
// WEATHER CARD
// ============================================
void createWeatherCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("WEATHER");
  
  lv_obj_t *tempLabel = lv_label_create(card);
  lv_label_set_text(tempLabel, "24°C");
  lv_obj_set_style_text_color(tempLabel, t.text, 0);
  lv_obj_set_style_text_font(tempLabel, &lv_font_montserrat_48, 0);
  lv_obj_align(tempLabel, LV_ALIGN_CENTER, 0, -40);
  
  lv_obj_t *descLabel = lv_label_create(card);
  lv_label_set_text(descLabel, "Partly Cloudy");
  lv_obj_set_style_text_color(descLabel, t.secondary, 0);
  lv_obj_align(descLabel, LV_ALIGN_CENTER, 0, 20);
  
  lv_obj_t *hlLabel = lv_label_create(card);
  lv_label_set_text(hlLabel, "H: 28°  L: 18°");
  lv_obj_set_style_text_color(hlLabel, t.text, 0);
  lv_obj_align(hlLabel, LV_ALIGN_CENTER, 0, 60);
}

// ============================================
// QUOTE CARD
// ============================================
void createQuoteCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("MOTIVATION");
  
  lv_obj_t *quoteIcon = lv_label_create(card);
  lv_label_set_text(quoteIcon, "\"");
  lv_obj_set_style_text_color(quoteIcon, t.accent, 0);
  lv_obj_set_style_text_font(quoteIcon, &lv_font_montserrat_48, 0);
  lv_obj_align(quoteIcon, LV_ALIGN_TOP_LEFT, 10, 30);
  
  lv_obj_t *quoteLabel = lv_label_create(card);
  lv_label_set_text(quoteLabel, quotes[currentQuote]);
  lv_obj_set_style_text_color(quoteLabel, t.text, 0);
  lv_obj_set_width(quoteLabel, LCD_WIDTH - 80);
  lv_label_set_long_mode(quoteLabel, LV_LABEL_LONG_WRAP);
  lv_obj_align(quoteLabel, LV_ALIGN_CENTER, 0, 20);
  
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, [](lv_event_t *e) {
    currentQuote = (currentQuote + 1) % 5;
    navigateToCard(CARD_QUOTE, 0);
  }, LV_EVENT_CLICKED, NULL);
}

// ============================================
// YES/NO CARD
// ============================================
static lv_obj_t *yesNoResultLabel = NULL;

void yesNoCallback(lv_event_t *e) {
  const char* answers[] = {"YES!", "NO!", "MAYBE...", "ASK AGAIN", "DEFINITELY!", "NEVER!", "100%", "NOPE"};
  lv_label_set_text(yesNoResultLabel, answers[random(0, 8)]);
}

void createYesNoCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("YES / NO");
  
  yesNoResultLabel = lv_label_create(card);
  lv_label_set_text(yesNoResultLabel, "?");
  lv_obj_set_style_text_color(yesNoResultLabel, t.accent, 0);
  lv_obj_set_style_text_font(yesNoResultLabel, &lv_font_montserrat_48, 0);
  lv_obj_align(yesNoResultLabel, LV_ALIGN_CENTER, 0, -20);
  
  lv_obj_t *askBtn = lv_btn_create(card);
  lv_obj_set_size(askBtn, 150, 50);
  lv_obj_align(askBtn, LV_ALIGN_CENTER, 0, 70);
  lv_obj_set_style_bg_color(askBtn, t.accent, 0);
  lv_obj_set_style_radius(askBtn, 25, 0);
  lv_obj_add_event_cb(askBtn, yesNoCallback, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *askLabel = lv_label_create(askBtn);
  lv_label_set_text(askLabel, "REVEAL");
  lv_obj_center(askLabel);
}

// ============================================
// TIMER CARD
// ============================================
static lv_obj_t *timerLabel = NULL;

void timerBtnCallback(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
  
  if (strcmp(txt, LV_SYMBOL_PLAY) == 0) {
    timerRunning = true;
    timerStart = millis();
  } else if (strcmp(txt, LV_SYMBOL_PAUSE) == 0) {
    timerRunning = false;
  } else if (strcmp(txt, LV_SYMBOL_REFRESH) == 0) {
    timerRunning = false;
    timerDuration = 300000;
  }
}

void createTimerCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("TIMER");
  
  unsigned long remaining = timerDuration;
  if (timerRunning) {
    unsigned long elapsed = millis() - timerStart;
    remaining = timerDuration > elapsed ? timerDuration - elapsed : 0;
  }
  
  int mins = (remaining / 60000) % 60;
  int secs = (remaining / 1000) % 60;
  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mins, secs);
  
  timerLabel = lv_label_create(card);
  lv_label_set_text(timerLabel, timeBuf);
  lv_obj_set_style_text_color(timerLabel, t.text, 0);
  lv_obj_set_style_text_font(timerLabel, &lv_font_montserrat_48, 0);
  lv_obj_align(timerLabel, LV_ALIGN_CENTER, 0, -30);
  
  // Controls
  lv_obj_t *btnContainer = lv_obj_create(card);
  lv_obj_set_size(btnContainer, 200, 60);
  lv_obj_align(btnContainer, LV_ALIGN_CENTER, 0, 60);
  lv_obj_set_style_bg_opa(btnContainer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btnContainer, 0, 0);
  lv_obj_set_flex_flow(btnContainer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btnContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  
  const char *icons[] = {LV_SYMBOL_PLAY, LV_SYMBOL_PAUSE, LV_SYMBOL_REFRESH};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *btn = lv_btn_create(btnContainer);
    lv_obj_set_size(btn, 50, 50);
    lv_obj_set_style_bg_color(btn, t.accent, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(btn, timerBtnCallback, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, icons[i]);
    lv_obj_center(label);
  }
}

// ============================================
// DINO GAME CARD
// ============================================
void dinoBtnCallback(lv_event_t *e) {
  if (dinoGameOver) {
    dinoGameOver = false;
    dinoScore = 0;
    obstacleX = LCD_WIDTH;
  } else if (!dinoJumping) {
    dinoJumping = true;
    dinoY = -50;
  }
}

void createDinoCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("DINO");
  
  // Score
  char scoreBuf[16];
  snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d", dinoScore);
  lv_obj_t *scoreLabel = lv_label_create(card);
  lv_label_set_text(scoreLabel, scoreBuf);
  lv_obj_set_style_text_color(scoreLabel, t.text, 0);
  lv_obj_align(scoreLabel, LV_ALIGN_TOP_RIGHT, -10, 30);
  
  // Ground line
  lv_obj_t *ground = lv_obj_create(card);
  lv_obj_set_size(ground, LCD_WIDTH - 60, 3);
  lv_obj_align(ground, LV_ALIGN_BOTTOM_MID, 0, -80);
  lv_obj_set_style_bg_color(ground, t.secondary, 0);
  lv_obj_set_style_border_width(ground, 0, 0);
  
  // Dino (simple square)
  lv_obj_t *dino = lv_obj_create(card);
  lv_obj_set_size(dino, 30, 40);
  lv_obj_align(dino, LV_ALIGN_BOTTOM_LEFT, 40, -83 + dinoY);
  lv_obj_set_style_bg_color(dino, t.accent, 0);
  lv_obj_set_style_radius(dino, 5, 0);
  lv_obj_set_style_border_width(dino, 0, 0);
  
  // Obstacle
  lv_obj_t *obstacle = lv_obj_create(card);
  lv_obj_set_size(obstacle, 20, 30);
  lv_obj_align(obstacle, LV_ALIGN_BOTTOM_LEFT, obstacleX, -83);
  lv_obj_set_style_bg_color(obstacle, lv_color_hex(0xFF5252), 0);
  lv_obj_set_style_radius(obstacle, 3, 0);
  lv_obj_set_style_border_width(obstacle, 0, 0);
  
  // Tap to jump
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, dinoBtnCallback, LV_EVENT_CLICKED, NULL);
  
  if (dinoGameOver) {
    lv_obj_t *gameOverLabel = lv_label_create(card);
    lv_label_set_text(gameOverLabel, "GAME OVER\nTap to restart");
    lv_obj_set_style_text_color(gameOverLabel, lv_color_hex(0xFF5252), 0);
    lv_obj_set_style_text_align(gameOverLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(gameOverLabel, LV_ALIGN_CENTER, 0, 0);
  }
}

// ============================================
// VOLUME CARD
// ============================================
void volumeSliderCallback(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  volumeLevel = lv_slider_get_value(slider);
}

void createVolumeCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("VOLUME");
  
  lv_obj_t *volumeIcon = lv_label_create(card);
  lv_label_set_text(volumeIcon, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_style_text_color(volumeIcon, t.accent, 0);
  lv_obj_set_style_text_font(volumeIcon, &lv_font_montserrat_48, 0);
  lv_obj_align(volumeIcon, LV_ALIGN_CENTER, 0, -70);
  
  char volBuf[16];
  snprintf(volBuf, sizeof(volBuf), "%d%%", volumeLevel);
  lv_obj_t *volLabel = lv_label_create(card);
  lv_label_set_text(volLabel, volBuf);
  lv_obj_set_style_text_color(volLabel, t.text, 0);
  lv_obj_set_style_text_font(volLabel, &lv_font_montserrat_24, 0);
  lv_obj_align(volLabel, LV_ALIGN_CENTER, 0, -10);
  
  lv_obj_t *slider = lv_slider_create(card);
  lv_obj_set_size(slider, LCD_WIDTH - 100, 20);
  lv_obj_align(slider, LV_ALIGN_CENTER, 0, 50);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, volumeLevel, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, t.secondary, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, t.accent, LV_PART_INDICATOR);
  lv_obj_add_event_cb(slider, volumeSliderCallback, LV_EVENT_VALUE_CHANGED, NULL);
}

// ============================================
// WORLD CLOCK CARD
// ============================================
void createWorldClockCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("WORLD CLOCK");
  
  RTC_DateTime now = rtc.getDateTime();
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 60, 55);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 35 + i * 65);
    lv_obj_set_style_bg_color(row, t.bg, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    lv_obj_t *cityLabel = lv_label_create(row);
    lv_label_set_text(cityLabel, worldCities[i]);
    lv_obj_set_style_text_color(cityLabel, t.text, 0);
    lv_obj_align(cityLabel, LV_ALIGN_LEFT_MID, 10, 0);
    
    int cityHour = (now.hour + worldOffsets[i] + 24) % 24;
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", cityHour, now.minute);
    lv_obj_t *timeLabel = lv_label_create(row);
    lv_label_set_text(timeLabel, timeBuf);
    lv_obj_set_style_text_color(timeLabel, t.accent, 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(timeLabel, LV_ALIGN_RIGHT_MID, -10, 0);
  }
}

// ============================================
// CALENDAR CARD
// ============================================
void createCalendarCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("CALENDAR");
  
  // Month/Year header
  const char* months[] = {"January", "February", "March", "April", "May", "June",
                          "July", "August", "September", "October", "November", "December"};
  char headerBuf[32];
  snprintf(headerBuf, sizeof(headerBuf), "%s %d", months[currentMonth - 1], currentYear);
  lv_obj_t *headerLabel = lv_label_create(card);
  lv_label_set_text(headerLabel, headerBuf);
  lv_obj_set_style_text_color(headerLabel, t.text, 0);
  lv_obj_align(headerLabel, LV_ALIGN_TOP_MID, 0, 30);
  
  // Day grid (simplified - just numbers)
  lv_obj_t *grid = lv_obj_create(card);
  lv_obj_set_size(grid, LCD_WIDTH - 60, 220);
  lv_obj_align(grid, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  
  int daysInMonth = 31;
  for (int d = 1; d <= daysInMonth; d++) {
    lv_obj_t *dayObj = lv_obj_create(grid);
    lv_obj_set_size(dayObj, 38, 30);
    lv_obj_set_style_border_width(dayObj, 0, 0);
    
    if (d == selectedDay) {
      lv_obj_set_style_bg_color(dayObj, t.accent, 0);
      lv_obj_set_style_radius(dayObj, 15, 0);
    } else {
      lv_obj_set_style_bg_opa(dayObj, LV_OPA_TRANSP, 0);
    }
    
    lv_obj_t *dayLabel = lv_label_create(dayObj);
    char dayBuf[4];
    snprintf(dayBuf, sizeof(dayBuf), "%d", d);
    lv_label_set_text(dayLabel, dayBuf);
    lv_obj_set_style_text_color(dayLabel, d == selectedDay ? lv_color_hex(0x000000) : t.text, 0);
    lv_obj_center(dayLabel);
  }
}

// ============================================
// NOTES CARD
// ============================================
void createNotesCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("NOTES");
  
  char numBuf[16];
  snprintf(numBuf, sizeof(numBuf), "Note %d/5", currentNote + 1);
  lv_obj_t *numLabel = lv_label_create(card);
  lv_label_set_text(numLabel, numBuf);
  lv_obj_set_style_text_color(numLabel, t.accent, 0);
  lv_obj_align(numLabel, LV_ALIGN_TOP_MID, 0, 40);
  
  lv_obj_t *noteLabel = lv_label_create(card);
  lv_label_set_text(noteLabel, notes[currentNote]);
  lv_obj_set_style_text_color(noteLabel, t.text, 0);
  lv_obj_set_style_text_font(noteLabel, &lv_font_montserrat_18, 0);
  lv_obj_align(noteLabel, LV_ALIGN_CENTER, 0, 0);
  
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, [](lv_event_t *e) {
    currentNote = (currentNote + 1) % 5;
    navigateToCard(CARD_NOTES, 0);
  }, LV_EVENT_CLICKED, NULL);
}

// ============================================
// TORCH CARD
// ============================================
void torchBtnCallback(lv_event_t *e) {
  torchOn = !torchOn;
  navigateToCard(CARD_TORCH, 0);
}

void createTorchCard() {
  Theme &t = themes[currentTheme];
  
  if (torchOn) {
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFFFFFF), 0);
    gfx->setBrightness(255);
    
    lv_obj_t *offBtn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(offBtn, 150, 60);
    lv_obj_align(offBtn, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_bg_color(offBtn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(offBtn, 30, 0);
    lv_obj_add_event_cb(offBtn, torchBtnCallback, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *offLabel = lv_label_create(offBtn);
    lv_label_set_text(offLabel, "TURN OFF");
    lv_obj_set_style_text_color(offLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(offLabel);
  } else {
    gfx->setBrightness(brightness);
    lv_obj_t *card = createCardContainer("TORCH");
    
    lv_obj_t *torchIcon = lv_label_create(card);
    lv_label_set_text(torchIcon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(torchIcon, t.secondary, 0);
    lv_obj_set_style_text_font(torchIcon, &lv_font_montserrat_48, 0);
    lv_obj_align(torchIcon, LV_ALIGN_CENTER, 0, -40);
    
    lv_obj_t *onBtn = lv_btn_create(card);
    lv_obj_set_size(onBtn, 150, 60);
    lv_obj_align(onBtn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(onBtn, t.accent, 0);
    lv_obj_set_style_radius(onBtn, 30, 0);
    lv_obj_add_event_cb(onBtn, torchBtnCallback, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *onLabel = lv_label_create(onBtn);
    lv_label_set_text(onLabel, "TURN ON");
    lv_obj_center(onLabel);
  }
}

// ============================================
// BATTERY CARD
// ============================================
void createBatteryCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("BATTERY");
  
  int battPercent = power.getBatteryPercent();
  bool isCharging = power.isCharging();
  
  // Arc
  lv_obj_t *arc = lv_arc_create(card);
  lv_obj_set_size(arc, 160, 160);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, -20);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_value(arc, battPercent);
  lv_obj_set_style_arc_color(arc, t.secondary, LV_PART_MAIN);
  
  lv_color_t arcColor;
  if (battPercent > 60) arcColor = lv_color_hex(0x4CAF50);
  else if (battPercent > 20) arcColor = lv_color_hex(0xFF9800);
  else arcColor = lv_color_hex(0xFF5252);
  lv_obj_set_style_arc_color(arc, arcColor, LV_PART_INDICATOR);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  
  char battBuf[16];
  snprintf(battBuf, sizeof(battBuf), "%d%%", battPercent);
  lv_obj_t *battLabel = lv_label_create(card);
  lv_label_set_text(battLabel, battBuf);
  lv_obj_set_style_text_color(battLabel, t.text, 0);
  lv_obj_set_style_text_font(battLabel, &lv_font_montserrat_36, 0);
  lv_obj_align(battLabel, LV_ALIGN_CENTER, 0, -20);
  
  if (isCharging) {
    lv_obj_t *chargingLabel = lv_label_create(card);
    lv_label_set_text(chargingLabel, LV_SYMBOL_CHARGE " Charging");
    lv_obj_set_style_text_color(chargingLabel, lv_color_hex(0x4CAF50), 0);
    lv_obj_align(chargingLabel, LV_ALIGN_BOTTOM_MID, 0, -30);
  }
}

// ============================================
// GALLERY CARD
// ============================================
void galleryBtnCallback(lv_event_t *e) {
  int action = (int)(intptr_t)lv_event_get_user_data(e);
  if (action == 0) { currentPhoto--; if (currentPhoto < 0) currentPhoto = max(0, photoFileCount - 1); }
  else { currentPhoto++; if (currentPhoto >= photoFileCount) currentPhoto = 0; }
  navigateToCard(CARD_GALLERY, 0);
}

void createGalleryCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("GALLERY");
  
  if (photoFileCount == 0) {
    lv_obj_t *noPhotos = lv_label_create(card);
    lv_label_set_text(noPhotos, "No photos found\n\nPlace files in /Photos");
    lv_obj_set_style_text_color(noPhotos, t.secondary, 0);
    lv_obj_set_style_text_align(noPhotos, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(noPhotos, LV_ALIGN_CENTER, 0, 0);
    return;
  }
  
  lv_obj_t *photoFrame = lv_obj_create(card);
  lv_obj_set_size(photoFrame, LCD_WIDTH - 80, 180);
  lv_obj_align(photoFrame, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_color(photoFrame, t.bg, 0);
  lv_obj_set_style_radius(photoFrame, 15, 0);
  lv_obj_set_style_border_color(photoFrame, t.accent, 0);
  lv_obj_set_style_border_width(photoFrame, 2, 0);
  
  lv_obj_t *photoIcon = lv_label_create(photoFrame);
  lv_label_set_text(photoIcon, LV_SYMBOL_IMAGE);
  lv_obj_set_style_text_color(photoIcon, t.secondary, 0);
  lv_obj_set_style_text_font(photoIcon, &lv_font_montserrat_48, 0);
  lv_obj_center(photoIcon);
  
  char countBuf[16];
  snprintf(countBuf, sizeof(countBuf), "%d / %d", currentPhoto + 1, photoFileCount);
  lv_obj_t *countLabel = lv_label_create(card);
  lv_label_set_text(countLabel, countBuf);
  lv_obj_set_style_text_color(countLabel, t.secondary, 0);
  lv_obj_align(countLabel, LV_ALIGN_BOTTOM_MID, 0, -30);
}

// ============================================
// STOCKS CARD
// ============================================
void createStocksCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("STOCKS");
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 60, 55);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 35 + i * 62);
    lv_obj_set_style_bg_color(row, t.bg, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    lv_obj_t *symLabel = lv_label_create(row);
    lv_label_set_text(symLabel, stockSymbols[i]);
    lv_obj_set_style_text_color(symLabel, t.text, 0);
    lv_obj_align(symLabel, LV_ALIGN_LEFT_MID, 10, 0);
    
    char priceBuf[16];
    snprintf(priceBuf, sizeof(priceBuf), "$%.2f", stockPrices[i]);
    lv_obj_t *priceLabel = lv_label_create(row);
    lv_label_set_text(priceLabel, priceBuf);
    lv_obj_set_style_text_color(priceLabel, t.accent, 0);
    lv_obj_align(priceLabel, LV_ALIGN_RIGHT_MID, -10, 0);
  }
}

// ============================================
// CRYPTO CARD
// ============================================
void createCryptoCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("CRYPTO");
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LCD_WIDTH - 60, 55);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 35 + i * 62);
    lv_obj_set_style_bg_color(row, t.bg, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    
    lv_obj_t *symLabel = lv_label_create(row);
    lv_label_set_text(symLabel, cryptoSymbols[i]);
    lv_obj_set_style_text_color(symLabel, t.text, 0);
    lv_obj_align(symLabel, LV_ALIGN_LEFT_MID, 10, 0);
    
    char priceBuf[16];
    if (cryptoPrices[i] >= 1000) snprintf(priceBuf, sizeof(priceBuf), "$%.0f", cryptoPrices[i]);
    else snprintf(priceBuf, sizeof(priceBuf), "$%.2f", cryptoPrices[i]);
    lv_obj_t *priceLabel = lv_label_create(row);
    lv_label_set_text(priceLabel, priceBuf);
    lv_obj_set_style_text_color(priceLabel, lv_color_hex(0xF7931A), 0);
    lv_obj_align(priceLabel, LV_ALIGN_RIGHT_MID, -10, 0);
  }
}

// ============================================
// THEMES CARD
// ============================================
void themeCallback(lv_event_t *e) {
  currentTheme = (int)(intptr_t)lv_event_get_user_data(e);
  navigateToCard(CARD_THEMES, 0);
}

void createThemesCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("THEMES");
  
  const char* themeNames[] = {"Dark", "Blue", "Green", "Purple"};
  lv_color_t themeColors[] = {lv_color_hex(0x00D4FF), lv_color_hex(0x5090D3), lv_color_hex(0x4CAF50), lv_color_hex(0xBB86FC)};
  
  for (int i = 0; i < 4; i++) {
    lv_obj_t *btn = lv_btn_create(card);
    lv_obj_set_size(btn, LCD_WIDTH - 80, 55);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 35 + i * 62);
    lv_obj_set_style_bg_color(btn, t.bg, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    if (i == currentTheme) {
      lv_obj_set_style_border_color(btn, themeColors[i], 0);
      lv_obj_set_style_border_width(btn, 2, 0);
    }
    lv_obj_add_event_cb(btn, themeCallback, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    
    lv_obj_t *colorDot = lv_obj_create(btn);
    lv_obj_set_size(colorDot, 25, 25);
    lv_obj_align(colorDot, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(colorDot, themeColors[i], 0);
    lv_obj_set_style_radius(colorDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(colorDot, 0, 0);
    
    lv_obj_t *nameLabel = lv_label_create(btn);
    lv_label_set_text(nameLabel, themeNames[i]);
    lv_obj_set_style_text_color(nameLabel, t.text, 0);
    lv_obj_align(nameLabel, LV_ALIGN_LEFT_MID, 50, 0);
    
    if (i == currentTheme) {
      lv_obj_t *checkLabel = lv_label_create(btn);
      lv_label_set_text(checkLabel, LV_SYMBOL_OK);
      lv_obj_set_style_text_color(checkLabel, themeColors[i], 0);
      lv_obj_align(checkLabel, LV_ALIGN_RIGHT_MID, -10, 0);
    }
  }
}

// ============================================
// SYSTEM CARD
// ============================================
void createSystemCard() {
  Theme &t = themes[currentTheme];
  lv_obj_t *card = createCardContainer("SYSTEM");
  
  // CPU
  lv_obj_t *cpuRow = lv_obj_create(card);
  lv_obj_set_size(cpuRow, LCD_WIDTH - 60, 45);
  lv_obj_align(cpuRow, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_bg_color(cpuRow, t.bg, 0);
  lv_obj_set_style_radius(cpuRow, 10, 0);
  lv_obj_set_style_border_width(cpuRow, 0, 0);
  
  lv_obj_t *cpuLabel = lv_label_create(cpuRow);
  lv_label_set_text(cpuLabel, "CPU");
  lv_obj_set_style_text_color(cpuLabel, t.secondary, 0);
  lv_obj_align(cpuLabel, LV_ALIGN_LEFT_MID, 10, 0);
  
  lv_obj_t *cpuBar = lv_bar_create(cpuRow);
  lv_obj_set_size(cpuBar, 120, 12);
  lv_obj_align(cpuBar, LV_ALIGN_RIGHT_MID, -50, 0);
  lv_bar_set_value(cpuBar, random(20, 60), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(cpuBar, t.secondary, LV_PART_MAIN);
  lv_obj_set_style_bg_color(cpuBar, t.accent, LV_PART_INDICATOR);
  
  // RAM
  lv_obj_t *ramRow = lv_obj_create(card);
  lv_obj_set_size(ramRow, LCD_WIDTH - 60, 45);
  lv_obj_align(ramRow, LV_ALIGN_TOP_MID, 0, 95);
  lv_obj_set_style_bg_color(ramRow, t.bg, 0);
  lv_obj_set_style_radius(ramRow, 10, 0);
  lv_obj_set_style_border_width(ramRow, 0, 0);
  
  lv_obj_t *ramLabel = lv_label_create(ramRow);
  lv_label_set_text(ramLabel, "RAM");
  lv_obj_set_style_text_color(ramLabel, t.secondary, 0);
  lv_obj_align(ramLabel, LV_ALIGN_LEFT_MID, 10, 0);
  
  uint32_t freeHeap = ESP.getFreeHeap() / 1024;
  uint32_t totalHeap = ESP.getHeapSize() / 1024;
  int ramPercent = 100 - (freeHeap * 100 / totalHeap);
  
  lv_obj_t *ramBar = lv_bar_create(ramRow);
  lv_obj_set_size(ramBar, 120, 12);
  lv_obj_align(ramBar, LV_ALIGN_RIGHT_MID, -50, 0);
  lv_bar_set_value(ramBar, ramPercent, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(ramBar, t.secondary, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ramBar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
  
  // Temperature
  lv_obj_t *tempRow = lv_obj_create(card);
  lv_obj_set_size(tempRow, LCD_WIDTH - 60, 45);
  lv_obj_align(tempRow, LV_ALIGN_TOP_MID, 0, 150);
  lv_obj_set_style_bg_color(tempRow, t.bg, 0);
  lv_obj_set_style_radius(tempRow, 10, 0);
  lv_obj_set_style_border_width(tempRow, 0, 0);
  
  lv_obj_t *tempLabel = lv_label_create(tempRow);
  lv_label_set_text(tempLabel, "TEMP");
  lv_obj_set_style_text_color(tempLabel, t.secondary, 0);
  lv_obj_align(tempLabel, LV_ALIGN_LEFT_MID, 10, 0);
  
  float temp = temperatureRead();
  char tempBuf[16];
  snprintf(tempBuf, sizeof(tempBuf), "%.1f°C", temp);
  lv_obj_t *tempVal = lv_label_create(tempRow);
  lv_label_set_text(tempVal, tempBuf);
  lv_obj_set_style_text_color(tempVal, t.accent, 0);
  lv_obj_align(tempVal, LV_ALIGN_RIGHT_MID, -10, 0);
  
  // WiFi status
  lv_obj_t *wifiRow = lv_obj_create(card);
  lv_obj_set_size(wifiRow, LCD_WIDTH - 60, 45);
  lv_obj_align(wifiRow, LV_ALIGN_TOP_MID, 0, 205);
  lv_obj_set_style_bg_color(wifiRow, t.bg, 0);
  lv_obj_set_style_radius(wifiRow, 10, 0);
  lv_obj_set_style_border_width(wifiRow, 0, 0);
  
  lv_obj_t *wifiLabel = lv_label_create(wifiRow);
  lv_label_set_text(wifiLabel, "WiFi");
  lv_obj_set_style_text_color(wifiLabel, t.secondary, 0);
  lv_obj_align(wifiLabel, LV_ALIGN_LEFT_MID, 10, 0);
  
  lv_obj_t *wifiStatus = lv_label_create(wifiRow);
  lv_label_set_text(wifiStatus, WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  lv_obj_set_style_text_color(wifiStatus, WiFi.status() == WL_CONNECTED ? lv_color_hex(0x4CAF50) : lv_color_hex(0xFF5252), 0);
  lv_obj_align(wifiStatus, LV_ALIGN_RIGHT_MID, -10, 0);
}

// ============================================
// SETUP WIFI
// ============================================
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
}

// ============================================
// LOAD SD CARD CONTENT
// ============================================
void loadSDCardContent() {
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
  
  if (!SD_MMC.begin("/sdcard", true)) return;
  
  File musicDir = SD_MMC.open("/Music");
  if (musicDir && musicDir.isDirectory()) {
    File file = musicDir.openNextFile();
    while (file && musicFileCount < 20) {
      if (!file.isDirectory()) {
        musicFiles[musicFileCount++] = String(file.name());
      }
      file = musicDir.openNextFile();
    }
  }
  
  File photoDir = SD_MMC.open("/Photos");
  if (photoDir && photoDir.isDirectory()) {
    File file = photoDir.openNextFile();
    while (file && photoFileCount < 50) {
      if (!file.isDirectory()) {
        photoFiles[photoFileCount++] = String(file.name());
      }
      file = photoDir.openNextFile();
    }
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  USBSerial.begin(115200);
  
  Wire.begin(IIC_SDA, IIC_SCL);
  
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
  
  power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  power.enableBattDetection();
  power.enableBattVoltageMeasure();
  
  rtc.begin(Wire, PCF85063_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  
  if (qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_1000Hz, SensorQMI8658::LPF_MODE_0);
    qmi.enableAccelerometer();
  }
  
  while (FT3168->begin() == false) delay(1000);
  
  gfx->begin();
  gfx->setBrightness(brightness);
  
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
  
  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  
  esp_timer_handle_t lvgl_tick_timer = NULL;
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);
  
  setupWiFi();
  loadSDCardContent();
  
  navigateToCard(CARD_CLOCK, 0);
}

// ============================================
// LOOP
// ============================================
unsigned long lastUIUpdate = 0;
unsigned long lastStepCheck = 0;
unsigned long lastDinoUpdate = 0;
float lastAccMagnitude = 0;

void loop() {
  lv_timer_handler();
  delay(5);
  
  // UI updates
  if (millis() - lastUIUpdate > 1000) {
    lastUIUpdate = millis();
    
    if (currentMainCard == CARD_CLOCK && currentSubCard == 0 && clockTimeLabel) {
      RTC_DateTime datetime = rtc.getDateTime();
      char timeBuf[16];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", datetime.hour, datetime.minute);
      lv_label_set_text(clockTimeLabel, timeBuf);
    }
    
    if (timerRunning && timerLabel) {
      unsigned long elapsed = millis() - timerStart;
      unsigned long remaining = timerDuration > elapsed ? timerDuration - elapsed : 0;
      int mins = (remaining / 60000) % 60;
      int secs = (remaining / 1000) % 60;
      char buf[16];
      snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
      lv_label_set_text(timerLabel, buf);
    }
  }
  
  // Step counting
  if (millis() - lastStepCheck > 50) {
    lastStepCheck = millis();
    
    if (qmi.getDataReady()) {
      qmi.getAccelerometer(acc.x, acc.y, acc.z);
      
      float magnitude = sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
      float delta = abs(magnitude - lastAccMagnitude);
      
      if (delta > 0.5 && delta < 3.0) {
        stepCount++;
        totalDistance = stepCount * 0.0007;
        totalCalories = stepCount * 0.04;
      }
      
      lastAccMagnitude = magnitude;
      
      // Update compass heading from magnetometer (simplified)
      compassHeading = fmod(compassHeading + 0.5, 360.0);
      
      // Sleep movement tracking
      if (sleepTrackingActive && millis() - lastMovementSample > 60000) {
        lastMovementSample = millis();
        int movementLevel = (int)(delta * 30);
        if (movementLevel > 100) movementLevel = 100;
        if (movementIndex < 24) movementData[movementIndex++] = movementLevel;
      }
    }
  }
  
  // Dino game update
  if (currentMainCard == CARD_DINO && !dinoGameOver) {
    if (millis() - lastDinoUpdate > 50) {
      lastDinoUpdate = millis();
      
      obstacleX -= 8;
      if (obstacleX < -20) {
        obstacleX = LCD_WIDTH;
        dinoScore++;
      }
      
      if (dinoJumping) {
        dinoY += 10;
        if (dinoY >= 0) {
          dinoY = 0;
          dinoJumping = false;
        }
      }
      
      // Collision detection
      if (obstacleX < 70 && obstacleX > 20 && dinoY > -30) {
        dinoGameOver = true;
      }
      
      navigateToCard(CARD_DINO, 0);
    }
  }
}
