#!/usr/bin/env python3
"""
Loriot cross-device router
==========================
Subscribes to the Loriot WebSocket feed and forwards 13-byte sensor payloads
to gateway_slave as downlinks that gateway_slave can print on Serial.

Full flow:
  PC serial terminal --> gateway_slave (FREQ:120\n)
  gateway_slave --> LoRaWAN port 2 uplink --> Loriot
  loriot_router.py (this script) --> Loriot WebSocket downlink --> gateway_slave

Install dependencies:
    pip install websockets

Usage:
    Fill in the CONFIG section below, then run:
    python loriot_router.py
"""

import asyncio
import base64
import json
import struct
from typing import Optional, Tuple

import websockets

# ================================================================
# CONFIG — fill these in from the Loriot / iotnet.teracom.dk console
# ================================================================
SERVER    = "iotnet.teracom.dk"
APP_ID    = "BE7A000000001465"         # hex app ID, e.g. "BE7A000000000001"
TOKEN     = "vnoUZQAAABFpb3RuZXQudGVyYWNvbS5ka7UQ2G66F7G46LgTOrqT8n4="      # Applications → (your app) → Token

SLAVE_EUI = "0004A30B01107987"  # gateway_slave DevEUI, 16 hex chars, no separators
TXMTR_EUI = "0004A30B0110979E" # transmitter_v2 DevEUI, 16 hex chars, no separators

MIN_SLEEP = 30
MAX_SLEEP = 3600
# ================================================================

WS_URL   = f"wss://{SERVER}/app?id={APP_ID}&token={TOKEN}"


def decode_payload(data: str) -> Optional[bytes]:
    """
    Decode Loriot's uplink payload.

    Some Loriot views/API messages provide 'data' as hex, while others provide
    it as base64. Try hex first because the console examples are hex strings.
    """
    data = data.strip()
    if not data:
        return None

    if len(data) % 2 == 0:
        try:
            return bytes.fromhex(data)
        except ValueError:
            pass

    try:
        return base64.b64decode(data, validate=True)
    except Exception:
        return None


def parse_freq(raw: bytes) -> Optional[int]:
    """
    Return a uint16 freq value from the 2-byte gateway_slave command payload.
    """
    if len(raw) != 2:
        return None
    return struct.unpack(">H", raw)[0]


def parse_sensor_payload(raw: bytes) -> Optional[Tuple[int, float, int, int, int, int, int]]:
    """
    Parse transmitter_v2's 13-byte sensor uplink:
      device_id, temp C, humidity %, lux, soil ADC, leak flag, battery %.
    """
    if len(raw) != 13:
        return None

    device_id = struct.unpack(">I", raw[0:4])[0]
    temp_c = struct.unpack(">h", raw[4:6])[0] / 10.0
    humidity = raw[6]
    lux = struct.unpack(">H", raw[7:9])[0]
    soil = struct.unpack(">H", raw[9:11])[0]
    leak = raw[11]
    battery = raw[12]
    return device_id, temp_c, humidity, lux, soil, leak, battery


async def queue_payload_downlink(ws, target_eui: str, port: int, payload_hex: str, label: str) -> None:
    payload_hex = payload_hex.upper()
    body = {
        "cmd": "tx",
        "EUI": target_eui,
        "port": port,
        "confirmed": False,
        "priority": 3,
        "data": payload_hex,
    }
    await ws.send(json.dumps(body))
    print(f"  -> Sent {label} downlink to {target_eui} on port {port}: 0x{payload_hex}")


async def queue_freq_downlink(ws, freq: int) -> None:
    payload_hex = struct.pack(">H", freq).hex().upper()
    await queue_payload_downlink(ws, TXMTR_EUI, 1, payload_hex, f"frequency {freq}s")


async def listen() -> None:
    print(f"Connecting to {SERVER}...")
    async with websockets.connect(WS_URL) as ws:
        print("Connected. Listening for gateway_slave port-2 uplinks...\n")
        async for raw in ws:
            msg = json.loads(raw)

            # Only process uplinks from gateway_slave on port 2.
            if msg.get("cmd") != "rx":
                if msg.get("cmd") == "tx" or "error" in msg:
                    print(f"WebSocket response: {json.dumps(msg)}")
                continue
            if msg.get("EUI", "").upper() != SLAVE_EUI.upper():
                continue
            if msg.get("port") != 2:
                continue

            data = msg.get("data", "")
            print(f"Uplink from gateway_slave: {data}")

            raw_payload = decode_payload(data)
            if raw_payload is None:
                print("  Could not decode payload as hex or base64, ignoring.")
                continue

            freq = parse_freq(raw_payload)
            if freq is None:
                sensor = parse_sensor_payload(raw_payload)
                if sensor is None:
                    print(f"  Unexpected payload length ({len(raw_payload)} bytes), ignoring.")
                    continue

                device_id, temp_c, humidity, lux, soil, leak, battery = sensor
                print(
                    "  Parsed sensor payload: "
                    f"device=0x{device_id:08X}, temp={temp_c:.1f}C, "
                    f"humidity={humidity}%, lux={lux}, soil={soil}, "
                    f"leak={leak}, battery={battery}%"
                )
                await queue_payload_downlink(
                    ws,
                    TXMTR_EUI,
                    1,
                    raw_payload.hex(),
                    "sensor payload"
                )
                continue

            if not (MIN_SLEEP <= freq <= MAX_SLEEP):
                print(f"  Freq {freq}s out of range [{MIN_SLEEP}, {MAX_SLEEP}], ignoring.")
                continue

            print(f"  Parsed: FREQ={freq}s")
            await queue_freq_downlink(ws, freq)


async def main() -> None:
    while True:
        try:
            await listen()
        except Exception as e:
            print(f"Connection lost ({e}), reconnecting in 5 s...")
            await asyncio.sleep(5)


if __name__ == "__main__":
    asyncio.run(main())
