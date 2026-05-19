#include <DHT.h>

#define DHTTYPE DHT22

// This code is for ESP32.

// the DATA packet.
uint8_t packet_data[10];

// The Configurations for DHT22.
const int DHTPin = 27;
// Initialize DHT sensor.
DHT dht(DHTPin, DHTTYPE);

// The Configurations for Capacitive Soil Moisture Sensor.
const int soil_moisture_pin = 32;

// These 2 parameters need to be validated. Maybe 0-4095?
const int complete_dry_value = 2750;
const int complete_wet_value = 1100;

int soil_moisture_sensor_value = 0;
float soil_moisture = 0;

// The Configurations for the photoresistor (KY-018)
const int photoresistor_pin = 33;
int light_intensity_raw = 0;

// The Configurations for the water leakage sensor.
const int leakage_sensor_pin = 35;

void setup() {
  // put your setup code here, to run once:
  packet_data[0] = 0x01;     // message type: data.j
  Serial.begin(115200);
  dht.begin();
  pinMode(leakage_sensor_pin, INPUT);
}

void loop() {
  // put your main code here, to run repeatedly:

  // DHT22 part.
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  uint16_t humidity_final = (uint16_t)round(humidity*10);
  uint16_t temperature_final = (uint16_t)round(temperature*10);

  // load it into the data packet.
  packet_data[1] = (temperature_final >> 8) & 0xFF;
  packet_data[2] = temperature_final & 0xFF;
  packet_data[3] = (humidity_final >> 8) & 0xFF;
  packet_data[4] = humidity_final & 0xFF;

  Serial.printf("Temperature: %.2f Celsius Degrees, Humidity: %.2f%%\n", temperature, humidity);

  delay(1000);

  // Capacitive Soil Moisture Sensor part.
  soil_moisture_sensor_value = analogRead(soil_moisture_pin);
  Serial.printf("Original sensor value of soil moisture: %d\n", soil_moisture_sensor_value);
  soil_moisture = (float)(complete_dry_value - soil_moisture_sensor_value) / (complete_dry_value - complete_wet_value) * 100.0;
  soil_moisture = min(100.0f, max(0.0f, soil_moisture));
  uint16_t soil_moisture_final = (uint16_t)round(soil_moisture * 10);

  // load the soil moisture data into the data packet.
  packet_data[5] = (soil_moisture_final >> 8) & 0xFF;
  packet_data[6] = soil_moisture_final & 0xFF; 

  Serial.printf("Soil moisture: %.2f%%\n", soil_moisture);

  delay(1000);
  
  // Photoresistor (KY-018 part)
  light_intensity_raw = analogRead(photoresistor_pin);
  float voltage = light_intensity_raw * 3.3 / 4095.0;
  voltage = min(3.29f, voltage);    // preventing from exceeding the range.
  float resistance = 10000.0 * voltage / (3.3 - voltage);
  float lux = 5000000.0 / resistance;
  lux = min(65535.0f, max(lux, 0.0f));
  uint16_t light_intensity_lux = (uint16_t)lux;
  packet_data[7] = (light_intensity_lux >> 8) & 0xFF;
  packet_data[8] = light_intensity_lux & 0xFF;

  Serial.printf("Light Intensity: %d Lux\n", light_intensity_lux);

  delay(1000);

  // Water Leakage sensor part (HW-038)
  int water_leakage_raw = analogRead(leakage_sensor_pin);
  Serial.printf("Water Leakage Raw: %d\n", water_leakage_raw);
  bool isLeakage = (water_leakage_raw > 1000) ? true : false;

  // clear the previous data of water leakage (LSB) every time.
  packet_data[9] &= 0xFE;
  if (isLeakage) {
    packet_data[9] |= 0x01;
    Serial.println("Water Leakage Detected!");
  } else {
    Serial.println("Safe! No water leakage!");
  }

  // print the sent package
  Serial.print("The sent package: ");
  for (int i = 0; i < 10; i++){
    Serial.printf("%d ", packet_data[i]);
  }
  Serial.print("\n");

  delay(12000);
}
