#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DHT.h>
#include <HardwareSerial.h>
#include <esp_sleep.h>

// --- Pin Definitions ---
// Display uses I2C bus
#define I2C_SDA 21
#define I2C_SCL 22

// Light Sensor (KY-018 Photoresistor)
#define LIGHT_PIN 34

// Temperature/Humidity Sensor (DHT22)
#define DHTPIN 4
#define DHTTYPE DHT22

// Soil Moisture Sensor (Analog Pin)
#define SOIL_PIN 35    

// LoRa Pins
#define LORA_UART_RX_PIN 16
#define LORA_UART_TX_PIN 17
#define RN2483_RST_PIN   14

// --- LoRa Settings ---
#define LORA_BAUD_RATE   57600UL
#define LORA_FREQ        868100000UL
#define LORA_PWR         14
#define LORA_SF          "sf12"
#define LORA_AFCBW       "41.7"
#define LORA_RXBW        125
#define LORA_PRLEN       8
#define LORA_CRC         "on"
#define LORA_IQI         "off"
#define LORA_CR          "4/5"
#define LORA_SYNC        "12"
#define LORA_BW          125
#define LORA_WDT         60000UL

// --- Transmitter & Payload Config ---
#define SENSOR_PAYLOAD_BYTES 13
uint32_t DEVICE_ID; // Global variable for hardware generated ID
HardwareSerial loraSerial(2);
bool loraReady = false;

// --- Hardware Objects ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 // No reset pin
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oled_connected = false;

// Temperature Sensor
DHT dht(DHTPIN, DHTTYPE);

// State variables for UI
unsigned long lastBlinkTime = 0;
bool blinkState = false;

const unsigned long SENSOR_INTERVAL_MS = 5000;
unsigned long lastReadTime = 0;

float currentTemp = 0.0;
float currentHum = 0.0;
int currentLux = 0;
int currentSoil = 0;

// ==========================================
// BITMAP GRAPHICS
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

// --- Data Structure ---
struct SensorReading {
  int16_t  tempTenthsC;
  uint8_t  humidity;
  uint16_t lux;
  uint16_t soilMoistureRaw;
  uint8_t  waterLeak;
  uint8_t  battery;
};

void buildSensorPayload(const SensorReading& r, uint8_t* out13) {
  out13[0] = (DEVICE_ID >> 24) & 0xFF;
  out13[1] = (DEVICE_ID >> 16) & 0xFF;
  out13[2] = (DEVICE_ID >> 8)  & 0xFF;
  out13[3] =  DEVICE_ID        & 0xFF;

  uint16_t t = (uint16_t)r.tempTenthsC;
  out13[4] = (t >> 8) & 0xFF;
  out13[5] =  t       & 0xFF;

  out13[6] = r.humidity;

  out13[7] = (r.lux >> 8) & 0xFF;
  out13[8] =  r.lux       & 0xFF;

  out13[9]  = (r.soilMoistureRaw >> 8) & 0xFF;
  out13[10] =  r.soilMoistureRaw       & 0xFF;

  out13[11] = r.waterLeak ? 1 : 0;
  out13[12] = r.battery;
}

String bytesToHex(const uint8_t* data, size_t len) {
  static const char hx[] = "0123456789ABCDEF";
  String s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    s += hx[(data[i] >> 4) & 0x0F];
    s += hx[data[i] & 0x0F];
  }
  return s;
}

void updateUI() {
  if (!oled_connected) return;

  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setCursor(0, 1);
  display.printf("ID:%08X", DEVICE_ID);
  
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
  display.drawRect(106, 0, 18, 9, SH110X_WHITE);
  display.fillRect(124, 2, 2, 5, SH110X_WHITE);
  display.fillRect(108, 2, 4, 5, SH110X_WHITE);
  display.fillRect(113, 2, 4, 5, SH110X_WHITE);
  display.fillRect(118, 2, 4, 5, SH110X_WHITE);
  
  display.drawLine(0, 12, 128, 12, SH110X_WHITE);

  // Data section
  if (isnan(currentHum) || isnan(currentTemp)) {
    display.setCursor(0, 25);
    display.println("DHT: Error Reading!");
  } else {
    // Top Left: Temperature
    display.drawBitmap(2, 16, icon_thermo, 16, 16, SH110X_WHITE);
    display.setCursor(20, 18);
    display.setTextSize(2); 
    display.print((int)currentTemp);
    display.setTextSize(1);
    display.print("C ");
    
    // Top Right: Humidity
    display.setCursor(70, 18);
    display.setTextSize(2);
    display.print((int)currentHum);
    display.setTextSize(1);
    display.print("%");
  }
  
  // Soil moisture info slightly below temperature
  display.setCursor(2, 36);
  display.print("Soil:");
  display.print(currentSoil);
  
  // TX Countdown
  unsigned long elapsed = millis() - lastReadTime;
  int secLeft = (elapsed > SENSOR_INTERVAL_MS) ? 0 : (SENSOR_INTERVAL_MS - elapsed) / 1000;
  display.setCursor(68, 36);
  display.printf("TX In: %ds", secLeft);
  
  // Footer
  display.drawLine(0, 48, 128, 48, SH110X_WHITE);
  display.drawBitmap(0, 49, icon_sun, 16, 16, SH110X_WHITE);
  display.setCursor(20, 54);
  display.print("Light: "); 
  display.print(currentLux); 
  display.println(" lx");

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Generate Hardware-based ID (Sensor Node -> First byte 0x01)
  uint64_t mac = ESP.getEfuseMac();
  // Assemble: 0x01 + lower 3 bytes of MAC 
  DEVICE_ID = 0x01000000 | ((mac >> 24) & 0x00FFFFFF);
  
  Serial.println("\n--- Integration Test: Sensors + LoRa ---");
  Serial.printf("Generated LoRa Device ID: 0x%08X\n", DEVICE_ID);

  Wire.begin(I2C_SDA, I2C_SCL);
  dht.begin();
  
  pinMode(LIGHT_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);

  // Initialize LoRa Hardware
  pinMode(RN2483_RST_PIN, OUTPUT);
  digitalWrite(RN2483_RST_PIN, HIGH);
  loraSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_UART_RX_PIN, LORA_UART_TX_PIN);
  loraSerial.setTimeout(2000);
  
  // Hard reset RN2483
  digitalWrite(RN2483_RST_PIN, LOW);
  delay(200);
  digitalWrite(RN2483_RST_PIN, HIGH);
  delay(500);

  // Simple initialization logic for integration test
  while (loraSerial.available()) loraSerial.read();
  loraSerial.print("sys get ver\r\n");
  delay(100);
  String ver = loraSerial.readStringUntil('\n');
  ver.trim();
  if (ver.length() > 0) {
    Serial.print("RN2483 Ready: ");
    Serial.println(ver);
    loraReady = true;

    // Apply LoRa configs
    loraSerial.print("mac pause\r\n"); delay(50);
    loraSerial.print("radio set mod lora\r\n"); delay(50);
    loraSerial.print("radio set freq 868100000\r\n"); delay(50);
    loraSerial.print("radio set pwr 14\r\n"); delay(50);
    loraSerial.print("radio set sf sf12\r\n"); delay(50);
    loraSerial.print("radio set afcbw 41.7\r\n"); delay(50);
    loraSerial.print("radio set rxbw 125\r\n"); delay(50);
    loraSerial.print("radio set prlen 8\r\n"); delay(50);
    loraSerial.print("radio set crc on\r\n"); delay(50);
    loraSerial.print("radio set iqi off\r\n"); delay(50);
    loraSerial.print("radio set cr 4/5\r\n"); delay(50);
    loraSerial.print("radio set sync 12\r\n"); delay(50);
    loraSerial.print("radio set bw 125\r\n"); delay(50);
    while(loraSerial.available()) {
      Serial.print((char)loraSerial.read()); // read and print the OKs out
    }
  } else {
    Serial.println("WARNING: LoRa RN2483 Module not responding.");
  }

  if(!display.begin(0x3C, true) && !display.begin(0x3D, true)) {
    Serial.println("WARNING: OLED not found");
  } else {
    oled_connected = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("Sensors Init OK");
    display.display();
  }
  
  Serial.println("Setup Complete. Starting sensor loop...");
}

void loop() {
  unsigned long currentMillis = millis();
  bool needsUIUpdate = false;

  // Blink logic & countdown refresh
  if (currentMillis - lastBlinkTime >= 500) {
    lastBlinkTime = currentMillis;
    blinkState = !blinkState;
    needsUIUpdate = true;
  }

  // Sample every SENSOR_INTERVAL_MS
  if (currentMillis - lastReadTime >= SENSOR_INTERVAL_MS) {
    lastReadTime = currentMillis;

    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();
    
    int light_raw = analogRead(LIGHT_PIN);
    float voltage = min(3.29f, (float)(light_raw * 3.3 / 4095.0)); 
    float resistance = 10000.0f * voltage / (3.3f - voltage);
    float lux = min(65535.0f, max((float)(5000000.0f / resistance), 0.0f));

    int soil_raw = analogRead(SOIL_PIN);

    // Store in globals for UI logic
    currentTemp = temperature;
    currentHum = humidity;
    currentLux = (int)lux;
    currentSoil = soil_raw;

    SensorReading reading;
    reading.tempTenthsC = isnan(temperature) ? 0 : (int16_t)(temperature * 10);
    reading.humidity = isnan(humidity) ? 0 : (uint8_t)humidity;
    reading.lux = (uint16_t)lux;
    reading.soilMoistureRaw = (uint16_t)soil_raw;
    reading.waterLeak = 0; 
    reading.battery = 100; 

    uint8_t payload[SENSOR_PAYLOAD_BYTES];
    buildSensorPayload(reading, payload);
    String payloadHex = bytesToHex(payload, SENSOR_PAYLOAD_BYTES);

    Serial.printf("Temp: %.1f C | Hum: %.1f %% | Light: %d lx | Soil: %d\n", 
                  temperature, humidity, (int)lux, soil_raw);
    Serial.print("Payload HEX: ");
    Serial.println(payloadHex);

    // TX via LoRa
    if (loraReady) {
      Serial.println("Transmitting via LoRa...");
      loraSerial.print("radio tx " + payloadHex + "\r\n");
      
      // Wait briefly for LoRa module to confirm transmission started
      delay(200); 
      while (loraSerial.available()) {
        String response = loraSerial.readStringUntil('\n');
        response.trim();
        Serial.println("RN2483: " + response);
      }
      
      // Note: non-blocking test implementation
    }
    Serial.println("--------------------------------------------------");

    needsUIUpdate = true;
  }

  // Refresh UI explicitly
  if (needsUIUpdate) {
    updateUI();
  }
}

