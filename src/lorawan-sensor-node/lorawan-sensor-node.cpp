/*
  Board: ESP32 (DevKit / WROOM) with RN2483 LoRa module
  Role:  LoRaWAN sensor node. Joins Cibicom (OTAA) once on boot, then
         sends a 13-byte mock sensor payload (port 1) every TX_INTERVAL_MS.
         Reads any queued downlink for a frequency update (2-byte big-endian
         interval in seconds) after each transmission.

  Wiring (per Cibicom hookup guide):
    ESP32 GPIO18 <-> RX on RN2483
    ESP32 GPIO19 <-> TX on RN2483
    ESP32 GPIO23 <-> RST on RN2483
    ESP32 3.3V   <-> 3.3V on RN2483
    ESP32 GND    <-> GND on RN2483

  Setup:
    1. Register this device on iotnet.teracom.dk (OTAA, "Generate all
       parameters except DevEUI"). Enter the HWEUI printed at startup.
    2. Fill in APPEUI/APPKEY from the Loriot device page.
*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <rn2xx3.h>

// ---------- Credentials (fill in from Loriot device page) ----------
static const char* APPEUI = "apeui";  // big-endian, 16 hex chars
static const char* APPKEY = "1apppkey";

// ---------- Pins ----------
#define RXD2     18
#define TXD2     19
#define RST_PIN  23
#define LED_PIN   2

// ---------- Timing ----------
#define DEFAULT_TX_INTERVAL_MS  30000UL
#define MIN_TX_INTERVAL_MS      30000UL
#define MAX_TX_INTERVAL_MS    3600000UL

// ---------- Payload ----------
#define DEVICE_ID  0x02AABBCC   // MSB 0x02 = transmitter_v2 node type

static uint32_t txIntervalMs = DEFAULT_TX_INTERVAL_MS;
static uint32_t lastTxMs     = 0;
static bool     joined       = false;

HardwareSerial loraSerial(1);
rn2xx3 myLora(loraSerial);

struct SensorReading {
    int16_t  tempTenthsC;
    uint8_t  humidity;
    uint16_t lux;
    uint16_t soilMoistureRaw;
    uint8_t  waterLeak;
    uint8_t  battery;
    const char* label;
};

static SensorReading readSensorData() {
    static const SensorReading scenarios[] = {
        {  225, 55,  450, 1800, 0, 85, "normal" },
        {  210, 60,   80, 1750, 0, 82, "too dark" },
        {  240, 40,  500, 3500, 0, 78, "dry soil" },
        {  220, 75,  300, 1900, 1, 80, "water leak" },
        {  230, 55,  400, 1850, 0, 12, "low battery" }
    };
    return scenarios[random(0, 5)];
}

// Packs reading into 13-byte payload.
// tempTenthsC is cast to uint16_t (two's complement) — server decoder must
// cast back to int16_t to recover negative temperatures.
static void buildPayload(const SensorReading& r, uint8_t* out) {
    out[0] = (DEVICE_ID >> 24) & 0xFF;
    out[1] = (DEVICE_ID >> 16) & 0xFF;
    out[2] = (DEVICE_ID >>  8) & 0xFF;
    out[3] =  DEVICE_ID        & 0xFF;

    uint16_t t = (uint16_t)r.tempTenthsC;
    out[4] = (t >> 8) & 0xFF;
    out[5] =  t       & 0xFF;

    out[6] = r.humidity;

    out[7] = (r.lux >> 8) & 0xFF;
    out[8] =  r.lux       & 0xFF;

    out[9]  = (r.soilMoistureRaw >> 8) & 0xFF;
    out[10] =  r.soilMoistureRaw       & 0xFF;

    out[11] = r.waterLeak ? 1 : 0;
    out[12] = r.battery;
}

static String toHex(const uint8_t* data, size_t len) {
    String s;
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x10) s += '0';
        s += String(data[i], HEX);
    }
    s.toUpperCase();
    return s;
}

static bool initAndJoin() {
    pinMode(RST_PIN, OUTPUT);
    digitalWrite(RST_PIN, LOW);
    delay(200);
    digitalWrite(RST_PIN, HIGH);
    delay(2000);

    loraSerial.begin(57600, SERIAL_8N1, RXD2, TXD2);
    loraSerial.setRxBufferSize(1024);
    loraSerial.setTimeout(1000);

    myLora.autobaud();
    String eui = myLora.hweui();
    if (eui.length() == 0) {
        Serial.println("Module not ready, retrying autobaud...");
        delay(1000);
        myLora.autobaud();
        eui = myLora.hweui();
    }

    if (eui.length() == 0) {
        Serial.println("Communication with RN2xx3 unsuccessful. Power cycle the board.");
        return false;
    }

    Serial.print("When using OTAA, register this DevEUI: ");
    Serial.println(eui);
    Serial.print("RN2xx3 firmware version: ");
    Serial.println(myLora.sysver());

    Serial.println("Trying to join Cibicom (OTAA)...");
    bool ok = myLora.initOTAA(APPEUI, APPKEY);
    if (ok) {
        Serial.println("Successfully joined the network.");
    } else {
        Serial.println("Unable to join. Are your keys correct, and do you have Cibicom coverage?");
    }
    return ok;
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    randomSeed(esp_random());
    delay(1000);

    Serial.println("=== Transmitter v2 starting ===");

    joined = initAndJoin();
}

void loop() {
    if (!joined) {
        Serial.println("Not joined. Retrying in 10 s...");
        delay(10000);
        joined = initAndJoin();
        return;
    }

    uint32_t now = millis();
    if (now - lastTxMs < txIntervalMs) {
        return;
    }
    lastTxMs = now;

    digitalWrite(LED_PIN, HIGH);

    SensorReading r = readSensorData();
    Serial.print("Scenario: ");
    Serial.println(r.label);

    uint8_t payload[13];
    buildPayload(r, payload);
    String hexPayload = toHex(payload, 13);
    Serial.print("TXing: ");
    Serial.println(hexPayload);

    TX_RETURN_TYPE result = myLora.txCommand("mac tx uncnf 2 ", hexPayload, false);
    Serial.print("TX result: ");
    Serial.println(result == TX_SUCCESS  ? "TX_SUCCESS"
                 : result == TX_WITH_RX ? "TX_WITH_RX"
                                        : "TX_FAIL");

    if (result == TX_WITH_RX) {
        String rx = myLora.getRx();
        if (rx.length() == 4) {  // 2 bytes = 4 hex chars
            uint8_t hi = (uint8_t)strtol(rx.substring(0, 2).c_str(), nullptr, 16);
            uint8_t lo = (uint8_t)strtol(rx.substring(2, 4).c_str(), nullptr, 16);
            uint32_t newInterval = ((uint32_t)((uint16_t)hi << 8 | lo)) * 1000UL;

            if (newInterval >= MIN_TX_INTERVAL_MS && newInterval <= MAX_TX_INTERVAL_MS) {
                txIntervalMs = newInterval;
                Serial.print("Interval updated to ");
                Serial.print(txIntervalMs / 1000);
                Serial.println(" s.");
            } else if (newInterval == 0) {
                Serial.println("Downlink = 0, keeping current interval.");
            } else {
                Serial.print("Downlink interval out of range (");
                Serial.print(newInterval / 1000);
                Serial.println(" s), ignoring.");
            }
        } else if (rx.length() > 0) {
            Serial.print("Unexpected downlink (");
            Serial.print(rx.length() / 2);
            Serial.println(" bytes), ignoring.");
        } else {
            Serial.println("No downlink data.");
        }
    }

    digitalWrite(LED_PIN, LOW);
}
