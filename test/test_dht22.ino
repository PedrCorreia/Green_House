#include <DHT.h>

// Temperature/Humidity Sensor (DHT22)
#define DHTPIN 4
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- DHT22 Isolation Test ---");
  Serial.println("Expected Pin: GPIO 4");
  Serial.println("Ensure a 4.7k ohm resistor connects 3.3V and GPIO 4 (Data line pull-up).");
  
  dht.begin();
  Serial.println("Sensor initialized. Waiting 2 seconds before first read...\n");
  delay(2000);
}

void loop() {
  // Read Temperature and Humidity
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  Serial.print("Status: ");
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("ERROR! Could not read from DHT sensor.");
    Serial.println("  -> Check power (3.3V) and Ground.");
    Serial.println("  -> Check if Data pin is securely in GPIO 4.");
    Serial.println("  -> Check if the 4.7k pull-up resistor is present.");
  } else {
    Serial.print("SUCCESS! | ");
    Serial.print("Temp: ");
    Serial.print(temperature);
    Serial.print(" °C | Hum: ");
    Serial.print(humidity);
    Serial.println(" %");
  }

  // DHT22 requires at least 2 seconds between reads
  delay(2500);
}
