#include <stdio.h>

#include "Manchester.h"
#include "srts.h"
#include "homeasy.h"

enum {
  HOMEASY = 1,
  SRTS
};

#define RF_SENDER_PIN       3
#define RF_RECEIVER_PIN     4
#define BLINK_PIN           12

uint8_t data[sizeof(struct homeasy_payload) + 1];
struct homeasy_payload *homeasy = (struct homeasy_payload *)(data + 1);

char req_buff[256];
unsigned int req_off = 0;

unsigned long last_time;
unsigned int last_type;

void setup()
{
  Serial.begin(9600);

  pinMode(RF_SENDER_PIN, OUTPUT);
  pinMode(BLINK_PIN, OUTPUT);

  pinMode(RF_RECEIVER_PIN, INPUT);
  last_time = micros();
  last_type = HIGH;

  man.setupReceive(RF_RECEIVER_PIN, MAN_1200);
  man.beginReceiveArray(sizeof(struct homeasy_payload) + 1, data);
}

void send_homeasy_payload(struct homeasy_payload *payload)
{
  Serial.print(HOMEASY);
  Serial.print(" ");
  Serial.print(payload->address1);
  Serial.print(" ");
  Serial.print(payload->address2);
  Serial.print(" ");
  Serial.print(payload->receiver);
  Serial.print(" ");
  Serial.print(payload->ctrl);
  Serial.print(" ");
  Serial.print("0");
  Serial.println("");
}

void send_srts_payload(struct srts_payload *payload) {
  unsigned short address1, address2;

  srts_get_address(payload, &address1, &address2);

  Serial.print(SRTS);
  Serial.print(" ");
  Serial.print(address1);
  Serial.print(" ");
  Serial.print(address2);
  Serial.print(" ");
  Serial.print("0");
  Serial.print(" ");
  Serial.print(payload->ctrl);
  Serial.print(" ");
  Serial.print(payload->code);
  Serial.println("");
}

void loop()
{
  struct srts_payload srts_payload;
  struct homeasy_payload homeasy_payload;
  unsigned int type, duration;
  unsigned short address1, address2;
  unsigned long now;
  int rc = 0;

  receiveMessage();

  if (man.receiveComplete()) {
    send_homeasy_payload(homeasy);

    man.beginReceiveArray(sizeof(struct homeasy_payload) + 1, data);
  }

  type = digitalRead(RF_RECEIVER_PIN);
  if (type != last_type) {
    now = micros();
    duration = now - last_time;
    last_time = now;

    digitalWrite(BLINK_PIN, type * 255);

    rc = srts_receive(RF_RECEIVER_PIN, last_type, duration, &srts_payload);
    if (rc == 1) {
      send_srts_payload(&srts_payload);
    }

    rc = homeasy_receive(RF_RECEIVER_PIN, last_type, duration, &homeasy_payload);
    if (rc == 1) {
      send_homeasy_payload(&homeasy_payload);
    }

    last_type = type;
  }
}

void handleRequest(unsigned char type, unsigned short address1,
                   unsigned short address2, unsigned short receiver,
                   unsigned char ctrl, unsigned short code,
                   unsigned char repeat)
{
  if (type == HOMEASY) {
    homeasy_transmit(RF_SENDER_PIN, address1, address2, receiver, ctrl, 0, repeat);
  } else if (type == SRTS) {
    srts_transmit(RF_SENDER_PIN, address1, address2, receiver, ctrl, code, repeat);
  }
}

void receiveMessage()
{
  unsigned short type;
  unsigned short address1;
  unsigned short address2;
  unsigned short receiver;
  unsigned short ctrl;
  unsigned short code;
  unsigned char repeat;
  unsigned char c;

  while (Serial.available() > 0) {
    c = Serial.read();
    if (c == '\n') {
      req_buff[req_off] = '\0';
      req_off = 0;

      sscanf(req_buff, "%d %d %d %d %d %d %d",
             &type, &address1, &address2, &receiver, &ctrl, &code, &repeat);

      handleRequest(type, address1, address2, receiver, ctrl, code, repeat);
    } else {
      req_buff[req_off++] = c;
    }
  }
}
