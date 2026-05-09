# LoRa Light Control Test

This project has two Arduino sketches:

## Files

### `gateway_mqtt_cmd_test.ino`

Runs on the **Gateway ESP32**.

It:

- Connects to WiFi and MQTT.
- Receives `ON` / `OFF` from MQTT.
- Also supports Serial input:
  - `1` = light ON
  - `0` = light OFF
- Sends LoRa CMD packets to the Light Node.
- Waits for ACK from the Light Node.
- Publishes status messages to MQTT.
- Retries LoRa init if RN2483 is not ready.

### `ntg_ACK.ino`

Runs on the **Light Node ESP32**.

It:

- Listens for LoRa CMD packets.
- Checks if the packet is for device ID `5`.
- Turns the LED ON / OFF.
- Sends ACK OK or ACK FAIL back to the Gateway.
- Ignores packets for other device IDs.
- Retries LoRa init if RN2483 is not ready.

## MQTT Topics

```text
Control: esp32/myroom123/control
Status:  esp32/myroom123/status
```

## Packet Format

CMD and ACK packets are both 5 bytes:

```text
[4 bytes device ID] [1 byte command/status]
```

Light Node device ID is `5`:

```text
00000005
```

## CMD Packets

Gateway to Light Node:

| Action | Packet |
|---|---|
| Light ON | `0000000501` |
| Light OFF | `0000000500` |

## ACK Packets

Light Node to Gateway:

| Result | Packet |
|---|---|
| ACK OK | `0000000501` |
| ACK FAIL | `0000000500` |

`0000000501` can mean CMD ON or ACK OK depending on direction.

`0000000500` can mean CMD OFF or ACK FAIL depending on direction.

## Light Node Behavior

When the Light Node receives a packet:

| Case | Action |
|---|---|
| Packet length is not 5 bytes | Ignore |
| Device ID is not `5` | Ignore |
| Device ID is `5`, value is `01` | Turn LED ON, send ACK OK |
| Device ID is `5`, value is `00` | Turn LED OFF, send ACK OK |
| Device ID is `5`, value is unknown | Send ACK FAIL |

## Gateway Failure Handling

| Problem | Gateway behavior |
|---|---|
| LoRa init failed | Keep running and retry init every few seconds |
| MQTT disconnected | Keep trying to reconnect |
| WiFi disconnected | Keep trying to reconnect |
| LoRa TX failed | Publish failure status and keep running |
| ACK timeout | Retry CMD once, then publish failure if still no ACK |
| ACK FAIL | Publish failure status, do not retry |

## MQTT Status Messages

Gateway publishes to:

```text
esp32/myroom123/status
```

| Status message | Meaning |
|---|---|
| `Light ON CMD sent` | Gateway sent ON packet by LoRa |
| `Light OFF CMD sent` | Gateway sent OFF packet by LoRa |
| `Light ON ACK OK` | Light Node received ON CMD and ran GPIO command |
| `Light OFF ACK OK` | Light Node received OFF CMD and ran GPIO command |
| `Light CMD failed: LoRa not ready` | Gateway LoRa is not initialized yet |
| `Light CMD failed: Gateway LoRa TX failed` | Gateway RN2483 failed to send the packet |
| `Light CMD failed: ACK timeout` | Gateway did not receive ACK after one retry |
| `Light CMD failed: ACK FAIL` | Light Node received the packet but command value was invalid |
| `Light CMD failed: ACK wait failed` | Gateway failed to enter LoRa RX mode |
| `LoRa init failed, retrying` | Gateway LoRa init failed and will retry |
| `LoRa reconnected` | Gateway LoRa init succeeded again |

## Important Notes

- ACK OK means the Light Node received a valid command and executed `digitalWrite()`.
- ACK OK does not prove the physical lamp is actually on or off.

## Quick Test

Subscribe to:

```text
esp32/myroom123/status
```

Publish `ON` or `OFF` to:

```text
esp32/myroom123/control
```

Expected:

- `ON` sends `0000000501`, LED turns ON, Gateway publishes `Light ON ACK OK`.
- `OFF` sends `0000000500`, LED turns OFF, Gateway publishes `Light OFF ACK OK`.

