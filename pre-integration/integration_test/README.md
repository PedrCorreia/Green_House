# Gateway Integration Test

This folder contains the `gateway_test.ino` sketch used to validate the RN2483 receiver path, OLED status output, and end-to-end LoRa payload handling before full firmware integration.

## Test Purpose

The sketch boots an ESP32-based gateway, initializes the RN2483 over UART2, and listens for LoRa payloads. When a packet is received, the code parses the sensor payload and updates the OLED with the latest node ID, temperature, humidity, RSSI, and receive age.

## Hardware Setup

- ESP32 gateway board
- RN2483 LoRa module
- SH1106 OLED display on I2C
- UART wiring:
  - `GPIO16` -> RN2483 TX
  - `GPIO17` -> RN2483 RX
  - `GPIO2` -> RN2483 reset

## Test Result Summary

During range testing, the link remained usable out to **257.1 m** before the receiver started ranging from **4 seconds to 19 seconds** between visible updates.

This suggests the radio link is still functioning at that distance, but the receive timing is becoming unstable and should be treated as marginal rather than fully reliable.

## Known Issue

The current implementation does **not** have a working signal quality index.

At the moment, the gateway only records the raw RSSI field when it is present in the RN2483 response. If the module returns `radio_rx <payload>` without an RSSI value, the display falls back to the last known value or `n/a` in the debug output. A proper signal quality metric still needs to be implemented and validated.

## Notes

- The OLED refreshes once per second.
- The gateway re-enters continuous receive mode after each packet.
- If the screen stays on "Waiting for LoRa...", check the Serial Monitor first and confirm the RN2483 is returning `radio_rx` lines.
- The payload parser expects a 13-byte payload encoded as 26 hex characters.

## Debug Output To Watch

Successful reception should look similar to this:

```text
RN2483 raw: radio_rx 01B8F6B40108250ADD00000064
[LoRa RX] 01B8F6B40108250ADD00000064 [RSSI: n/a dBm]
  => ID   : 0x01B8F6B4
  => Temp : 25.6 C
  => Hum  : 37 %
  => Lux  : 2589 lx
  => Soil : 221
```

If you see `Invalid payload length!`, the gateway is receiving data but the payload format does not match the expected 13-byte structure.