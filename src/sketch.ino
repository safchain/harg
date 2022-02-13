#include <stdio.h>

#include "Manchester.h"
#include "srts.h"
#include "homeasy.h"
#include "homemade.h"

enum {
  DEBUG = 0,
  HOMEASY,
  SRTS,
  HOMEMADE,
  ECHO_REQUEST,
  ECHO_REPLY
};

#define DEBUG_ENABLE

#define RF_SENDER_PIN       2
#define RF_RECEIVER_PIN     4
#define BLINK_PIN           12
#define RF_POWER            5

uint8_t homemade_data[sizeof(struct homemade_payload) + 1];
struct homemade_payload *homemade = (struct homemade_payload *)(homemade_data + 1);

char req_buff[256];
unsigned int req_off = 0;

unsigned long last_time;
unsigned int last_type;

unsigned short last_address1;
unsigned short last_address2;
unsigned char last_ctrl;
unsigned char last_code;
unsigned long last_transmit;

void receive_message();

void setup()
{
  Serial.begin(9600);

  pinMode(RF_SENDER_PIN, OUTPUT);
  pinMode(BLINK_PIN, OUTPUT);

  pinMode(RF_RECEIVER_PIN, INPUT);
  last_time = micros();
  last_type = HIGH;

  pinMode(RF_POWER, OUTPUT);
  digitalWrite(RF_POWER, HIGH);

  memset(homemade_data, 0, sizeof(homemade_data));
  man.setupReceive(RF_RECEIVER_PIN, MAN_1200);
  man.beginReceiveArray(sizeof(struct homemade_payload) + 1, homemade_data);
  man.setupTransmit(RF_SENDER_PIN, MAN_1200);
  man.workAround1MhzTinyCore();
}

int forward_homeasy_payload(struct homeasy_payload *payload)
{
  unsigned long now = micros();
  if (last_address1 == payload->address1 && last_address2 == payload->address2 &&
      last_ctrl == payload->ctrl && now - last_transmit < 2000000) {
    return 0;
  }

  if (payload->address1 == 0 || payload->address2 == 0) {
    return 0;
  }

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
  Serial.flush();

  return 1;
}

int forward_homemade_payload(struct homemade_payload *payload)
{
  unsigned long now = micros();
  if (last_address1 == payload->address1 && last_address2 == payload->address2 &&
      last_ctrl == payload->ctrl && last_code == payload->code &&
      now - last_transmit < 2000000) {
    return 0;
  }

  if (payload->address1 == 0 || payload->address2 == 0) {
    return 0;
  }

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
        Serial.flush();
        return 1;
    }
    Serial.print(payload->size);
    Serial.print(" ");

    for (int i = 0; i != payload->size; i++) {
      Serial.print(payload->data[i]);
    }
  }
  Serial.println("");
  Serial.flush();

  return 1;
}


int forward_srts_payload(struct srts_payload *payload) {
  unsigned short address1, address2;

  srts_get_address(payload, &address1, &address2);

  unsigned long now = micros();
  if (last_address1 == address1 && last_address2 == address2 &&
      last_ctrl == payload->ctrl && last_code == payload->code &&
      now - last_transmit < 2000000) {

    return 0;
  }

  if (address1 == 0 || address2 == 0) {
    return 0;
  }

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
  Serial.flush();

  return 1;
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

struct srts_payload srts_payload;
struct homeasy_payload homeasy_payload;

void loop()
{
  unsigned int type, duration;
  unsigned long now;
  int rc = 0;

  receive_message();

  if (man.receiveComplete()) {
    if (forward_homemade_payload(homemade)) {
      blink();
    }

    memset(homemade_data, 0, sizeof(homemade_data));
    man.beginReceiveArray(sizeof(struct homemade_payload) + 1, homemade_data);

    return;
  }

  type = digitalRead(RF_RECEIVER_PIN);
  if (type != last_type) {
    now = micros();
    duration = now - last_time;
    last_time = now;

    rc = srts_receive(RF_RECEIVER_PIN, last_type, duration, &srts_payload);
    if (rc == 1) {
      if (forward_srts_payload(&srts_payload)) {
        blink();
      }
      memset(&srts_payload, 0, sizeof(struct srts_payload));
    }

    rc = homeasy_receive(RF_RECEIVER_PIN, last_type, duration, &homeasy_payload);
    if (rc == 1) {
      if (forward_homeasy_payload(&homeasy_payload)) {
        blink();
      }
      memset(&homeasy_payload, 0, sizeof(struct homeasy_payload));
    }

    last_type = type;
  }
}

volatile unsigned char key = 1;

void handle_request(unsigned char type, unsigned short address1,
                   unsigned short address2, unsigned short receiver,
                   unsigned short ctrl, unsigned short code,
                   unsigned char repeat)
{
  int transmitted = 0;
  if (type == HOMEASY) {
    homeasy_transmit(RF_SENDER_PIN, address1, address2, receiver, ctrl, 0, repeat);

    transmitted = 1;
  } else if (type == SRTS) {
    srts_transmit(RF_SENDER_PIN, key, address1, address2, ctrl, code, 0);

    transmitted = 1;
    key++;
  } else if (type == HOMEMADE) {
    homemade_transmit(RF_SENDER_PIN, address1, address2, receiver, ctrl, (unsigned char)code);

    transmitted = 1;
  } else if (type == ECHO_REQUEST) {
    Serial.print(ECHO_REPLY);
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
    Serial.flush();
  }

  if (transmitted) {
    last_address1 = address1;
    last_address2 = address2;
    last_ctrl = ctrl;
    last_code = code;
    last_transmit = micros();
  }

#ifdef DEBUG_ENABLE
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
  Serial.flush();
#endif
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

      if (strlen(req_buff) > 0) {
        sscanf(req_buff, "%4hu %4hu %4hu %4hu %4hu %4hu %2hhu",
               &type, &address1, &address2, &receiver, &ctrl, &code, &repeat);

        handle_request(type, address1, address2, receiver, ctrl, code, repeat);
     }

      req_off = 0;
      req_buff[req_off] = '\0';
    } else {
      if (req_off > 250) {
	req_off = 0;
      }
      req_buff[req_off++] = c;
    }
  }
}
