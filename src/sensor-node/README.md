# ESP32 Sensor Node - Battery-Powered Environmental Monitor

## Table of Contents
1. [System Overview](#system-overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Integration with Gateway](#integration-with-gateway)
4. [Data Structures](#data-structures)
5. [Execution Flow](#execution-flow)
6. [Methods Reference & Execution Order](#methods-reference--execution-order)
7. [Communication Protocols](#communication-protocols)
8. [Setup & Deployment](#setup--deployment)

---

## System Overview

### Purpose
The **Sensor Node** is a battery-powered environmental monitoring device that wakes periodically to collect sensor data and transmit it to the gateway. It implements aggressive power management, sleeping 99% of the time to maximize battery life.

### Key Responsibilities
- **Periodic Wake-Up**: Listen for gateway PING on schedule
- **Sensor Readings**: Collect temperature, humidity, light, soil moisture, water leak, and battery voltage
- **Data Transmission**: Encode 13-byte payload and transmit via LoRa P2P
- **Command Reception**: Listen for light control and sleep interval updates from gateway
- **Sleep Management**: Return to deep sleep with gateway-provided interval (10-65000 seconds)
- **Battery Efficiency**: Sleep ~99% of runtime; active for <1 second per cycle

### When to Use
- **Distributed monitoring**: Multiple sensor nodes across greenhouse
- **Long battery life**: 2×AA batteries last 6-12 months in normal operation
- **Wireless**: No wiring required, nodes can be placed anywhere in range
- **Scalable**: Add more nodes without changing gateway code (just update APPROVED_DEVICE_IDS)

---

## Hardware Architecture

### Main Components

#### **ESP32 Microcontroller (Sensor Node)**
- **Role**: Main control board with sensors
- **Power**: 2×AA Battery (~3V), deep sleep mode to minimize consumption
- **UART Ports**:
  - **UART1** (pins 16/17): Connected to RN2483 LoRa module at 57600 baud
  - **UART0** (USB): USB Serial for debugging at 115200 baud (programming only)
- **Sensors**:
  - **GPIO4**: DHT22 (temperature/humidity) digital sensor
  - **GPIO34**: KY-018 photoresistor (analog light sensor)
  - **GPIO35**: Capacitive soil moisture sensor (analog)
  - **GPIO33**: Water leak detector (analog or digital)
  - **I2C (GPIO21/22)**: Optional OLED display (128x64 SH1106)
- **Sleep**:
  - **RTC Memory**: Stores device ID, sync state, sleep duration across deep sleep
  - **Timer**: Wakeup on interval (set by gateway)

#### **RN2483 LoRa Module (P2P)**
- **Communication**: Connected via UART1 at 57600 baud
- **Protocol**: LoRa P2P (not LoRaWAN, proprietary mode)
- **Frequency**: 868.1 MHz (European ISM band)
- **Purpose**: Receive PING from gateway, transmit sensor data
- **Interface**: AT command protocol
  - Commands: `radio rx`, `radio tx`, `radio set`, `mac pause`
  - Responses: `ok`, `radio_rx <hex>`, `radio_tx_ok`, `radio_err`

### Pin Mapping

```
ESP32 (Sensor Node) Pin Configuration:
├─ UART1 (LoRa P2P):
│  ├─ GPIO16 (RX) <- RN2483 TX
│  └─ GPIO17 (TX) -> RN2483 RX
├─ GPIO14: RN2483 Reset (active-low)
├─ GPIO2: LED (activity indicator)
├─ Analog Sensors:
│  ├─ GPIO34: Light sensor (analog in)
│  ├─ GPIO35: Soil moisture (analog in)
│  └─ GPIO33: Water leak (analog or digital in)
├─ GPIO4: DHT22 data line
├─ I2C (Optional OLED):
│  ├─ GPIO21: SDA
│  ├─ GPIO22: SCL
│  └─ Adafruit SH1106 128x64 display
└─ UART0 (USB Serial @ 115200):
   └─ Used for Serial.print() debugging during development

Power Pins:
├─ 3.3V: Battery (via voltage regulator)
└─ GND: Battery return

Baud Rates:
├─ UART1: 57600 (RN2483 standard)
└─ USB Serial: 115200 (debugging)
```

### Power Consumption Profile

```
State          Duration    Current    Energy Per Cycle
─────────────────────────────────────────────────────
Deep Sleep     118.5s      ~3 µA      ~0.35 µJ
Wake CPU       0.5s        ~100 mA    ~50 µJ (reading sensors)
RX (Listen)    12s         ~100 mA    ~12 mJ
TX (Send)      0.5s        ~600 mA    ~0.3 mJ
─────────────────────────────────────────────────────
Total Cycle    120s        ~3 µA avg  ~12.5 mJ / cycle

Typical Battery Life (2×AA, 2500 mAh each):
  Total capacity: ~2.5 * 3600 = 9000 C (coulombs)
  Cycles: 9000 / (12.5 mJ / 3V) = ~432,000 cycles
  Duration: 432,000 cycles * 120s = 50.4 million seconds ≈ 585 days (~19 months)
```

---

## Integration with Gateway

### Wake Cycle Orchestration

```
T=0min    Gateway sends PING broadcast (every 120 seconds)
          └─ 8-byte: [0x50,0x49,0x4E,0x47] + [SHARED_KEY]
          └─ Encrypted with XOR
          └─ Broadcasts on 868.1 MHz

T=0-12s   Sensor Node Listening Window
          ├─ ESP32 in deep sleep (RN2483 stays powered, in RX mode)
          ├─ RN2483 radio interrupt wakes CPU if PING received
          ├─ If PING valid (header + key match):
          │  └─ MCU wakes, begins sensor collection
          └─ If PING not received:
             └─ Sleep again (sync lost, retry next cycle)

T=1-3s    Sensor Data Collection
          ├─ Read DHT22: temperature, humidity
          ├─ Read TSL2561: ambient light (lux)
          ├─ Read capacitive sensor: soil moisture
          ├─ Read GPIO: water leak detector
          ├─ Read ADC: battery voltage
          └─ Pack into 13-byte struct

T=3-4s    Sensor Data Transmission
          ├─ Build 13-byte payload
          ├─ XOR encrypt with SHARED_KEY
          ├─ Convert to 26-char hex string
          ├─ Send: "radio tx <hex>"
          └─ Wait for "radio_tx_ok"

T=4-8s    Listen for Light Command
          ├─ Enter RX mode: "radio rx 0" (POST_TX_RX_MS = 8 seconds)
          ├─ If "radio_rx <hex>" received:
          │  ├─ Decrypt with XOR
          │  ├─ Parse: device ID, desired lux, next sleep seconds
          │  └─ Update rtcSleepSeconds (RTC memory for next cycle)
          └─ If no RX: use DEFAULT_SLEEP_S

T=8s      Return to Deep Sleep
          ├─ Store rtcSleepSeconds in RTC memory
          ├─ esp_sleep_enable_timer_wakeup(rtcSleepSeconds * 1e6)
          ├─ esp_deep_sleep()  [CPU powered off]
          └─ RN2483 may remain powered or enter sleep (implementation dependent)

T=120s    Gateway sends next PING (cycle repeats)
```

### Initialization Flow (First Boot vs. Sync Loss)

```
FIRST BOOT (rtcSyncedWithGateway = false):
  ├─ Listen for FIRST_SYNC_PING_WAIT_MS (130 seconds)
  └─ If PING not received within 130s:
     └─ Sleep for shorter interval, retry more often

NORMAL OPERATION (rtcSyncedWithGateway = true):
  ├─ Listen for PING_WAIT_MS (12 seconds)
  └─ If PING received:
     └─ Use gateway-provided sleep interval

SYNC LOSS (PING not received in expected window):
  ├─ Fall back to DEFAULT_SLEEP_S (115 seconds)
  ├─ Retry next cycle
  └─ Re-establish sync when PING arrives
```

---

## Data Structures

### SensorReading (13 bytes)

**Transmitted from sensor to gateway**:
```
struct SensorReading {
  int16_t  tempTenthsC;      // 2 bytes, tenths of °C (e.g., 235 = 23.5°C)
  uint8_t  humidity;          // 1 byte, 0-100%
  uint16_t lux;              // 2 bytes, light level (0-65535)
  uint16_t soilMoistureRaw;  // 2 bytes, capacitive ADC (0-4095)
  bool     waterLeak;         // 1 byte, false=0 or true=1
  uint8_t  battery;           // 1 byte, battery percentage (0-100%) or raw ADC
};

Binary Layout (13 bytes total):
Byte 0-3:   Device ID (0x01AABBCC, big-endian)
Byte 4-5:   Temperature (big-endian int16_t)
Byte 6:     Humidity
Byte 7-8:   Lux (big-endian uint16_t)
Byte 9-10:  Soil Moisture (big-endian uint16_t)
Byte 11:    Water Leak
Byte 12:    Battery

Encryption: XOR with SHARED_KEY before TX
```

### PING Packet (8 bytes, from gateway)

```
uint8_t pingBuf[8] = {
  0x50, 0x49, 0x4E, 0x47,     // ASCII "PING"
  0xA3, 0x7F, 0x2C, 0x91      // SHARED_KEY
};

Received as 16-char hex string: "50494E47A37F2C91"
Decrypted (XOR) and validated before waking MCU
```

### Light Command Packet (8 bytes, from gateway)

```
uint8_t lightCmd[8] = {
  // Byte 0-3: Target device ID (must match this node's DEVICE_ID)
  // Byte 4-5: Desired lux level (0 if no light needed, 500+ if needed)
  // Byte 6-7: Next sleep interval in seconds (10-65000)
};

Received as 16-char hex string
Decrypted (XOR) and parsed
If device ID matches: update rtcSleepSeconds for next cycle
```

### RTC Memory Variables (survive deep sleep)

```cpp
RTC_DATA_ATTR uint32_t rtcSleepSeconds = DEFAULT_SLEEP_S;
  └─ Next sleep interval (updated by gateway command)

RTC_DATA_ATTR bool rtcSyncedWithGateway = false;
  └─ Whether we've received a PING (affects listen window length)
```

---

## Execution Flow

### Boot Sequence

```
main() [ESP32 bootloader]
  ↓
setup()
  ├─ Initialize Serial (115200 USB debug)
  ├─ Check RTC memory (persists across deep sleep):
  │  ├─ If rtcSyncedWithGateway: use sync'd interval
  │  └─ Else: use FIRST_SYNC_PING_WAIT_MS (longer listen)
  │
  ├─ Initialize GPIO (LED, sensors)
  ├─ Initialize I2C (for optional OLED display)
  ├─ Initialize DHT22 sensor
  │
  ├─ connectToLoRa()
  │  ├─ Reset RN2483 (GPIO14 pulse)
  │  ├─ Begin UART1 @ 57600 baud
  │  ├─ Get RN2483 version
  │  ├─ "mac pause" (disable LoRaWAN MAC)
  │  └─ Configure 13 radio parameters
  │
  ├─ Print "Sensor node ready"
  └─ loraReady = true

[Loop begins]
```

### Main Loop (runs until sleep)

```
loop() [typically 30-60 iterations before timeout]
  ├─ Set flag: "Listening for PING"
  │
  ├─ Enter RX mode:
  │  └─ "radio rx 0" (listen until packet or watchdog timeout)
  │
  ├─ Wait for PING packet with timeout:
  │  └─ PING_WAIT_MS (12s) or FIRST_SYNC_PING_WAIT_MS (130s)
  │
  ├─ If PING received within timeout:
  │  ├─ Validate packet (decrypt, check header + key)
  │  ├─ If valid:
  │  │  ├─ Set rtcSyncedWithGateway = true
  │  │  ├─ Collect sensor readings:
  │  │  │  ├─ DHT22.read() → temp, humidity
  │  │  │  ├─ Read GPIO34 (light sensor, analog)
  │  │  │  ├─ Read GPIO35 (soil moisture, analog)
  │  │  │  ├─ Read GPIO33 (water leak, analog/digital)
  │  │  │  └─ Read ADC (battery voltage)
  │  │  ├─ Pack into 13-byte struct
  │  │  ├─ XOR encrypt
  │  │  ├─ Convert to hex: bytesToHex()
  │  │  ├─ Transmit: "radio tx <hex>"
  │  │  ├─ Wait for "radio_tx_ok"
  │  │  └─ Set rtcSleepSeconds = default (wait for light command)
  │  │
  │  └─ Enter post-TX RX window (8 seconds):
  │     └─ "radio rx 0" (POST_TX_RX_MS)
  │        ├─ If light command received:
  │        │  ├─ Decrypt (XOR)
  │        │  ├─ Check device ID (must match this node)
  │        │  ├─ Extract sleep interval
  │        │  └─ Update rtcSleepSeconds
  │        └─ If no light command: use DEFAULT_SLEEP_S
  │
  └─ Exit loop (goto sleep)

[After loop completes or timeout]
```

### Sleep Sequence

```
[Loop exits]
  ├─ Print: "Sleep interval = X seconds"
  │
  ├─ esp_sleep_enable_timer_wakeup(rtcSleepSeconds * 1e6)
  │  └─ Set ESP32 timer to wake after rtcSleepSeconds
  │
  └─ esp_deep_sleep()
     ├─ Save RTC variables (rtcSleepSeconds, rtcSyncedWithGateway)
     ├─ Power off CPU (retains RTC memory, SRAM, GPIO states)
     ├─ Wait for timer interrupt
     └─ CPU wakes, setup() runs again
```

---

## Methods Reference & Execution Order

### **Phase 1: Initialization Functions** (called in `setup()`)

#### 1. `connectToLoRa()`
- **Purpose**: Initialize RN2483 LoRa P2P module
- **Called**: Once during setup
- **Parameters**: None
- **Flow**:
  1. Set GPIO14 (RN2483 reset pin) to HIGH
  2. Begin UART1 @ 57600 baud
  3. Pulse reset LOW → HIGH (hardware reset)
  4. Flush serial buffer
  5. Read banner from RN2483
  6. Get module version ("sys get ver")
  7. Send "mac pause" (disable LoRaWAN, enable P2P)
  8. Configure 13 radio parameters (same as gateway):
     - Frequency, power, SF, bandwidth, CRC, etc.
  9. Return true if all commands succeed, false if any fail
- **Returns**: bool

#### 2. `initSensors()`
- **Purpose**: Initialize sensor objects (DHT22, I2C for OLED)
- **Called**: Once during setup
- **Parameters**: None
- **Flow**:
  1. DHT.begin(DHTPIN)
  2. Initialize I2C (Wire.begin)
  3. Check OLED display (Adafruit_SH1106G)
  4. Set oledConnected flag
- **Returns**: void

#### 3. `readRTCData()`
- **Purpose**: Load persistent variables from RTC memory
- **Called**: During setup
- **Parameters**: None
- **Flow**:
  1. Read rtcSleepSeconds (set by previous cycle or gateway command)
  2. Read rtcSyncedWithGateway (whether we've established sync)
  3. Print to Serial for debugging
- **Returns**: void

---

### **Phase 2: Main Loop Functions** (called repeatedly in `loop()`)

#### 4. `waitForPING(int timeoutMs)` 🎯 **CRITICAL**
- **Purpose**: Listen for gateway PING broadcast
- **Called**: Once per loop, earliest action
- **Parameters**: timeoutMs (PING_WAIT_MS or FIRST_SYNC_PING_WAIT_MS)
- **Flow**:
  1. Send "radio rx 0" (listen until packet or watchdog)
  2. Call readLoRaLine(timeoutMs)
  3. If "radio_rx <hex>" received:
     - Extract hex payload
     - Call hexToBytes() to convert to 8 bytes
     - Call xorWithKey() to decrypt
     - Validate: bytes 0-3 == "PING" (0x50,0x49,0x4E,0x47)
     - Validate: bytes 4-7 == SHARED_KEY
     - Return true if both checks pass
  4. If timeout or "radio_err": return false
- **Returns**: bool (true = valid PING, false = timeout/invalid)

#### 5. `collectSensorData(SensorReading& out)` 📊
- **Purpose**: Read all sensors and populate SensorReading struct
- **Called**: After PING received
- **Parameters**: out (reference to SensorReading to fill)
- **Flow**:
  1. Read DHT22:
     - humidity = DHT.readHumidity()
     - tempC = DHT.readTemperature()
     - Convert to tenths: tempTenthsC = (int16_t)(tempC * 10)
  2. Read light sensor (GPIO34):
     - lux = analogRead(LIGHT_PIN) + TSL2561 I2C if available
     - Convert ADC to lux (calibration factor)
  3. Read soil moisture (GPIO35):
     - soilMoistureRaw = analogRead(SOIL_PIN) (0-4095)
  4. Read water leak (GPIO33):
     - waterLeak = (analogRead(LEAK_PIN) > threshold)
  5. Read battery voltage:
     - battery = analogRead(BAT_PIN) * calibration_factor
     - Convert to percentage (0-100%)
  6. Return (struct filled)
- **Returns**: void (modifies out parameter)

#### 6. `transmitSensorData(const SensorReading& reading)` 📤
- **Purpose**: Encode sensor data and transmit via LoRa
- **Called**: After sensor collection
- **Parameters**: reading (SensorReading struct with all sensor values)
- **Flow**:
  1. Build 13-byte buffer:
     - Bytes 0-3: DEVICE_ID (0x01AABBCC, big-endian)
     - Bytes 4-5: tempTenthsC (big-endian int16_t)
     - Byte 6: humidity
     - Bytes 7-8: lux (big-endian uint16_t)
     - Bytes 9-10: soilMoistureRaw (big-endian)
     - Byte 11: waterLeak (0x00 or 0x01)
     - Byte 12: battery
  2. XOR encrypt: xorWithKey(buffer, 13)
  3. Convert to hex: bytesToHex(buffer, 13) → 26-char string
  4. Send: "radio tx <hex>"
  5. Wait for "radio_tx_ok" response
  6. Log to Serial
- **Returns**: bool (true = TX successful, false = failed)

#### 7. `waitForLightCommand(int timeoutMs)` 💡
- **Purpose**: Listen for light control and sleep interval update from gateway
- **Called**: After TX completes
- **Parameters**: timeoutMs (POST_TX_RX_MS, typically 8 seconds)
- **Flow**:
  1. Send "radio rx 0"
  2. Call readLoRaLine(timeoutMs)
  3. If "radio_rx <hex>" received:
     - Extract 8-byte payload (16-char hex)
     - Decrypt: xorWithKey()
     - Parse:
       - Bytes 0-3: check device ID (must match DEVICE_ID)
       - Bytes 4-5: desiredLux (0 if no light, 500+ if needed)
       - Bytes 6-7: nextSleepSeconds
     - If device ID matches:
       - Update rtcSleepSeconds = nextSleepSeconds
       - Clamp to MIN_SLEEP_S (10) and MAX_SLEEP_S (65000)
  4. If timeout or invalid: keep current rtcSleepSeconds
- **Returns**: bool (true = valid command, false = timeout/no command)

#### 8. `displayOnOLED(const SensorReading& reading)` 📺
- **Purpose**: Show sensor data on optional OLED display
- **Called**: After sensor collection (debugging only)
- **Parameters**: reading (SensorReading struct)
- **Flow**:
  1. If oledConnected:
     - display.clearDisplay()
     - display.setCursor(0, 0)
     - Print temperature, humidity, lux, soil, battery
     - display.display()
- **Returns**: void

---

### **Phase 3: LoRa Communication Functions**

#### 9. `readLoRaLine(int timeoutMs)`
- **Purpose**: Read one line of text from RN2483 with timeout
- **Called**: By waitForPING() and waitForLightCommand()
- **Parameters**: timeoutMs (maximum time to wait)
- **Flow**:
  1. Initialize timer and empty String
  2. While millis() < timeout:
     - If loraSerial.available():
       - Read char
       - Skip '\r'
       - If '\n': trim and return
       - Else: append to line
     - Delay 1ms
  3. If timeout: return whatever accumulated (possibly empty)
- **Returns**: String (one line, or empty if timeout)

#### 10. `flushLoRaInput()`
- **Purpose**: Clear pending data in LoRa serial buffer
- **Called**: Before entering RX mode
- **Parameters**: None
- **Flow**:
  1. While loraSerial.available(): read and discard
- **Returns**: void

#### 11. `sendLoRaCommand(const String& cmd, int timeoutMs)`
- **Purpose**: Send AT command to RN2483 and get response
- **Called**: During initLoRa() and radio control
- **Parameters**:
  - cmd: AT command (e.g., "radio tx ABCDEF")
  - timeoutMs: timeout for response
- **Flow**:
  1. Print to loraSerial with "\r\n" terminator
  2. Call readLoRaLine(timeoutMs)
  3. Log to Serial
  4. Return response
- **Returns**: String (response from RN2483)

---

### **Phase 4: Sensor Reading Functions**

#### 12. `readDHT22(float& temp, uint8_t& humidity)`
- **Purpose**: Read temperature and humidity from DHT22
- **Called**: By collectSensorData()
- **Parameters**:
  - temp: reference to store temperature (°C)
  - humidity: reference to store humidity (%)
- **Flow**:
  1. Call DHT.readTemperature()
  2. Call DHT.readHumidity()
  3. Check for read errors
  4. Store in references
- **Returns**: bool (true = success, false = sensor error)

#### 13. `readLightSensor()` 💡
- **Purpose**: Read ambient light level from photoresistor (GPIO34)
- **Called**: By collectSensorData()
- **Parameters**: None
- **Flow**:
  1. Read analogRead(LIGHT_PIN) (0-4095 ADC)
  2. Apply calibration: lux = raw_adc * calibration_factor
  3. Return lux value
- **Returns**: uint16_t (lux, 0-65535)

#### 14. `readSoilMoisture()`
- **Purpose**: Read soil moisture from capacitive sensor (GPIO35)
- **Called**: By collectSensorData()
- **Parameters**: None
- **Flow**:
  1. Read analogRead(SOIL_PIN) (0-4095 ADC)
  2. No calibration needed (raw ADC is acceptable)
  3. Return value
- **Returns**: uint16_t (0-4095)

#### 15. `readWaterLeak()`
- **Purpose**: Check water leak detector (GPIO33)
- **Called**: By collectSensorData()
- **Parameters**: None
- **Flow**:
  1. Read analogRead(LEAK_PIN) or digitalWrite(LEAK_PIN)
  2. Compare to threshold (if analog) or check GPIO state (if digital)
  3. Return true if leak detected, false if dry
- **Returns**: bool

#### 16. `readBattery()`
- **Purpose**: Read battery voltage via ADC
- **Called**: By collectSensorData()
- **Parameters**: None
- **Flow**:
  1. Read analogRead(BATTERY_PIN) (ADC on 0-3.3V)
  2. Convert ADC (0-4095) to voltage: V = raw * 3.3 / 4095
  3. Convert to percentage:
     - 3.0V (depleted) = 0%
     - 3.3V (full) = 100%
     - percentage = (voltage - 3.0) * 100 / 0.3
  4. Clamp to 0-100%
- **Returns**: uint8_t (battery percentage, 0-100)

---

### **Phase 5: Utility Functions**

#### 17. `bytesToHex(const uint8_t* data, size_t len)`
- **Purpose**: Convert byte array to hex string
- **Called**: By transmitSensorData()
- **Parameters**:
  - data: input byte array
  - len: number of bytes
- **Flow**:
  1. For each byte:
     - Extract high nibble (bits 7-4): (byte >> 4) & 0x0F
     - Extract low nibble (bits 3-0): byte & 0x0F
     - Convert each to hex char ('0'-'9', 'A'-'F')
     - Append both to output String
  2. Return uppercase hex string
- **Returns**: String (2*len hex characters)

#### 18. `hexToBytes(const String& hexStr, uint8_t* out, size_t outLen)`
- **Purpose**: Convert hex string to byte array
- **Called**: By waitForPING() and waitForLightCommand()
- **Parameters**:
  - hexStr: hex string (e.g., "50494E47A37F2C91")
  - out: output byte array
  - outLen: size of output array
- **Flow**:
  1. Validate hexStr.length() == outLen * 2
  2. For each pair of hex chars:
     - Call hexNibble() for high nibble
     - Call hexNibble() for low nibble
     - Combine: byte = (high << 4) | low
     - Store in out[i]
  3. Return true if success
- **Returns**: bool

#### 19. `hexNibble(char c)`
- **Purpose**: Convert single hex character to 0-15
- **Called**: By hexToBytes()
- **Parameters**: c (hex char '0'-'9', 'a'-'f', 'A'-'F')
- **Flow**:
  1. If c >= '0' && c <= '9': return c - '0'
  2. If c >= 'a' && c <= 'f': return c - 'a' + 10
  3. If c >= 'A' && c <= 'F': return c - 'A' + 10
  4. Else: return -1 (invalid)
- **Returns**: int (0-15, or -1 for error)

#### 20. `xorWithKey(uint8_t* data, size_t len)`
- **Purpose**: XOR encrypt/decrypt data with SHARED_KEY
- **Called**: By waitForPING(), transmitSensorData(), waitForLightCommand()
- **Parameters**:
  - data: byte array to encrypt/decrypt (modified in place)
  - len: length of data
- **Flow**:
  1. For each byte at index i:
     - data[i] ^= SHARED_KEY[i % 4]
  2. (XOR is reversible; apply twice to recover original)
- **Returns**: void (modifies data in place)

#### 21. `setLed(bool on)`
- **Purpose**: Turn sensor node LED on/off (activity indicator)
- **Called**: When transmitting or receiving
- **Parameters**: on (true = LED on, false = LED off)
- **Flow**:
  1. digitalWrite(LED_PIN, on ? HIGH : LOW)
  2. Log to Serial
- **Returns**: void

---

## Communication Protocols

### UART1: RN2483 LoRa P2P (57600 baud)

#### AT Command Format
```
Request (Node → RN2483):
  <command>\r\n
  Example: "radio tx 50494E47A37F2C91\r\n"

Response (RN2483 → Node):
  <response>\r\n
  Examples:
    "ok\r\n"            (command accepted)
    "radio_tx_ok\r\n"   (TX complete)
    "radio_rx 50494E47A37F2C91\r\n"  (packet received)
    "radio_err\r\n"     (RX timeout/error)
```

#### Common Commands
```
Initialization:
  sys get ver           → Get RN2483 version
  mac pause             → Disable LoRaWAN, enable P2P

Radio Configuration (same as gateway):
  radio set freq 868100000
  radio set sf sf12
  radio set pwr 14
  radio set crc on
  [etc., 13 commands total]

Reception:
  radio rx 0            → Listen indefinitely until packet or watchdog
  Response: "ok" (listening started)
           Later: "radio_rx <hex>" (packet received)
                 or "radio_err" (timeout)

Transmission:
  radio tx <hex>        → Transmit packet (hex-encoded)
  Response: "ok" (accepted)
           Later: "radio_tx_ok" (TX complete)
```

### MQTT (Gateway Receives, Node Doesn't Directly Access)

**Gateway publishes sensor data from this node**:
```json
{
  "type": "sensorData",
  "deviceId": "0x01AABBCC",
  "temperature": 23.5,
  "humidity": 65,
  "lux": 150,
  "soilMoisture": 512,
  "waterLeak": false,
  "battery": 92,
  "needsLight": true
}
```

---

## Setup & Deployment

### Prerequisites

#### Hardware
- ESP32 DevKit board
- RN2483 LoRa module
- DHT22 temperature/humidity sensor
- KY-018 photoresistor or TSL2561 I2C light sensor
- Capacitive soil moisture sensor
- Water leak detector
- 2×AA battery holder
- USB cable for programming
- (Optional) SH1106 OLED 128x64 display

#### Libraries
```
Arduino IDE → Sketch → Include Library → Manage Libraries

Install:
  - DHT (by Adafruit)
  - Adafruit_SH110X (for OLED, optional)
  - HardwareSerial (built-in)
```

### Configuration

**File**: [sensor-node/transmitter.cpp](transmitter.cpp) (lines 65-75)

```cpp
#define DEVICE_ID                0x01AABBCC
#define PING_WAIT_MS             12000UL
#define FIRST_SYNC_PING_WAIT_MS  130000UL
#define POST_TX_RX_MS            8000UL
#define DEFAULT_SLEEP_S          115UL

// Shared secret (must match gateway)
static const uint8_t SHARED_KEY[4] = { 0xA3, 0x7F, 0x2C, 0x91 };

// Calibration factors (adjust for your specific sensors)
#define LUX_CALIBRATION_FACTOR   0.48  // Convert ADC to lux
#define BATTERY_MIN_VOLTAGE      3.0   // Depleted
#define BATTERY_MAX_VOLTAGE      3.3   // Full
```

**Important**: Make sure `DEVICE_ID` (0x01AABBCC) matches the gateway's `APPROVED_DEVICE_IDS` array:

```cpp
// In gateway.cpp:
static const uint32_t APPROVED_DEVICE_IDS[] = {
  0x01AABBCC   // Must match sensor node DEVICE_ID
};
```

### Flashing Procedure

```
Board: ESP32 Dev Module
Upload Speed: 921600
Port: COM[your device]
File: sensor-node/transmitter.cpp
```

### Verification

**Serial Monitor Output** (115200 baud, during programming/debugging):
```
ESP32 Sensor Node starting...
RN2483 connected.
Sensor node ready.
[Listening for PING... (12000 ms)]
[PING received!]
Temperature: 23.5°C
Humidity: 65%
Lux: 150
Soil Moisture: 512
Water Leak: No
Battery: 92%
[Sending payload...]
[TX complete]
[Listening for light command... (8000 ms)]
Sleep interval = 118 seconds
[Going to deep sleep...]

[After ~118 seconds, cycle repeats]
```

**Battery Operation**:
Once deployed on battery, Serial Monitor will show no output (USB disconnected). LED blinks during TX will indicate activity.

---

## Power Management Tips

### Extending Battery Life

1. **Increase PING_INTERVAL** in gateway (less frequent wake-ups)
2. **Use higher minimum sleep duration** (MIN_SLEEP_S in code)
3. **Disable OLED display** (comment out `displayOnOLED()` call)
4. **Reduce sensor read frequency** (skip some readings)
5. **Use lower LoRa power** (LORA_PWR = 10 instead of 14 dBm, slight range loss)

### Expected Battery Life

With default settings (120-second PING interval, 2×AA battery):
- **Normal operation**: 6-12 months
- **Power-optimized**: 12-24 months
- **Worst case** (continuous RX, no sleep): 1-2 days

---

## Summary

The **Sensor Node** provides reliable, long-lived environmental monitoring by:

| Aspect | Detail |
|--------|--------|
| **Power Source** | 2×AA Battery (~3V) |
| **Runtime** | 6-12 months on default settings |
| **Wake Cycle** | Synchronized to 120-second gateway PING |
| **Sensors** | Temperature, humidity, light, soil moisture, water leak, battery |
| **Transmission** | LoRa P2P, 13-byte encrypted payload |
| **Sleep Strategy** | 99% deep sleep, 1% active |
| **Range** | ~1-2 km P2P (depending on obstacles, SF12) |

**Key Functions**:
1. Wait for PING from gateway
2. Collect sensor readings
3. Encrypt and transmit 13-byte payload
4. Receive light control + sleep interval commands
5. Return to deep sleep for next cycle

See [Main Gateway Documentation](../gateway/README.md) for system-wide architecture.
