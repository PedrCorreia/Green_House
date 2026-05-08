#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// --- LoRa Pins (Matching your Gateway) ---
#define LORA_UART_RX_PIN 16    // ESP32 GPIO16 -> RN2483 TX
#define LORA_UART_TX_PIN 17    // ESP32 GPIO17 -> RN2483 RX
#define RN2483_RST_PIN   2

// --- Display Pins ---
#define I2C_SDA 21
#define I2C_SCL 22

// --- LoRa Settings ---
#define LORA_BAUD_RATE   57600UL
#define LORA_FREQ        868100000UL
#define LORA_PWR         14
#define LORA_SF          "sf12"

// --- Hardware Objects ---
HardwareSerial loraSerial(2);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oled_connected = false;

// Variables to track last received data
String lastRxHex = "";
uint32_t lastId = 0;
float lastTemp = 0.0;
uint8_t lastHum = 0;
uint16_t lastLux = 0;
uint16_t lastSoil = 0;
unsigned long lastRxTime = 0;
int lastRssi = 0; // Signal strength (dBm)

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool hexToBytes(const String& hexStr, uint8_t* out, size_t outLen) {
  if (hexStr.length() != outLen * 2) return false;
  for (size_t i = 0; i < outLen; i++) {
    int hi = hexNibble(hexStr.charAt(i * 2));
    int lo = hexNibble(hexStr.charAt(i * 2 + 1));
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

void parsePayload(String hexStr) {
  if (hexStr.length() != 26) {
    Serial.println("Invalid payload length!");
    return;
  }

  uint8_t b[13];
  if (!hexToBytes(hexStr, b, sizeof(b))) {
    Serial.println("Invalid hex string!");
    return;
  }

  // Parse ID
  lastId = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
  
  // Parse Temp (tenths of C)
  int16_t tempRaw = (int16_t)((b[4] << 8) | b[5]);
  lastTemp = tempRaw / 10.0;

  // Parse Humidity
  lastHum = b[6];

  // Parse Lux
  lastLux = (uint16_t)((b[7] << 8) | b[8]);

  // Parse Soil
  lastSoil = (uint16_t)((b[9] << 8) | b[10]);

  lastRxTime = millis();
}

void updateUI() {
  if (!oled_connected) return;
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("TEST GATEWAY RX");
  display.drawLine(0, 10, 128, 10, SH110X_WHITE);

  if (lastRxTime == 0) {
    display.setCursor(0, 25);
    display.println("Waiting for LoRa...");
  } else {
    display.setCursor(0, 15);
    display.printf("Node ID: %08X", lastId);
    
    display.setCursor(0, 28);
    display.printf("T:%.1fC  H:%d%%", lastTemp, lastHum);
    
    display.setCursor(0, 41);
    display.printf("RSSI: %d dBm", lastRssi);

    // Time since last RX
    unsigned long secAgo = (millis() - lastRxTime) / 1000;
    display.setCursor(0, 54);
    display.printf("Last RX: %ds ago", secAgo);
  }
  
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Integration Test: Gateway RX ---");

  // OLED Init
  Wire.begin(I2C_SDA, I2C_SCL);
  if(!display.begin(0x3C, true) && !display.begin(0x3D, true)) {
    Serial.println("WARNING: OLED not found");
  } else {
    oled_connected = true;
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    updateUI();
  }

  // LoRa Init
  pinMode(RN2483_RST_PIN, OUTPUT);
  loraSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_UART_RX_PIN, LORA_UART_TX_PIN);
  loraSerial.setTimeout(1000);
  
  digitalWrite(RN2483_RST_PIN, LOW);
  delay(200);
  digitalWrite(RN2483_RST_PIN, HIGH);
  delay(500);

  while (loraSerial.available()) loraSerial.read();
  
  loraSerial.print("sys get ver\r\n");
  String ver = loraSerial.readStringUntil('\n');
  ver.trim();
  if (ver.length() > 0) {
    Serial.print("RN2483 Ready: ");
    Serial.println(ver);

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
    while (loraSerial.available()) {
       Serial.print((char)loraSerial.read());
    }
    
    // Put radio into continuous RX mode
    loraSerial.print("radio rx 0\r\n");
    Serial.println("Listening on 868.1 MHz for incoming payloads...");
  } else {
    Serial.println("Error: Failed to init LoRa module.");
  }
}

void loop() {
  // Check for incoming LoRa data
  if (loraSerial.available()) {
    String line = loraSerial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      // Debug: show raw RN2483 output so we can see unexpected responses
      Serial.print("RN2483 raw: ");
      Serial.println(line);
      if (line.startsWith("radio_rx ")) {
        // Handle both formats:
        //  - "radio_rx <payload_hex> <rssi>"
        //  - "radio_rx <payload_hex>"
        String payloadAndMaybeRssi = line.substring(9);
        payloadAndMaybeRssi.trim();
        int sp = payloadAndMaybeRssi.indexOf(' ');
        String hexStr;
        String rssiStr;
        if (sp == -1) {
          hexStr = payloadAndMaybeRssi;
          rssiStr = "";
        } else {
          hexStr = payloadAndMaybeRssi.substring(0, sp);
          rssiStr = payloadAndMaybeRssi.substring(sp + 1);
        }
        hexStr.trim();
        rssiStr.trim();
        if (rssiStr.length() > 0) {
          lastRssi = rssiStr.toInt(); // Parse RSSI value if present
        }
        lastRxHex = hexStr;
        Serial.println("\n[LoRa RX] " + hexStr + " [RSSI: " + (rssiStr.length() ? rssiStr : "n/a") + " dBm]");
        parsePayload(hexStr);
        
        Serial.printf("  => ID   : 0x%08X\n", lastId);
        Serial.printf("  => Temp : %.1f C\n", lastTemp);
        Serial.printf("  => Hum  : %d %%\n", lastHum);
        Serial.printf("  => Lux  : %d lx\n", lastLux);
        Serial.printf("  => Soil : %d\n", lastSoil);

        // Re-enter RX mode after receiving a packet
        loraSerial.print("radio rx 0\r\n"); 
      }
      else if (line == "radio_err") {
        // usually happens if RX mode interrupted or error
        loraSerial.print("radio rx 0\r\n"); 
      }
    }
  }

  // Update UI once a second
  static unsigned long lastUIRefresh = 0;
  if (millis() - lastUIRefresh > 1000) {
    lastUIRefresh = millis();
    updateUI();
  }
}