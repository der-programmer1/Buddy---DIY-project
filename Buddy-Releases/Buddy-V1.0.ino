#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// ----------------------------------------------------
//  PIN-DEFINITIONEN
// ----------------------------------------------------

// OLED rechts (Info)
#define OLED_R_SDA 13
#define OLED_R_SCL 15
#define OLED_R_ADDR 0x3C

// OLED links (frei für spätere UI)
#define OLED_L_SDA 21
#define OLED_L_SCL 22
#define OLED_L_ADDR 0x3C

// Joystick
#define JOY_X 32
#define JOY_Y 33
#define JOY_SW 25
#define DEADZONE 500

// Buttons
#define BTN1 14
#define BTN2 27

// Buzzer
#define BUZZER 23

// IR-Sender
#define IR_PIN 2
IRsend irsend(IR_PIN);

// Seeed Ultrasonic Ranger v3.0 (ein SIG-Pin)
#define US_SIG 3

bool boxesFilled = !true;

bool aiEyes = false;

float eyeX = 0;
float eyeY = 0;

float targetX = 0;
float targetY = 0;

unsigned long nextTargetTime = 0;

bool matrixMode = false;

const int matrixCols = 16;   // 128px / 8px pro Zeichen
int matrixY[matrixCols];

char currentMode = currentMode;

// ----------------------------------------------------
//  OLED-OBJEKTE
// ----------------------------------------------------

TwoWire I2CR = TwoWire(0);
TwoWire I2CL = TwoWire(1);

Adafruit_SSD1306 dispR(128, 64, &I2CR, -1);
Adafruit_SSD1306 dispL(128, 64, &I2CL, -1);

// ----------------------------------------------------
//  KEYPAD
// ----------------------------------------------------

byte rowPins[4] = {4, 16, 17, 5};
byte colPins[4] = {18, 19, 26, 12};

char keys[4][4] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);

static const uint8_t image_data_Saraarray[1024] PROGMEM = {
    0x00,0x0b,0x40,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40
,0x00,0x0a,0x40,0x80,0x0b,0x01,0x00,0x07,0x80,0x1f,0x80,0x10,0x00,0x20,0x00,0x00
,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
,0x00,0x01,0x00,0x03,0x00,0x11,0x00,0x31,0x00,0x31,0x80,0x1a,0x00,0x00,0x40,0x00
,0x40,0x40,0x80,0x40,0x80,0x20,0x00,0x00,0x40,0x00,0x40,0x02,0x00,0x00,0x00,0x14
,0x80,0x40,0x80,0x01,0x00,0x08,0x40,0x22,0xc0,0x26,0x80,0x1c,0x80,0x09,0x80,0x01
,0x00,0x61,0x00,0x76,0x00,0x7e,0x00,0x7e,0x00,0x3c,0x00,0x38,0x00,0x30,0x00,0x01
,0x00,0x01,0x00,0x00,0x00,0x22,0x00,0x3e,0x00,0x1c,0x00,0x00,0x00,0x00,0x00,0x00
,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x04
,0x00,0x08,0x00,0x48,0x00,0x30,0x00
};

// ----------------------------------------------------
//  ULTRASONIC MESSUNG (Seeed v3.0, 1-Pin SIG)
// ----------------------------------------------------

float measureUltrasonic() {
  // Trigger senden
  pinMode(US_SIG, OUTPUT);
  digitalWrite(US_SIG, LOW);
  delayMicroseconds(2);
  digitalWrite(US_SIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(US_SIG, LOW);

  // Echo empfangen
  pinMode(US_SIG, INPUT);
  long duration = pulseIn(US_SIG, HIGH, 30000);

  if (duration == 0) return -1; // kein Echo
  return duration * 0.0343 / 2.0; // cm
}

// ----------------------------------------------------
//  SETUP
// ----------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(JOY_SW, INPUT_PULLUP);
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);

  irsend.begin();

  // OLED rechts
  I2CR.begin(OLED_R_SDA, OLED_R_SCL);
  dispR.begin(SSD1306_SWITCHCAPVCC, OLED_R_ADDR);

  // OLED links
  I2CL.begin(OLED_L_SDA, OLED_L_SCL);
  dispL.begin(SSD1306_SWITCHCAPVCC, OLED_L_ADDR);

  // Startbildschirme
  dispR.clearDisplay();
  dispR.setTextSize(1);
  dispR.setTextColor(SSD1306_WHITE);
  dispR.setCursor(0,0);
  dispR.println("BAG-BUDDY START");
  dispR.display();

  dispL.clearDisplay();
  dispL.setTextSize(1);
  dispL.setTextColor(SSD1306_WHITE);
  dispL.setCursor(0,0);
  dispL.println("READY");
  dispL.display();
  delay(800);
}

// ----------------------------------------------------
//  LOOP
// ----------------------------------------------------

void loop() {

  // ------------------- Joystick -------------------
  int rawX = analogRead(JOY_X) - 2048;
  int rawY = analogRead(JOY_Y) - 2048;

  int joyX = (abs(rawX) < DEADZONE) ? 0 : rawX;
  int joyY = (abs(rawY) < DEADZONE) ? 0 : rawY;
  bool joyBtn = !digitalRead(JOY_SW);

  // ------------------- Buttons -------------------
  bool b1 = !digitalRead(BTN1);
  bool b2 = !digitalRead(BTN2);

  // ------------------- Keypad -------------------
  char key = keypad.getKey();

  // ---------------- Reset - mit - 0 ------------------

  if (key == '0') {
    ESP.restart();
  }

  // ------------------- Matrix Mode -------------------

  if (key == 'A') matrixMode = true;
  if (key == 'B') matrixMode = false;

  if (matrixMode) {
    dispR.clearDisplay();
    dispR.setTextSize(1);
    dispR.setTextColor(SSD1306_WHITE);

    for (int col = 0; col < matrixCols; col++) {

        char c = char(random(55, 228));

        int x = col * 8;
        int y = matrixY[col];

        dispR.setCursor(x, y);
        dispR.write(c);

        matrixY[col] += 2;

        if (matrixY[col] > 64) {
            matrixY[col] = random(-20, 0);
        }
    }

    dispR.display();
    return;
  }

  else {
    // ------------------- OLED rechts: Statusanzeige -------------------
    dispR.clearDisplay();
    dispR.setCursor(0,0);
    dispR.println("STATUS");

    dispR.print("JoyX: "); dispR.println(joyX);
    dispR.print("JoyY: "); dispR.println(joyY);
    dispR.print("JoyBtn: "); dispR.println(joyBtn);

    dispR.print("BTN1: "); dispR.println(b1);
    dispR.print("BTN2: "); dispR.println(b2);

    dispR.print("Key: "); dispR.println(key ? key : '-');

    dispR.display();
  }

  // ------------------- Buzzer Beispiel -------------------

  if (b1) {
    // tone(BUZZER, 1500);
    boxesFilled = true;   // Zustand merken
  }
  else if (b2) {
    // tone(BUZZER, 800);
    boxesFilled = false;  // Zustand zurücksetzen
  }
  else {
    noTone(BUZZER);
  }

  // ------------------- OLED rechts: Statusanzeige -------------------
  dispR.clearDisplay();
  dispR.setCursor(0,0);
  dispR.println("STATUS");

  dispR.print("JoyX: "); dispR.println(joyX);
  dispR.print("JoyY: "); dispR.println(joyY);
  dispR.print("JoyBtn: "); dispR.println(joyBtn);

  dispR.print("BTN1: "); dispR.println(b1);
  dispR.print("BTN2: "); dispR.println(b2);

  dispR.print("Key: "); dispR.println(key ? key : '-');

  dispR.display();
  

  // ------------------- OLED Links --------------
  // ------------------- MOODS (stehen bleiben!) -------------------

  if(key == '4') currentMode = '4';
  else if(key == '5') currentMode = '5';
  else if(key == '6') currentMode = '6';
  else if(key == '7') currentMode = '7';
  else if(key == '8') currentMode = 'default';

  else if (currentMode == '4') {
    // HAPPY
    dispL.clearDisplay();

    dispL.drawCircle(32, 28, 14, WHITE);
    dispL.fillCircle(32, 28, 6, WHITE);

    dispL.drawCircle(96, 28, 14, WHITE);
    dispL.fillCircle(96, 28, 6, WHITE);

    dispL.drawLine(44, 55, 84, 55, WHITE); // Smile

    dispL.display();
  }

  else if (key == '5') {
    // ANGRY
    dispL.clearDisplay();

    dispL.drawLine(14, 18, 50, 32, WHITE);   // linke Braue
    dispL.drawLine(78, 32, 114, 18, WHITE);  // rechte Braue

    dispL.drawCircle(32, 42, 14, WHITE);
    dispL.fillCircle(32, 42, 6, WHITE);

    dispL.drawCircle(96, 42, 14, WHITE);
    dispL.fillCircle(96, 42, 6, WHITE);

    dispL.display();
  }

  else if (key == '6') {
    // SLEEPY / BLINK
    dispL.clearDisplay();

    dispL.drawLine(18, 32, 46, 32, WHITE);
    dispL.drawLine(86, 32, 114, 32, WHITE);

    dispL.display();
  }

  else if (key == '7') {
    // SAD
    dispL.clearDisplay();

    dispL.drawCircle(32, 32, 14, WHITE);
    dispL.fillCircle(32, 32, 6, WHITE);

    dispL.drawCircle(96, 32, 14, WHITE);
    dispL.fillCircle(96, 32, 6, WHITE);

    dispL.drawLine(44, 62, 84, 62, WHITE); // trauriger Mund

    dispL.display();
  } 

  else if (currentMode == 'default') {

    for (int i = 0; i < matrixCols; i++) {
      matrixY[i] = random(-64, 0);
    }

    dispL.display();
    dispL.clearDisplay();

    if (millis() > nextTargetTime) {
      targetX = random(-10, 15);   // horizontale Bewegung
      targetY = random(-10, 13);   // vertikale Bewegung
      nextTargetTime = millis() + random(500, 900);
    }

    // Smooth Bewegung Richtung Ziel
    eyeX += (targetX - eyeX) * 0.30;
    eyeY += (targetY - eyeY) * 0.30;

    // Augen zeichnen
    dispL.fillRoundRect(10 + eyeX, 5 + eyeY, 40, 40, 4, WHITE);   // links
    dispL.fillRoundRect(88 + eyeX, 5 + eyeY, 40, 40, 4, WHITE);   // rechts

    dispL.display();
  }
}