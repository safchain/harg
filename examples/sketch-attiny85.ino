#include <Manchester.h>
#include "homeasy.h"

#define TX_PIN  0

uint8_t data[sizeof(struct homeasy_payload) + 1];
struct homeasy_payload *homeasy = (struct homeasy_payload *)(data + 1);

void setup()
{
  pinMode(TX_PIN, OUTPUT);

  man.workAround1MhzTinyCore();
  man.setupTransmit(TX_PIN, MAN_1200);
}

void loop()
{
  data[0] = sizeof(struct homeasy_payload);

  homeasy->address1 = 456;
  homeasy->address2 = 789;
  homeasy->receiver = 0;
  homeasy->ctrl = 1;
  homeasy->group = 0;

  man.transmitArray(sizeof(struct homeasy_payload), data);
  delay(5000);
}
