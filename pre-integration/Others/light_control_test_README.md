# Light Control Test Guide


## 1. Test Gateway → Light Node CMD + ACK first

Use these two files first:

- `gateway_to_lightnode_CMD_test.ino`  
  Upload this to the gateway ESP.

- `lightnode_to_gateway_ACK_test.ino`  
  Upload this to the light control node ESP.

This test checks the direct LoRa communication between the gateway and the light node.

Expected flow:

```text
Gateway sends CMD:
0201 = light ON
0200 = light OFF

Light node receives CMD
Light node controls the LED
Light node sends ACK back:
0301 = ACK OK
0300 = ACK FAIL
```

Test this part first to make sure:

- the light node can receive the CMD from the gateway
- the gateway can receive the ACK from the light node

If this does not work, do not test MQTT yet. Fix the LoRa CMD/ACK communication first.

## 2. Then test MQTT → Gateway → Light Node

After the first test works, upload this file to the gateway ESP:

- `gateway_mqtt_cmd_test.ino`

Keep the light control node running the light node code.

This version checks the full control path:

```text
MQTT ON/OFF message
→ gateway receives the MQTT message
→ gateway packs it into LoRa CMD
→ gateway sends CMD to light control node
→ light node turns LED ON/OFF
```

Use MQTT Explorer / MQTTX to publish messages:

```text
Topic: esp32/myroom123/control
Payload: ON
```

or

```text
Topic: esp32/myroom123/control
Payload: OFF
```

The gateway should send:

```text
ON  -> 0201
OFF -> 0200
```

## Notes

- `gateway_to_lightnode_CMD_test.ino` is only for direct LoRa CMD/ACK testing.
- `gateway_mqtt_cmd_test.ino` is for MQTT control testing.
- `light_control.ino` is the basic light control logic version.
