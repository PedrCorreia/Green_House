#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> // Using SH1106 library instead of SSD1306!

// --- Pin Definitions ---
#define I2C_SDA 21
#define I2C_SCL 22

// --- Hardware Objects ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64 // 0.96" screens are 128x64
#define OLED_RESET -1 // No reset pin
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool oled_connected = false;

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- OLED Display Isolation Test ---");
  Serial.println("Expected Pins: SDA = GPIO 21, SCL = GPIO 22");
  
  // 1. Initialize the I2C bus
  Wire.begin(I2C_SDA, I2C_SCL);

  // --- I2C Scanner ---
  Serial.println("\n-> Let's scan the I2C bus for the screen...");
  byte error, address;
  int nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("   -> Success! Found I2C device at address 0x");
      Serial.println(address, HEX);
      nDevices++;
    }
  }
  if (nDevices == 0) {
    Serial.println("\n   [CRITICAL WARNING] No I2C devices found!");
    Serial.println("   -> Your ESP32 cannot see the screen physically.");
    Serial.println("   -> Check if OLED has 3.3V and GND.");
    Serial.println("   -> Check if SDA(21) and SCL(22) are wired backwards.");
  } else {
    Serial.println("   (Standard 0.96\" OLEDs are usually 0x3C)\n");
  }

  // 2. Initialize OLED Display (Attempt 0x3C, then 0x3D)
  delay(250); // Small delay to let screen boot
  if(!display.begin(0x3C, true)) { // SH110X uses (address, reset)
    Serial.println(F("-> Connection to 0x3C failed. Attempting 0x3D..."));
    if(!display.begin(0x3D, true)) {
      Serial.println(F("-> [ERROR] Failed to start OLED on both addresses. Screen driver failed."));
      while (true) delay(100); // Stop here
    } else {
      Serial.println("-> OLED allocation successful on I2C address 0x3D");
      oled_connected = true;
    }
  } else {
    Serial.println("-> OLED allocation successful on I2C address 0x3C");
    oled_connected = true;
  }

  if(oled_connected) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println("OLED TEST: SUCCESS!");
    display.display();
  }
}

int counter = 0;

void loop() {
  if (oled_connected) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Testing Display...");
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.print("Count: ");
    display.print(counter);
    display.display();
    
    counter++;
  }
  delay(1000);
}
