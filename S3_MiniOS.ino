/*
 * ═══════════════════════════════════════════════════════════════════════════
 * S3 MiniOS - v0.1
 * ESP32-S3 Touch AMOLED 1.8" Starter Firmware
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * A single-file firmware that combines:
 *  • Real-time clock
 *  • Step counter (pedometer)
 *  • Productivity timer (Pomodoro-style)
 * 
 * Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.8
 *  - Display: SH8601 (368×448 AMOLED, QSPI)
 *  - Touch: FT3168 (I2C)
 *  - IMU: QMI8658 6-axis (I2C)
 *  - RTC: PCF85063 (I2C)
 *  - Power: AXP2101 (I2C)
 * 
 * Required Libraries:
 *  - GFX_Library_for_Arduino (offline install)
 *  - Arduino_DriveBus (offline install)
 *  - SensorLib (v0.2.1+)
 *  - XPowersLib (v0.2.6+)
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 */

// ═══════════════════════════════════════════════════════════════════════════
// INCLUDES
// ═══════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "XPowersLib.h"

// SensorLib - specific sensor headers
#include "SensorQMI8658.hpp"
#include "SensorPCF85063.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// PIN DEFINITIONS - Waveshare ESP32-S3-Touch-AMOLED-1.8
// ═══════════════════════════════════════════════════════════════════════════
// Display (QSPI)
#define LCD_CS        10
#define LCD_SCLK      12
#define LCD_SDIO0     11
#define LCD_SDIO1     13
#define LCD_SDIO2     14
#define LCD_SDIO3     21
#define LCD_RST       -1
#define LCD_WIDTH     368
#define LCD_HEIGHT    448

// I2C Bus
#define IIC_SDA       15
#define IIC_SCL       16

// Touch Controller (FT3168)
#define TOUCH_INT     17
#define TOUCH_RST     18

// Buttons
#define BUTTON_BOOT   0
#define BUTTON_PWR    4   // Via EXIO4 on TCA9554

// ═══════════════════════════════════════════════════════════════════════════
// THEME COLORS (Dark Mode - AMOLED Optimized)
// ═══════════════════════════════════════════════════════════════════════════
#define COL_BG        0x0000  // Black background
#define COL_CARD      0x1082  // Dark gray card
#define COL_TEXT      0xFFFF  // White text
#define COL_ACCENT    0x07FF  // Cyan accent
#define COL_STEPS     0x07E0  // Green for steps
#define COL_FOCUS     0xFD20  // Orange for focus
#define COL_BUTTON    0x2945  // Dark blue button
#define COL_BTN_TEXT  0xFFFF  // White button text

// ═══════════════════════════════════════════════════════════════════════════
// GLOBAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════
Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
Arduino_IIC_Touch *FT3168 = nullptr;

SensorQMI8658 qmi;
SensorPCF85063 rtc;
XPowersAXP2101 PMU;

// Touch interrupt flag
volatile bool touchInterruptFlag = false;

// ═══════════════════════════════════════════════════════════════════════════
// SCREEN MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════
enum Screen {
  SCREEN_HOME,
  SCREEN_STEPS,
  SCREEN_FOCUS
};

Screen currentScreen = SCREEN_HOME;
bool uiDirty = true;  // Flag to trigger screen redraw

// ═══════════════════════════════════════════════════════════════════════════
// TIME MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════
unsigned long lastSecond = 0;
unsigned long lastUI = 0;
int hours = 12, minutes = 0, seconds = 0;
bool rtcAvailable = false;

// ═══════════════════════════════════════════════════════════════════════════
// POWER MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════
bool pmuAvailable = false;
int batteryPercent = 0;
bool isCharging = false;
unsigned long lastBatteryCheck = 0;
#define BATTERY_CHECK_INTERVAL 5000  // Check every 5 seconds

// ═══════════════════════════════════════════════════════════════════════════
// STEP COUNTER
// ═══════════════════════════════════════════════════════════════════════════
int stepCount = 0;
int stepGoal = 10000;
float lastAccelMagnitude = 0;
unsigned long lastStepTime = 0;
bool qmiAvailable = false;
#define STEP_THRESHOLD 1.2    // Acceleration threshold for step detection
#define STEP_COOLDOWN 300     // Milliseconds between steps

// ═══════════════════════════════════════════════════════════════════════════
// FOCUS TIMER
// ═══════════════════════════════════════════════════════════════════════════
bool focusRunning = false;
int focusSeconds = 25 * 60;  // 25 minutes default
int focusSessions = 0;
unsigned long lastFocusUpdate = 0;

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════
bool touchAvailable = false;
unsigned long lastTouchTime = 0;
#define TOUCH_DEBOUNCE 200  // Milliseconds

// ═══════════════════════════════════════════════════════════════════════════
// POWER SAVING / DEEP SLEEP
// ═══════════════════════════════════════════════════════════════════════════
unsigned long lastActivity = 0;
#define SLEEP_TIMEOUT 30000  // 30 seconds of inactivity before sleep
bool sleepEnabled = true;  // Can be toggled

// ═══════════════════════════════════════════════════════════════════════════
// UI HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Draw a header bar at the top of the screen
 */
void drawHeader(const char* title) {
  gfx->fillRect(0, 0, LCD_WIDTH, 50, COL_CARD);
  gfx->setTextColor(COL_TEXT);
  gfx->setTextSize(3);
  gfx->setCursor(20, 15);
  gfx->print(title);
}

/**
 * Draw a rounded button (simulated with filled rect)
 */
void drawButton(int x, int y, int w, int h, const char* txt, uint16_t bgColor) {
  // Draw button background
  gfx->fillRoundRect(x, y, w, h, 10, bgColor);
  
  // Draw button border for depth
  gfx->drawRoundRect(x, y, w, h, 10, COL_TEXT);
  
  // Center text
  gfx->setTextSize(2);
  int16_t x1, y1;
  uint16_t tw, th;
  gfx->getTextBounds(txt, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor(x + (w - tw) / 2, y + (h - th) / 2);
  gfx->setTextColor(COL_BTN_TEXT);
  gfx->print(txt);
}

/**
 * Draw a card/panel with rounded corners
 */
void drawCard(int x, int y, int w, int h) {
  gfx->fillRoundRect(x, y, w, h, 15, COL_CARD);
}

/**
 * Draw a horizontal progress bar
 */
void drawProgressBar(int x, int y, int w, int h, float progress, uint16_t color) {
  // Background
  gfx->fillRoundRect(x, y, w, h, h/2, COL_CARD);
  
  // Progress
  int fillWidth = (int)(w * progress);
  if (fillWidth > 0) {
    gfx->fillRoundRect(x, y, fillWidth, h, h/2, color);
  }
  
  // Border
  gfx->drawRoundRect(x, y, w, h, h/2, COL_TEXT);
}

/**
 * Draw a circular progress ring (simplified as arc segments)
 */
void drawProgressRing(int cx, int cy, int radius, float progress, uint16_t color) {
  // Draw background circle
  gfx->drawCircle(cx, cy, radius, COL_CARD);
  gfx->drawCircle(cx, cy, radius - 1, COL_CARD);
  gfx->drawCircle(cx, cy, radius - 2, COL_CARD);
  
  // Draw progress arc (simplified - draw dots along circumference)
  int totalSteps = 60;
  int progressSteps = (int)(totalSteps * progress);
  
  for (int i = 0; i < progressSteps; i++) {
    float angle = (i * 360.0 / totalSteps - 90) * PI / 180.0;  // Start at top
    int x = cx + (int)(radius * cos(angle));
    int y = cy + (int)(radius * sin(angle));
    gfx->fillCircle(x, y, 2, color);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// BACKGROUND SERVICES
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Update clock service - runs every second
 */
void serviceClock() {
  unsigned long now = millis();
  
  if (now - lastSecond >= 1000) {
    lastSecond = now;
    
    // Try to read from RTC if available
    if (rtcAvailable && rtc.isRunning()) {
      RTC_Date datetime = rtc.getDateTime();
      hours = datetime.hour;
      minutes = datetime.minute;
      seconds = datetime.second;
    } else {
      // Fallback to millis-based time
      seconds++;
      if (seconds >= 60) {
        seconds = 0;
        minutes++;
        if (minutes >= 60) {
          minutes = 0;
          hours++;
          if (hours >= 24) {
            hours = 0;
          }
        }
      }
    }
    
    uiDirty = true;  // Trigger screen update
  }
}

/**
 * Update battery service - reads battery status
 */
void serviceBattery() {
  if (!pmuAvailable) return;
  
  unsigned long now = millis();
  
  if (now - lastBatteryCheck >= BATTERY_CHECK_INTERVAL) {
    lastBatteryCheck = now;
    
    // Read battery percentage
    batteryPercent = PMU.getBatteryPercent();
    
    // Check charging status
    isCharging = PMU.isCharging();
    
    uiDirty = true;
  }
}

/**
 * Update step counter - reads accelerometer
 */
void serviceSteps() {
  if (!qmiAvailable) return;
  
  unsigned long now = millis();
  
  // Check if cooldown period has passed
  if (now - lastStepTime < STEP_COOLDOWN) return;
  
  // Read accelerometer data
  if (qmi.getDataReady()) {
    float ax, ay, az;
    qmi.getAccelerometer(ax, ay, az);
    
    // Calculate magnitude
    float magnitude = sqrt(ax * ax + ay * ay + az * az);
    
    // Detect step (simple threshold crossing)
    float delta = abs(magnitude - lastAccelMagnitude);
    
    if (delta > STEP_THRESHOLD) {
      stepCount++;
      lastStepTime = now;
      uiDirty = true;
    }
    
    lastAccelMagnitude = magnitude;
  }
}

/**
 * Update focus timer - countdown management
 */
void serviceFocusTimer() {
  if (!focusRunning) return;
  
  unsigned long now = millis();
  
  if (now - lastFocusUpdate >= 1000) {
    lastFocusUpdate = now;
    focusSeconds--;
    
    if (focusSeconds <= 0) {
      focusRunning = false;
      focusSeconds = 25 * 60;  // Reset
      focusSessions++;
      
      // Optional: Add beep/vibration here
    }
    
    uiDirty = true;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// SCREEN DRAW FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Draw home screen - main hub
 */
void drawHomeScreen() {
  gfx->fillScreen(COL_BG);
  
  // --- Battery Status (Top Right) ---
  if (pmuAvailable) {
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT);
    
    char battStr[16];
    if (isCharging) {
      sprintf(battStr, "CHG %d%%", batteryPercent);
      gfx->setTextColor(COL_ACCENT);
    } else {
      sprintf(battStr, "%d%%", batteryPercent);
      if (batteryPercent < 20) {
        gfx->setTextColor(0xF800);  // Red for low battery
      } else {
        gfx->setTextColor(COL_STEPS);  // Green for good battery
      }
    }
    
    int16_t x1, y1;
    uint16_t tw, th;
    gfx->getTextBounds(battStr, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor(LCD_WIDTH - tw - 20, 15);
    gfx->print(battStr);
  }
  
  // --- Large Clock Display ---
  char timeStr[16];
  sprintf(timeStr, "%02d:%02d", hours, minutes);
  
  gfx->setTextSize(6);
  int16_t x1, y1;
  uint16_t tw, th;
  gfx->getTextBounds(timeStr, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor((LCD_WIDTH - tw) / 2, 60);
  gfx->setTextColor(COL_TEXT);
  gfx->print(timeStr);
  
  // Seconds (smaller)
  char secStr[8];
  sprintf(secStr, ":%02d", seconds);
  gfx->setTextSize(3);
  gfx->setTextColor(COL_ACCENT);
  gfx->print(secStr);
  
  // --- Steps Preview Card ---
  drawCard(20, 180, LCD_WIDTH - 40, 80);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_STEPS);
  gfx->setCursor(40, 195);
  gfx->print("STEPS");
  
  gfx->setTextSize(3);
  gfx->setTextColor(COL_TEXT);
  gfx->setCursor(40, 220);
  gfx->print(stepCount);
  gfx->setTextSize(2);
  gfx->print(" / ");
  gfx->print(stepGoal);
  
  // --- Focus Timer Status Card ---
  drawCard(20, 280, LCD_WIDTH - 40, 60);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_FOCUS);
  gfx->setCursor(40, 295);
  gfx->print("FOCUS: ");
  
  gfx->setTextColor(COL_TEXT);
  if (focusRunning) {
    int mins = focusSeconds / 60;
    int secs = focusSeconds % 60;
    char focusStr[16];
    sprintf(focusStr, "%02d:%02d", mins, secs);
    gfx->print(focusStr);
  } else {
    gfx->print("Ready");
  }
  
  // --- Navigation Buttons ---
  drawButton(30, 370, 140, 50, "Steps", COL_BUTTON);
  drawButton(198, 370, 140, 50, "Focus", COL_BUTTON);
}

/**
 * Draw steps screen - detailed step counter
 */
void drawStepsScreen() {
  gfx->fillScreen(COL_BG);
  
  // Header
  drawHeader("Steps");
  
  // Back button indicator
  gfx->setTextSize(2);
  gfx->setTextColor(COL_ACCENT);
  gfx->setCursor(10, 20);
  gfx->print("<");
  
  // --- Very Large Step Count ---
  char stepStr[16];
  sprintf(stepStr, "%d", stepCount);
  
  gfx->setTextSize(8);
  int16_t x1, y1;
  uint16_t tw, th;
  gfx->getTextBounds(stepStr, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor((LCD_WIDTH - tw) / 2, 120);
  gfx->setTextColor(COL_STEPS);
  gfx->print(stepStr);
  
  // Goal text
  gfx->setTextSize(2);
  gfx->setTextColor(COL_TEXT);
  char goalStr[32];
  sprintf(goalStr, "Goal: %d steps", stepGoal);
  gfx->getTextBounds(goalStr, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor((LCD_WIDTH - tw) / 2, 210);
  gfx->print(goalStr);
  
  // --- Progress Bar ---
  float progress = (float)stepCount / stepGoal;
  if (progress > 1.0) progress = 1.0;
  drawProgressBar(30, 250, LCD_WIDTH - 60, 20, progress, COL_STEPS);
  
  // Progress percentage
  int percentage = (int)(progress * 100);
  char percStr[8];
  sprintf(percStr, "%d%%", percentage);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_TEXT);
  gfx->getTextBounds(percStr, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor((LCD_WIDTH - tw) / 2, 280);
  gfx->print(percStr);
  
  // --- Estimated Distance (fake calculation) ---
  float distanceKm = stepCount * 0.0007;  // Rough estimate
  char distStr[32];
  sprintf(distStr, "~%.2f km", distanceKm);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_ACCENT);
  gfx->getTextBounds(distStr, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor((LCD_WIDTH - tw) / 2, 320);
  gfx->print(distStr);
  
  // --- Reset Button ---
  drawButton(84, 370, 200, 50, "Reset Steps", COL_BUTTON);
}

/**
 * Draw focus screen - productivity timer
 */
void drawFocusScreen() {
  gfx->fillScreen(COL_BG);
  
  // Header
  drawHeader("Focus");
  
  // Back button indicator
  gfx->setTextSize(2);
  gfx->setTextColor(COL_ACCENT);
  gfx->setCursor(10, 20);
  gfx->print("<");
  
  // --- Large Timer Display ---
  int mins = focusSeconds / 60;
  int secs = focusSeconds % 60;
  char timerStr[16];
  sprintf(timerStr, "%02d:%02d", mins, secs);
  
  gfx->setTextSize(8);
  int16_t x1, y1;
  uint16_t tw, th;
  gfx->getTextBounds(timerStr, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor((LCD_WIDTH - tw) / 2, 120);
  
  // Color changes when running
  if (focusRunning) {
    gfx->setTextColor(COL_FOCUS);
  } else {
    gfx->setTextColor(COL_TEXT);
  }
  gfx->print(timerStr);
  
  // --- Progress Ring ---
  float totalTime = 25 * 60;
  float elapsed = totalTime - focusSeconds;
  float progress = elapsed / totalTime;
  if (progress > 1.0) progress = 1.0;
  
  drawProgressRing(LCD_WIDTH / 2, 260, 60, progress, COL_FOCUS);
  
  // --- Session Counter ---
  gfx->setTextSize(2);
  gfx->setTextColor(COL_TEXT);
  char sessStr[32];
  sprintf(sessStr, "Sessions: %d", focusSessions);
  gfx->getTextBounds(sessStr, 0, 0, &x1, &y1, &tw, &th);
  gfx->setCursor((LCD_WIDTH - tw) / 2, 350);
  gfx->print(sessStr);
  
  // --- Start/Pause Button ---
  if (focusRunning) {
    drawButton(84, 390, 200, 50, "Pause", COL_BUTTON);
  } else {
    drawButton(84, 390, 200, 50, "Start", COL_FOCUS);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH HANDLING
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Handle touch on home screen
 */
void handleTouchHome(int32_t x, int32_t y) {
  // Steps button (30, 370, 140, 50)
  if (x >= 30 && x <= 170 && y >= 370 && y <= 420) {
    currentScreen = SCREEN_STEPS;
    uiDirty = true;
  }
  
  // Focus button (198, 370, 140, 50)
  if (x >= 198 && x <= 338 && y >= 370 && y <= 420) {
    currentScreen = SCREEN_FOCUS;
    uiDirty = true;
  }
}

/**
 * Handle touch on steps screen
 */
void handleTouchSteps(int32_t x, int32_t y) {
  // Back button area (left side of header)
  if (x <= 80 && y <= 50) {
    currentScreen = SCREEN_HOME;
    uiDirty = true;
    return;
  }
  
  // Reset button (84, 370, 200, 50)
  if (x >= 84 && x <= 284 && y >= 370 && y <= 420) {
    stepCount = 0;
    uiDirty = true;
  }
}

/**
 * Handle touch on focus screen
 */
void handleTouchFocus(int32_t x, int32_t y) {
  // Back button area (left side of header)
  if (x <= 80 && y <= 50) {
    currentScreen = SCREEN_HOME;
    uiDirty = true;
    return;
  }
  
  // Start/Pause button (84, 390, 200, 50)
  if (x >= 84 && x <= 284 && y >= 390 && y <= 440) {
    if (focusRunning) {
      focusRunning = false;
    } else {
      focusRunning = true;
      lastFocusUpdate = millis();
    }
    uiDirty = true;
  }
}

/**
 * Main touch handler - routes to appropriate screen handler
 */
void handleTouch() {
  if (!touchAvailable || !FT3168) return;
  
  // Check if touch interrupt triggered
  if (touchInterruptFlag) {
    touchInterruptFlag = false;
    
    unsigned long now = millis();
    
    // Reset sleep timer on any touch
    lastActivity = now;
    
    // Debounce
    if (now - lastTouchTime < TOUCH_DEBOUNCE) {
      return;
    }
    lastTouchTime = now;
    
    // Read touch data
    if (FT3168->isPressed()) {
      uint8_t fingers = FT3168->getPointNum();
      
      if (fingers > 0) {
        int32_t x = FT3168->getPoint(0).x;
        int32_t y = FT3168->getPoint(0).y;
        
        // Route to appropriate handler
        switch (currentScreen) {
          case SCREEN_HOME:
            handleTouchHome(x, y);
            break;
          case SCREEN_STEPS:
            handleTouchSteps(x, y);
            break;
          case SCREEN_FOCUS:
            handleTouchFocus(x, y);
            break;
        }
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH INTERRUPT CALLBACK
// ═══════════════════════════════════════════════════════════════════════════
void Arduino_IIC_Touch_Interrupt(void) {
  touchInterruptFlag = true;
}

// ═══════════════════════════════════════════════════════════════════════════
// POWER MANAGEMENT - DEEP SLEEP
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Enter deep sleep mode and configure wake on touch
 */
void enterDeepSleep() {
  Serial.println("Entering deep sleep...");
  
  // Turn off display
  gfx->displayOff();
  
  // Configure touch interrupt as wakeup source
  esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_INT, LOW);
  
  // Optional: Also wake on boot button
  esp_sleep_enable_ext1_wakeup(1ULL << BUTTON_BOOT, ESP_EXT1_WAKEUP_ANY_HIGH);
  
  // Enter deep sleep
  esp_deep_sleep_start();
}

/**
 * Check for inactivity and sleep if needed
 */
void checkSleepTimeout() {
  if (!sleepEnabled) return;
  
  unsigned long now = millis();
  
  // Don't sleep if focus timer is running
  if (focusRunning) {
    lastActivity = now;
    return;
  }
  
  // Check if timeout exceeded
  if (now - lastActivity >= SLEEP_TIMEOUT) {
    enterDeepSleep();
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP - ONE-TIME INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
  // Initialize serial for debugging
  Serial.begin(115200);
  delay(1000);
  Serial.println("═══════════════════════════════════════");
  Serial.println("S3 MiniOS v0.1 - Starting...");
  
  // Check wake reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Woke up from touch interrupt");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Woke up from button press");
      break;
    default:
      Serial.println("Cold boot / reset");
      break;
  }
  
  Serial.println("═══════════════════════════════════════");
  
  // ─────────────────────────────────────
  // DISPLAY INITIALIZATION
  // ─────────────────────────────────────
  Serial.println("Initializing display...");
  bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
  
  gfx = new Arduino_SH8601(
    bus, LCD_RST, 0 /* rotation */, false /* IPS */, LCD_WIDTH, LCD_HEIGHT);
  
  if (!gfx->begin()) {
    Serial.println("ERROR: Display initialization failed!");
    while (1) { delay(1000); }
  }
  
  gfx->fillScreen(COL_BG);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_TEXT);
  gfx->setCursor(80, 200);
  gfx->print("Initializing...");
  
  // Note: setBrightness() not available in current Arduino_GFX version
  // Use hardware-specific brightness control if needed
  Serial.println("Display OK");
  
  // ─────────────────────────────────────
  // I2C BUS INITIALIZATION
  // ─────────────────────────────────────
  Serial.println("Initializing I2C bus...");
  Wire.begin(IIC_SDA, IIC_SCL);
  Wire.setClock(400000);  // 400kHz
  Serial.println("I2C OK");
  
  // ─────────────────────────────────────
  // TOUCH CONTROLLER INITIALIZATION
  // ─────────────────────────────────────
  Serial.println("Initializing touch controller...");
  
  pinMode(TOUCH_INT, INPUT_PULLUP);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(50);
  
  FT3168 = new Arduino_IIC_Touch(
    Wire, 0x38, TOUCH_INT, TOUCH_RST, 
    LCD_WIDTH, LCD_HEIGHT);
  
  if (FT3168->begin()) {
    attachInterrupt(TOUCH_INT, Arduino_IIC_Touch_Interrupt, FALLING);
    touchAvailable = true;
    Serial.println("Touch OK");
  } else {
    Serial.println("WARNING: Touch controller not found");
  }
  
  // ─────────────────────────────────────
  // ACCELEROMETER INITIALIZATION
  // ─────────────────────────────────────
  Serial.println("Initializing accelerometer...");
  
  if (qmi.begin(Wire, 0x6B, IIC_SDA, IIC_SCL)) {  // QMI8658 I2C address
    qmi.configAccelerometer(
      SensorQMI8658::AccRange::ACC_RANGE_4G,
      SensorQMI8658::AccOdr::ACC_ODR_1000Hz,
      SensorQMI8658::LpfMode::LPF_MODE_0,
      true);
    
    qmi.enableAccelerometer();
    qmiAvailable = true;
    Serial.println("Accelerometer OK");
  } else {
    Serial.println("WARNING: Accelerometer not found");
  }
  
  // ─────────────────────────────────────
  // RTC INITIALIZATION
  // ─────────────────────────────────────
  Serial.println("Initializing RTC...");
  
  if (rtc.begin(Wire, 0x51, IIC_SDA, IIC_SCL)) {  // PCF85063 I2C address
    if (!rtc.isRunning()) {
      // Set initial time if RTC was not running
      rtc.setDateTime(2025, 1, 21, 12, 0, 0);
      rtc.enableCLK();
    }
    
    // Read initial time
    RTC_Date datetime = rtc.getDateTime();
    hours = datetime.hour;
    minutes = datetime.minute;
    seconds = datetime.second;
    
    rtcAvailable = true;
    Serial.println("RTC OK");
  } else {
    Serial.println("WARNING: RTC not found, using millis() for time");
  }
  
  // ─────────────────────────────────────
  // POWER MANAGEMENT (AXP2101)
  // ─────────────────────────────────────
  Serial.println("Initializing power management...");
  
  if (PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    // Enable power rails for peripherals
    PMU.enableALDO1();  // Display power
    PMU.enableALDO2();  // Touch/sensors
    PMU.enableBLDO1();  // Additional peripherals
    
    // Read initial battery status
    batteryPercent = PMU.getBatteryPercent();
    isCharging = PMU.isCharging();
    
    pmuAvailable = true;
    Serial.println("PMU OK");
    Serial.printf("  Battery: %d%% %s\n", batteryPercent, isCharging ? "(Charging)" : "");
  } else {
    Serial.println("WARNING: PMU not found");
  }
  
  // ─────────────────────────────────────
  // FINAL SETUP
  // ─────────────────────────────────────
  Serial.println("═══════════════════════════════════════");
  Serial.println("S3 MiniOS Ready!");
  Serial.println("Hardware Status:");
  Serial.printf("  Display: OK\n");
  Serial.printf("  Touch: %s\n", touchAvailable ? "OK" : "NOT FOUND");
  Serial.printf("  Accelerometer: %s\n", qmiAvailable ? "OK" : "NOT FOUND");
  Serial.printf("  RTC: %s\n", rtcAvailable ? "OK" : "NOT FOUND");
  Serial.printf("  PMU: %s\n", pmuAvailable ? "OK" : "NOT FOUND");
  Serial.println("═══════════════════════════════════════");
  
  delay(2000);
  
  // Initialize activity timer
  lastActivity = millis();
  
  // Draw initial screen
  drawHomeScreen();
  uiDirty = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════
void loop() {
  // Run background services
  serviceClock();
  serviceBattery();
  serviceSteps();
  serviceFocusTimer();
  
  // Handle touch input
  handleTouch();
  
  // Update screen if needed (max 10 FPS to save power)
  unsigned long now = millis();
  if (uiDirty && (now - lastUI >= 100)) {
    lastUI = now;
    
    switch (currentScreen) {
      case SCREEN_HOME:
        drawHomeScreen();
        break;
      case SCREEN_STEPS:
        drawStepsScreen();
        break;
      case SCREEN_FOCUS:
        drawFocusScreen();
        break;
    }
    
    uiDirty = false;
  }
  
  // Check for sleep timeout
  checkSleepTimeout();
  
  // Small delay to prevent CPU hogging
  delay(10);
}
