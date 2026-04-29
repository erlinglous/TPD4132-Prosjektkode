#include <Wire.h>
#include <LiquidCrystal_I2C.h> //Bibliotek til LCD-skjerm
#include <Adafruit_NeoPixel.h> //Bibliotek til NeoPixel

LiquidCrystal_I2C lcd(0x25, 16, 2);

int charge = 0;

const int numReadings = 20;
int readings[numReadings];
int readIndex = 0;
long total = 0;

// Releer og knapp
const int releOpenPin = 8;
const int releClosePin = 9;
const int knappPin = 2;
bool forrigeKnapp = HIGH;

// Riktig spenningsområde
const int rMin = 200;
const int rMaks = 670;
const unsigned long kravTid = 5000;      // 5 sekunder totalt
const unsigned long openPulsTid = 500;   // 0,5 sekund puls

// Hvor fort tiden faller tilbake utenfor området
const float leakFaktor = 0.4;

// Sperretid for close etter open (hindrer hammer i låses før den blir løftet)
const unsigned long blokkTid = 5000;
unsigned long openTid = 0;
bool lukkingBlokkert = false;

bool openSendt = false;

// Tidsteller for riktig spenningsområde
long akkumulertTid = 0;
unsigned long forrigeMillis = 0;

// NeoPixel
const int ledPin1 = 11;
const int ledPin2 = 12;
const int antallLeds = 75;

Adafruit_NeoPixel strip1(antallLeds, ledPin1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(antallLeds, ledPin2, NEO_GRB + NEO_KHZ800);

// Oppdatering av NeoPixel-lenker
void oppdaterLedLenkerProgress(unsigned long aktivTidMs) {
  if (aktivTidMs > kravTid) aktivTidMs = kravTid;

  int antallPaa = map(aktivTidMs, 0, kravTid, 0, antallLeds);

  strip1.clear();
  strip2.clear();

  for (int i = antallLeds - antallPaa; i < antallLeds; i++) {
    if (i >= 0 && i < antallLeds) {
      strip1.setPixelColor(i, strip1.Color(10, 20, 25));
      strip2.setPixelColor(i, strip2.Color(10, 20, 25));
    }
  }

  strip1.show();
  strip2.show();
}

void SendString(byte InstrNo, int MValue) {
  Serial.print('#');
  Serial.print(InstrNo);
  Serial.print('M');
  Serial.print(MValue);
  Serial.print('<');
}

// Åpning av hammermekanisme
void pulseOpen(unsigned long tidMs) {
  digitalWrite(releClosePin, LOW);
  digitalWrite(releOpenPin, HIGH);
  delay(tidMs);
  digitalWrite(releOpenPin, LOW);
}

// Selve lynnedslaget styres av en tilfeldig 2 sekund blinkesekvens
void lynRandom(unsigned long varighetMs) {
  unsigned long start = millis();

  while (millis() - start < varighetMs) {

    // tilfeldig om det skal blinke nå
    if (random(0, 100) > 60) {
      
      // tilfeldig styrke (litt blå/hvit lynfarge)
      int r = random(150, 255);
      int g = random(150, 255);
      int b = random(200, 255);

      for (int i = 0; i < antallLeds; i++) {
        strip1.setPixelColor(i, strip1.Color(r, g, b));
        strip2.setPixelColor(i, strip2.Color(r, g, b));
      }

      strip1.show();
      strip2.show();

      delay(random(20, 80));  // kort flash
    }

    // mørk pause (varierende)
    strip1.clear();
    strip2.clear();
    strip1.show();
    strip2.show();

    delay(random(30, 150));
  }
}

void setup() {
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();

  pinMode(releOpenPin, OUTPUT);
  pinMode(releClosePin, OUTPUT);
  pinMode(knappPin, INPUT_PULLUP);

  digitalWrite(releOpenPin, LOW);
  digitalWrite(releClosePin, LOW);

  strip1.begin();
  strip2.begin();
  strip1.setBrightness(80);
  strip2.setBrightness(80);
  strip1.show();
  strip2.show();

  int initialRead = analogRead(A0);
  for (int i = 0; i < numReadings; i++) {
    readings[i] = initialRead;
    total += initialRead;
  }

  forrigeMillis = millis();

  lcd.setCursor(0, 0);
  lcd.print("READY - IDLE...");
  delay(1000);
}

void loop() {
  unsigned long naa = millis();
  unsigned long deltaTid = naa - forrigeMillis;
  forrigeMillis = naa;

  // Glatting av analogverdi
  total = total - readings[readIndex];
  readings[readIndex] = analogRead(A0);
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % numReadings;
  int average = total / numReadings;

  static int calValue = 512;
  if (millis() < 2000) {
    calValue = average;
  }

  int sensitivity = 120;

  charge = map(average, calValue, calValue - sensitivity, 0, 100);
  charge = constrain(charge, 0, 100);

  // Fjern blokkering etter blokkTid
  if (lukkingBlokkert && millis() - openTid >= blokkTid) {
    lukkingBlokkert = false;

    akkumulertTid = 0;
    oppdaterLedLenkerProgress(0);
  }

  bool knapp = digitalRead(knappPin);

  // Holder close aktiv så lenge hammeren er lukket
  if (knapp == LOW && !lukkingBlokkert) {
    digitalWrite(releClosePin, HIGH);
  } else {
    digitalWrite(releClosePin, LOW);
  }

  // Når knappen slippes → reset for ny syklus
  if (knapp == HIGH && forrigeKnapp == LOW && !lukkingBlokkert) {
    openSendt = false;
  }

  forrigeKnapp = knapp;

  // Aktivt område: 200 < R < 650
  bool innenforOmraade = (average < rMaks && average > rMin);

  // Tiden med riktig sppenningsområde, som styrer hvor mange piksler som lyser
  if (!openSendt) {
    if (innenforOmraade) {
      akkumulertTid += deltaTid;
    } else {
      akkumulertTid -= deltaTid * leakFaktor;
    }

    if (akkumulertTid < 0) akkumulertTid = 0;
    if (akkumulertTid > kravTid) akkumulertTid = kravTid;

    oppdaterLedLenkerProgress(akkumulertTid);

    if (akkumulertTid >= kravTid) {
      openTid = millis();
      lukkingBlokkert = true;

      pulseOpen(openPulsTid);
      lynRandom(2000);

      openSendt = true;
    }
  }

  // LCD oppdatering - viser tidsprosent og melding
  int tidProsent = map(akkumulertTid, 0, kravTid, 0, 100);
  tidProsent = constrain(tidProsent, 0, 100);

  lcd.setCursor(0, 0);
  lcd.print("LADNING:");
  lcd.print(tidProsent);
  lcd.print("% ");
  lcd.print(average);
  lcd.print("   ");
  lcd.setCursor(0, 1);

  if (lukkingBlokkert) {
    lcd.print("TREKK OPP HAMMER");
  }
  else if (tidProsent <= 20) {
    lcd.print("Gni ballong     ");
  }
  else if (tidProsent <= 60) {
    lcd.print("Bra jobba!      ");
  }
  else if (tidProsent < 100) {
    lcd.print("Nesten der      ");
  }
  else {
    lcd.print("TREKK OPP HAMMER");
  }

  SendString(1, charge);
  
  // void loop kjører 33 ganger per sekund
  delay(30);
}