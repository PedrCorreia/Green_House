# Data Packets - Implementation

## Summary

| Type | Sender | Receiver | Size | Format |
|------|--------|----------|------|--------|
| Sensor Data | Sensor Node | Gateway | 13 bytes | Device ID + Temperature + Humidity + Lux + Soil + Leak + Battery |
| Light Command | Gateway | Light Node | 5 bytes | Device ID + Command (0x00=OFF, 0x01=ON) |
| Light ACK | Light Node | Gateway | 5 bytes | Device ID + Result (0x00=OK, 0x01=FAIL) |
| Wake Ping | Gateway | Sensor Nodes | 4 bytes | "PING" in hex (0x50494E47) |

---

## 1. Sensor Payload (13 bytes)

```
Offset  Field           Type      Size
0-3     Device ID       uint32    4 B
4-5     Temperature     int16     2 B   (°C × 10, e.g., 245 = 24.5°C)
6       Humidity        uint8     1 B   (0-100%)
7-8     Lux             uint16    2 B   (0-65535)
9-10    Soil Moisture   uint16    2 B   (ADC raw)
11      Water Leak      uint8     1 B   (0=dry, 1=leak)
12      Battery         uint8     1 B   (0-100%)
```

Example hex: `010000A1F53703BC0800020048008055`

---

## 2. Light Command (5 bytes)

```
Offset  Field      Type      Size
0-3     Device ID  uint32    4 B
4       Command    uint8     1 B   (0x00=OFF, 0x01=ON)
```

---

## 3. Light ACK (5 bytes)

```
Offset  Field      Type      Size
0-3     Device ID  uint32    4 B
4       Result     uint8     1 B   (0x00=OK, 0x01=FAIL)
```

---

## 4. Wake Ping (4 bytes)

```
Hex: 50494E47
ASCII: "PING"
```

---

## Device ID Format

```
Byte 3 (MSB)  |  Bytes 2-0 (LSB)
Type flag     |  Unique ID
0x01          |  Sensor node
0x02          |  Light node
```

Example: `0x010000A1` = Sensor node with ID 0x0000A1

---

## Gateway Device Validation

Gateway has hardcoded whitelist:

```c
#define APPROVED_DEVICE_IDS {0x010000A1, 0x010000A2, 0x010000A3, 0x020000B1}
```

Only approved Device IDs are processed. Others are ignored.

---

## LoRa Settings (RN2483)

- Frequency: 865000000 Hz (865 MHz, EU)
- Power: 14 dBm
- Spreading Factor: SF12
- AFC Bandwidth: 41.7
- RX Bandwidth: 125 kHz
- TX Bandwidth: 125 kHz
- Preamble Length: 8
- CRC: enabled
- IQ Inversion: disabled
- Coding Rate: 4/5
- Watchdog Timer: 60000 ms
- Sync Word: 0x12
- UART Baud: 57600
