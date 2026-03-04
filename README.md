
---

#  Real-Time Earthquake Monitoring & Alert System (RTEQM)

An IoT-based seismic monitoring station built with the ESP32. This system detects earthquakes in real-time, displays a live seismograph on a TFT screen, and triggers a multi-channel alert system (220V physical siren, SMS blasts, and Blynk mobile push notifications). It also features an automated drill simulation system synchronized via Network Time Protocol (NTP).

##  Key Features

* **Live Seismic Graph**: Real-time plotting of X, Y, and Z acceleration vectors using an ADXL345 sensor and a 2.8" ILI9341 TFT display.
* **Automated Emergency Alerts**: Triggers a 220V AC physical siren and sends SMS blasts to multiple contacts concurrently via a SIM800L module.
* **Non-Blocking Logic**: Custom state-machine programming ensures the UI and sensor readings never freeze, even while the GSM module is processing SMS tasks.
* **NTP Drill Simulation**: Schedule emergency drills directly from the Blynk mobile app. The ESP32 syncs with global time servers, automatically triggering and stopping the drill without needing a physical RTC module.
* **Remote IoT Management**: Fully integrated with the Blynk IoT platform for remote monitoring, manual siren overrides, and sensitivity adjustments.

---

##  Materials Used

* **Microcontroller**: ESP32 Development Board (30-pin)
* **Sensor**: Adafruit ADXL345 Accelerometer (I2C)
* **Display**: 2.8" ILI9341 TFT LCD Screen (SPI)
* **GSM Module**: SIM800L (2G Quad-band)
* **Relay**: 25A Solid State Relay (SSR) (AC Output, DC Input)
* **Alarm**: 220V AC Industrial Siren / Bell
* **Inputs**: 4x Push Buttons (for local UI navigation)
* **Power**: 5V/2A DC Power Supply (ESP32/SIM800L) & 220V AC Mains (Siren)

---

##  Hardware Wiring & Connections

### Display & Sensors

| Component | Pin Label | ESP32 Pin | Notes |
| --- | --- | --- | --- |
| **ADXL345** | SDA / SCL | **D21 / D22** | Hardware I2C |
| **TFT Screen** | CS | **D15** | Chip Select |
|  | DC / RS | **D2** | Data/Command |
|  | RESET | **D4** | Display Reset |
|  | LED | **D32** *(or 3.3V)* | Backlight control |
|  | MOSI / SCK | **D23 / D18** | Hardware SPI |
| **Push Buttons** | GRAPH / TXT | **D27 / D26** | Connect to GND (INPUT_PULLUP) |
|  | MNL / BCK | **D25 / D33** | Connect to GND (INPUT_PULLUP) |

### Communication & High Voltage

| Component | Pin Label | ESP32 Pin | Notes |
| --- | --- | --- | --- |
| **SIM800L** | TXD | **D16 (RX2)** | Hardware Serial 2 |
|  | RXD | **D17 (TX2)** | Hardware Serial 2 |
| **SSR (220V)** | DC+ | **D13** | Controls the Siren |
|  | DC- | **GND** | System Ground |

> **⚠️ SAFETY WARNING:** The 220V AC circuit is completely isolated from the ESP32 logic via the Solid State Relay. Never connect 220V mains power directly to the ESP32. Ensure the SSR is rated for AC output (e.g., 24-380V AC).

---

## 📚 Libraries Required

You will need to install the following libraries via the Arduino IDE Library Manager:

1. **Blynk** (by Volodymyr Shymanskyy)
2. **Adafruit ILI9341** (by Adafruit)
3. **Adafruit GFX Library** (by Adafruit)
4. **Adafruit ADXL345** (by Adafruit)
5. **Adafruit Unified Sensor** (by Adafruit)
6. **Time** (by Michael Margolis) - *Required for NTP Drill Logic*

*(Note: `WidgetRTC.h`, `WiFi.h`, `Wire.h`, and `SPI.h` are built-in or included with the Blynk installation).*

---

##  Blynk App Configuration

To replicate the mobile dashboard, configure your Datastreams and Widgets as follows:

* **V0 (Gauge)**: Live Vibration Output (Float)
* **V1 (Value Display)**: Peak Magnitude (Float)
* **V2 (Value Display)**: Shake Duration in seconds (Float)
* **V3 (Switch)**: Manual Siren Toggle (Integer 0/1)
* **V4 (Button)**: Remote SMS Blast Trigger (Integer 0/1)
* **V5 (Time Input)**: Drill Simulation Scheduler (String/Time)

---

##  Setup Instructions

1. Clone this repository to your local machine.
2. Open the `.ino` file in the Arduino IDE.
3. Replace the placeholder values with your specific credentials:
```cpp
#define BLYNK_AUTH_TOKEN "Your_Blynk_Token"
char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

```


4. Update the `recipientNumbers` array with valid mobile numbers (ensure your SIM800L has load/balance and the PIN lock is disabled).
5. Compile and upload to your ESP32.

---

Would you like me to help you set up a `.gitignore` file next so you don't accidentally publish your WiFi password and Blynk token when you push this to GitHub?
