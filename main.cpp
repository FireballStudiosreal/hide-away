#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>


#define SS_PIN 10
#define RST_PIN 9
#define SERVO_PIN 8

// ---------------- OBJECTS ----------------
MFRC522 rfid(SS_PIN, RST_PIN);
Servo lockServo;

// ---------------- STATE ----------------
bool stateLocked = true;
bool adminMode = false;

// ---------------- ADMIN CARD UID ----------------
byte adminUID[4] = {0x56, 0x66, 0x09, 0xF8};

// ---------------- SETTINGS ----------------
#define MAX_CARDS 10
#define UID_SIZE 4

// ---------------- WATCHDOG ----------------
unsigned long lastRFIDCheck = 0;

// ---------------- LED ----------------
int colorOut(int r, int g, int b) {
  analogWrite(14, b);
  analogWrite(15, g);
  analogWrite(16, r);
  return 0;
}

// ---------------- SERVO ----------------
void updateLockState() {
  if (stateLocked) {
    lockServo.write(90);    // one direction
    colorOut(255, 0, 0);
  } else {
    lockServo.write(180);   // 90° the OTHER direction
    colorOut(0, 255, 0);
  }
}

// ---------------- EEPROM ----------------
int getCardAddress(int index) {
  return index * UID_SIZE;
}

bool compareUID(byte *a, byte *b, byte size) {
  for (int i = 0; i < size; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool isRegistered(byte *uid, byte size) {
  for (int i = 0; i < MAX_CARDS; i++) {
    byte stored[UID_SIZE];
    for (int j = 0; j < UID_SIZE; j++) {
      stored[j] = EEPROM.read(getCardAddress(i) + j);
    }
    if (compareUID(uid, stored, size)) return true;
  }
  return false;
}

void saveCard(byte *uid, byte size) {
  for (int i = 0; i < MAX_CARDS; i++) {
    byte stored[UID_SIZE];
    bool empty = true;

    for (int j = 0; j < UID_SIZE; j++) {
      stored[j] = EEPROM.read(getCardAddress(i) + j);
      if (stored[j] != 0xFF) empty = false;
    }

    if (empty) {
      for (int j = 0; j < UID_SIZE; j++) {
        EEPROM.write(getCardAddress(i) + j, uid[j]);
      }
      Serial.println("Card registered.");
      return;
    }
  }
  Serial.println("No space to save card.");
}

void deleteCard(byte *uid, byte size) {
  for (int i = 0; i < MAX_CARDS; i++) {
    byte stored[UID_SIZE];
    for (int j = 0; j < UID_SIZE; j++) {
      stored[j] = EEPROM.read(getCardAddress(i) + j);
    }

    if (compareUID(uid, stored, size)) {
      for (int j = 0; j < UID_SIZE; j++) {
        EEPROM.write(getCardAddress(i) + j, 0xFF);
      }
      Serial.println("Card removed.");
      return;
    }
  }
}

// ---------------- RFID ----------------
void printUID(byte *uid, byte size) {
  Serial.print("UID: ");
  for (int i = 0; i < size; i++) {
    if (uid[i] < 0x10) Serial.print("0");
    Serial.print(uid[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

bool isAdmin(byte *uid, byte size) {
  return compareUID(uid, adminUID, size);
}

// ---------------- ERROR BLINK ----------------
void errorBlink() {
  for (int i = 0; i < 3; i++) {
    colorOut(255, 0, 0);
    delay(200);
    colorOut(0, 0, 0);
    delay(200);
  }
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  lockServo.attach(SERVO_PIN);

  pinMode(14, OUTPUT);
  pinMode(15, OUTPUT);
  pinMode(16, OUTPUT);

  updateLockState();

  Serial.println("Ready.");
}

// ---------------- LOOP ----------------
void loop() {
  
  if (millis() - lastRFIDCheck > 5000) {
    Serial.println("Resetting RFID...");
    rfid.PCD_Init();
    lastRFIDCheck = millis();
  }

  // Wait for card
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  lastRFIDCheck = millis(); // reset watchdog timer

  byte *uid = rfid.uid.uidByte;
  byte size = rfid.uid.size;

  printUID(uid, size);

  // ADMIN MODE ENTRY
  if (isAdmin(uid, size)) {
    adminMode = true;
    Serial.println("Admin mode: scan card to add/remove");
    colorOut(0, 0, 255); // blue
    delay(1000);
    
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  // ADMIN ACTION
  if (adminMode) {
    if (isRegistered(uid, size)) {
      deleteCard(uid, size);
    } else {
      saveCard(uid, size);
    }
    adminMode = false;
    updateLockState();

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    delay(300);
    return;
  }

  // NORMAL ACCESS
  if (isRegistered(uid, size)) {
    stateLocked = !stateLocked;
    updateLockState();
    Serial.println("Access granted.");
  } else {
    Serial.println("Access denied.");
    errorBlink();
  }

  // 🔑 Proper cleanup (VERY IMPORTANT)
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(300); // small stability delay
}