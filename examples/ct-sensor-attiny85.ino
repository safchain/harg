#include <Manchester.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/sleep.h>
#include <EmonLib.h>
#include "SystemStatus.h"

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

#define MAX_ENTRIES 5
#define LED_PIN 0
#define TX_PIN 3
#define CT_PIN A1
#define CT_POWER 1

EnergyMonitor emon1;
SystemStatus status;

// Incremented by watchdog interrupt
volatile uint8_t wdt_count = 0;

enum COMMAND {
  HOMEMADE_OFF,
  HOMEMADE_ON,
  HOMEMADE_FLOAT,
  HOMEMADE_STRING,
  HOMEMADE_INT
};

struct homemade_payload {
  /* use short since arduino is limit to 16bits */
  unsigned short address1;
  unsigned short address2;
  unsigned char receiver;
  unsigned char ctrl;
  unsigned char code;
  unsigned char size;
  /* extra info not used by the original protocol */
  char data[5];
};

double entries[MAX_ENTRIES];
uint8_t data[sizeof(struct homemade_payload) + 1];
struct homemade_payload *homemade = (struct homemade_payload *)(data + 1);

double avg(double *entries, int n)
{
  double avg = 0;
  for (int i = 0; i != n; i++) {
    avg += entries[i];
  }
  avg /= MAX_ENTRIES;

  return avg;
}

double deviation(double *entries, int n, double avg)
{
 double variance = 0;
 for (int i = 0; i != n; i++) {
  variance += abs(entries[i] - avg);
 }
 variance /= MAX_ENTRIES;

 return variance;
}

void measure()
{
  for (int i = 0; i != 5; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(50);
  }

  digitalWrite(CT_POWER, HIGH);
  delay(1000);

  for (int i = 0; i != MAX_ENTRIES; i++) {
    double w = emon1.calcIrms(1480);
    while (w < 0) {
      w = emon1.calcIrms(1480);
    }
    entries[i] = w;
  }

  digitalWrite(CT_POWER, LOW);

  double a = avg(entries, MAX_ENTRIES);
  double v = deviation(entries, MAX_ENTRIES, a);
  if (a == 0) {
    return;
  }

  double percent = v/a*100;
  if (percent > 20) {
    a = 0;
  }

  data[0] = sizeof(struct homemade_payload);

  homemade->address1 = 999;
  homemade->address2 = 888;
  homemade->receiver = 0;
  homemade->ctrl = HOMEMADE_FLOAT;
  homemade->code = 0;
  homemade->size = 4;

  int f1 = int(a);
  float f2 = a - f1;
  int i1 = (int)f1;
  int i2 = (int)100 * f2;

  memset(homemade->data, 0, sizeof(homemade->data));
  sprintf(homemade->data, "%02x%02x", i1, i2);

  for (int i = 0; i != 5; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(20);
    digitalWrite(LED_PIN, HIGH);
    delay(20);
  }

  delay(200);

  man.transmitArray(sizeof(struct homemade_payload), data);
  digitalWrite(LED_PIN, LOW);
}

void reportVcc() {
  data[0] = sizeof(struct homemade_payload);

  homemade->address1 = 999;
  homemade->address2 = 889;
  homemade->receiver = 0;
  homemade->ctrl = HOMEMADE_INT;
  homemade->code = 0;
  homemade->size = 4;

  memset(homemade->data, 0, sizeof(homemade->data));
  sprintf(homemade->data, "%04x", status.getVCC());

  for (int i = 0; i != 2; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(20);
    digitalWrite(LED_PIN, HIGH);
    delay(20);
  }

  delay(200);

  man.transmitArray(sizeof(struct homemade_payload), data);
  digitalWrite(LED_PIN, LOW);
}

void setup() {
  // Configure pin modes
  pinMode(LED_PIN, OUTPUT);
  pinMode(TX_PIN, OUTPUT);
  pinMode(CT_PIN, INPUT);
  pinMode(CT_POWER, OUTPUT);

  digitalWrite(CT_POWER, LOW);

  man.workAround1MhzTinyCore();
  man.setupTransmit(TX_PIN, MAN_1200);

  emon1.current(CT_PIN, 30);
}

void loop() {
 if ((wdt_count % 8) == 0) {
   measure();
 }

 if (wdt_count > 80) {
   wdt_count = 0;
 }

 if (wdt_count == 0) {
   reportVcc();
 }

 setup_watchdog(8);
 system_sleep();
}

// set system into the sleep state
// system wakes up when wtchdog is timed out
void system_sleep() {
  cbi(ADCSRA,ADEN);                    // switch Analog to Digitalconverter OFF

  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // sleep mode is set here
  sleep_enable();

  sleep_mode();                        // System sleeps here

  sleep_disable();                     // System continues execution here when watchdog timed out
  sbi(ADCSRA, ADEN);                   // switch Analog to Digitalconverter ON
}

// 0=16ms, 1=32ms,2=64ms,3=128ms,4=250ms,5=500ms
// 6=1 sec,7=2 sec, 8=4 sec, 9= 8sec
void setup_watchdog(int ii) {
  byte bb;
  int ww;

  if (ii > 9 )
    ii = 9;
  bb = ii & 7;
  if (ii > 7)
    bb|= (1 << 5);
  bb|= (1 << WDCE);
  ww = bb;

  MCUSR &= ~(1 << WDRF);
  // start timed sequence
  WDTCR |= (1 << WDCE) | (1 << WDE);
  // set new watchdog timeout value
  WDTCR = bb;
  WDTCR |= _BV(WDIE);
}

// Watchdog Interrupt Service / is executed when watchdog timed out
ISR(WDT_vect) {
  wdt_count++;  // set global flag
}
