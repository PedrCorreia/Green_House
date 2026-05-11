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
       receive the ping, transmit a (mock) reading, and either turn its
       LED on or stay dark depending on the lux value picked.
    3. Then it sleeps for SLEEP_SECONDS and the cycle repeats.


*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_sleep.h>

// ---------- Pins ----------
#define LED_PIN          2
#define LORA_UART_RX_PIN 18    // ESP32 GPIO18 -> RN2483 TX
#define LORA_UART_TX_PIN 19    // ESP32 GPIO19 -> RN2483 RX
#define RN2483_RST_PIN   23

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
#define LORA_PING_HEX            "50494E47"   // "PING"
#define SENSOR_PAYLOAD_BYTES     13
#define LIGHT_CMD_BYTES          6
#define LIGHT_CMD_HEX_CHARS      12

// ---------- Shared secret ----------
// Must match gateway. XOR-obfuscates payloads; not cryptographically strong
// but filters out noise and rogue nodes that don't know the key.
static const uint8_t SHARED_KEY[4] = { 0xA3, 0x7F, 0x2C, 0x91 };

#define PING_WAIT_MS             12000UL    // listen for gateway ping; matches gateway ping interval
#define POST_TX_RX_MS            8000UL     // listen this long after TX for a light command
#define SLEEP_SECONDS            105ULL     // sleep + ~3s boot + 12s listen ≈ 120s = gateway ping cycle

HardwareSerial loraSerial(1);
bool loraReady = false;

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

SensorReading readSensorData();
void buildSensorPayload(const SensorReading& r, uint8_t* out13);
bool parseLightCommand(const String& hex, uint32_t& targetIdOut, uint16_t& desiredLuxOut);

bool   hexToBytes(const String& hexStr, uint8_t* out, size_t outLen);
String bytesToHex(const uint8_t* data, size_t len);
int    hexNibble(char c);
void xorWithKey(uint8_t* data, size_t len);

void goToDeepSleep();


// =================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println();
  Serial.println("=== Sensor node waking ===");

  // esp_random() is hardware-backed and works right after reset.
  // Good enough for picking a mock scenario.
  randomSeed(esp_random());

  if (!initLoRa()) {
    Serial.println("LoRa init failed; sleeping anyway to save power.");
    goToDeepSleep();
  }

  // 1) Wait for the gateway's PING.
  // (If the gateway pings while we're still booting, we miss this cycle —
  // not a real problem, the next cycle catches up.)
  Serial.print("Waiting for PING (up to ");
  Serial.print(PING_WAIT_MS);
  Serial.println(" ms)...");

  String pingHex;
  bool gotPing = receivePacket(PING_WAIT_MS, pingHex);

  if (!gotPing) {
    Serial.println("No ping received. Going back to sleep.");
    goToDeepSleep();
  }

  Serial.print("Received: ");
  Serial.println(pingHex);

  if (!pingHex.equalsIgnoreCase(LORA_PING_HEX)) {
    Serial.println("(Not literal PING, but treating it as a wake trigger.)");
  }

  // 2) Read sensors (currently mock) and build the payload.
  SensorReading r = readSensorData();
  Serial.print("Mock scenario: ");
  Serial.println(r.label);

  uint8_t payload[SENSOR_PAYLOAD_BYTES];
  buildSensorPayload(r, payload);
  String payloadHex = bytesToHex(payload, SENSOR_PAYLOAD_BYTES);
  Serial.print("TX payload: ");
  Serial.println(payloadHex);

  // 3) Transmit.
  if (!sendPayload(payloadHex)) {
    Serial.println("TX failed. Sleeping.");
    goToDeepSleep();
  }

  // 4) Short RX window for the gateway's light command.
  Serial.println("Listening for light command...");
  String cmdHex;
  if (receivePacket(POST_TX_RX_MS, cmdHex)) {
    uint32_t targetId;
    uint16_t desiredLux;
    if (parseLightCommand(cmdHex, targetId, desiredLux)) {
      if (targetId == DEVICE_ID) {
        Serial.print("Light command for us. desiredLux=");
        Serial.println(desiredLux);
        digitalWrite(LED_PIN, desiredLux > 0 ? HIGH : LOW);
        // Hold the LED state visibly for a moment before sleeping.
        // Deep sleep will turn it off again — that's expected; this is just
        // a demo so you can see it light up before the chip powers down.
        delay(500);
      } else {
        Serial.print("Light command was for device 0x");
        Serial.print(targetId, HEX);
        Serial.println(", not us. Ignoring.");
      }
    } else {
      Serial.println("Got a packet but it wasn't a valid light command.");
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


// =================================================================
// Sensor reading (mock for now)
// =================================================================
//
// TODO: replace this with real sensor reads once the hardware is wired up.
//   - DHT22 -> tempTenthsC, humidity
//   - LDR / BH1750 / TSL2561 -> lux
//   - capacitive soil sensor on ADC -> soilMoistureRaw
//   - water leak probe (digital) -> waterLeak
//   - battery divider on ADC -> battery (%)
//
// Until then, pick a random scenario so the gateway sees varied data.
SensorReading readSensorData() {
  static const SensorReading scenarios[] = {
    // tempC*10, hum%, lux,  soilADC, leak, batt%, label
    {  225,      55,  450,   1800,    0,    85, "normal greenhouse" },
    {  210,      60,   80,   1750,    0,    82, "too dark, needs light" },
    {  240,      40,  500,   3500,    0,    78, "dry soil" },
    {  220,      75,  300,   1900,    1,    80, "water leak!" },
    {  230,      55,  400,   1850,    0,    12, "low battery" }
  };

  const long n = (long)(sizeof(scenarios) / sizeof(scenarios[0]));
  return scenarios[random(0, n)];
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


bool parseLightCommand(const String& hex, uint32_t& targetIdOut, uint16_t& desiredLuxOut) {
  if (hex.length() != LIGHT_CMD_HEX_CHARS) return false;

  uint8_t b[LIGHT_CMD_BYTES];
  if (!hexToBytes(hex, b, LIGHT_CMD_BYTES)) return false;

  targetIdOut = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
                ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
  desiredLuxOut = ((uint16_t)b[4] << 8) | b[5];
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

  Serial.print("Sleeping for ");
  Serial.print((unsigned long)SLEEP_SECONDS);
  Serial.println(" s. See you on the other side.");
  Serial.flush();

  digitalWrite(LED_PIN, LOW);
  esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
}