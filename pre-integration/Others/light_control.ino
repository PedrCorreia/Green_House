#include <Arduino.h>

// packet type
const uint8_t cmdType = 0x02;
const uint8_t ackType = 0x03;

// command value
const uint8_t cmdOff = 0x00;
const uint8_t cmdOn = 0x01;

// esult value inside ACKpacket
const uint8_t ackFail = 0x00;
const uint8_t ackOk = 0x01;

// external LED pin
const int ledPin = 2;

// define a ACK packet
// format: [ackType, result]
uint8_t ack[2];


// control LED state
void setLed(bool on) {
  if (on) {
    digitalWrite(ledPin, HIGH);
  } else {
    digitalWrite(ledPin, LOW);
  }
}

// build ACK packet after handling CMD
void makeAck(uint8_t result) {
  ack[0] = ackType;
  ack[1] = result;
}

// currently only prints ACK to Serial for testing
// sen ack[0], ack[1] back to gateway through real LoRa
// can be replaced by real send function in communication layer
void sendAck() {
  Serial.print("ACK: [");
  Serial.print(ack[0], HEX);
  Serial.print(", ");
  Serial.print(ack[1], HEX);
  Serial.println("]");
}

// handle the CMD packet
// format:[cmdType, cmdValue]
void handleCmd(uint8_t cmd[2]) {
  // first byte must be CMD type, if not, return ACK fail
  if (cmd[0] != cmdType) {
    makeAck(ackFail);
    sendAck();
    return;
  }

  // second byte chack
  if (cmd[1] == cmdOn) {
    setLed(true);
    makeAck(ackOk);
    sendAck();
  }
  else if (cmd[1] == cmdOff) {
    setLed(false);
    makeAck(ackOk);
    sendAck();
  }

  // undefined command value 
  else {
    makeAck(ackFail);
    sendAck();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
}

void loop() {
  // current loop only simulates received CMD packet for testing
  // later, communication layer should replace this test part
  // with real received packet from gateway
  
  // test packet: CMD ON
  uint8_t cmd1[2] = {0x02, 0x01};
  Serial.println("ON");
  handleCmd(cmd1);
  delay(3000);

  // test packet: CMD OFF
  uint8_t cmd2[2] = {0x02, 0x00};
  Serial.println("OFF");
  handleCmd(cmd2);
  delay(3000);
}