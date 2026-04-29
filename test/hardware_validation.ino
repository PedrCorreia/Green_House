#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
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

// 0.91" OLED displays usually have a resolution of 128x32
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1 // No reset pin
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Temperature Sensor
DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Hardware Validation Test Started ---");

  // 1. Initialize the I2C bus using the custom pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // 2. Initialize DHT22
  dht.begin();
  Serial.println("-> DHT22 Initialized on GPIO 4");

  // 3. Initialize the OLED Display (I2C Address: 0x3C)
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("-> FAILED: SSD1306 OLED allocation failed!"));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Screen OK");
    display.display();
    Serial.println("-> OLED Screen Initialized on I2C");
  }

  // 4. Initialize Photoresistor pin
  pinMode(LIGHT_PIN, INPUT);
  Serial.println(F("-> KY-018 Photoresistor initialized on GPIO 34"));

  Serial.println("----------------------------------------\n");
  delay(2000);
}

void loop() {
  // Read Temperature and Humidity
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  
  // Read Light Level (Analog)
  int light_raw = analogRead(LIGHT_PIN);
  
  // Calculate voltage/lux roughly like sensors.ino
  float voltage = light_raw * 3.3 / 4095.0;
  voltage = min(3.29f, voltage); 
  float resistance = 10000.0 * voltage / (3.3 - voltage);
  float lux = 5000000.0 / resistance;
  lux = min(65535.0f, max(lux, 0.0f));

  // Print results to the Serial Monitor
  Serial.print("DHT Temp: "); 
  if (isnan(temperature)) Serial.print("ERROR"); else Serial.print(temperature);
  Serial.print(" °C | DHT Hum: ");
  if (isnan(humidity)) Serial.print("ERROR"); else Serial.print(humidity);
  Serial.print(" % | Light: ");
  Serial.print((int)lux);
  Serial.println(" lx (approx)");

  // Update results on the OLED Display
  display.clearDisplay();
  display.setCursor(0, 0);
  
  if (isnan(humidity) || isnan(temperature)) {
    display.println("DHT: Error Reading!");
  } else {
    display.print("Temp: "); display.print(temperature); display.println(" C");
    display.print("Hum : "); display.print(humidity); display.println(" %");
  }
  
  display.print("Lux : "); display.print(lux); display.println(" lx");
  display.display();

  // Wait 2 seconds before the next reading
  delay(2000);
}
