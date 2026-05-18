/*
  Board: ESP32 (DevKit / WROOM) with RN2483 LoRa module
  Role:  LoRaWAN sensor node. Wakes from deep sleep, joins Cibicom (OTAA),
         sends a 13-byte mock sensor payload (port 1), reads any queued
         downlink for a frequency update (2-byte big-endian sleep seconds),
         then returns to deep sleep.

  Note: OTAA re-join is required on every boot (RN2483 loses session across
        power cycles). Join latency is ~5-15 s.

  Setup:
    1. Register this device on iotnet.teracom.dk (separate device from slave).
    2. Fill in APPEUI/APPKEY from the Loriot console.
*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <rn2xx3.h>
#include <esp_sleep.h>

#define DEBUG 1

#ifdef DEBUG
  #define DBG(x)   Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x)
  #define DBGLN(x)
#endif

// ---------- Credentials (fill in from Loriot device page) ----------
static const char* APPEUI = "0000000000000000";  // big-endian, 16 hex chars
static const char* APPKEY = "00000000000000000000000000000000";

// ---------- Pins ----------
#define RXD2      18
#define TXD2      19
#define RST_PIN   23
#define LED_PIN    2

// ---------- Timing ----------
#define DEFAULT_SLEEP_S  120UL
#define MIN_SLEEP_S       30UL
#define MAX_SLEEP_S     3600UL

// ---------- Payload ----------
#define DEVICE_ID  0x02AABBCC   // MSB 0x02 = transmitter_v2 node type

RTC_DATA_ATTR uint32_t rtcSleepSeconds = DEFAULT_SLEEP_S;

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

// Packs reading into 13-byte payload. No XOR; LoRaWAN AES-128 handles encryption.
// tempTenthsC is cast to uint16_t (two's complement) — server decoder must cast back
// to int16_t to recover negative temperatures.
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

    // autobaud can fail if the module isn't ready yet; retry once.
    myLora.autobaud();
    String eui = myLora.hweui();
    if (eui.length() == 0) {
        DBGLN("Module not ready, retrying...");
        delay(1000);
        myLora.autobaud();
        eui = myLora.hweui();
    }

    DBG("DevEUI: ");
    DBGLN(eui);

    DBGLN("Joining Cibicom (OTAA)...");
    bool ok = myLora.initOTAA(APPEUI, APPKEY);
    DBGLN(ok ? "Joined." : "Join failed.");
    return ok;
}

static void goToDeepSleep() {
    DBG("Sleeping for ");
    DBG(rtcSleepSeconds);
    DBGLN(" s.");
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)rtcSleepSeconds * 1000000ULL);
    esp_deep_sleep_start();
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    randomSeed(esp_random());
    delay(100);

    DBGLN("=== Transmitter v2 waking ===");

    if (!initAndJoin()) {
        DBGLN("Join failed - sleeping.");
        goToDeepSleep();
    }

    // Build and send sensor payload.
    SensorReading r = readSensorData();
    DBG("Scenario: ");
    DBGLN(r.label);

    uint8_t payload[13];
    buildPayload(r, payload);
    String hexPayload = toHex(payload, 13);
    DBG("TX payload: ");
    DBGLN(hexPayload);

    TX_RETURN_TYPE result = myLora.txCommand("mac tx uncnf 1 ", hexPayload, false);
    DBG("TX result: ");
    DBGLN(result == TX_SUCCESS  ? "TX_SUCCESS"
        : result == TX_WITH_RX ? "TX_WITH_RX"
                               : "TX_FAIL");

    if (result == TX_FAIL) {
        // No RX window opened, so no downlink possible. Any queued frequency
        // update on the server will be delivered on the next successful TX.
        DBGLN("TX failed - sleeping.");
        goToDeepSleep();
    }

    // Check for frequency-update downlink.
    String rx = myLora.getRx();
    if (rx.length() == 4) {  // 2 bytes = 4 hex chars
        uint8_t hi = (uint8_t)strtol(rx.substring(0, 2).c_str(), nullptr, 16);
        uint8_t lo = (uint8_t)strtol(rx.substring(2, 4).c_str(), nullptr, 16);
        uint16_t newSleep = ((uint16_t)hi << 8) | lo;

        if (newSleep >= MIN_SLEEP_S && newSleep <= MAX_SLEEP_S) {
            rtcSleepSeconds = newSleep;
            DBG("Freq update: sleep = ");
            DBG(rtcSleepSeconds);
            DBGLN(" s.");
        } else if (newSleep == 0) {
            DBGLN("Freq downlink = 0, keeping current interval.");
        } else {
            DBG("Freq downlink out of range (");
            DBG(newSleep);
            DBGLN(" s), ignoring.");
        }
    } else if (rx.length() > 0) {
        DBG("Unexpected downlink (");
        DBG(rx.length() / 2);
        DBGLN(" bytes), ignoring.");
    } else {
        DBGLN("No downlink.");
    }

    goToDeepSleep();
}

void loop() {
    // Never reached; setup() always ends in deep sleep.
}
