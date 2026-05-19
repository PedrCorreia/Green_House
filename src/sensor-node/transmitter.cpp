/*

  Board: ESP32 (DevKit / WROOM) with RN2483 LoRa module
  Role:  Sensor node. Wakes from deep sleep, listens for the gateway's PING,
         transmits a 13-byte sensor payload, listens briefly for a light
         command, then goes back to sleep.

  Important: ESP32 cannot listen on the radio while it is in deep sleep —
  deep sleep powers down the CPU. So the cycle is:
    wake -> RX ping -> TX data -> RX command (short window) -> deep sleep.


  Test:
    1. Flash, open Serial Monitor at 115200.
    2. Have the gateway running. Within PING_WAIT_MS the node should
       receive the ping, transmit a real sensor reading, and either turn its
       LED on or stay dark depending on the lux value.
    3. Then it sleeps for the gateway-provided RTC sleep duration and the cycle repeats.


*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <DHT.h>
#include <esp_sleep.h>

// ---------- Pins ----------
#define LED_PIN          2
// ---------------------- PINS UPDATED we are using pedros setup for easier use of PCB but update on everyone elses
#define LORA_UART_RX_PIN 16    // ESP32 GPIO16 -> RN2483 TX
#define LORA_UART_TX_PIN 17    // ESP32 GPIO17 -> RN2483 RX

#define RN2483_RST_PIN   14
#define I2C_SDA          21
#define I2C_SCL          22

// ---------- Sensor pins ----------
#define LIGHT_PIN        34    // KY-018/photoresistor analog output
#define DHTPIN           4
#define DHTTYPE          DHT22
#define SOIL_PIN         35    

// ---------- LoRa radio settings (must match gateway) ----------
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


#define DEVICE_ID                0x01AABBCC   // top byte 0x01 = sensor node
#define SENSOR_PAYLOAD_BYTES     13
#define LIGHT_CMD_BYTES          8
#define LIGHT_CMD_HEX_CHARS      16

// ---------- Shared secret ----------
static const uint8_t SHARED_KEY[4] = { 0xA3, 0x7F, 0x2C, 0x91 };
static const uint8_t LORA_PING_HEADER[4] = { 0x50, 0x49, 0x4E, 0x47 };  // "PING"

#define PING_WAIT_MS             12000UL    // listen this long after sync is established
#define FIRST_SYNC_PING_WAIT_MS  130000UL   
#define POST_TX_RX_MS            8000UL     
#define DEFAULT_SLEEP_S          115UL      

HardwareSerial loraSerial(1);
DHT dht(DHTPIN, DHTTYPE);
bool loraReady = false;
RTC_DATA_ATTR uint32_t rtcSleepSeconds = DEFAULT_SLEEP_S;
RTC_DATA_ATTR bool rtcSyncedWithGateway = false;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledConnected = false;


void xorWithKey(uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    data[i] ^= SHARED_KEY[i % 4];
  }
}

struct SensorReading {
  int16_t  tempTenthsC;     // tenths of °C
  uint8_t  humidity;        // %
  uint16_t lux;
  uint16_t soilMoistureRaw; // ADC
  uint8_t  waterLeak;       // 0 = dry, 1 = leak
  uint8_t  battery;         // %
  const char* label;        // for debug logging
};

// ---------- Forward decls ----------
bool   initLoRa();
String sendLoRaCommand(const String& cmd, int timeoutMs);
String readLoRaLine(int timeoutMs);
void   flushLoRaInput();
void   stopRx();

bool receivePacket(int timeoutMs, String& payloadHexOut);
bool sendPayload(const String& hex);

bool isValidPing(const String& hex);
bool waitForValidPing(int timeoutMs);

SensorReading readSensorData();
void initOled();
void updateUi(const SensorReading& r, const char* stateText);
void buildSensorPayload(const SensorReading& r, uint8_t* out13);
bool parseLightCommand(const String& hex,
                       uint32_t& targetIdOut,
                       uint16_t& desiredLuxOut,
                       uint16_t& nextSleepSOut);

bool   hexToBytes(const String& hexStr, uint8_t* out, size_t outLen);
String bytesToHex(const uint8_t* data, size_t len);
int    hexNibble(char c);
void xorWithKey(uint8_t* data, size_t len);

void goToDeepSleep();


bool isValidPing(const String& hex) {
  if (hex.length() < 16) return false;
  uint8_t b[8];
  if (!hexToBytes(hex.substring(0, 16), b, 8)) return false;
  if (b[0] != LORA_PING_HEADER[0] || b[1] != LORA_PING_HEADER[1] ||
      b[2] != LORA_PING_HEADER[2] || b[3] != LORA_PING_HEADER[3]) return false;
  for (int i = 0; i < 4; i++) {
    if (b[4 + i] != SHARED_KEY[i]) return false;
  }
  return true;
}

bool waitForValidPing(int timeoutMs) {
  unsigned long deadline = millis() + (unsigned long)timeoutMs;
  while (millis() < deadline) {
    long remaining = (long)(deadline - millis());
    if (remaining <= 100) break;
    String hex;
    if (!receivePacket((int)remaining, hex)) {
      Serial.println("RX error; retrying within window.");
      continue;
    }
    if (isValidPing(hex)) {
      Serial.println("Valid PING received.");
      return true;
    }
    Serial.print("Rejected packet (key mismatch or not PING): ");
    Serial.println(hex);
  }
  return false;
}

// =================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(LIGHT_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);
  dht.begin();
  initOled();

  Serial.println();
  Serial.println("=== Sensor node waking ===");


  if (!initLoRa()) {
    Serial.println("LoRa init failed; sleeping anyway to save power.");
    delay(750);
    goToDeepSleep();
  }

  // 1) Wait for the gateway's PING.
  unsigned long pingWaitMs = rtcSyncedWithGateway ? PING_WAIT_MS : FIRST_SYNC_PING_WAIT_MS;
  Serial.print("Waiting for PING (up to ");
  Serial.print(pingWaitMs);
  Serial.println(" ms)...");

  if (!waitForValidPing((int)pingWaitMs)) {
    Serial.println("No valid ping received. Going back to sleep.");
    delay(750);
    goToDeepSleep();
  }


  // 2) Read real sensors and build the payload.
  SensorReading r = readSensorData();
  Serial.print("Sensor read: ");
  Serial.println(r.label);
  updateUi(r, "TX pending");

  uint8_t payload[SENSOR_PAYLOAD_BYTES];
  buildSensorPayload(r, payload);
  xorWithKey(payload, SENSOR_PAYLOAD_BYTES);
  String payloadHex = bytesToHex(payload, SENSOR_PAYLOAD_BYTES);
  Serial.print("TX payload: ");
  Serial.println(payloadHex);

  // 3) Transmit.
  if (!sendPayload(payloadHex)) {
    Serial.println("TX failed. Sleeping.");
    updateUi(r, "TX failed");
    delay(750);
    goToDeepSleep();
  }

  // 4) Short RX window for the gateway's light command.
  updateUi(r, "RX command");
  Serial.println("Listening for light command...");
  String cmdHex;
  if (receivePacket(POST_TX_RX_MS, cmdHex)) {
    uint8_t cmdRaw[LIGHT_CMD_BYTES];
    uint32_t targetId;
    uint16_t desiredLux;
    uint16_t nextSleepS;
    if (cmdHex.length() == LIGHT_CMD_HEX_CHARS &&
        hexToBytes(cmdHex, cmdRaw, LIGHT_CMD_BYTES)) {
      xorWithKey(cmdRaw, LIGHT_CMD_BYTES);
      String decryptedCmd = bytesToHex(cmdRaw, LIGHT_CMD_BYTES);
      if (parseLightCommand(decryptedCmd, targetId, desiredLux, nextSleepS)) {
        if (targetId == DEVICE_ID) {
          Serial.print("Light command for us. desiredLux=");
          Serial.print(desiredLux);
          Serial.print(" nextSleepS=");
          Serial.println(nextSleepS);

          if (desiredLux > 0) {
            updateUi(r, "LED ON");
            digitalWrite(LED_PIN, HIGH);
            delay(500);
          } else {
            Serial.println("desiredLux=0 -> leaving LED state unchanged.");
            updateUi(r, "LED unchanged");
          }

          if (nextSleepS > 0) {
            rtcSleepSeconds = nextSleepS;
            rtcSyncedWithGateway = true;
            Serial.print("Updated RTC sleep duration to ");
            Serial.print(rtcSleepSeconds);
            Serial.println(" s.");
          } else {
            Serial.println("nextSleepS=0 -> keeping previous RTC sleep duration.");
          }
        } else {
          Serial.print("Light command was for device 0x");
          Serial.print(targetId, HEX);
          Serial.println(", not us. Ignoring.");
        }
      } else {
        Serial.println("Got a packet but it wasn't a valid light command.");
      }
    } else {
      Serial.println("Light cmd: bad length or hex decode failed.");
    }
  } else {
    Serial.println("No command received.");
  }

  // 5) Sleep.
  goToDeepSleep();
}

void loop() {
  // Never reached: setup() always ends in deep sleep.
  // After wake, the chip restarts and runs setup() again.
}

void initOled() {
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(0x3C, true) && !display.begin(0x3D, true)) {
    Serial.println("WARNING: OLED not found");
    oledConnected = false;
    return;
  }

  oledConnected = true;
  display.oled_command(SH110X_DISPLAYON);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Greenhouse Node");
  display.setCursor(0, 12);
  display.println("Booting...");
  display.display();
}

void updateUi(const SensorReading& r, const char* stateText) {
  if (!oledConnected) return;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.printf("ID:%08X", DEVICE_ID);

  display.setCursor(0, 12);
  display.print("T:");
  display.print(r.tempTenthsC / 10.0f, 1);
  display.print("C H:");
  display.print((int)r.humidity);
  display.print("%");

  display.setCursor(0, 24);
  display.print("Lux:");
  display.print((int)r.lux);
  display.print(" Soil:");
  display.print((int)r.soilMoistureRaw);

  display.setCursor(0, 36);
  display.print("Batt:");
  display.print((int)r.battery);
  display.print(" Leak:");
  display.print(r.waterLeak ? "Y" : "N");

  display.setCursor(0, 50);
  display.print("State: ");
  display.print(stateText);

  display.display();
}


bool initLoRa() {
  pinMode(RN2483_RST_PIN, OUTPUT);
  digitalWrite(RN2483_RST_PIN, HIGH);

  loraSerial.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_UART_RX_PIN, LORA_UART_TX_PIN);
  loraSerial.setTimeout(2000);

  digitalWrite(RN2483_RST_PIN, LOW);
  delay(200);
  digitalWrite(RN2483_RST_PIN, HIGH);
  delay(500);

  flushLoRaInput();

  String banner = readLoRaLine(500);
  if (banner.length()) {
    Serial.print("RN2483 banner: ");
    Serial.println(banner);
  }

  String ver = sendLoRaCommand("sys get ver", 1500);
  Serial.print("RN2483 version: ");
  Serial.println(ver);

  String mp = sendLoRaCommand("mac pause", 2000);
  if (mp.length() == 0 || mp == "invalid_param") {
    Serial.println("mac pause failed");
    return false;
  }

  String cmds[] = {
    "radio set mod lora",
    String("radio set freq ")  + String(LORA_FREQ),
    String("radio set pwr ")   + String(LORA_PWR),
    String("radio set sf ")    + String(LORA_SF),
    String("radio set afcbw ") + String(LORA_AFCBW),
    String("radio set rxbw ")  + String(LORA_RXBW),
    String("radio set prlen ") + String(LORA_PRLEN),
    String("radio set crc ")   + String(LORA_CRC),
    String("radio set iqi ")   + String(LORA_IQI),
    String("radio set cr ")    + String(LORA_CR),
    String("radio set wdt ")   + String(LORA_WDT),
    String("radio set sync ")  + String(LORA_SYNC),
    String("radio set bw ")    + String(LORA_BW)
  };

  for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
    String r = sendLoRaCommand(cmds[i], 2000);
    if (r != "ok") {
      Serial.print("RN2483 rejected: ");
      Serial.print(cmds[i]);
      Serial.print("  -> ");
      Serial.println(r.length() ? r : "<timeout>");
      return false;
    }
  }

  loraReady = true;
  return true;
}

String sendLoRaCommand(const String& cmd, int timeoutMs) {
  Serial.print("RN2483 <= ");
  Serial.println(cmd);

  loraSerial.print(cmd);
  loraSerial.print("\r\n");

  String resp = readLoRaLine(timeoutMs);
  Serial.print("RN2483 => ");
  Serial.println(resp.length() ? resp : "<timeout>");
  return resp;
}

String readLoRaLine(int timeoutMs) {
  String line;
  unsigned long start = millis();
  while (millis() - start < (unsigned long)timeoutMs) {
    while (loraSerial.available()) {
      char c = (char)loraSerial.read();
      if (c == '\r') continue;
      if (c == '\n') { line.trim(); return line; }
      line += c;
    }
    delay(1);
  }
  line.trim();
  return line;
}

void flushLoRaInput() {
  while (loraSerial.available()) loraSerial.read();
}


void stopRx() {
  loraSerial.print("radio rxstop\r\n");
  for (int i = 0; i < 2; i++) {
    String line = readLoRaLine(300);
    if (line.length() == 0) break;
    Serial.print("RN2483 (drain) => ");
    Serial.println(line);
  }
}



bool receivePacket(int timeoutMs, String& payloadHexOut) {
  payloadHexOut = "";

  String first = sendLoRaCommand("radio rx 0", 1500);
  if (first != "ok") {
    Serial.println("RX mode not entered.");
    return false;
  }

  String line = readLoRaLine(timeoutMs);

  if (line.startsWith("radio_rx ")) {
    payloadHexOut = line.substring(9);
    payloadHexOut.trim();
    return true;
  }
  if (line == "radio_err") {
    Serial.println("RX: radio_err / timeout.");
    return false;
  }

  // Either our own timeout or unexpected text. RN2483 may still be in RX.
  Serial.print("RX: timeout or unexpected line: ");
  Serial.println(line.length() ? line : "<empty>");
  stopRx();
  return false;
}

bool sendPayload(const String& hex) {
  String cmd = String("radio tx ") + hex;

  String first = sendLoRaCommand(cmd, 2000);
  if (first != "ok") {
    Serial.print("TX rejected: ");
    Serial.println(first);
    return false;
  }

  String done = readLoRaLine(10000);
  Serial.print("RN2483 => ");
  Serial.println(done.length() ? done : "<timeout>");
  return (done == "radio_tx_ok");
}


SensorReading readSensorData() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  int lightRaw = analogRead(LIGHT_PIN);
  float voltage = (float)lightRaw * 3.3f / 4095.0f;
  if (voltage > 3.29f) voltage = 3.29f;  // avoid divide-by-zero near Vcc

  float resistance = 10000.0f * voltage / (3.3f - voltage);
  float luxFloat = 0.0f;
  if (resistance > 0.0f) {
    luxFloat = 5000000.0f / resistance;
  }
  if (luxFloat < 0.0f) luxFloat = 0.0f;
  if (luxFloat > 65535.0f) luxFloat = 65535.0f;

  int soilRaw = analogRead(SOIL_PIN);

  SensorReading r;
  r.tempTenthsC = isnan(temperature) ? 0 : (int16_t)(temperature * 10.0f);
  r.humidity = isnan(humidity) ? 0 : (uint8_t)humidity;
  r.lux = (uint16_t)luxFloat;
  r.soilMoistureRaw = (uint16_t)soilRaw;
  r.waterLeak = 0;      // no leak sensor wired yet
  r.battery = 100;      // no battery divider wired yet
  r.label = (isnan(temperature) || isnan(humidity))
              ? "real sensors, DHT read failed"
              : "real sensors";

  Serial.print("Temp: ");
  if (isnan(temperature)) Serial.print("nan");
  else Serial.print(temperature, 1);
  Serial.print(" C | Hum: ");
  if (isnan(humidity)) Serial.print("nan");
  else Serial.print(humidity, 1);
  Serial.print(" % | Light raw: ");
  Serial.print(lightRaw);
  Serial.print(" | Lux: ");
  Serial.print((int)luxFloat);
  Serial.print(" lx | Soil: ");
  Serial.println(soilRaw);

  return r;
}

void buildSensorPayload(const SensorReading& r, uint8_t* out13) {
  // Bytes 0-3: device ID, big-endian
  out13[0] = (DEVICE_ID >> 24) & 0xFF;
  out13[1] = (DEVICE_ID >> 16) & 0xFF;
  out13[2] = (DEVICE_ID >> 8)  & 0xFF;
  out13[3] =  DEVICE_ID        & 0xFF;

  // Bytes 4-5: temperature in tenths of °C, big-endian (signed -> as uint16 bits)
  uint16_t t = (uint16_t)r.tempTenthsC;
  out13[4] = (t >> 8) & 0xFF;
  out13[5] =  t       & 0xFF;

  // Byte 6: humidity %
  out13[6] = r.humidity;

  // Bytes 7-8: lux, big-endian
  out13[7] = (r.lux >> 8) & 0xFF;
  out13[8] =  r.lux       & 0xFF;

  // Bytes 9-10: soil moisture raw ADC, big-endian
  out13[9]  = (r.soilMoistureRaw >> 8) & 0xFF;
  out13[10] =  r.soilMoistureRaw       & 0xFF;

  // Byte 11: water leak flag
  out13[11] = r.waterLeak ? 1 : 0;

  // Byte 12: battery %
  out13[12] = r.battery;
}


bool parseLightCommand(const String& hex,
                       uint32_t& targetIdOut,
                       uint16_t& desiredLuxOut,
                       uint16_t& nextSleepSOut) {
  if (hex.length() != LIGHT_CMD_HEX_CHARS) return false;

  uint8_t b[LIGHT_CMD_BYTES];
  if (!hexToBytes(hex, b, LIGHT_CMD_BYTES)) return false;

  targetIdOut = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
                ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
  desiredLuxOut = ((uint16_t)b[4] << 8) | b[5];
  nextSleepSOut = ((uint16_t)b[6] << 8) | b[7];
  return true;
}



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



void goToDeepSleep() {

  stopRx();

  if (oledConnected) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.println("Sleeping...");
    display.display();
    display.oled_command(SH110X_DISPLAYOFF);
  }

  Serial.print("Sleeping for ");
  Serial.print((unsigned long)rtcSleepSeconds);
  Serial.println(" s.");
  Serial.flush();

  esp_sleep_enable_timer_wakeup((uint64_t)rtcSleepSeconds * 1000000ULL);
  esp_deep_sleep_start();
}