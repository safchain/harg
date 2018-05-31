#include <Manchester.h>
#include <Tiny4kOLED.h>
#include <TinyWireM.h>

#define LED_PIN PB0
#define LCD_POWER PA1
#define TX_PIN PB2

#define CT_PIN PA7
#define CT_POWER PA0

#define MAX_ENTRIES 5

enum COMMAND {
  HOMEMADE_OFF,
  HOMEMADE_ON,
  HOMEMADE_FLOAT
};

const unsigned char bitmap [] PROGMEM =
{
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xF8, 0x18, 0xE0, 0x00, 0xF8, 0x18, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xE0, 0x10, 0x10, 0x10, 0x10, 0xE0, 0x83, 0xFD, 0x05,
  0x75, 0x01, 0x05, 0x05, 0x01, 0x01, 0xE5, 0xFD, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x03, 0x04,
  0x08, 0x18, 0x38, 0xF8, 0x18, 0x08, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x07, 0x08, 0x08,
  0x08, 0x08, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

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
  char data[10];
};

double entries[MAX_ENTRIES];
uint8_t data[sizeof(struct homemade_payload) + 1];
struct homemade_payload *homemade = (struct homemade_payload *)(data + 1);

#define ADC_BITS    10
#define ADC_COUNTS  (1<<ADC_BITS)

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

class EnergyMonitor
{
  public:
    void current(unsigned int _inPinI, double _ICAL);
    double calcIrms(unsigned int NUMBER_OF_SAMPLES);

    double Irms;

  private:
    unsigned int inPinI;
    double ICAL;
    int sampleI;
    double filteredI;
    double offsetI;
    double sqI,sumI;
};

void EnergyMonitor::current(unsigned int _inPinI, double _ICAL)
{
  inPinI = _inPinI;
  ICAL = _ICAL;
  offsetI = ADC_COUNTS >> 1;
}

double EnergyMonitor::calcIrms(unsigned int Number_of_Samples)
{
  int SupplyVoltage = 5000;

  for (unsigned int n = 0; n < Number_of_Samples; n++) {
    sampleI = analogRead(inPinI);

    offsetI = (offsetI + (sampleI-offsetI)/1024);
    filteredI = sampleI - offsetI;

    sqI = filteredI * filteredI;
    sumI += sqI;
  }

  double I_RATIO = ICAL *((SupplyVoltage/1000.0) / (ADC_COUNTS));
  Irms = I_RATIO * sqrt(sumI / Number_of_Samples);

  sumI = 0;

  return Irms;
}

EnergyMonitor emon1;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(TX_PIN, OUTPUT);
  pinMode(CT_PIN, INPUT);
  pinMode(CT_POWER, OUTPUT);

  DDRA |= (1 << CT_POWER);
  PORTA |= (1 << CT_POWER);

  DDRA |= (1 << LCD_POWER);
  PORTA |= (1 << LCD_POWER);

  man.workAround1MhzTinyCore();
  man.setupTransmit(TX_PIN, MAN_1200);

  emon1.current(CT_PIN, 62);

  GIMSK |= _BV(PCIE0);   // Enable Pin Change Interrupts
  PCMSK0 |= _BV(PCINT2); // Use PA7 as interrupt pin
  sei();

  digitalWrite(LED_PIN, HIGH);
  delay(3000);
  oled.begin();
  oled.clear();

  oled.on();
  oled.switchRenderFrame();
  oled.setFont(FONT8X16);
  digitalWrite(LED_PIN, LOW);
}

void writeAndWait(int pin, int level, int wait)
{
  digitalWrite(pin, level);
  delay(wait);
}

char chars[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
void charToHex(char c, char *hex)
{
  int i, q = c;

  for (i = 0; i != 2; i++) {
    hex[1-i] = chars[q % 16];
    q /= 16;
  }
}

int watt = 0, i1 = 0, i2 = 0;

double avg(double *entries, int n)
{
  double avg = 0;
  for (int i = 0; i != n; i++) {
    avg += entries[i];
  }
  avg /= MAX_ENTRIES;

  return avg;
}

void measure()
{
  PORTA |= (1 << CT_POWER);
  delay(1000);

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

  PORTA &= ~(1 << CT_POWER);

  double a = avg(entries, MAX_ENTRIES);

  data[0] = sizeof(struct homemade_payload);

  homemade->address1 = 777;
  homemade->address2 = 123;
  homemade->receiver = 0;
  homemade->ctrl = HOMEMADE_FLOAT;
  homemade->code = 0;
  homemade->size = 4;

  watt = a * 220;

  int f1 = int(a);
  double f2 = a - f1;
  i1 = (int)f1;
  i2 = (int)100 * f2;

  memset(homemade->data, 0, sizeof(homemade->data));
  charToHex(i1, homemade->data);
  charToHex(i2, homemade->data + 2);

  for (int i = 0; i != 5; i++) {
    writeAndWait(LED_PIN, LOW, 20);
    writeAndWait(LED_PIN, HIGH, 20);
  }

  delay(500);

  man.transmitArray(sizeof(struct homemade_payload), data);
  digitalWrite(LED_PIN, LOW);
}

int count = 0, display = 0;
void loop() {
  if (display > 0) {
    display--;

    oled.clear();

    oled.setCursor(0, 0);
    oled.bitmap(0, 0, 32, 4, bitmap);

    oled.setCursor(40, 0);

    char str[10];
    itoa(watt, str, 10);

    oled.print(str);
    oled.print(F(" Watt"));

    oled.setCursor(40, 2);
    itoa(i1, str, 10);
    oled.print(str);
    oled.print(F("."));
    itoa(i2, str, 10);
    oled.print(str);
    oled.print(F(" Amp."));

    oled.switchFrame();
  } else {
    oled.clear();
    oled.switchFrame();
  }

  delay(100);
  if (count++ > 30) {
    measure();
    count = 0;
  }
}

ISR(PCINT0_vect)
{
  display = 100;
}
