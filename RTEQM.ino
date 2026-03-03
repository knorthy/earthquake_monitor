#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

// --- Pin Definitions ---
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

#define SSR_PIN 13  // <--- NEW: Connect the SSR (+) Control pin here

// --- Configuration ---
String recipientNumber = "+639XXXXXXXXX"; 
float dangerThreshold = 3.5;              
unsigned long screenTimeout = 30000; 

// --- System Variables ---
enum State { HOME, GRAPH_MODE };
State currentState = HOME;
unsigned long lastActivityTime = 0;
bool screenIsOn = true;
bool smsSentForCurrentEvent = false;

// --- SMS Status Variables ---
enum SMSStatus { IDLE, ALERTING, SENT };
SMSStatus currentSmsStatus = IDLE;
unsigned long statusTimer = 0;

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// --- Seismic Variables ---
int xPos = 6, prevY = 130;
float threshold = 2.2; 
float peakMag = 0.0, lastDuration = 0.0;
unsigned long startTime = 0, lastShakeTime = 0, eventEndTime = 0;
unsigned long minDuration = 1000, exitCooldown = 2000, alertHoldTime = 5000;
bool isVibrating = false, isConfirmedEarthquake = false, showingPostEvent = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);
  
  // --- NEW: SSR Setup ---
  pinMode(SSR_PIN, OUTPUT);
  digitalWrite(SSR_PIN, LOW); // Ensure alarm is OFF at startup

  pinMode(BTN_GRAPH, INPUT_PULLUP);
  pinMode(BTN_TXT,   INPUT_PULLUP);
  pinMode(BTN_MNL,   INPUT_PULLUP);
  pinMode(BTN_BCK,   INPUT_PULLUP);

  tft.begin();
  tft.setRotation(1); 
  if(!accel.begin()) while(1);
  accel.setRange(ADXL345_RANGE_2_G);

  lastActivityTime = millis();
  drawHomeScreen();
}

void loop() {
  manageSleep();
  checkNavigation();
  runSeismicLogic(); 
  updateSMSStatus(); 
}

void wakeScreen() {
  if (!screenIsOn) {
    digitalWrite(TFT_LED, HIGH);
    screenIsOn = true;
    if (currentState == HOME) drawHomeScreen();
    else setupGraphUI();
  }
  lastActivityTime = millis(); 
}

void manageSleep() {
  if (screenIsOn && (millis() - lastActivityTime > screenTimeout) && !isVibrating) {
    digitalWrite(TFT_LED, LOW);
    screenIsOn = false;
  }
}

void updateSMSStatus() {
  if (Serial2.available()) {
    String response = Serial2.readString();
    if (response.indexOf("OK") != -1 || response.indexOf("+CMGS") != -1) {
      if (currentSmsStatus == ALERTING) {
        currentSmsStatus = SENT;
        statusTimer = millis(); 
      }
    }
  }

  if (currentSmsStatus == SENT && (millis() - statusTimer > 3000)) {
    currentSmsStatus = IDLE;
    if (currentState == GRAPH_MODE) tft.fillRect(240, 5, 75, 20, ILI9341_BLACK); 
  }
}

void checkNavigation() {
  bool btnGraph = (digitalRead(BTN_GRAPH) == LOW);
  bool btnTxt   = (digitalRead(BTN_TXT) == LOW);
  bool btnMnl   = (digitalRead(BTN_MNL) == LOW);
  bool btnBck   = (digitalRead(BTN_BCK) == LOW);

  if (btnGraph || btnTxt || btnMnl || btnBck) {
    bool wasOff = !screenIsOn;
    wakeScreen();
    if (wasOff) { delay(300); return; } 
  }

  if (!screenIsOn) return; 

  if (btnBck) {
    if (currentState != HOME) { currentState = HOME; drawHomeScreen(); delay(300); }
  }

  if (currentState == HOME) {
    if (btnGraph) { currentState = GRAPH_MODE; setupGraphUI(); delay(300); }
    else if (btnTxt) { triggerManualSMS(); } 
    else if (btnMnl) { triggerManualAlarm(); }
  }
}

void runSeismicLogic() {
  sensors_event_t event;
  accel.getEvent(&event);
  float vibration = abs(sqrt(sq(event.acceleration.x) + sq(event.acceleration.y) + sq(event.acceleration.z)) - 9.81);
  unsigned long now = millis();

  if (vibration > threshold) {
    if (!isVibrating) { 
      isVibrating = true; 
      showingPostEvent = false; 
      startTime = now; 
      peakMag = 0; 
    }
    lastShakeTime = now; 
    if (vibration > peakMag) peakMag = vibration;

    if (now - startTime > minDuration) {
      if (!isConfirmedEarthquake) {
        isConfirmedEarthquake = true;
        wakeScreen(); 
        if(currentState != GRAPH_MODE) {
           currentState = GRAPH_MODE;
           setupGraphUI();
        }
      }
      
      // AUTO ALARM LOGIC: Turn on bell if intensity is dangerous
      if (peakMag > dangerThreshold) {
        digitalWrite(SSR_PIN, HIGH); // Bell ON
        if (!smsSentForCurrentEvent) {
          startSMSSending(peakMag);
          smsSentForCurrentEvent = true; 
        }
      }
    }
  } 

  if (isVibrating && (now - lastShakeTime > exitCooldown)) {
    isVibrating = false;
    digitalWrite(SSR_PIN, LOW); // Bell OFF when shaking stops
    smsSentForCurrentEvent = false; 
    lastDuration = (lastShakeTime - startTime) / 1000.0;
    eventEndTime = now;
    if (isConfirmedEarthquake) showingPostEvent = true;
    updateMetrics();
  }

  if (screenIsOn && currentState == GRAPH_MODE) {
    drawGraphElements(vibration);
  }
  delay(15); 
}

void drawGraphElements(float vibration) {
    tft.setTextSize(1);
    tft.setCursor(10, 190);
    
    if (isVibrating) {
      tft.setTextColor(isConfirmedEarthquake ? ILI9341_RED : ILI9341_BLUE, ILI9341_BLACK);
      tft.print(isConfirmedEarthquake ? "EARTHQUAKE IN PROGRESS!      " : "ANALYZING VIBRATION...       ");
    } else if (showingPostEvent) {
      if (millis() - eventEndTime < alertHoldTime) {
        tft.setTextColor(ILI9341_ORANGE, ILI9341_BLACK); tft.print("EARTHQUAKE HAS OCCURRED    ");
      } else { showingPostEvent = false; isConfirmedEarthquake = false; }
    } else {
      tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK); tft.print("STABLE / MONITORING...       ");
    }

    tft.setCursor(240, 12);
    if (currentSmsStatus == ALERTING) {
      tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
      tft.print("Alerting...");
    } else if (currentSmsStatus == SENT) {
      tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
      tft.print("Sent!      ");
    }

    int yGraph = constrain(map(vibration * 25, 0, 100, 175, 45), 45, 175);
    uint16_t color = isVibrating ? (isConfirmedEarthquake ? ILI9341_RED : ILI9341_BLUE) : ILI9341_GREEN;
    tft.drawLine(xPos, prevY, xPos + 1, yGraph, color);
    prevY = yGraph; xPos++;
    if (xPos >= 310) { xPos = 6; tft.fillRect(6, 41, 308, 138, ILI9341_BLACK); }
}

void startSMSSending(float mag) {
  currentSmsStatus = ALERTING;
  Serial2.println("AT+CMGF=1"); 
  delay(100); 
  Serial2.println("AT+CMGS=\"" + recipientNumber + "\""); 
  delay(100); 
  Serial2.print("EARTHQUAKE ALERT!\n");
  Serial2.print("Intensity: "); Serial2.print(mag, 2); Serial2.println(" m/s2");
  Serial2.print("Status: SHAKING DETECTED\n");
  Serial2.print("Stay safe!");
  Serial2.write(26); 
}

void drawHomeScreen() {
  tft.fillScreen(ILI9341_BLACK);
  tft.drawRect(0, 0, 320, 240, ILI9341_WHITE);
  tft.setCursor(25, 30); tft.setTextColor(ILI9341_CYAN); tft.setTextSize(2);
  tft.print("EARTHQUAKE MONITOR");
  drawButtonLabel(70, "1. VIEW LIVE GRAPH", ILI9341_GREEN);
  drawButtonLabel(110, "2. SEND TEXT ALERT", ILI9341_YELLOW);
  drawButtonLabel(150, "3. MANUAL ALERT", ILI9341_RED);
}

void drawButtonLabel(int y, String text, uint16_t color) {
  tft.drawRoundRect(40, y, 240, 30, 5, color);
  tft.setCursor(60, y + 8); tft.setTextColor(color); tft.print(text);
}

void setupGraphUI() {
  currentSmsStatus = IDLE; 
  tft.fillScreen(ILI9341_BLACK);
  tft.drawRect(0, 0, 320, 240, ILI9341_WHITE);
  tft.setCursor(10, 12); tft.setTextColor(ILI9341_CYAN); tft.setTextSize(2);
  tft.print("LIVE MONITORING");
  tft.drawRect(5, 40, 310, 140, ILI9341_WHITE); 
  tft.setTextSize(1); tft.setTextColor(ILI9341_YELLOW);
  tft.setCursor(10, 190); tft.print("SYSTEM STATUS:");
  tft.setCursor(10, 215); tft.print("MAX INTENSITY:");
  tft.setCursor(170, 215); tft.print("DURATION:");
  xPos = 6; 
}

void updateMetrics() {
  if (!screenIsOn || currentState != GRAPH_MODE) return;
  tft.fillRect(100, 213, 60, 12, ILI9341_BLACK);
  tft.setCursor(100, 215); tft.setTextColor(ILI9341_WHITE); tft.print(peakMag, 2); tft.print(" m/s2");
  tft.fillRect(230, 213, 60, 12, ILI9341_BLACK);
  tft.setCursor(230, 215); tft.print(lastDuration, 2); tft.print(" s");
}

void triggerManualSMS() {
  tft.fillScreen(ILI9341_BLUE);
  tft.drawRect(10, 10, 300, 220, ILI9341_WHITE);
  tft.setCursor(45, 80); tft.setTextColor(ILI9341_WHITE); tft.setTextSize(3);
  tft.println("SENDING ALERTS!");
  Serial2.println("AT+CMGF=1"); delay(100);
  Serial2.println("AT+CMGS=\"" + recipientNumber + "\""); delay(100);
  Serial2.print("EARTHQUAKE MONITOR: Manual Test OK.");
  Serial2.write(26); 
  delay(3000); 
  drawHomeScreen();
}

void triggerManualAlarm() {
  tft.fillScreen(ILI9341_RED);
  tft.drawRect(10, 10, 300, 220, ILI9341_WHITE);
  tft.setCursor(45, 60); tft.setTextColor(ILI9341_WHITE); tft.setTextSize(3);
  tft.println(" EMERGENCY!");
  
  digitalWrite(SSR_PIN, HIGH); // BELL ON
  delay(5000); 
  digitalWrite(SSR_PIN, LOW);  // BELL OFF
  
  drawHomeScreen();
}
