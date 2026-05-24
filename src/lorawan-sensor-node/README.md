# ESP32 LoRaWAN Sensor Node - Cloud-Direct Environmental Monitor

## Table of Contents
1. [System Overview](#system-overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Integration with Cloud](#integration-with-cloud)
4. [Data Structures](#data-structures)
5. [Execution Flow](#execution-flow)
6. [Methods Reference](#methods-reference)
7. [Communication Protocols](#communication-protocols)
8. [Setup & Deployment](#setup--deployment)

---

## System Overview

### Purpose
The **LoRaWAN Sensor Node** is an alternative to the P2P sensor node that connects directly to the cloud (Loriot, TTN, or other LoRaWAN network server) instead of relying on the main gateway. It uploads sensor data periodically and can receive commands via Class A downlinks.

### Key Responsibilities
- **LoRaWAN Join**: Perform OTAA join to Loriot EU868 cluster once at startup
- **Periodic Uplinks**: Send 13-byte sensor payload every TX_INTERVAL_MS (30-3600 seconds)
- **Downlink Reception**: Receive frequency/sleep updates via Class A downlinks
- **Sleep Management**: Deep sleep between transmissions (configurable interval)
- **Independent Operation**: Works without main gateway (different network topology)

### When to Use
- **Dedicated cloud pathway**: When P2P coverage is poor but LoRaWAN coverage is good
- **Cloud-first monitoring**: Direct data logging in LoRaWAN network backend
- **Multi-network redundancy**: Sensor data goes to cloud AND P2P gateway (if both devices present)
- **Standalone deployment**: Single sensor node in area without main gateway
- **Device ID**: 0x02AABBCC (MSB 0x02 identifies as LoRaWAN node type)

---

## Hardware Architecture

### Main Components

#### **ESP32 Microcontroller (LoRaWAN Sensor)**
- **Role**: LoRaWAN endpoint with sensors
- **Power**: Battery (2×AA) or USB, with periodic deep sleep
- **UART Ports**:
  - **UART1** (pins 18/19): Connected to RN2483 LoRaWAN module at 57600 baud
  - **UART0** (USB): USB Serial for debugging at 115200 baud (programming only)
- **Sensors**: Same as P2P sensor node (DHT22, light, soil moisture, water leak, battery)
- **Sleep**: RTC timer for wake-up on interval
- **Device ID**: 0x02AABBCC (MSB 0x02 = LoRaWAN node type)

#### **RN2483 LoRa Module (LoRaWAN)**
- **Communication**: Connected via UART1 at 57600 baud
- **Protocol**: LoRaWAN Class A (OTAA join)
- **Frequency**: 868.1 MHz (EU868 cluster)
- **Network**: Loriot backend (iotnet.teracom.dk)
- **Purpose**: Send sensor data to cloud, receive downlinks
- **Interface**: AT command protocol (via rn2xx3 library)

### Pin Mapping

```
ESP32 (LoRaWAN Sensor Node) Pin Configuration:
├─ UART1 (LoRaWAN):
│  ├─ GPIO18 (RX) <- RN2483 TX
│  └─ GPIO19 (TX) -> RN2483 RX
├─ GPIO23: RN2483 Reset (active-low)
├─ GPIO2: LED (activity indicator)
├─ Analog Sensors (same as P2P node):
│  ├─ GPIO34: Light sensor
│  ├─ GPIO35: Soil moisture
│  └─ GPIO33: Water leak
├─ GPIO4: DHT22 data line
└─ UART0 (USB Serial @ 115200):
   └─ Debugging during development only

Baud Rates:
├─ UART1: 57600 (RN2483 standard)
└─ USB Serial: 115200 (debugging)
```

---

## Integration with Cloud

### Data Flow Architecture

```
                    ☁️ Loriot Cloud
                    (EU868 cluster)
                         ↑
                    LoRaWAN uplink
                    (port 1 or 2)
                         |
    ┌────────────────────┴────────────────┐
    │   RN2483 LoRaWAN Module            │
    │   (EU868, Class A)                 │
    └────────────────────┬────────────────┘
                         ↑
                    UART1 @ 57600
                         |
    ┌─────────────────────────────────────┐
    │  ESP32 LoRaWAN Sensor Node          │
    │  ├─ DHT22 (temp/humidity)           │
    │  ├─ Light sensor (lux)              │
    │  ├─ Soil moisture (ADC)             │
    │  ├─ Water leak detector             │
    │  └─ Battery ADC                     │
    └─────────────────────────────────────┘
         ↓
    Deep sleep (60-3600 seconds)
         ↓
    [RTC timer wakes CPU]
         ↓
    Repeat cycle
```

### LoRaWAN Class A Receive Window

```
T=0s    Device sends uplink to Loriot on port 2
        └─ "mac tx ucnf 2 <13-byte payload>"

T=0-5s  RN2483 waits for confirmation
        └─ "mac_tx_ok" (uplink confirmed sent)

T=5-6s  Device listens on RX1 window
        └─ Loriot can send downlink if queued
        └─ Downlink received as "mac_rx <port> <data>"

T=6s    Device enters deep sleep

[If downlink received]
  ├─ Parse port (e.g., port 2 = frequency update)
  ├─ Extract 2-byte interval (big-endian)
  ├─ Update txIntervalMs for next cycle
  └─ Return to sleep
```

### Comparison: LoRaWAN vs. P2P

| Aspect | LoRaWAN Node | P2P Sensor Node |
|--------|--------------|-----------------|
| **Network Type** | Cloud-based LoRaWAN | Local P2P broadcast |
| **Gateway Required** | Loriot only | Main gateway (always-on) |
| **Connectivity** | Anywhere in Loriot coverage | ~1-2 km to main gateway |
| **Device ID** | 0x02AABBCC (cloud-registered) | 0x01AABBCC (local whitelist) |
| **Data Path** | Direct to Loriot cloud | Via gateway to MQTT/cloud |
| **Downlink** | From Loriot backend | From main gateway |
| **Power** | Battery, periodic sleep | Battery, PING-synchronized sleep |
| **Setup** | Register on Loriot + OTAA | Update APPROVED_DEVICE_IDS on gateway |

---

## Data Structures

### LoRaWAN Uplink Payload (13 bytes, port 2)

**Same format as P2P sensor data, but with LoRaWAN encryption**:
```
Bytes 0-3:   Device ID (0x02AABBCC, big-endian)
Bytes 4-5:   Temperature (int16_t / 10, big-endian)
Byte 6:      Humidity (0-100%)
Bytes 7-8:   Lux (big-endian uint16_t)
Bytes 9-10:  Soil Moisture (big-endian uint16_t)
Byte 11:     Water Leak (0x00 or 0x01)
Byte 12:     Battery (0-255%, or raw ADC)

Encryption: Automatic LoRaWAN encryption (AppSKey + NwkSKey)
  └─ RN2483 and Loriot backend handle AES encryption transparently
```

### LoRaWAN Downlink Packet (2+ bytes, port 2)

**Received from Loriot, typically a frequency/sleep update**:
```
Byte 0-1:   TX interval in seconds (big-endian uint16_t)
  Example: [0x00, 0x78] = 120 seconds

Byte 2+:    Optional additional parameters

Decryption: Automatic LoRaWAN decryption (hardware)
```

---

## Execution Flow

### Boot Sequence (First Time)

```
main() [ESP32 bootloader]
  ↓
setup()
  ├─ Initialize Serial (115200 USB debug)
  ├─ Print "LoRaWAN Sensor Node starting..."
  │
  ├─ connectToLoRa()
  │  ├─ Reset RN2483 (GPIO23 pulse)
  │  ├─ Begin UART1 @ 57600 baud
  │  ├─ Get hardware EUI ("sys get hweui")
  │  │  └─ Print to Serial: "HWEUI: 70B3D5XXXXXXXX"
  │  │     [User copies this to Loriot registration]
  │  └─ Print "Register this HWEUI on Loriot"
  │
  ├─ joinLoRaWAN()
  │  ├─ Set APPEUI from code define
  │  ├─ Set APPKEY from code define
  │  ├─ Attempt "mac join otaa"
  │  ├─ Wait up to 10 seconds for "accepted"
  │  │
  │  ├─ If accepted:
  │  │  ├─ Set joined = true
  │  │  ├─ Print "Joined successfully"
  │  │  └─ Proceed to main loop
  │  │
  │  └─ If timeout:
  │     ├─ Set joined = false
  │     ├─ Print "Join failed, retrying in loop"
  │     └─ Retry in loop every iteration
  │
  └─ Print "Node ready"

[Loop begins]
```

### Main Loop (continuous)

```
loop() [runs until sleep interval OR timeout]
  ├─ Check if joined to Loriot:
  │  └─ If not: attempt rejoin
  │
  ├─ Check if time to send uplink:
  │  ├─ If (now - lastTxMs) >= txIntervalMs:
  │  │
  │  ├─ Collect sensor data:
  │  │  ├─ DHT22: temp, humidity
  │  │  ├─ Light sensor: lux
  │  │  ├─ Soil moisture: ADC value
  │  │  ├─ Water leak: boolean
  │  │  └─ Battery: ADC percentage
  │  │
  │  ├─ Build 13-byte payload
  │  ├─ Send uplink:
  │  │  ├─ "mac tx ucnf 2 <hex_payload>"
  │  │  │  (ucnf = unconfirmed, confirmed also available)
  │  │  └─ Wait for "mac_tx_ok"
  │  │
  │  ├─ Listen for Class A downlink (1 second):
  │  │  ├─ If "mac_rx 2 <data>" received:
  │  │  │  ├─ Parse 2-byte interval (big-endian)
  │  │  │  ├─ Update txIntervalMs for next cycle
  │  │  │  └─ Clamp to MIN/MAX range
  │  │  └─ If no downlink: keep current interval
  │  │
  │  └─ Update lastTxMs = millis()
  │
  ├─ If sleep interval enabled:
  │  └─ Check if in sleep window
  │     ├─ If time to sleep:
  │     │  ├─ Print "Entering deep sleep for X seconds"
  │     │  ├─ esp_sleep_enable_timer_wakeup()
  │     │  └─ esp_deep_sleep()
  │     │
  │     └─ CPU wakes after timer, setup() runs again
  │
  └─ Repeat loop (if not sleeping)
```

### Sleep Cycle (if battery-powered)

```
[After uplink TX + downlink RX]
  ├─ Calculate next TX time
  ├─ esp_sleep_enable_timer_wakeup(txIntervalMs * 1e6)
  └─ esp_deep_sleep()
     └─ RTC counter runs, wakes CPU when timer expires
```

---

## Methods Reference

### **Phase 1: Initialization**

#### 1. `connectToLoRa()`
- **Purpose**: Initialize RN2483 LoRaWAN module
- **Called**: Once during setup
- **Parameters**: None
- **Flow**:
  1. Reset RN2483 (GPIO23 pulse)
  2. Begin UART1 @ 57600
  3. Flush serial
  4. Get hardware EUI: "sys get hweui"
  5. Print EUI to Serial (for Loriot registration)
  6. Return success
- **Returns**: bool

#### 2. `joinLoRaWAN()`
- **Purpose**: Perform OTAA join to Loriot LoRaWAN network
- **Called**: Once during setup, retry in loop if failed
- **Parameters**: None
- **Flow**:
  1. Set APPEUI from code define
  2. Set APPKEY from code define
  3. Call myLora.join(OTAA mode)
  4. Wait up to 10 seconds for "accepted"
  5. If joined: set joined = true, return true
  6. If timeout: set joined = false, return false
- **Returns**: bool

---

### **Phase 2: Main Loop**

#### 3. `checkAndSendUplink()` 🎯 **CRITICAL**
- **Purpose**: Check if time to send and transmit sensor data
- **Called**: Every loop iteration
- **Parameters**: None
- **Flow**:
  1. If not joined: attempt rejoin
  2. If (now - lastTxMs) >= txIntervalMs:
     - Collect sensor data
     - Build 13-byte payload
     - Send uplink: "mac tx ucnf 2 <hex>"
     - Wait for "mac_tx_ok"
     - Listen for downlink (1 second window)
     - Update lastTxMs
- **Returns**: bool (true if uplink sent)

#### 4. `collectSensorData(uint8_t* payload)` 📊
- **Purpose**: Read sensors and populate 13-byte uplink payload
- **Called**: By checkAndSendUplink()
- **Parameters**: payload (13-byte buffer to fill)
- **Flow**:
  1. Read DHT22: temp, humidity
  2. Read light sensor: lux
  3. Read soil moisture: ADC
  4. Read water leak: boolean
  5. Read battery: ADC percentage
  6. Pack into 13-byte struct (big-endian)
- **Returns**: void (modifies payload)

#### 5. `sendLoRaUplink(const uint8_t* payload, size_t len)`
- **Purpose**: Send LoRaWAN uplink to Loriot on port 2
- **Called**: By checkAndSendUplink()
- **Parameters**:
  - payload: 13-byte sensor data
  - len: 13
- **Flow**:
  1. Convert payload to hex string
  2. Call myLora.txUncnf(2, hex_payload)
     - port 2 = sensor data uplink
     - ucnf = unconfirmed (no MAC-level ACK)
  3. Wait for "mac_tx_ok"
- **Returns**: bool (true if TX confirmed)

#### 6. `handleDownlink()` 📥
- **Purpose**: Receive and process Class A downlinks
- **Called**: 1 second after uplink TX
- **Parameters**: None
- **Flow**:
  1. Listen for "mac_rx <port> <data>"
  2. If received on port 2:
     - Parse 2-byte interval (big-endian uint16_t)
     - Validate range (MIN_TX_INTERVAL_MS to MAX_TX_INTERVAL_MS)
     - Update txIntervalMs
     - Log to Serial
  3. If received on other port: ignore or log
- **Returns**: void

---

### **Phase 3: LoRa Communication (via rn2xx3 library)**

#### 7. `readLoRaLine(int timeoutMs)`
- **Purpose**: Read one line from RN2483
- **Called**: By join and response reading
- **Parameters**: timeoutMs (timeout)
- **Flow**:
  1. Set timeout
  2. Read chars until '\n'
  3. Return line (or empty if timeout)
- **Returns**: String

#### 8. `sendLoRaCommand(const String& cmd, int timeoutMs)`
- **Purpose**: Send AT command to RN2483
- **Called**: During initialization
- **Parameters**:
  - cmd: AT command
  - timeoutMs: response timeout
- **Flow**:
  1. Print to loraSerial
  2. readLoRaLine()
  3. Return response
- **Returns**: String

---

### **Phase 4: Sensor Reading**

#### 9. `readDHT22(float& temp, uint8_t& humidity)`, `readLightSensor()`, `readSoilMoisture()`, `readWaterLeak()`, `readBattery()`
- **Same as P2P Sensor Node** (see [sensor-node README](../sensor-node/README.md#phase-4-sensor-reading-functions))

---

### **Phase 5: Utility Functions**

#### 10. `bytesToHex()`, `hexToBytes()`, `hexNibble()`, `setLed()`
- **Same as other nodes** (see previous component READMEs)

---

## Communication Protocols

### UART1: RN2483 LoRaWAN Module (57600 baud)

#### AT Command Format (LoRaWAN)
```
Request (Node → RN2483):
  <command>\r\n
  Example: "mac tx ucnf 2 A50149474F50\r\n"

Response (RN2483 → Node):
  <response>\r\n
  Examples:
    "ok\r\n"              (command accepted)
    "accepted\r\n"        (OTAA join successful)
    "mac_tx_ok\r\n"       (uplink transmitted)
    "mac_rx 2 XXXXXX\r\n" (downlink received on port 2)
    "mac_err\r\n"         (error)
```

#### Common Commands (LoRaWAN)
```
OTAA Join:
  sys get hweui           → Hardware EUI (register on Loriot)
  mac set appeui <hex>    → Application EUI (from Loriot)
  mac set appkey <hex>    → Application Key (from Loriot)
  mac join otaa           → Initiate join
  Response: "ok", later "accepted" or timeout

Transmission:
  mac tx ucnf 2 <hex>     → Unconfirmed uplink on port 2
  mac tx cnf 2 <hex>      → Confirmed uplink (waits for ACK)
  Response: "ok" (sent), later "mac_tx_ok" (confirmed)

Reception (automatic, 1 second after TX):
  [RN2483 automatically opens RX windows]
  mac_rx <port> <data>    → Downlink received on port
```

---

## Setup & Deployment

### Prerequisites

#### Loriot Account & Device Registration
1. **Visit iotnet.teracom.dk**
2. **Create account** and log in
3. **Register new device**:
   - Select "OTAA" mode
   - Click "Generate all except DevEUI"
   - Leave HWEUI field empty (will fill from device)
4. **Note the APPEUI and APPKEY** displayed

#### Flash the Device to Get HWEUI
1. **Upload firmware** to ESP32
2. **Open Serial Monitor** (115200 baud)
3. **Copy HWEUI** from output:
   ```
   LoRaWAN Sensor Node starting...
   HWEUI: 70B3D5XXXXXXXX
   Register this HWEUI on Loriot
   ```
4. **Return to Loriot**:
   - Fill in HWEUI field with copied value
   - Device is now registered

### Configuration

**File**: [lorawan-sensor-node/lorawan-sensor-node.cpp](lorawan-sensor-node.cpp) (lines 28-35)

```cpp
static const char* APPEUI = "0123456789ABCDEF";
static const char* APPKEY = "0123456789ABCDEF0123456789ABCDEF";

#define DEVICE_ID             0x02AABBCC
#define DEFAULT_TX_INTERVAL_MS 30000UL   // 30 seconds
#define MIN_TX_INTERVAL_MS     30000UL   // 30 sec minimum
#define MAX_TX_INTERVAL_MS   3600000UL   // 60 min maximum
```

### Flashing Procedure

```
Board: ESP32 Dev Module
Upload Speed: 921600
Port: COM[your device]
File: lorawan-sensor-node/lorawan-sensor-node.cpp
```

### Verification

**Serial Monitor Output** (115200 baud):
```
LoRaWAN Sensor Node starting...
HWEUI: 70B3D5XXXXXXXX
[Copy this to Loriot]

RN2483 connected.
Attempting LoRaWAN join (OTAA)...
Sending: mac join otaa
[Waiting for accepted...]
Joined successfully!

[Every 30 seconds]
Collecting sensors...
Temperature: 23.5°C
Humidity: 65%
Lux: 150
Soil: 512
Battery: 92%

Sending uplink on port 2...
Uplink: A50149474F50...
TX complete.
Listening for downlink (1 second)...
[No downlink]
Next uplink in 30s.

[If downlink received]
Downlink received on port 2: 0078
Parsed TX interval: 120 seconds
Updated next uplink interval.
```

**Loriot Backend**:
- Navigate to device page on iotnet.teracom.dk
- View "Recent Messages" → should show your 13-byte payloads
- Each uplink contains device ID (0x02AABBCC) + sensor data

---

## Remote Control via Downlinks

To change TX interval from Loriot dashboard:

1. **On device page**: Go to "Send downlink"
2. **Port**: 2
3. **Data (hex)**: Enter 2-byte interval
   - `0078` = 120 seconds
   - `00F0` = 240 seconds
   - `0E10` = 3600 seconds (1 hour)
4. **Send**

Device will receive on next RX window (1 second after next uplink) and update interval.

---

## Summary

The **LoRaWAN Sensor Node** provides cloud-native environmental monitoring by:

| Aspect | Detail |
|--------|--------|
| **Network** | LoRaWAN (Loriot, EU868) |
| **Device ID** | 0x02AABBCC (MSB 0x02 = LoRaWAN type) |
| **Uplink Interval** | 30-3600 seconds (configurable via downlink) |
| **Power** | Battery with periodic deep sleep |
| **Encryption** | LoRaWAN AES (automatic) |
| **Downlinks** | Class A, 1 second after TX |
| **Independence** | Works without main gateway |

**Key Differences from P2P Node**:
- ✅ Direct cloud connectivity (no gateway bridge needed)
- ✅ Official LoRaWAN standard encryption
- ✅ Works anywhere in Loriot coverage
- ❌ No local gateway backup
- ❌ Slower response (polling-based, not event-driven)

**Use Case**: 
- Long-term cloud data logging
- Coverage in remote areas (Loriot has better coverage than local P2P)
- Integration with LoRaWAN-native platforms (Loriot, TTN)
- Backup path when main gateway is offline

See [Main Gateway Documentation](../gateway/README.md) for complete system architecture.
