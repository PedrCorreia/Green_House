#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DHT.h>

// --- Pin Definitions based on PCB Documentation ---

// Display uses I2C bus
#define I2C_SDA 21
#define I2C_SCL 22

// Light Sensor (KY-018 Photoresistor)
#define LIGHT_PIN 34

// Temperature/Humidity Sensor (DHT22)
#define DHTPIN 4
#define DHTTYPE DHT22

// --- Hardware Objects ---

// 0.96" OLED displays resolution
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 // No reset pin
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oled_connected = false;

// Temperature Sensor
DHT dht(DHTPIN, DHTTYPE);

// --- Add these global variables near the top of your file ---
unsigned long lastSensorReadTime = 0;
const unsigned long sensorInterval = 2000; // Read every 2 seconds

// Variables to track state and only update screen when needed
float lastTemp = -999.0;
float lastHum = -999.0;
float lastLux = -999.0;

unsigned long lastBlinkTime = 0;
bool blinkState = false;

// ==========================================
// BITMAP GRAPHICS (Generated from image2cpp)
// ==========================================
// 16x16 Thermometer Icon
const unsigned char icon_thermo [] PROGMEM = {
  0x01, 0x80, 0x02, 0x40, 0x02, 0x40, 0x02, 0x40, 0x02, 0x50, 0x02, 0x50, 0x02, 0x50, 0x02, 0x50, 
  0x02, 0x50, 0x04, 0x20, 0x0c, 0x30, 0x0c, 0x30, 0x0c, 0x30, 0x0e, 0x70, 0x07, 0xe0, 0x03, 0xc0
};
// 16x16 Sun Icon
const unsigned char icon_sun [] PROGMEM = {
  0x01, 0x80, 0x00, 0x00, 0x20, 0x04, 0x11, 0x88, 0x03, 0xc0, 0x07, 0xe0, 0x47, 0xe2, 0x4f, 0xf2, 
  0x4f, 0xf2, 0x47, 0xe2, 0x07, 0xe0, 0x03, 0xc0, 0x11, 0x88, 0x20, 0x04, 0x00, 0x00, 0x01, 0x80
};

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Hardware Validation Test Started ---");

  // 1. Initialize the I2C bus using the custom pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // --- I2C Scanner to debug OLED ---
  Serial.println("-> Scanning I2C bus for devices...");
  byte error, address;
  int nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("   Found I2C device at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      nDevices++;
    }
  }
  if (nDevices == 0) Serial.println("   WARNING: No I2C devices found! Check OLED wiring, power and 3V3 rail.");

  // 2. Initialize DHT22
  dht.begin();
  Serial.println("-> DHT22 Initialized on GPIO 4");

  // 3. Initialize the OLED Display (Attempt 0x3C, then 0x3D)
  delay(250); // Small delay to let screen boot
  if(!display.begin(0x3C, true)) { // SH110X uses (address, reset)
    Serial.println(F("-> FAILED at 0x3C. Trying address 0x3D..."));
    if(!display.begin(0x3D, true)) {
      Serial.println(F("-> FAILED: SH1106 OLED allocation failed completely! Check SDA (21) / SCL (22) traces."));
    } else {
      Serial.println("-> OLED Screen Initialized on I2C address 0x3D");
      oled_connected = true;
      display.clearDisplay(); display.display();
    }
  } else {
    oled_connected = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("Screen OK");
    display.display();
    Serial.println("-> OLED Screen Initialized on I2C address 0x3C");
  }

  // 4. Initialize Photoresistor pin
  pinMode(LIGHT_PIN, INPUT);
  Serial.println(F("-> KY-018 Photoresistor initialized on GPIO 34"));

  Serial.println("----------------------------------------\n");
  delay(2000);
}

void loop() {
  unsigned long currentMillis = millis();
  bool needsUIUpdate = false;

  // 1. Blink Timer (Runs every 500ms)
  if (currentMillis - lastBlinkTime >= 500) {
    lastBlinkTime = currentMillis;
    blinkState = !blinkState;
    if (lastTemp != -999.0) { // Don't blink before our first sensor read completes
      needsUIUpdate = true;
    }
  }

  // 2. Non-blocking sensor read (runs every 2 seconds)
  if (currentMillis - lastSensorReadTime >= sensorInterval) {
    lastSensorReadTime = currentMillis;
    
    // Read sensors
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    
    int light_raw = analogRead(LIGHT_PIN);
    float voltage = min(3.29f, (float)(light_raw * 3.3 / 4095.0)); 
    float resistance = 10000.0f * voltage / (3.3f - voltage);
    float lux = min(65535.0f, max((float)(5000000.0f / resistance), 0.0f));

    // Print to serial
    Serial.printf("Temp: %.1f C | Hum: %.1f %% | Light: %d lx\n", temperature, humidity, (int)lux);

    // React to sensor changes
    if (abs(temperature - lastTemp) > 0.1 || abs(humidity - lastHum) > 0.1 || abs(lux - lastLux) > 5.0) {
      lastTemp = temperature;
      lastHum = humidity;
      lastLux = lux;
      needsUIUpdate = true;
    }
  }

  // 3. Centralized UI Update execution
  if (needsUIUpdate && oled_connected) {
    updateUI(lastTemp, lastHum, lastLux);
  }

  // 4. You can put other reactive code here!
  // E.g., checking a physical button to switch UI screens:
  // if (digitalRead(BUTTON_PIN) == LOW) { switchScreen(); }
}

void updateUI(float temp, float hum, float lux) {
  if (!oled_connected) return;

  display.clearDisplay();
  
  // -- UI Layout Design --
  
  // Header
  display.setTextSize(1);
  display.setCursor(0, 1);
  display.print("GREENHOUSE");
  
  // W (WiFi) Blinking Status
  display.setCursor(68, 1);
  display.print("W");
  if (blinkState) display.fillCircle(78, 4, 3, SH110X_WHITE);
  else display.drawCircle(78, 4, 3, SH110X_WHITE);
  
  // L (LoRa) Blinking Status
  display.setCursor(85, 1);
  display.print("L");
  if (blinkState) display.fillCircle(95, 4, 3, SH110X_WHITE);
  else display.drawCircle(95, 4, 3, SH110X_WHITE);

  // Battery Icon 
  display.drawRect(106, 0, 18, 9, SH110X_WHITE);  // Main body
  display.fillRect(124, 2, 2, 5, SH110X_WHITE);   // Positive terminal
  display.fillRect(108, 2, 4, 5, SH110X_WHITE);   // Bar 1
  display.fillRect(113, 2, 4, 5, SH110X_WHITE);   // Bar 2
  display.fillRect(118, 2, 4, 5, SH110X_WHITE);   // Bar 3
  
  display.drawLine(0, 12, 128, 12, SH110X_WHITE); // Divider line

  // Data section
  if (isnan(hum) || isnan(temp)) {
    display.setCursor(0, 25);
    display.println("DHT: Error Reading!");
  } else {
    // Top Left: Temperature (Draw icon, then text)
    display.drawBitmap(2, 16, icon_thermo, 16, 16, SH110X_WHITE);
    display.setCursor(22, 18);
    display.setTextSize(2); 
    display.print((int)temp);
    display.setTextSize(1);
    display.print(" C");

    // Top Right: Humidity
    display.setCursor(80, 18);
    display.setTextSize(2);
    display.print((int)hum);
    display.setTextSize(1);
    display.print(" %");
  }
  
  // Footer
  display.drawLine(0, 48, 128, 48, SH110X_WHITE);
  display.drawBitmap(0, 49, icon_sun, 16, 16, SH110X_WHITE);
  display.setCursor(20, 54);
  display.print("Light: "); 
  display.print((int)lux); 
  display.println(" lx");

  // Push buffer to the screen
  display.display();
}
