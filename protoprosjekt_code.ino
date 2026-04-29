#include <Wire.h>
#include <LiquidCrystal_I2C.h> //Bibliotek til LCD-skjerm
#include <Adafruit_NeoPixel.h> //Bibliotek til NeoPixel

LiquidCrystal_I2C lcd(0x25, 16, 2);

int charge = 0;

// Jevning av sensorsignal på A0
const int numReadings = 20;
int readings[numReadings];
int readIndex = 0;
long total = 0;

// Releer og knapp
const int releOpenPin = 8;
const int releClosePin = 9;
const int knappPin = 2;
bool forrigeKnapp = HIGH;

// Riktig spenningsområde, verdiene er funnet ved testing
const int rMin = 200;
const int rMaks = 670;
const unsigned long kravTid = 5000;      // 5 sekunder totalt
const unsigned long openPulsTid = 500;   // 0,5 sekund puls

// Hvor fort tiden faller tilbake utenfor området
const float leakFaktor = 0.4;

// Sperretid for close etter open (hindrer hammer i å låses før den blir løftet)
const unsigned long blokkTid = 5000;
unsigned long openTid = 0;
bool lukkingBlokkert = false;

// Sier om lynnedslaget og åpning av hammer har skjedd
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

// Brukt til tidlig testing, inspirert av koden i https://projecthub.arduino.cc/mircemk/diy-static-charge-monitor-electrostatic-field-detector-arduino-tl071-198193
// void SendString(byte InstrNo, int MValue) {
//  Serial.print('#');
//  Serial.print(InstrNo);
//  Serial.print('M');
//  Serial.print(MValue);
//  Serial.print('<');
//}

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

    // tilfeldig når de blinker
    if (random(0, 100) > 60) {
      
      // tilfeldig styrke, blåhvit blits
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
//Serial.begin(9600);

  lcd.init();
  lcd.backlight();

  // Definerer pins
  pinMode(releOpenPin, OUTPUT);
  pinMode(releClosePin, OUTPUT);
  pinMode(knappPin, INPUT_PULLUP);

  // Releer er av ved oppstart
  digitalWrite(releOpenPin, LOW);
  digitalWrite(releClosePin, LOW);

  // Definerer NeoPixel
  strip1.begin();
  strip2.begin();
  strip1.setBrightness(80);
  strip2.setBrightness(80);
  strip1.show();
  strip2.show();

  // Avlesning av A0, static charge senor
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

  //Oppdaterer tiden for hver kjøring av kode, Millis er brukt for at koden ikke skal "delaye" og stoppe opp
  unsigned long naa = millis();
  unsigned long deltaTid = naa - forrigeMillis;
  forrigeMillis = naa;

  // Glatting av analogverdi
  total = total - readings[readIndex];
  readings[readIndex] = analogRead(A0);
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % numReadings;
  int average = total / numReadings;

  // Kalibrering av sensorverdi første 2 sekundene av kjøring
  static int calValue = 512;
  if (millis() < 2000) {
    calValue = average;
  }

  // sensitivitet til sensor (noen av disse verdie, charge f. eks brukes ikke i nåværende kode, men var nyttig ved tidligere iterasjoner og testing)
  int sensitivity = 120;

  charge = map(average, calValue, calValue - sensitivity, 0, 100);
  charge = constrain(charge, 0, 100);

  // Fjern blokkering etter blokkTid, og nullstiller lyslenker
  if (lukkingBlokkert && millis() - openTid >= blokkTid) {
    lukkingBlokkert = false;
    openSendt = false;

    akkumulertTid = 0;
    oppdaterLedLenkerProgress(0);
  }

  // Lagrer knappens (hammerlåsens) tilstand, dette gjør at rele kun for én puls istedet for å kontinuerlig bli trykket
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

  // Oppdaterer bool knapp
  forrigeKnapp = knapp;

  // Aktivt område: 200 < R < 650
  bool innenforOmraade = (average < rMaks && average > rMin);

  // Tiden med riktig sppenningsområde, som styrer hvor mange piksler som lyser opp
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

  // LCD oppdatering - viser tidsprosent (ladning), melding og r-verdi på A0
  int tidProsent = map(akkumulertTid, 0, kravTid, 0, 100);
  tidProsent = constrain(tidProsent, 0, 100);

  lcd.setCursor(0, 0);
  lcd.print("LADNING:");
  lcd.print(tidProsent);
  lcd.print("% ");
  lcd.print(average);
  lcd.print("   ");
  lcd.setCursor(0, 1);

  // Ulik tilbakemelding basert på tidsprosent
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

  // SendString(1, charge); (Mest brukt før vi fikk LCD, for å kunne teste static charge sensoren)
  
  // void loop kjører 33 ganger per sekund
  delay(30);
}