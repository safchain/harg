#include <Manchester.h>

#define TX_PIN 4

enum COMMAND {
  HOMEMADE_OFF,
  HOMEMADE_ON,
  HOMEMADE_FLOAT,
  HOMEMADE_STRING
};

struct homemade_payload {
  /* use short since arduino is limit to 16bits */
  unsigned short address1;
  unsigned short address2;
  unsigned char receiver;
  unsigned char ctrl;
  unsigned char size;
  /* extra info not used by the original protocol */
  char data[32];
};

uint8_t data[sizeof(struct homemade_payload) + 1];
struct homemade_payload *homemade = (struct homemade_payload *)(data + 1);

void setup() {
 pinMode(TX_PIN, OUTPUT);

 man.setupTransmit(TX_PIN, MAN_1200);
}

int getline(char *buffer)
{
  uint8_t idx = 0;
  char c;
  do {
    while (Serial.available() == 0) ; // wait for a char this causes the blocking
    c = Serial.read();
    buffer[idx++] = c;
  } while (c != '\n' && c != '\r'); 
  buffer[idx] = 0;

  return idx;
}

void loop() {
  data[0] = sizeof(struct homemade_payload);

  homemade->address1 = 123;
  homemade->address2 = 456;
  homemade->receiver = 0;
  homemade->ctrl = HOMEMADE_STRING;

  int size = getline(homemade->data);
  if (size > sizeof(homemade->data)) {
    return;
  }
  homemade->size = size;

  man.transmitArray(sizeof(struct homemade_payload), data);
}
