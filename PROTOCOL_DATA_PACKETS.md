# Data Packet Overview by Protocol

## Summary Table

| Protocol | Direction | Sender | Receiver | Payload Size | Data Fields |
|----------|-----------|--------|----------|--------------|-------------|
| **LoRa P2P** | TX | Sensor Node | Gateway | 13 bytes | Device ID, Temperature, Humidity, Lux, Soil Moisture, Water Leak, Battery |
| **LoRa P2P** | TX | Light Node | Gateway | 4 bytes | Device ID |
| **LoRa P2P** | RX | Light Node | Gateway | 5 bytes | Device ID, Command (ON/OFF) |
| **LoRa P2P** | TX | Gateway | Light Nodes | 5 bytes | Device ID, Command (ON/OFF) |
| **LoRa P2P** | TX | Gateway | Sensor Nodes (Wake) | 4 bytes | PING command ("PING" in ASCII hex) |
| **LoRa P2P** | TX | Light Node ACK | Gateway | 5 bytes | Device ID, Result (ACK OK/FAIL) |

---

## Detailed Protocol Specifications

### 1. **LoRa P2P - Sensor Node to Gateway**

**Payload Size:** 13 bytes

**Data Layout:**

```
Byte offset:  0    1    2    3    4    5    6    7    8    9   10   11   12   13   
              ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
Field:        │     Device ID     │   Temp     │Hum │   Lux      │   Soil     │Lk  │Bat │
              └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
Type:         │   uint32 (4 B)    │ int16(2B)  │ u8 │ uint16(2B) │ uint16(2B) │ u8 │ u8 │
```

**Field Details:**

| Offset | Field | Type | Size | Unit | Range | Example |
|--------|-------|------|------|------|-------|---------|
| 0–3 | Device ID | uint32 | 4 B | — | 0x01xxxxxx (sensor) | 0x010000A1 |
| 4–5 | Temperature | int16 | 2 B | °C × 10 | −400 to +600 | 245 = 24.5°C |
| 6 | Humidity | uint8 | 1 B | % | 0–100 | 55 |
| 7–8 | Lux | uint16 | 2 B | lux | 0–65535 | 3000 |
| 9–10 | Soil Moisture | uint16 | 2 B | ADC raw | 0–4095 | 2048 |
| 11 | Water Leak | uint8 | 1 B | bool | 0 (dry) / 1 (leak) | 0 |
| 12 | Battery | uint8 | 1 B | % | 0–100 | 85 |

**Transmission Format:** Hex string (26 hex characters)
- Example: `010000A1F53703BC0800020048008055`

---

### 2. **LoRa P2P - Light Node Registration (to Gateway)**

**Payload Size:** 4 bytes

**Data Layout:**

```
Byte offset:  0    1    2    3
              ┌────┬────┬────┬────┐
Field:        │      Device ID    │
              └────┴────┴────┴────┘
Type:         │     uint32 (4 B)  │
```

**Field Details:**

| Offset | Field | Type | Size | Unit | Range |
|--------|-------|------|------|------|-------|
| 0–3 | Device ID | uint32 | 4 B | — | 0x02xxxxxx (light node) |

---

### 3. **LoRa P2P - Gateway to Light Nodes (Command)**

**Payload Size:** 5 bytes

**Data Layout:**

```
Byte offset:  0    1    2    3    4
              ┌────┬────┬────┬────┬────┐
Field:        │   Device ID       │Cmd │
              └────┴────┴────┴────┴────┘
Type:         │    uint32 (4 B)   │ u8 │
```

**Field Details:**

| Offset | Field | Type | Size | Unit | Values | Purpose |
|--------|-------|------|------|------|--------|---------|
| 0–3 | Device ID | uint32 | 4 B | — | 0x02xxxxxx | Target light node |
| 4 | Command | uint8 | 1 B | — | 0x00 = OFF, 0x01 = ON | LED state control |

---

### 4. **LoRa P2P - Light Node ACK (to Gateway)**

**Payload Size:** 5 bytes

**Data Layout:**

```
Byte offset:  0    1    2    3    4
              ┌────┬────┬────┬────┬────┐
Field:        │      Device ID    │Res │
              └────┴────┴────┴────┴────┘
Type:         │     uint32 (4 B)  │ u8 │
```

**Field Details:**

| Offset | Field | Type | Size | Unit | Values |
|--------|-------|------|------|------|--------|
| 0–3 | Device ID | uint32 | 4 B | — | 0x02xxxxxx (sender's ID) |
| 4 | Result | uint8 | 1 B | — | 0x00 = ACK_OK, 0x01 = ACK_FAIL |

---

### 5. **LoRa P2P - Gateway Wake Ping (to Sensor Nodes)**

**Payload Size:** 4 bytes

**Data Layout:**

```
Byte offset:  0    1    2    3
              ┌────┬────┬────┬────┐
Field:        │  P  │  I  │  N  │  G  │
              └────┴────┴────┴────┘
Type:         │     ASCII "PING"      │
```

**Format:** ASCII string "PING" sent as hex `50494E47`

**Purpose:** Wake sleeping sensor nodes during their assigned time slot

---

## Device ID Structure

**Format:** uint32 with Type Flag in MSB (Most Significant Byte)

```
   Byte 3 (MSB)   Byte 2        Byte 1        Byte 0 (LSB)
  ┌────────────┬──────────────────────────────────────────┐
  │  Type flag │         Unique Hardware ID               │
  └────────────┴──────────────────────────────────────────┘
       0x01 = Sensor node
       0x02 = Light node
```

**Examples:**
- Sensor node: `0x010000A1` (Type: 0x01, ID: 0x0000A1)
- Light node: `0x020000B2` (Type: 0x02, ID: 0x0000B2)

---

## Protocol Timeline

### Boot / Node Discovery Phase

1. **Gateway starts with hardcoded approved device ID list**
   ```c
   #define APPROVED_DEVICE_IDS {0x010000A1, 0x010000A2, 0x010000A3, 0x020000B1, 0x020000B2}
   ```
2. **Nodes transmit their Device ID** at startup / power cycle
3. **Gateway validates** against approved list (white-list)
4. **If approved:** node stays online and joins operational cycle
5. **If not approved:** gateway ignores packets from that device

### Operational Cycle (T_cycle)

```
Deep Sleep → Gateway Wake → Sensor RX → Light TX → MQTT → Sleep
 (T_DS)      (1-2s)        (T_slot)   (T_slot)   (~5s)  (4-6h)
```

---

## Transmission Speeds & Timing

| Parameter | Value | Notes |
|-----------|-------|-------|
| LoRa Frequency | 865 MHz | EU ISM band |
| Spreading Factor | SF12 | Long range, slower speed |
| Bandwidth | 125 kHz | Standard LoRa |
| Data Rate | ~100 bps | SF12 at 125 kHz |
| Estimated TX Time (13-byte payload) | ~1–2 seconds | Depends on LoRa settings |
| MQTT Publish Interval | ~30 seconds (test) / 5–10 min (deployment) | Configurable |
| Auto-Wake Ping Interval | 2 min (test) / 6 hours (deployment) | Defined as PING_INTERVAL_MS |
| Gateway Heartbeat | 60 seconds | HEARTBEAT_INTERVAL_MS |

---

## Configuration Constants

### LoRa Radio Settings (RN2483)

```c
#define LORA_FREQ   865000000UL    // 865 MHz (EU)
#define LORA_PWR    14             // 14 dBm
#define LORA_SF     "sf12"         // Spreading Factor 12
#define LORA_AFCBW  "41.7"         // AFC Bandwidth
#define LORA_RXBW   125            // RX Bandwidth (kHz)
#define LORA_PRLEN  8              // Preamble Length
#define LORA_CRC    "on"           // CRC enabled
#define LORA_IQI    "off"          // IQ Inversion off
#define LORA_CR     "4/5"          // Coding Rate
#define LORA_WDT    60000UL        // Watchdog Timer (ms)
#define LORA_SYNC   "12"           // Sync Word
#define LORA_BW     125            // Bandwidth (kHz)
```

### Sleep Configuration

```c
#define T_DS        20             // Deep Sleep (seconds, 4h = 14400 deployment)
#define POST_PING_WAIT_MS  10000   // Listen window after ping (10s)
#define PING_INTERVAL_MS   120000  // Auto-ping every 2 min (test) / 6h (deployment)
```

---

## Known Issues & Limitations

1. **No Light Node ACK Listen** – Gateway doesn't verify light commands were received
2. **No Local MQTT Buffer** – Missed cycles during connectivity loss discard data
3. **Unencrypted Registration** – Hardcoded password vulnerable to sniffing
4. **Clock Drift** – Nodes need re-sync every 4–6 hours to prevent slot misalignment
5. **Linear Cycle Growth** – Cycle time = (N nodes × T_slot) + overhead; large N can exceed 4-hour target
