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

char ssid[] = "Tiffany_2G";  // pa change na lang sa gagamitin nyong ssid and pass  
char pass[] = "kniahmaitim"; // note that  dapat same yung entwork nyo na illagay here  saka sa  connection ng blynk sa phone

const char* recipientNumbers[] = {
  "+639123456789", // Person 1
  "+639987654321", // Person 2
  "+639000000000"  // Person 3
}; // Change this sa mga numbers na pagssendan ng sim800l

const int totalRecipients = sizeof(recipientNumbers) / sizeof(recipientNumbers[0]);

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
        bool smsDelivered = false;
        int retryCount = 0;
        int maxRetries = 3;

        // Loop until sent OR we run out of retries
        while (!smsDelivered && retryCount < maxRetries) {
          alertIndicator = 1; // Shows alerting...
          if(currentScreen == SCREEN_GRAPH) drawAlertIndicator();

      
          smsDelivered = sendSmsToAll("EARTHQUAKE ALERT! Seismic activity detected.");

          if (smsDelivered) {
            alertIndicator = 2; // Shows sent
            autoSmsSent = true;
          } else {
            alertIndicator = 3; // Shows error message
            if(currentScreen == SCREEN_GRAPH) drawAlertIndicator();
            retryCount++;
            if(retryCount < maxRetries) delay(3000); // Wait before trying again
          }
        }
        
        alertShownAt = millis();
        if(currentScreen == SCREEN_GRAPH) drawAlertIndicator();

        // Marquee and Blynk triggers
        marqueeActive = true;
        marqueeX = 320;
        marqueeStartTime = millis(); 
        if (WiFi.status() == WL_CONNECTED) {
          Blynk.logEvent("earthquake_detected", "Earthquake detected! Take cover!");
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

bool sendSmsToAll(String message) {
  bool allPersonSuccess = true;
  for (int i = 0; i < totalRecipients; i++) {
    Serial2.println("AT+CMGF=1"); delay(200);
    Serial2.print("AT+CMGS=\"");
    Serial2.print(recipientNumbers[i]);
    Serial2.println("\""); delay(200);
    Serial2.print(message); delay(200);
    Serial2.write(26); // Send CTRL+Z

    // listens 
    unsigned long startWait = millis();
    bool personOk = false;
    while (millis() - startWait < 5000) { // Wait 5 seconds for response
      if (Serial2.available()) {
        String res = Serial2.readString();
        if (res.indexOf("+CMGS:") != -1) { personOk = true; break; }
        if (res.indexOf("ERROR") != -1) { personOk = false; break; }
      }
    }
    if (!personOk) allPersonSuccess = false;
    delay(1000);
  }
  return allPersonSuccess; // This tells the "Retry" loop if it worked
}

void drawAlertIndicator() {
  tft.fillRect(220, 8, 94, 14, ILI9341_BLACK);
  if (alertIndicator == 1) {
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_ORANGE);
    tft.setCursor(220, 8);
    tft.print("alerting...");
  } else if (alertIndicator == 2) {
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(238, 8);
    tft.print("sent");
  } else if (alertIndicator == 3) { // state inwhich nakikinig yungg system sa response ni sim800l
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(220, 8);
    tft.print("retry error!");
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
  tft.setTextColor(ILI9341_RED);
  tft.setTextSize(2);
  tft.setCursor(55, 12);
  tft.print("MANUAL ALARM");
  tft.drawFastHLine(5, 35, 310, ILI9341_WHITE);
  updateManualAlarmDisplay();
  tft.setTextColor(ILI9341_DARKGREY);
  tft.setCursor(10, 228);
  tft.print("[BCK]=HOME  |  [BTN 3]=TOGGLE ALARM");
}

void updateManualAlarmDisplay() {
  tft.fillRect(16, 91, 288, 108, ILI9341_BLACK);
  if (manualAlarmActive) {
    tft.drawRoundRect(15, 90, 290, 110, 6, ILI9341_RED);
    tft.fillRect(16, 91, 288, 108, 0x2000);
    tft.setTextColor(ILI9341_RED);
    tft.setTextSize(2);
    tft.setCursor(70, 102); tft.print("!! ALARM ON !!");
  } else {
    tft.drawRoundRect(15, 90, 290, 110, 6, ILI9341_GREEN);
    tft.setTextColor(ILI9341_GREEN);
    tft.setTextSize(2);
    tft.setCursor(80, 102); tft.print("ALARM OFF");
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


//  MAIN LOOP Logic
void loop() {
  // BLYNK Run background tasks only if WiFi is alive
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }

  // 1. CONSTANTLY CHECK SENSOR
  checkSeismicActivity();

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
          bool manualSmsDelivered = false;
          int retries = 0;
          
          while (!manualSmsDelivered && retries < 3) {
            // UI Feedback Sending
            tft.fillRect(15, 90, 290, 60, ILI9341_BLACK);
            tft.drawRoundRect(15, 90, 290, 60, 6, ILI9341_ORANGE);
            tft.setTextColor(ILI9341_ORANGE);
            tft.setCursor(25, 100); tft.print("STATUS: SENDING...");
            if(retries > 0) { tft.print(" (TRY "); tft.print(retries + 1); tft.print(")"); }

            manualSmsDelivered = sendSmsToAll("MANUAL EARTHQUAKE ALERT! Please evacuate."); 

            if (manualSmsDelivered) {
              tft.fillRect(15, 90, 290, 60, ILI9341_BLACK);
              tft.drawRoundRect(15, 90, 290, 60, 6, ILI9341_GREEN);
              tft.setTextColor(ILI9341_GREEN);
              tft.setCursor(25, 100); tft.print("STATUS: ALL SMS SENT!");
            } else {
              tft.fillRect(15, 90, 290, 60, ILI9341_BLACK);
              tft.drawRoundRect(15, 90, 290, 60, 6, ILI9341_RED);
              tft.setTextColor(ILI9341_RED);
              tft.setCursor(25, 100); tft.print("STATUS: SEND ERROR!");
              retries++;
              if(retries < 3) delay(2000); 
            }
          }
      }
      break;

    case SCREEN_MANUALALARM:
      if (btnPressed(BTN_MNL, lastDebounce_MNL)) {
        manualAlarmActive = !manualAlarmActive;
        digitalWrite(SSR_PIN, manualAlarmActive ? HIGH : LOW);
        updateManualAlarmDisplay();

        if (manualAlarmActive) {
          bool sirenSmsDelivered = false;
          int sRetries = 0;

          while (!sirenSmsDelivered && sRetries < 3) {
            alertIndicator = 1; // "alerting..."
            drawAlertIndicator(); 

            sirenSmsDelivered = sendSmsToAll("MANUAL SIREN ACTIVATED! Emergency in progress.");

            if (sirenSmsDelivered) {
              alertIndicator = 2; // "sent"
              drawAlertIndicator();
            } else {
              alertIndicator = 3; // "retry error!"
              drawAlertIndicator();
              sRetries++;
              if(sRetries < 3) delay(2000);
            }
          }
          alertShownAt = millis(); // Starts the timer to clear the "sent" text later
        }
      }
      break;
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
  
  drawHomeScreen();
}
