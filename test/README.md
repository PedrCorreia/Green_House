# Hardware Validation Tests

This directory contains isolated testing sketches used to validate physical connections, PCB traces, and sensor viability prior to the full firmware integration.

## DHT22 Isolation Test (`test_dht22.ino`)

**Purpose:** 
Validates the DHT22 temperature and humidity sensor connection on GPIO 4. It ensures that the mandatory 4.7kΩ pull-up resistor (R2) is correctly placed and that the data line is not floating.

**Expected Behavior:** 
The microcontroller initializes the sensor, waits 2 seconds, and then successfully reads and prints the ambient temperature and humidity in a continuous loop.

### Validation Log
**Date:** 29/04/2026  
**Result:** ✅ PASSED

```text
22:35:08.162 -> --- DHT22 Isolation Test ---
22:35:08.203 -> Expected Pin: GPIO 4
22:35:08.203 -> Ensure a 4.7k ohm resistor connects 3.3V and GPIO 4 (Data line pull-up).
22:35:08.203 -> Sensor initialized. Waiting 2 seconds before first read...
22:35:08.203 -> 
22:35:10.201 -> Status: SUCCESS! | Temp: 21.20 °C | Hum: 35.20 %
22:35:12.687 -> Status: SUCCESS! | Temp: 21.10 °C | Hum: 36.50 %
```

## OLED Display Isolation Test (`test_oled.ino`)

**Purpose:** 
Validates the OLED display connection over the I2C bus (SDA: GPIO 21, SCL: GPIO 22). It verifies the physical connection via an I2C scanner and confirms the correct driver and resolution configuration for the specific OLED module used.

**Hardware Observation:** 
During physical testing, the `128x32 SSD1306` configuration resulted in glitched output. The hardware was validated to actually be a `128x64 SH1106` driven OLED screen. The test script uses the `Adafruit_SH110X` library to drive the display properly without visual artifacts.

**Expected Behavior:** 
The microcontroller performs an I2C scan, successfully connects to the display, and begins printing an incrementing loop counter to the screen.

### Validation Log
**Date:** 29/04/2026  
**Result:** ✅ PASSED

```text
22:43:06.395 -> --- OLED Display Isolation Test ---
22:43:06.395 -> Expected Pins: SDA = GPIO 21, SCL = GPIO 22
22:43:06.440 -> 
22:43:06.440 -> -> Let's scan the I2C bus for the screen...
22:43:06.440 ->    -> Success! Found I2C device at address 0x3C
22:43:06.440 ->    (Standard 0.96" OLEDs are usually 0x3C)
22:43:06.440 -> 
22:43:06.697 -> -> OLED allocation successful on I2C address 0x3C
```
