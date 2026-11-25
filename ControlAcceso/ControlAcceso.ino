#include <SPI.h>
#include <MFRC522.h>
#include <AsyncTaskLib.h>
#include <EEPROM.h>
#include <string.h>

const byte PIN_SELECCION = 53;
const byte PIN_RESET = 5;
const byte CANTIDAD_USUARIOS = 3;
const byte MARCA_USUARIOS_V2 = 0xA2;

MFRC522 lector(PIN_SELECCION, PIN_RESET);

struct Usuario {
  byte uid[4];
  char nombre[12];
  char rol[12];
};

const Usuario usuariosIniciales[CANTIDAD_USUARIOS] = {
  {{0x01, 0x02, 0x03, 0x04}, "Steven",   "Operario"},
  {{0x11, 0x22, 0x33, 0x44}, "Sergio",   "Coordinador"},
  {{0x55, 0x66, 0x77, 0x88}, "Santiago", "Gerente"}
};

// La posición 0 guarda la marca. Los usuarios empiezan en la posición 1.
int direccionUsuario(byte indice) {
  return 1 + indice * sizeof(Usuario);
}

void prepararEEPROM() {
  if (EEPROM.read(0) == MARCA_USUARIOS_V2) {
    Serial.println("EEPROM: usuarios ya registrados.");
    return;
  }

  for (byte i = 0; i < CANTIDAD_USUARIOS; i++) {
    EEPROM.put(direccionUsuario(i), usuariosIniciales[i]);
  }

  EEPROM.update(0, MARCA_USUARIOS_V2);
  Serial.println("EEPROM: se guardaron los tres usuarios.");
}

void mostrarUsuarioDeTarjeta() {
  Usuario usuario;

  for (byte i = 0; i < CANTIDAD_USUARIOS; i++) {
    EEPROM.get(direccionUsuario(i), usuario);

    if (lector.uid.size == sizeof(usuario.uid) &&
        memcmp(lector.uid.uidByte, usuario.uid, sizeof(usuario.uid)) == 0) {
      Serial.print("Usuario reconocido: ");
      Serial.print(usuario.nombre);
      Serial.print(" | ");
      Serial.println(usuario.rol);
      return;
    }
  }

  Serial.println("Tarjeta desconocida");
}

void leerTarjeta() {
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

    if (lector.uid.uidByte[i] < 0x10) {
      Serial.print('0');
    }

    Serial.print(lector.uid.uidByte[i], HEX);
  }

  Serial.println();
  mostrarUsuarioDeTarjeta();
  lector.PICC_HaltA();
}

AsyncTask tareaRFID(50, true, leerTarjeta);

void setup() {
  Serial.begin(9600);
  SPI.begin();
  lector.PCD_Init();

  prepararEEPROM();
  tareaRFID.Start();
  Serial.println("Presenta una tarjeta.");
}

void loop() {
  tareaRFID.Update();
}
