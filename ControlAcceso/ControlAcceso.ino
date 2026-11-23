#include <SPI.h>
#include <MFRC522.h>

const byte PIN_SELECCION = 53;
const byte PIN_RESET = 5;

MFRC522 lector(PIN_SELECCION, PIN_RESET);

void setup() {
  Serial.begin(9600);
  SPI.begin();
  lector.PCD_Init();

  Serial.println("Lector RFID listo. Presenta una tarjeta.");
}

void loop() {
  if (!lector.PICC_IsNewCardPresent()) {
    return;
  }

  if (!lector.PICC_ReadCardSerial()) {
    return;
  }

  Serial.print("UID leido: ");

  for (byte i = 0; i < lector.uid.size; i++) {
    if (i > 0) {
      Serial.print(':');
    }

    byte valor = lector.uid.uidByte[i];
    if (valor < 0x10) {
      Serial.print('0');
    }
    Serial.print(valor, HEX);
  }

  Serial.println();
  lector.PICC_HaltA();
}
