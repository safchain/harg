#include <stdio.h>

#include "Manchester.h"
#include "srts.h"
#include "homeasy.h"
#include "homemade.h"

enum {
  DEBUG = 0,
  HOMEASY,
  SRTS,
  HOMEMADE
};

#define RF_SENDER_PIN       3
#define RF_RECEIVER_PIN     4
#define BLINK_PIN           12

uint8_t homemade_data[sizeof(struct homemade_payload) + 1];
struct homemade_payload *homemade = (struct homemade_payload *)(homemade_data + 1);

char req_buff[256];
unsigned int req_off = 0;

unsigned long last_time;
unsigned int last_type;

void receive_message();

void setup()
{
  Serial.begin(9600);

  pinMode(RF_SENDER_PIN, OUTPUT);
  pinMode(BLINK_PIN, OUTPUT);

  pinMode(RF_RECEIVER_PIN, INPUT);
  last_time = micros();
  last_type = HIGH;

  memset(homemade_data, 0, sizeof(homemade_data));
  man.setupReceive(RF_RECEIVER_PIN, MAN_1200);
  man.beginReceiveArray(sizeof(struct homeasy_payload) + 1, homemade_data);
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

void send_homemade_payload(struct homemade_payload *payload)
{
  Serial.print(HOMEMADE);
  Serial.print(" ");
  Serial.print(payload->address1);
  Serial.print(" ");
  Serial.print(payload->address2);
  Serial.print(" ");
  Serial.print(payload->receiver);
  Serial.print(" ");
  Serial.print(payload->ctrl);
  Serial.print(" ");
  Serial.print(payload->code);
  if (payload->ctrl > HOMEMADE_ON) {
    Serial.print(" ");
    if (payload->size > sizeof(payload->data)) {
	Serial.println(999);
        return;
    }
    Serial.print(payload->size);
    Serial.print(" ");

    for (int i = 0; i != payload->size; i++) {
      Serial.print(payload->data[i]);
    }
  }
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

void blink()
{
  digitalWrite(BLINK_PIN, HIGH);
  delay(200);
  digitalWrite(BLINK_PIN, LOW);
  delay(200);
  digitalWrite(BLINK_PIN, HIGH);
  delay(200);
  digitalWrite(BLINK_PIN, LOW);
}

void loop()
{
  struct srts_payload srts_payload;
  struct homeasy_payload homeasy_payload;
  unsigned int type, duration;
  unsigned long now;
  int rc = 0;

  receive_message();

  if (man.receiveComplete()) {
    send_homemade_payload(homemade);
    blink();

    memset(homemade_data, 0, sizeof(homemade_data));
    man.beginReceiveArray(sizeof(struct homemade_payload) + 1, homemade_data);
  }

  type = digitalRead(RF_RECEIVER_PIN);
  if (type != last_type) {
    now = micros();
    duration = now - last_time;
    last_time = now;

    rc = srts_receive(RF_RECEIVER_PIN, last_type, duration, &srts_payload);
    if (rc == 1) {
      send_srts_payload(&srts_payload);
      blink();
    }

    rc = homeasy_receive(RF_RECEIVER_PIN, last_type, duration, &homeasy_payload);
    if (rc == 1) {
      send_homeasy_payload(&homeasy_payload);
      blink();
    }

    last_type = type;
  }
}

void handle_request(unsigned char type, unsigned short address1,
                   unsigned short address2, unsigned short receiver,
                   unsigned char ctrl, unsigned short code,
                   unsigned char repeat)
{
  if (type == HOMEASY) {
    homeasy_transmit(RF_SENDER_PIN, address1, address2, receiver, ctrl, 0, repeat);
  } else if (type == SRTS) {
    srts_transmit(RF_SENDER_PIN, address1, address2, receiver, ctrl, code, repeat);
  }

  Serial.print(DEBUG);
  Serial.print(" ");
  Serial.print(address1);
  Serial.print(" ");
  Serial.print(address2);
  Serial.print(" ");
  Serial.print(receiver);
  Serial.print(" ");
  Serial.print(ctrl);
  Serial.print(" ");
  Serial.print(code);
  Serial.print(" ");
  Serial.print(repeat);
  Serial.println("");
}

void receive_message()
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
    if (c == '\n' || c == '\r') {
      req_buff[req_off] = '\0';
      req_off = 0;

      if (strlen(req_buff) > 0) {
        sscanf(req_buff, "%hu %hu %hu %hu %hu %hu %hhu",
               &type, &address1, &address2, &receiver, &ctrl, &code, &repeat);

        handle_request(type, address1, address2, receiver, ctrl, code, repeat);
      }
    } else {
      req_buff[req_off++] = c;
    }
  }
}
