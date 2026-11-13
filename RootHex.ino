#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- PIN DEFINITIONS ----------------
#define PIN_RELAY        25   // Relay IN pin (Controls 5V Water Pump)
#define PIN_SOIL         32   // SparkFun Soil Moisture Sensor (Analog)
#define PIN_LDR          33   // LDR Module Digital Output (do)
#define PIN_WATER_LEVEL  34   // Water Level Sensor (Analog, Input-only GPIO)
#define PIN_OLED_SDA     21   // OLED I2C Data
#define PIN_OLED_SCL     22   // OLED I2C Clock (SCK)

// ---------------- OLED SETTINGS ------------------
#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT    64
#define OLED_RESET       -1
#define SCREEN_ADDRESS   0x3C // Standard I2C address for 128x64 OLED

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- HARDWARE LOGIC CONFIG ----------
// Many 5V relay modules are Active-LOW. Swap HIGH/LOW if your relay works in reverse.
#define RELAY_ON         HIGH
#define RELAY_OFF        LOW

// Standard LDR comparator modules output LOW when bright and HIGH when dark.
#define LDR_BRIGHT_STATE LOW

// ---------------- CALIBRATION & THRESHOLDS -------
// SparkFun conductive soil sensor: 0 = completely dry, ~3500 = submerged in water
const int SOIL_ADC_DRY          = 0;
const int SOIL_ADC_WET          = 3500;

// Water level sensor ADC range (12-bit: 0 to 4095)
const int WATER_ADC_EMPTY       = 0;
const int WATER_ADC_FULL        = 3000;

// Automation thresholds (in percentage)
const int SOIL_DRY_TRIGGER_PCT  = 30;  // Turn pump ON below 30% moisture
const int SOIL_WET_STOP_PCT     = 65;  // Turn pump OFF above 65% moisture
const int MIN_WATER_TANK_PCT    = 15;  // Cutoff pump if reservoir falls below 15%

// ---------------- STATE VARIABLES ----------------
int soilRaw = 0;
int soilPercent = 0;
int waterRaw = 0;
int waterPercent = 0;
bool isBright = false;
bool pumpActive = false;
bool autoMode = true;       // True = Automatic watering, False = Manual app override
bool oledAvailable = false;

unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL_MS = 1000; // Update OLED & Serial every 1 second

// ---------------- HELPER FUNCTIONS ---------------
void setPump(bool turnOn) {
  pumpActive = turnOn;
  digitalWrite(PIN_RELAY, turnOn ? RELAY_ON : RELAY_OFF);
}

void readSensors() {
  // 1. Read Soil Moisture (Analog 12-bit: 0 - 4095)
  soilRaw = analogRead(PIN_SOIL);
  soilPercent = map(soilRaw, SOIL_ADC_DRY, SOIL_ADC_WET, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

  // 2. Read Reservoir Water Level (Analog 12-bit: 0 - 4095)
  waterRaw = analogRead(PIN_WATER_LEVEL);
  waterPercent = map(waterRaw, WATER_ADC_EMPTY, WATER_ADC_FULL, 0, 100);
  waterPercent = constrain(waterPercent, 0, 100);

  // 3. Read LDR Digital Output
  int ldrState = digitalRead(PIN_LDR);
  isBright = (ldrState == LDR_BRIGHT_STATE);
}

void evaluatePumpControl() {
  // Safety Cutoff: Never allow the pump to run dry if the tank is empty
  if (waterPercent < MIN_WATER_TANK_PCT) {
    setPump(false);
    return;
  }

  // Automatic Irrigation Logic (with hysteresis so the relay doesn'trapidly click)
  if (autoMode) {
    if (!pumpActive && soilPercent < SOIL_DRY_TRIGGER_PCT) {
      setPump(true);
    } else if (pumpActive && soilPercent >= SOIL_WET_STOP_PCT) {
      setPump(false);
    }
  }
}

void updateOLED() {
  if (!oledAvailable) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header Bar
  display.setCursor(0, 0);
  display.println(F("ATP IIOT PLANT CTRL"));
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Soil Moisture Row
  display.setCursor(0, 15);
  display.print(F("Soil Moist : "));
  display.print(soilPercent);
  display.println(F("%"));

  // Water Tank Level Row
  display.setCursor(0, 27);
  display.print(F("Water Tank : "));
  display.print(waterPercent);
  display.print(F("%"));
  if (waterPercent < MIN_WATER_TANK_PCT) {
    display.print(F(" LOW!"));
  }

  // Light Status Row
  display.setCursor(0, 39);
  display.print(F("Light Lvl  : "));
  display.println(isBright ? F("BRIGHT") : F("DARK"));

  // Pump & Mode Status Row
  display.drawLine(0, 50, 127, 50, SSD1306_WHITE);
  display.setCursor(0, 54);
  display.print(F("Pump: "));
  display.print(pumpActive ? F("ON ") : F("OFF"));
  display.print(F(" | "));
  display.print(autoMode ? F("AUTO") : F("MAN"));

  display.display();
}

void sendSerialData() {
  // Outputs single-line JSON for easy parsing by your app
  Serial.print(F("{\"soil_raw\":"));
  Serial.print(soilRaw);
  Serial.print(F(",\"soil_pct\":"));
  Serial.print(soilPercent);
  Serial.print(F(",\"water_raw\":"));
  Serial.print(waterRaw);
  Serial.print(F(",\"water_pct\":"));
  Serial.print(waterPercent);
  Serial.print(F(",\"light\":\""));
  Serial.print(isBright ? F("BRIGHT") : F("DARK"));
  Serial.print(F("\",\"pump\":\""));
  Serial.print(pumpActive ? F("ON") : F("OFF"));
  Serial.print(F("\",\"mode\":\""));
  Serial.print(autoMode ? F("AUTO") : F("MANUAL"));
  Serial.print(F("\",\"low_water_alert\":"));
  Serial.print(waterPercent < MIN_WATER_TANK_PCT ? F("true") : F("false"));
  Serial.println(F("}"));
}

void handleSerialCommands() {
  // Optional: Allows your app to control the ESP32 via Serial commands
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "PUMP:ON") {
      autoMode = false;
      if (waterPercent >= MIN_WATER_TANK_PCT) setPump(true);
    } else if (cmd == "PUMP:OFF") {
      autoMode = false;
      setPump(false);
    } else if (cmd == "MODE:AUTO") {
      autoMode = true;
    }
  }
}

// ---------------- MAIN SETUP & LOOP --------------
void setup() {
  Serial.begin(115200);

  // Configure GPIO Pins
  pinMode(PIN_RELAY, OUTPUT);
  setPump(false); // Ensure pump starts OFF

  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_SOIL, INPUT);
  pinMode(PIN_WATER_LEVEL, INPUT); // GPIO34 is input-only

  // Set ADC resolution to 12-bit (0 - 4095)
  analogReadResolution(12);

  // Initialize I2C on GPIO21 (SDA) and GPIO22 (SCL)
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  // Initialize OLED Display
  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 25);
    display.println(F("System Booting..."));
    display.display();
    delay(1000);
  } else {
    Serial.println(F("{\"error\":\"OLED initialization failed at 0x3C\"}"));
  }
}

void loop() {
  handleSerialCommands();

  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = currentMillis;

    readSensors();
    evaluatePumpControl();
    updateOLED();
    sendSerialData();
  }
}