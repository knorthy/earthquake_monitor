#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

// --- TFT Pin Connections ---
#define TFT_DC    2   
#define TFT_CS    15  
#define TFT_RST   4   

// --- Initialize Components ---
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// --- Variables for Graphing & Smart Logic ---
int xPos = 6;
int prevY = 130;
float threshold = 1.8;       
unsigned long minDuration = 1000;    // 1 second to confirm it's not a bump
unsigned long exitCooldown = 2000;   // Wait 2 seconds of "quiet" before ending event
unsigned long alertHoldTime = 5000;  // Show "Has Occurred" for 5 seconds after

// Metrics & State tracking
float peakMag = 0.0;
unsigned long startTime = 0;
unsigned long lastShakeTime = 0;     // Tracks the last time vibration was ABOVE threshold
float lastDuration = 0.0;
bool isVibrating = false;            // Currently detecting movement
bool isConfirmedEarthquake = false;  // Met the duration requirement
bool showingPostEvent = false;       // Showing the "Has Occurred" summary
unsigned long eventEndTime = 0;

void setup() {
  Serial.begin(115200);
  tft.begin();
  tft.setRotation(1); 
  tft.fillScreen(ILI9341_BLACK);

  if(!accel.begin()){
    Serial.println("ADXL345 not found!");
    while(1);
  }
  accel.setRange(ADXL345_RANGE_2_G);

  // --- Static UI ---
  tft.drawRect(0, 0, 320, 240, ILI9341_WHITE);
  tft.setCursor(10, 12);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(2);
  tft.print("CvSU SEISMIC STATION");

  tft.drawRect(5, 40, 310, 140, ILI9341_WHITE); // Graph Area
  
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, 190); tft.print("SYSTEM STATUS:");
  tft.setCursor(10, 215); tft.print("MAX INTENSITY:");
  tft.setCursor(170, 215); tft.print("DURATION:");
}

void loop() {
  sensors_event_t event;
  accel.getEvent(&event);

  float rawMag = sqrt(sq(event.acceleration.x) + sq(event.acceleration.y) + sq(event.acceleration.z));
  float vibration = abs(rawMag - 9.81);

  unsigned long now = millis();

  // --- 1. DETECTION & HYSTERESIS LOGIC ---
  if (vibration > threshold) {
    lastShakeTime = now; // Update the "Last Active" timestamp
    
    if (!isVibrating) {
      isVibrating = true;
      showingPostEvent = false; 
      startTime = now;
      peakMag = 0;
    }
    
    if (vibration > peakMag) peakMag = vibration;

    // Confirm as Earthquake after 1 second of continuous/frequent shaking
    if (!isConfirmedEarthquake && (now - startTime > minDuration)) {
      isConfirmedEarthquake = true;
    }
  } 

  // LOGIC: If we ARE vibrating, but the last shake was more than 2 seconds ago...
  if (isVibrating && (now - lastShakeTime > exitCooldown)) {
    isVibrating = false;
    lastDuration = (lastShakeTime - startTime) / 1000.0;
    eventEndTime = now;
    
    if (isConfirmedEarthquake) {
      showingPostEvent = true; 
    } else {
      isConfirmedEarthquake = false; // Just a bump
    }
    updateMetrics();
  }

  // --- 2. STATUS TEXT RENDERING ---
  tft.setTextSize(1);
  tft.setCursor(100, 190);

  if (isVibrating) {
    if (isConfirmedEarthquake) {
      tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
      tft.print("EARTHQUAKE IN PROGRESS!    ");
    } else {
      tft.setTextColor(ILI9341_BLUE, ILI9341_BLACK);
      tft.print("ANALYZING VIBRATION...     ");
    }
  } 
  else if (showingPostEvent) {
    // Keep the "Occurred" message for 5 seconds
    if (now - eventEndTime < alertHoldTime) {
      tft.setTextColor(ILI9341_ORANGE, ILI9341_BLACK);
      tft.print("EARTHQUAKE HAS OCCURRED    ");
    } else {
      showingPostEvent = false;
      isConfirmedEarthquake = false;
    }
  } 
  else {
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.print("STABLE / MONITORING...     ");
  }

  // --- 3. GRAPHING ---
  int yGraph = map(vibration * 25, 0, 100, 110, 45); 
  yGraph = constrain(yGraph, 45, 175);

  uint16_t color = ILI9341_GREEN;
  if (isVibrating) {
    color = isConfirmedEarthquake ? ILI9341_RED : ILI9341_BLUE;
  }

  tft.drawLine(xPos, prevY, xPos + 1, yGraph, color);
  prevY = yGraph;
  xPos++;

  if (xPos >= 310) {
    xPos = 6;
    tft.fillRect(6, 41, 308, 138, ILI9341_BLACK);
    tft.drawFastHLine(6, 110, 308, 0x2104); 
  }

  delay(25); 
}

void updateMetrics() {
  tft.fillRect(100, 213, 60, 12, ILI9341_BLACK);
  tft.setCursor(100, 215);
  tft.setTextColor(ILI9341_WHITE);
  tft.print(peakMag, 2); tft.print(" m/s2");

  tft.fillRect(230, 213, 60, 12, ILI9341_BLACK);
  tft.setCursor(230, 215);
  tft.print(lastDuration, 2); tft.print(" s");
}
