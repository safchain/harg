#include <Manchester.h>
#include <avr/io.h>
#include <avr/wdt.h> 
#include <avr/sleep.h>
#include <EmonLib.h>

#define MAX_ENTRIES 10
#define LED_PIN 0
#define TX_PIN 3
#define CT_PIN A1

EnergyMonitor emon1;

// Incremented by watchdog interrupt
volatile uint8_t wdt_count;

enum COMMAND {
  HOMEMADE_OFF,
  HOMEMADE_ON,
  HOMEMADE_FLOAT,
  HOMEMADE_UNKNOWN
};

struct homemade_payload {
  /* use short since arduino is limit to 16bits */
  unsigned short address1;
  unsigned short address2;
  unsigned char receiver;
  unsigned char ctrl;
  unsigned char group;
  /* extra info not used by the original protocol */
  char data[10];
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
  // Enable ADC
  ADCSRA |= (1 << ADEN); 
  ADCSRA |= (1 << ADSC);

  for (int i = 0; i != MAX_ENTRIES; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(50);
    digitalWrite(LED_PIN, LOW);
    delay(120);
    
    double w = emon1.calcIrms(1480);
    while (w < 0) {
      w = emon1.calcIrms(1480);
    }
    entries[i] = w;
  }
  
  // Disable ADC  
  ACSR |= _BV(ACD);                         
  ADCSRA &= ~_BV(ADEN);
  
  digitalWrite(LED_PIN, LOW);

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
  homemade->group = 0;

  int f1 = int(a);
  float f2 = a - f1;
  int i1 = (int)f1;
  int i2 = (int)100 * f2;
  
  memset(homemade->data, 0, sizeof(homemade->data));
  sprintf(homemade->data, "%02x%02x", i1, i2);

  for (int i = 0; i != 10; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(20);
    digitalWrite(LED_PIN, HIGH);
    delay(20);
  }
  
  delay(2000);
  
  man.transmitArray(sizeof(struct homemade_payload), data);
  digitalWrite(LED_PIN, LOW);
  
  
  delay(2000);
}

void setup() {
  OSCCAL += 3;
  
  ADMUX &= (0<<REFS0); //Setting reference voltage to internal 1.1V
  
  ADCSRA &= ~_BV(ADEN);  // switch ADC OFF
  ACSR  |= _BV(ACD);     // switch Analog Compartaror OFF
  
  // Configure attiny85 sleep mode
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  // Reset watchdog interrupt counter
  wdt_count = 255; //max value
  
  // Configure pin modes
  pinMode(LED_PIN, OUTPUT);
  pinMode(TX_PIN, OUTPUT);
  pinMode(CT_PIN, INPUT);

  man.workAround1MhzTinyCore();
  man.setupTransmit(TX_PIN, MAN_1200);
  
  emon1.current(CT_PIN, 60);
}

void loop() {
  measure();
  
  wdt_count = 0;
  watchdog_start_interrupt(6);      // prescale of 6 ~= 1sec
  while(wdt_count < 10) {            // Wait 10 watchdog interupts (~10secs)
    sleep_mode();                   // Make CPU sleep until next WDT interrupt
  }
  watchdog_stop();
}

void watchdog_stop() {
  WDTCR |= _BV(WDCE) | _BV(WDE);
  WDTCR = 0x00;
}

void watchdog_start_interrupt(uint8_t wd_prescaler) {
  if(wd_prescaler > 9) 
    wd_prescaler = 9;
  byte _prescaler = wd_prescaler & 0x7;
  if (wd_prescaler > 7 ) 
    _prescaler |= _BV(WDP3); 
  // set new watchdog timer prescaler value
  WDTCR = _prescaler;

  // start timed sequence
  WDTCR |= _BV(WDIE) | _BV(WDCE) | _BV(WDE);
}

// Watchdog Interrupt Service / is executed when watchdog timed out
ISR(WDT_vect) {
  wdt_count++;
  WDTCR |= _BV(WDIE);           // Watchdog goes to interrupt not reset
}
