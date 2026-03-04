#define BLYNK_TEMPLATE_ID "TMPL64K0cdmL4"
#define BLYNK_TEMPLATE_NAME "RTEQM"
#define BLYNK_AUTH_TOKEN "K9eiRJvm4eYH2fEzHWVnqAR9QugnpvS5"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h> 
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <TimeLib.h>
#include <WidgetRTC.h> // for NTP  drill sim to basically hindi na need gumamit ng RTC the drill simulation runs using NTP

WidgetRTC rtc;
long drillTimeInSeconds = -1;

char ssid[] = "Tiffany_2G";  // pa change na lang sa gagamitin nyong ssid and pass  
char pass[] = "kniahmaitim"; // note that  dapat same yung entwork nyo na illagay here  saka sa  connection ng blynk sa phone

const char* recipientNumbers[] = {
  "+639123456789", // Person 1
  "+639987654321", // Person 2
  "+639000000000"  // Person 3
}; // Change this sa mga numbers na pagssendan ng sim800l

const int totalRecipients = sizeof(recipientNumbers) / sizeof(recipientNumbers[0]);

bool isSendingSms = false;
int currentRecipientIndex = 0;
unsigned long lastSmsStepTime = 0;
int smsStep = 0; 
String currentSmsMessage = "";

// defined pins
#define TFT_DC    2
#define TFT_CS    15
#define TFT_RST   4
#define TFT_LED   32

#define BTN_GRAPH  27
#define BTN_TXT    26
#define BTN_MNL    25
#define BTN_BCK    33

#define RXD2 16
#define TXD2 17
#define SSR_PIN 13

// comp init
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// screen stsates
enum Screen { SCREEN_HOME, SCREEN_GRAPH, SCREEN_SENDTEXT, SCREEN_MANUALALARM };
Screen currentScreen = SCREEN_HOME;

// debounce button
unsigned long lastDebounce_GRAPH = 0;
unsigned long lastDebounce_TXT   = 0;
unsigned long lastDebounce_MNL   = 0;
unsigned long lastDebounce_BCK   = 0;
const unsigned long DEBOUNCE_MS  = 200;

//  Graph & Seismic Variables
int xPos = 6;
int prevY = 110;
float threshold = 1.8;
unsigned long minDuration    = 1000;
unsigned long exitCooldown   = 2000;
unsigned long alertHoldTime  = 5000;

float peakMag = 0.0;
unsigned long startTime    = 0;
unsigned long lastShakeTime = 0;
float lastDuration = 0.0;
bool isVibrating           = false;
bool isConfirmedEarthquake = false;
bool showingPostEvent      = false;
unsigned long eventEndTime = 0;

// Manual Alarm State 
bool manualAlarmActive = false;
bool autoSmsSent = false;

// Marquee Logic (marquee ks yung moving text)
bool marqueeActive = false;
unsigned long marqueeStartTime = 0; 
const unsigned long MARQUEE_DURATION = 30000; 
const char* MARQUEE_TEXT = "  ALERT! EARTHQUAKE DETECTED - EVACUATE THE PREMISES IMMEDIATELY!  ";
int  marqueeX        = 320;
unsigned long lastMarqueeMs = 0;
const int MARQUEE_STEP  = 4;
const int MARQUEE_DELAY = 30;

int  alertIndicator      = 0;
unsigned long alertShownAt = 0;
const unsigned long ALERT_BADGE_MS = 6000; 

// Blynk Timing
unsigned long lastBlynkStream = 0;


//  CORE LOGIC FUNCTIONS (GLOBAL)
void checkSeismicActivity() {
  sensors_event_t event;
  accel.getEvent(&event);

  float rawMag = sqrt(sq(event.acceleration.x) + sq(event.acceleration.y) + sq(event.acceleration.z));
  float vibration = abs(rawMag - 9.81);
  unsigned long now = millis();

  // BLYNK Stream live seismograph data to V0 
  // unfortunately di kaya sa  graph  yung free tier 
  // if mag babayad kay o access point sa blynk ma uutilize yung widget na for graph
  // so ang ginawa  ko na lang na alt is gauge
  // you can change it back  from VO if gusto nyo graph pero  for gauge to be utilize ang  ginamit ko muna ay  V2
  if (WiFi.status() == WL_CONNECTED && (now - lastBlynkStream > 100)) {
    Blynk.virtualWrite(V0, vibration);
    lastBlynkStream = now;
  }

  if (!isVibrating && (now - lastShakeTime > 30000)) {
     autoSmsSent = false; 
  }

  if (vibration > threshold) {
    lastShakeTime = now;
    if (!isVibrating) {
      isVibrating = true;
      showingPostEvent = false;
      startTime = now;
      peakMag = 0;
    }
    if (vibration > peakMag) peakMag = vibration;

    if (!isConfirmedEarthquake && (now - startTime > minDuration)) {
      isConfirmedEarthquake = true;
if (!autoSmsSent) {
    alertIndicator = 1; // Shows alerting...
    if(currentScreen == SCREEN_GRAPH) drawAlertIndicator();

    triggerSmsAlert("EARTHQUAKE ALERT! Seismic activity detected."); // START SENDING
    
    autoSmsSent = true;
    alertShownAt = millis();

        // Marquee and Blynk triggers
        marqueeActive = true;
        marqueeX = 320;
        marqueeStartTime = millis(); 
        if (WiFi.status() == WL_CONNECTED) {
          String alertMsg = "EARTHQUAKE DETECTED! Intensity: " + String(peakMag) + " m/s2";
          Blynk.logEvent("earthquake_detected", alertMsg);
        }
      }
    }
  }

  if (isVibrating && (now - lastShakeTime > exitCooldown)) {
    isVibrating = false;
    lastDuration = (lastShakeTime - startTime) / 1000.0;
    eventEndTime = now;

    // BLYNK Send Peak (V1) and Duration (V2)
    if (WiFi.status() == WL_CONNECTED) {
      Blynk.virtualWrite(V1, peakMag);
      Blynk.virtualWrite(V2, lastDuration);
    }

    if (isConfirmedEarthquake) {
        showingPostEvent = true;
        isConfirmedEarthquake = false; 
    }
    if(currentScreen == SCREEN_GRAPH) updateMetrics();
  }
}

void tickMarquee() {
  if (!marqueeActive) return;

  if (millis() - marqueeStartTime > MARQUEE_DURATION) {
    marqueeActive = false;
    if (currentScreen == SCREEN_HOME) restoreHomeTitle();
    return;
  }

  if (millis() - lastMarqueeMs < (unsigned long)MARQUEE_DELAY) return;
  lastMarqueeMs = millis();

  tft.setTextWrap(false); 
  tft.fillRect(1, 1, 318, 33, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_RED);
  tft.setCursor(marqueeX, 9);
  tft.print(MARQUEE_TEXT);

  marqueeX -= MARQUEE_STEP;
  int textPixelWidth = strlen(MARQUEE_TEXT) * 12; 
  if (marqueeX < -textPixelWidth) marqueeX = 320;
}

//  UI HELPERS
bool btnPressed(int pin, unsigned long &lastTime) {
  if (digitalRead(pin) == LOW && millis() - lastTime > DEBOUNCE_MS) {
    lastTime = millis();
    return true;
  }
  return false;
}

void triggerSmsAlert(String message) {
  if (!isSendingSms) {
    isSendingSms = true;
    currentRecipientIndex = 0;
    smsStep = 0;
    currentSmsMessage = message;
    Serial.println("Background SMS Started...");
  }
}

void updateSmsProcess() {
  if (!isSendingSms) return;
  unsigned long now = millis();

  switch (smsStep) {
    case 0: // Initialize
      Serial2.println("AT+CMGF=1");
      lastSmsStepTime = now;
      smsStep = 1;
      break;

    case 1: // Set Recipient
      if (now - lastSmsStepTime >= 300) {
        Serial2.print("AT+CMGS=\"");
        Serial2.print(recipientNumbers[currentRecipientIndex]);
        Serial2.println("\"");
        lastSmsStepTime = now;
        smsStep = 2;
      }
      break;

    case 2: // Send Content
      if (now - lastSmsStepTime >= 300) {
        Serial2.print(currentSmsMessage);
        Serial2.write(26); 
        lastSmsStepTime = now;
        smsStep = 3; // Move to LISTENING mode
      }
      break;

    case 3: // LISTENING MODE 
      if (Serial2.available()) {
        String response = Serial2.readString();
        if (response.indexOf("+CMGS:") != -1) {
          // Success! Move to next step
          lastSmsStepTime = now;
          smsStep = 4;
        } else if (response.indexOf("ERROR") != -1) {
          // Failure! Mark as error and move to next step
          alertIndicator = 3; 
          if(currentScreen == SCREEN_GRAPH) drawAlertIndicator();
          lastSmsStepTime = now;
          smsStep = 4;
        }
      }
      // Safety Timeout if no response for 10 seconds, assume failed
      if (now - lastSmsStepTime > 10000) {
        alertIndicator = 3;
        smsStep = 4;
      }
      break;

    case 4: // Cooldown and Next Person
      if (now - lastSmsStepTime >= 1500) {
        currentRecipientIndex++;
        if (currentRecipientIndex >= totalRecipients) {
          isSendingSms = false;
          if (alertIndicator != 3) alertIndicator = 2; 
          alertShownAt = millis();
          if(currentScreen == SCREEN_GRAPH) drawAlertIndicator();
        } else {
          smsStep = 0; // Next person
        }
      }
      break;
  }
}

void drawAlertIndicator() {
  tft.fillRect(220, 8, 94, 14, ILI9341_BLACK);
  if (alertIndicator == 1) {
    tft.setTextSize(1); tft.setTextColor(ILI9341_ORANGE);
    tft.setCursor(220, 8); tft.print("alerting...");
  } else if (alertIndicator == 2) {
    tft.setTextSize(1); tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(238, 8); tft.print("sent");
  } else if (alertIndicator == 3) {
    tft.setTextSize(1); tft.setTextColor(ILI9341_RED);
    tft.setCursor(220, 8); tft.print("failed!");
  }
}

void restoreHomeTitle() {
  tft.fillRect(1, 1, 318, 33, ILI9341_BLACK);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(2);
  tft.setCursor(30, 10);
  tft.print("EARTHQUAKE MONITOR");
}

//  SCREEN DRAWING
void drawHomeScreen() {
  tft.fillScreen(ILI9341_BLACK);
  tft.drawRect(0, 0, 320, 240, ILI9341_WHITE);
  if (!marqueeActive) restoreHomeTitle();

  tft.drawFastHLine(5, 35, 310, ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(95, 42);
  tft.print("SEISMIC STATION");
  tft.drawFastHLine(5, 55, 310, ILI9341_DARKGREY);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(130, 62);
  tft.print("MAIN MENU");
  tft.drawFastHLine(5, 72, 310, ILI9341_DARKGREY);

  // Buttons
  tft.fillRoundRect(15, 82, 290, 38, 4, 0x0841);
  tft.drawRoundRect(15, 82, 290, 38, 4, ILI9341_CYAN);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(1); tft.setCursor(25, 90); tft.print("[BTN 1]");
  tft.setTextSize(2); tft.setCursor(90, 87); tft.print("SEISMIC GRAPH");

  tft.fillRoundRect(15, 130, 290, 38, 4, 0x0820);
  tft.drawRoundRect(15, 130, 290, 38, 4, ILI9341_GREEN);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(1); tft.setCursor(25, 138); tft.print("[BTN 2]");
  tft.setTextSize(2); tft.setCursor(90, 135); tft.print("SEND ALARM TEXT");

  tft.fillRoundRect(15, 178, 290, 38, 4, 0x2000);
  tft.drawRoundRect(15, 178, 290, 38, 4, ILI9341_RED);
  tft.setTextColor(ILI9341_RED);
  tft.setTextSize(1); tft.setCursor(25, 186); tft.print("[BTN 3]");
  tft.setTextSize(2); tft.setCursor(90, 183); tft.print("MANUAL ALARM");

  tft.drawFastHLine(5, 222, 310, ILI9341_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(10, 228);
  tft.print("[BACK] = Return  |  Select option above");
}

void drawGraphScreen() {
  tft.fillScreen(ILI9341_BLACK);
  tft.drawRect(0, 0, 320, 240, ILI9341_WHITE);
  tft.setCursor(10, 8);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(2);
  tft.print("SEISMIC MONITOR");
  tft.drawRect(5, 40, 310, 140, ILI9341_WHITE);
  tft.drawFastHLine(6, 110, 308, 0x2104);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, 190); tft.print("SYSTEM STATUS:");
  tft.setCursor(10, 215); tft.print("MAX INTENSITY:");
  tft.setCursor(170, 215); tft.print("DURATION:");
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(240, 228); tft.print("[BCK]=HOME");
  drawAlertIndicator();
  xPos = 6;
  prevY = 110;
}

void drawSendTextScreen() {
  tft.fillScreen(ILI9341_BLACK);
  tft.drawRect(0, 0, 320, 240, ILI9341_WHITE);
  tft.setTextColor(ILI9341_CYAN);
  tft.setTextSize(2);
  tft.setCursor(55, 12);
  tft.print("SEND ALARM TEXT");
  tft.drawFastHLine(5, 35, 310, ILI9341_WHITE);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 50); tft.print("Sends an emergency SMS alert to");
  tft.setCursor(10, 65); tft.print("registered contacts via GSM module.");
  tft.drawFastHLine(5, 80, 310, ILI9341_DARKGREY);
  tft.drawRoundRect(15, 90, 290, 60, 6, ILI9341_YELLOW);
  tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(25, 100); tft.print("STATUS: READY TO SEND");
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(25, 118); tft.print("Press [BTN 2] to SEND alert SMS now.");
  tft.drawFastHLine(5, 165, 310, ILI9341_DARKGREY);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(10, 228);
  tft.print("[BCK]=HOME  |  [BTN 2]=SEND NOW");
}

void drawManualAlarmScreen() {
  tft.fillScreen(ILI9341_BLACK);
  tft.drawRect(0, 0, 320, 240, ILI9341_WHITE);
  
  // Header
  tft.setTextColor(ILI9341_RED);
  tft.setTextSize(2);
  tft.setCursor(85, 12);
  tft.print("MANUAL ALARM");
  tft.drawFastHLine(5, 35, 310, ILI9341_WHITE);
  
  // Instructions 
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 50); tft.print("Manually triggers the physical siren and sends");
  tft.setCursor(10, 65); tft.print("an emergency SMS notification to all contacts.");
  tft.drawFastHLine(5, 80, 310, ILI9341_DARKGREY);

  // This calls the status box logic
  updateManualAlarmDisplay();

  // Footer
  tft.drawFastHLine(5, 215, 310, ILI9341_DARKGREY);
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(10, 228);
  tft.print("[BCK]=HOME  |  [BTN 3]=TOGGLE SIREN");
}

void updateManualAlarmDisplay() {

  tft.fillRect(15, 90, 290, 110, ILI9341_BLACK);
  
  if (manualAlarmActive) {
    
    tft.drawRoundRect(15, 90, 290, 110, 6, ILI9341_RED);
    tft.fillRect(20, 95, 280, 100, 0x2000); 
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(2);
    tft.setCursor(75, 120); tft.print("!! SIREN ON !!");
    
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(65, 155); tft.print("Press [BTN 3] to DEACTIVATE");
  } else {

    tft.drawRoundRect(15, 90, 290, 110, 6, ILI9341_GREEN);
    tft.setTextColor(ILI9341_GREEN);
    tft.setTextSize(2);
    tft.setCursor(100, 120); tft.print("STANDBY");
    
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(65, 155); tft.print("Press [BTN 3] to ACTIVATE");
  }
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

void drawStatusDots() {
  if (currentScreen == SCREEN_HOME || currentScreen == SCREEN_GRAPH) {
    int dotX = 280; 
    int dotY = 15;
    
    // Manual Alarm Dot (Red)
    if (manualAlarmActive) {
      if ((millis() / 500) % 2 == 0) {
        tft.fillCircle(dotX, dotY, 5, ILI9341_RED);
      } else {
        tft.fillCircle(dotX, dotY, 5, ILI9341_BLACK);
        tft.drawCircle(dotX, dotY, 5, ILI9341_RED);
      }
    } else {
      tft.fillCircle(dotX, dotY, 5, ILI9341_BLACK); 
    }

    // SMS Sending Dot (Green)
    if (isSendingSms) {
      if ((millis() / 300) % 2 == 0) {
        tft.fillCircle(dotX + 15, dotY, 5, ILI9341_GREEN);
      } else {
        tft.fillCircle(dotX + 15, dotY, 5, ILI9341_BLACK);
        tft.drawCircle(dotX + 15, dotY, 5, ILI9341_GREEN);
      }
    } else {
      tft.fillCircle(dotX + 15, dotY, 5, ILI9341_BLACK);
    }
  }
}

//  MAIN LOOP Logic
void loop() {

  drawStatusDots();

  // BLYNK Run background tasks only if may wifi
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }

  checkDrillTimer();

  // 1. CONSTANTLY CHECK SENSOR
  checkSeismicActivity(); 
  updateSmsProcess();

  // 2. TICK MARQUEE IF ON HOME
  if (marqueeActive && currentScreen == SCREEN_HOME) {
    tickMarquee();
  } else if (marqueeActive) {
    if (millis() - marqueeStartTime > MARQUEE_DURATION) marqueeActive = false;
  }

  // 3. NAVIGATION BUTTONS
  if (btnPressed(BTN_BCK, lastDebounce_BCK)) {
    if (currentScreen != SCREEN_HOME) {
      if (currentScreen == SCREEN_MANUALALARM) {
        manualAlarmActive = false;
        digitalWrite(SSR_PIN, LOW);
      }
      currentScreen = SCREEN_HOME;
      drawHomeScreen();
      return;
    }
  }

  // 4. SCREEN SPECIFIC UPDATES
  switch (currentScreen) {
    case SCREEN_HOME:
      if      (btnPressed(BTN_GRAPH, lastDebounce_GRAPH)) { currentScreen = SCREEN_GRAPH;      drawGraphScreen();      }
      else if (btnPressed(BTN_TXT,   lastDebounce_TXT))   { currentScreen = SCREEN_SENDTEXT;   drawSendTextScreen();   }
      else if (btnPressed(BTN_MNL,   lastDebounce_MNL))   { currentScreen = SCREEN_MANUALALARM; drawManualAlarmScreen();}
      break;

    case SCREEN_GRAPH:
      {
        if (alertIndicator != 0 && (millis() - alertShownAt > ALERT_BADGE_MS)) {
           alertIndicator = 0;
           drawAlertIndicator(); 
        }

        tft.setTextSize(1);
        tft.setCursor(100, 190);
        if (isVibrating) {
           if (isConfirmedEarthquake) { tft.setTextColor(ILI9341_RED, ILI9341_BLACK); tft.print("EARTHQUAKE IN PROGRESS!    "); }
           else { tft.setTextColor(ILI9341_BLUE, ILI9341_BLACK); tft.print("ANALYZING VIBRATION...      "); }
        } else if (showingPostEvent) { tft.setTextColor(ILI9341_ORANGE, ILI9341_BLACK); tft.print("EARTHQUAKE HAS OCCURRED    "); }
        else { tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK); tft.print("STABLE / MONITORING...     "); }

        sensors_event_t event; accel.getEvent(&event);
        float vibration = abs(sqrt(sq(event.acceleration.x) + sq(event.acceleration.y) + sq(event.acceleration.z)) - 9.81);
        int yGraph = map(vibration * 25, 0, 100, 110, 45);
        yGraph = constrain(yGraph, 45, 175);
        uint16_t color = isVibrating ? (isConfirmedEarthquake ? ILI9341_RED : ILI9341_BLUE) : ILI9341_GREEN;
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
      break;

    case SCREEN_SENDTEXT:
      if (btnPressed(BTN_TXT, lastDebounce_TXT)) {
          tft.fillRect(15, 90, 290, 60, ILI9341_BLACK);
          tft.drawRoundRect(15, 90, 290, 60, 6, ILI9341_ORANGE);
          tft.setTextColor(ILI9341_ORANGE);
          tft.setCursor(25, 100); tft.print("STATUS: SENDING...");

          triggerSmsAlert("MANUAL EARTHQUAKE ALERT! Please evacuate."); 
      }
      break;

    case SCREEN_MANUALALARM:
      if (btnPressed(BTN_MNL, lastDebounce_MNL)) {
        manualAlarmActive = !manualAlarmActive;
        digitalWrite(SSR_PIN, manualAlarmActive ? HIGH : LOW); // Physical alarm happens instantly
        updateManualAlarmDisplay(); // Screen updates instantly
        
        if (manualAlarmActive) {
           triggerSmsAlert("MANUAL SIREN ACTIVATED! Emergency in progress.");

          if (WiFi.status() == WL_CONNECTED) {
            Blynk.logEvent("earthquake_detected", "MANUAL ALARM: Emergency siren triggered manually!");
          }
        }
      }
      break;
  }
}

BLYNK_WRITE(V3) {
  int value = param.asInt(); // 1 = On, 0 = Off
  
  if (value == 1) {
    manualAlarmActive = true;
    digitalWrite(SSR_PIN, HIGH); // Physical siren ON
    
    if (WiFi.status() == WL_CONNECTED) {
       Blynk.logEvent("earthquake_detected", "MANUAL ALARM: Activated from Phone!");
    }
  } else {
    manualAlarmActive = false;
    digitalWrite(SSR_PIN, LOW); // Physical siren OFF
  }
}

// Remote SMS Trigger via V4
BLYNK_WRITE(V4) {
  if (param.asInt() == 1) {
    triggerSmsAlert("REMOTE SMS ALERT! Triggered from Blynk Mobile.");
  }
}

BLYNK_WRITE(V5) {
  TimeInputParam t(param);

  if (t.hasStartTime()) {
    // Convert the input (e.g., 12:30 PM) into total seconds since sa start ng day
    drillTimeInSeconds = (t.getStartHour() * 3600) + (t.getStartMinute() * 60);
    Serial.printf("Drill Scheduled: %02d:%02d\n", t.getStartHour(), t.getStartMinute());
  } else {
    drillTimeInSeconds = -1; 
  }
}


BLYNK_CONNECTED() {
  Blynk.syncVirtual(V3); 
}

void checkDrillTimer() {
  if (drillTimeInSeconds != -1 && WiFi.status() == WL_CONNECTED) {
    long currentTimeInSeconds = (hour() * 3600) + (minute() * 60) + second();

    if (currentTimeInSeconds >= drillTimeInSeconds && currentTimeInSeconds < drillTimeInSeconds + 2) {
      
      // TRIGGER ALARMS
      manualAlarmActive = true;
      digitalWrite(SSR_PIN, HIGH); 
      triggerSmsAlert("DRILL SIMULATION STARTED!"); 
      Blynk.logEvent("earthquake_detected", "Scheduled Drill in Progress."); 

      drillTimeInSeconds = -1; 
      if (currentScreen == SCREEN_MANUALALARM) updateManualAlarmDisplay();
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Hardware FIRST
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  pinMode(BTN_GRAPH, INPUT_PULLUP);
  pinMode(BTN_TXT,   INPUT_PULLUP);
  pinMode(BTN_MNL,   INPUT_PULLUP);
  pinMode(BTN_BCK,   INPUT_PULLUP);
  pinMode(SSR_PIN,   OUTPUT);
  pinMode(TFT_LED,   OUTPUT);
  digitalWrite(SSR_PIN, LOW);
  digitalWrite(TFT_LED, HIGH);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  if (!accel.begin()) { while (1); }
  accel.setRange(ADXL345_RANGE_2_G);
  drawHomeScreen();

// 1. Initialize WiFi
  WiFi.begin(ssid, pass);
  
  // 2. Configure Blynk 
  Blynk.config(BLYNK_AUTH_TOKEN);

  // This tells Blynk to try and reach the server in the background
  Blynk.connect(); 

  rtc.begin(); 
  setSyncInterval(10 * 60); 
  
  drawHomeScreen();
}
