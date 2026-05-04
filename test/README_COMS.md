# Communication (LoRa) Tests

This directory contains the test scripts used to validate the RN2483 LoRa module and the point-to-point (P2P) communication protocol outlined in the `Comunciation Plan 1.md`.

## Test Files

### 1. `test_lora.ino`
* **Purpose:** Basic hardware validation to ensure the ESP32 can talk to the RN2483 module over UART2 (`GPIO16`, `GPIO17`) and reset it via `GPIO14`.
* **Behavior:** Hard resets the LoRa module and reads its boot message. It acts as a serial passthrough, allowing you to type raw RN2483 commands (e.g., `sys get ver`) into the Serial Monitor and see the response.

### 2. `test_get_device_id.ino`
* **Purpose:** Retrieves the factory-burnt hardware MAC address and formats it into the 4-byte (`uint32_t`) `Device ID` required by the protocol.
* **Output:** Prints the full MAC address and the formatted 32-bit Hex ID to be used in payload headers.

### 3. `test_lora_p2p.ino`
* **Purpose:** The core protocol implementation testing script. It validates transmitting and receiving the synthetic 13-byte sensor payload. 
* **Modes:**
  * `[S]` **Continuous Sender:** Broadcasts data every 5 seconds using `millis()`.
  * `[R]` **Continuous Receiver:** Locks the gateway into continuous read mode.
  * `[D]` **Deep Sleep Sender:** Broadcasts the payload and triggers `esp_deep_sleep_start()` for 10 seconds.
  * `[G]` **Deep Sleep Receiver:** Wakes up, opens a 6-second listen window, captures the payload, and goes back to sleep for 10 seconds. Calculates **Clock Drift** upon payload receipt.

### 4. `test_lora_sleep_sender.ino`
* **Purpose:** A standalone, non-interactive version of the Deep Sleep Sender. It strictly boots, transmits, sleeps for 10s, and repeats.

---

## Current Problem (Known Issue)

**Deep Sleep Synchronization Failure**
When both the Sender and the Gateway are placed into their continuous 10-second Deep Sleep logic, **the Gateway only successfully receives the very first packet.** Subsequent packets are missed.

### Next Steps / Troubleshooting
* **Clock Drift:** We need to investigate and measure the true clock drift between the two ESP32 RTC timers. Because they are independent, one board's 10-second sleep might be slightly faster/slower than the other's.
* **Listen Window Offset:** The gateway's 6-second listen window (`radio rx 0`) may be drifting entirely out of phase with the sender's transmission time. We need to use the drift output from the first successful transmission to offset the subsequent wake windows.
* **RN2483 Boot Delay:** Need to ensure the LoRa module's wake-up time is being properly accounted for in the sync math before it starts transmitting.
