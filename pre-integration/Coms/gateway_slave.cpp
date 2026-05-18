/*
  Board: ESP32 (DevKit / WROOM)
  Role:  LoRaWAN serial bridge. Receives "FREQ:<seconds>\n" from the main
         gateway over UART2 (pins 16/17), sends it as a 2-byte big-endian
         LoRaWAN uplink on port 2 to Cibicom/Loriot, and periodically sends
         a tiny poll uplink so queued Class A downlinks can be received.

  Setup:
    1. Register this device on iotnet.teracom.dk (OTAA, "Generate all except DevEUI").
    2. Flash with APPEUI/APPKEY filled in from the Loriot console.
    3. Wire: slave GPIO17 (TX) -> main gateway GPIO18 (RX)
             slave GPIO16 (RX) <- main gateway GPIO19 (TX)

  UART2 bridge commands from main gateway (RXD3=16, TXD3=17):
    FREQ:<uint16>\n   e.g. "FREQ:120\n" -> sets transmitter_v2 sleep to 120 s
  Responses back to main gateway:
    OK\n    LoRaWAN uplink confirmed sent
    ERR\n   Join not established or TX failed

  Debug output:
    Define DEBUG to enable verbose prints on Serial (USB).
    The command channel is always on UART2 regardless of DEBUG.
*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <rn2xx3.h>

#define DEBUG 1

#ifdef DEBUG
  #define DBG(x)   Serial.print(x)
  #define DBGLN(x) Serial.println(x)
#else
  #define DBG(x)
  #define DBGLN(x)
#endif

// ---------- Credentials (fill in from Loriot device page) ----------
static const char* APPEUI = "BE7A000000001465";  // big-endian, 16 hex chars
static const char* APPKEY = "4E50C01541A6DDDFA8FB1649F6259F2F";  // 32 hex chars

// ---------- Pins ----------
#define RXD2      18
#define TXD2      19
#define RXD3      16
#define TXD3      17
#define RST_PIN   23
#define LED_PIN    2

// ---------- LoRaWAN port ----------
#define FREQ_PORT  2   // slave uplinks use port 2; transmitter_v2 uses port 1
#define POLL_PORT  2

// LoRaWAN Class A downlinks are delivered only after this device transmits.
#define POLL_INTERVAL_MS  30000UL
#define POLL_PAYLOAD_HEX  "00"

HardwareSerial loraSerial(1);
HardwareSerial bridgeSerial(2);  // UART2: command bridge to main gateway (pins 16/17)
rn2xx3 myLora(loraSerial);

static bool joined = false;
static unsigned long lastPollMs = 0;

static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool hexToBytes(const String& hex, uint8_t* out, size_t outLen) {
    if (hex.length() != outLen * 2) return false;

    for (size_t i = 0; i < outLen; i++) {
        int hi = hexNibble(hex.charAt(i * 2));
        int lo = hexNibble(hex.charAt(i * 2 + 1));
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }

    return true;
}

static bool initAndJoin() {
    DBGLN("[INIT] Resetting RN2483...");
    bridgeSerial.print("TEST:");
    pinMode(RST_PIN, OUTPUT);
    digitalWrite(RST_PIN, LOW);
    delay(200);
    digitalWrite(RST_PIN, HIGH);
    delay(1000);

    loraSerial.begin(57600, SERIAL_8N1, RXD2, TXD2);
    DBGLN("[INIT] Running autobaud...");
    myLora.autobaud();

    String eui = myLora.hweui();
    DBG("[INIT] DevEUI: ");
    DBGLN(eui.length() ? eui : "<no response>");

    String ver = myLora.sysver();
    DBG("[INIT] Firmware: ");
    DBGLN(ver.length() ? ver : "<no response>");

    DBGLN("[JOIN] OTAA join attempt...");
    bool ok = myLora.initOTAA(APPEUI, APPKEY);
    DBGLN(ok ? "[JOIN] Joined OK." : "[JOIN] Failed — will retry in 30 s.");
    return ok;
}

static void printDecodedDownlink(const String& rx) {
    if (rx.length() == 4) {
        uint8_t b[2];
        if (!hexToBytes(rx, b, sizeof(b))) {
            Serial.println("RX parse: invalid hex");
            return;
        }

        uint16_t value = ((uint16_t)b[0] << 8) | b[1];
        Serial.print("RX uint16: ");
        Serial.println(value);
        return;
    }

    if (rx.length() == 26) {
        uint8_t b[13];
        if (!hexToBytes(rx, b, sizeof(b))) {
            Serial.println("RX parse: invalid hex");
            return;
        }

        uint32_t deviceId = ((uint32_t)b[0] << 24) |
                            ((uint32_t)b[1] << 16) |
                            ((uint32_t)b[2] << 8) |
                            (uint32_t)b[3];
        int16_t tempTenths = (int16_t)(((uint16_t)b[4] << 8) | b[5]);
        uint8_t humidity = b[6];
        uint16_t lux = ((uint16_t)b[7] << 8) | b[8];
        uint16_t soil = ((uint16_t)b[9] << 8) | b[10];
        uint8_t leak = b[11];
        uint8_t battery = b[12];

        Serial.print("RX sensor device=0x");
        if (deviceId < 0x10000000UL) Serial.print('0');
        if (deviceId < 0x01000000UL) Serial.print('0');
        if (deviceId < 0x00100000UL) Serial.print('0');
        if (deviceId < 0x00010000UL) Serial.print('0');
        if (deviceId < 0x00001000UL) Serial.print('0');
        if (deviceId < 0x00000100UL) Serial.print('0');
        if (deviceId < 0x00000010UL) Serial.print('0');
        Serial.print(deviceId, HEX);
        Serial.print(" temp=");
        Serial.print(tempTenths / 10.0f, 1);
        Serial.print("C humidity=");
        Serial.print(humidity);
        Serial.print("% lux=");
        Serial.print(lux);
        Serial.print(" soil=");
        Serial.print(soil);
        Serial.print(" leak=");
        Serial.print(leak);
        Serial.print(" battery=");
        Serial.print(battery);
        Serial.println("%");
        return;
    }

    Serial.print("RX bytes: ");
    Serial.println(rx.length() / 2);
}

static void printDownlinkIfPresent(TX_RETURN_TYPE result) {
    if (result != TX_WITH_RX) return;

    String rx = myLora.getRx();
    rx.trim();

    Serial.print("[RX] raw hex (");
    Serial.print(rx.length() / 2);
    Serial.print(" bytes): ");
    Serial.println(rx);

    printDecodedDownlink(rx);

    // Forward 13-byte sensor payloads to main gateway.
    if (rx.length() == 26) {
        bridgeSerial.println(rx);
        Serial.println("[RX] forwarded to gateway via bridge");
    }
}

static TX_RETURN_TYPE sendHexUplink(uint8_t port, const String& hex) {
    String command = String("mac tx uncnf ") + String((int)port) + " ";
    TX_RETURN_TYPE result = myLora.txCommand(command, hex, false);
    printDownlinkIfPresent(result);
    return result;
}

// Encodes freq as 2-byte big-endian hex and sends as LoRaWAN uplink.
static TX_RETURN_TYPE sendFreq(uint16_t freq) {
    uint8_t buf[2] = { (uint8_t)(freq >> 8), (uint8_t)(freq & 0xFF) };
    String hex = "";
    for (int i = 0; i < 2; i++) {
        if (buf[i] < 0x10) hex += '0';
        hex += String(buf[i], HEX);
    }
    hex.toUpperCase();

    DBG("[TX] FREQ uplink freq=");
    DBG(freq);
    DBG(" port=");
    DBG(FREQ_PORT);
    DBG(" hex=");
    DBG(hex);
    DBG(" (");
    DBG(hex.length() / 2);
    DBGLN(" bytes)");

    return sendHexUplink(FREQ_PORT, hex);
}

static TX_RETURN_TYPE sendPoll() {
    DBGLN("LoRaWAN TX poll");
    return sendHexUplink(POLL_PORT, POLL_PAYLOAD_HEX);
}

static void pollForQueuedDownlinks() {
    unsigned long now = millis();
    if (now - lastPollMs < POLL_INTERVAL_MS) return;

    lastPollMs = now;
    DBGLN("[POLL] Sending poll uplink...");
    TX_RETURN_TYPE result = sendPoll();
    DBG("[POLL] Result: ");
    DBGLN(result == TX_SUCCESS ? "TX_SUCCESS" :
          result == TX_WITH_RX ? "TX_WITH_RX (downlink received)" : "TX_FAIL");
}

void setup() {
    Serial.begin(115200);
    bridgeSerial.begin(115200, SERIAL_8N1, RXD3, TXD3);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    delay(100);

    DBGLN("=== Gateway Slave starting ===");
    bridgeSerial.print("TEST1:");

    joined = initAndJoin();
    if (joined) {
        digitalWrite(LED_PIN, HIGH);
        lastPollMs = millis() - POLL_INTERVAL_MS;
    }
}

void loop() {
    // Retry join if not established.
    if (!joined) {
        digitalWrite(LED_PIN, LOW);
        DBGLN("[LOOP] Not joined — draining bridge buffer, then waiting 30 s before retry...");
        while (bridgeSerial.available()) bridgeSerial.read();
        delay(10000);
        joined = initAndJoin();
        if (joined) {
            digitalWrite(LED_PIN, HIGH);
            lastPollMs = millis() - POLL_INTERVAL_MS;
            DBGLN("[LOOP] Joined. Starting poll timer.");
        }
        return;
    }

    // Poll for commands from main gateway over UART2 bridge.
    if (bridgeSerial.available()) {
        String line = bridgeSerial.readStringUntil('\n');
        line.trim();
        DBG("[BRIDGE] Received: "); DBGLN(line);

        if (line.startsWith("FREQ:")) {
            int val = line.substring(5).toInt();
            if (val <= 0 || val > 65535) {
                bridgeSerial.println("ERR");
                DBGLN("[BRIDGE] ERR: FREQ value out of range");
                return;
            }
            DBG("[BRIDGE] Parsed FREQ="); DBGLN(val);
            TX_RETURN_TYPE result = sendFreq((uint16_t)val);
            bool ok = (result == TX_SUCCESS || result == TX_WITH_RX);
            bridgeSerial.println(ok ? "OK" : "ERR");
            DBG("[BRIDGE] Response sent: "); DBGLN(ok ? "OK" : "ERR");
        } else if (line.length() > 0) {
            DBGLN("[BRIDGE] Unknown command — sending ERR");
            bridgeSerial.println("ERR");
        }
    }

    pollForQueuedDownlinks();
}