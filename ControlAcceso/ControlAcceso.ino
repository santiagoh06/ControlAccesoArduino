#include <SPI.h>
#include <MFRC522.h>
#include <AsyncTaskLib.h>
#include <EEPROM.h>
#include <Keypad.h>
#include <string.h>

const byte PIN_SELECCION = 53;
const byte PIN_RESET = 5;
const byte CANTIDAD_USUARIOS = 3;
const byte MARCA_USUARIOS_V3 = 0xA3;

MFRC522 lector(PIN_SELECCION, PIN_RESET);

struct Usuario {
  byte uid[4];
  char nombre[12];
  char rol[12];
  char pin[5];
};

const Usuario usuariosIniciales[CANTIDAD_USUARIOS] = {
  {{0x01, 0x02, 0x03, 0x04}, "Steven",   "Operario", "1234"},
  {{0x11, 0x22, 0x33, 0x44}, "Sergio",   "Coordinador", "2468"},
  {{0x55, 0x66, 0x77, 0x88}, "Santiago", "Gerente", "1357"}
};

Usuario usuarioActual;
bool esperandoPIN = false;
char pinIngresado[5];
byte digitosPIN = 0;
const byte MAX_INTENTOS = 3;
const unsigned long DURACION_BLOQUEO = 10000;

byte intentosFallidos = 0;
bool accesoBloqueado = false;
unsigned long inicioBloqueo = 0;

// La posición 0 guarda la marca. Los usuarios empiezan en la posición 1.
int direccionUsuario(byte indice) {
  return 1 + indice * sizeof(Usuario);
}

void prepararEEPROM() {
  if (EEPROM.read(0) == MARCA_USUARIOS_V3) {
    Serial.println("EEPROM: usuarios ya registrados.");
    return;
  }

  for (byte i = 0; i < CANTIDAD_USUARIOS; i++) {
    EEPROM.put(direccionUsuario(i), usuariosIniciales[i]);
  }

  EEPROM.update(0, MARCA_USUARIOS_V3);
  Serial.println("EEPROM: se guardaron los tres usuarios.");
}

//horarios de entradas (OPERARIO, COORDINADOR O GERENTE)f
const unsigned long DURACION_FRANJA = 20000; // 20 segundos para Wokwi

const char rolesPorFranja[3][12] = {
  "Operario",
  "Coordinador",
  "Gerente"
};

int franjaActual = 0;
unsigned long inicioFranja = 0;

void mostrarFranja() {
  Serial.print("Rol habilitado: ");
  Serial.println(rolesPorFranja[franjaActual]);
}

void actualizarHorario() {
  unsigned long ahora = millis();
  unsigned long tiempoPasado = ahora - inicioFranja;

  if (tiempoPasado >= DURACION_FRANJA) {
    inicioFranja = ahora;

    if (franjaActual == 2) {
      franjaActual = 0;
    } else {
      franjaActual = franjaActual + 1;
    }

    if (esperandoPIN) {
      esperandoPIN = false;
      digitosPIN = 0;
      Serial.println("\nCambio de turno. Presenta una tarjeta.");
    }

    mostrarFranja();
  }
}

AsyncTask tareaHorario(50, true, actualizarHorario);

void mostrarUsuarioDeTarjeta() {
  esperandoPIN = false;
  digitosPIN = 0;
  actualizarHorario();

  Usuario usuario;
  for (byte i = 0; i < CANTIDAD_USUARIOS; i++) {
    EEPROM.get(direccionUsuario(i), usuario);

    if (lector.uid.size == sizeof(usuario.uid) &&
        memcmp(lector.uid.uidByte, usuario.uid, sizeof(usuario.uid)) == 0) {
   if (strcmp(usuario.rol, rolesPorFranja[franjaActual]) != 0) {
  Serial.print("Fuera de turno para el rol ");
  Serial.println(usuario.rol);
  indicarResultado(false);
  return;
}
      usuarioActual = usuario;
      esperandoPIN = true;

      Serial.print("Usuario reconocido: ");
      Serial.print(usuarioActual.nombre);
      Serial.print(" | ");
      Serial.println(usuarioActual.rol);
      Serial.println("Escribe tu PIN de 4 digitos y pulsa #.");
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

  if (accesoBloqueado) {
  lector.PICC_HaltA();
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

//Keypad
const byte FILAS = 4;
const byte COLUMNAS = 4;

char mapaTeclas[FILAS][COLUMNAS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte pinesFilas[FILAS] = {22, 23, 24, 25};
byte pinesColumnas[COLUMNAS] = {26, 27, 28, 29};

Keypad teclado(makeKeymap(mapaTeclas), pinesFilas, pinesColumnas,
              FILAS, COLUMNAS);
              
//Leds, pines de acceso permitido o no

const byte PIN_LED_VERDE = 30;
const byte PIN_LED_ROJO = 31;
const byte PIN_BUZZER = 8;
const unsigned long DURACION_LED = 2000;

bool senalActiva = false;
unsigned long inicioSenal = 0;

void indicarResultado(bool pinCorrecto) {
  int frecuencia;

  if (pinCorrecto) {
    digitalWrite(PIN_LED_VERDE, HIGH);
    digitalWrite(PIN_LED_ROJO, LOW);
    frecuencia = 1500;
  } else {
    digitalWrite(PIN_LED_VERDE, LOW);
    digitalWrite(PIN_LED_ROJO, HIGH);
    frecuencia = 350;
  }

  tone(PIN_BUZZER, frecuencia, 300);
  inicioSenal = millis();
  senalActiva = true;
}

void activarBloqueo() {
  accesoBloqueado = true;
  inicioBloqueo = millis();
  esperandoPIN = false;
  senalActiva = false;

  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_ROJO, HIGH);
  tone(PIN_BUZZER, 350, 1000);
  Serial.println("Acceso bloqueado por 10 segundos.");
}

void apagarIndicadores() {
  if (accesoBloqueado) {
    if (millis() - inicioBloqueo >= DURACION_BLOQUEO) {
      digitalWrite(PIN_LED_ROJO, LOW);
      accesoBloqueado = false;
      intentosFallidos = 0;
      Serial.println("Bloqueo terminado. Presenta una tarjeta.");
    }
    return;
  }

  if (senalActiva && millis() - inicioSenal >= DURACION_LED) {
    digitalWrite(PIN_LED_VERDE, LOW);
    digitalWrite(PIN_LED_ROJO, LOW);
    senalActiva = false;
  }
}

AsyncTask tareaIndicadores(20, true, apagarIndicadores);



void leerTeclado() {
  char tecla = teclado.getKey();

  if (tecla == NO_KEY || accesoBloqueado || !esperandoPIN) {
  return;
}

  if (tecla == '*') {
    digitosPIN = 0;
    Serial.println("\nPIN borrado. Escribelo de nuevo.");
    return;
  }

 
  if (tecla == '#') {
       actualizarHorario();
      if (!esperandoPIN) {
        return;
      }
    if (digitosPIN != 4) {
      Serial.println("\nCompleta los 4 digitos.");
      return;
    }
    
    pinIngresado[digitosPIN] = '\0';
    Serial.println();
    
    if (strcmp(pinIngresado, usuarioActual.pin) == 0) {
  intentosFallidos = 0;
  Serial.print("PIN correcto para ");
  Serial.println(usuarioActual.nombre);
  esperandoPIN = false;
  indicarResultado(true);
} else {
  intentosFallidos++;

  if (intentosFallidos >= MAX_INTENTOS) {
    activarBloqueo();
  } else {
    Serial.print("PIN incorrecto. Intento ");
    Serial.print(intentosFallidos);
    Serial.println(" de 3.");
    indicarResultado(false);
  }
}

    digitosPIN = 0;
    return;
  }

  if (tecla >= '0' && tecla <= '9' && digitosPIN < 4) {
    pinIngresado[digitosPIN] = tecla;
    digitosPIN++;
    Serial.print('*');
  }
}

AsyncTask tareaTeclado(20, true, leerTeclado);

void setup() {
  Serial.begin(9600);
  SPI.begin();
  lector.PCD_Init();

  prepararEEPROM();
  tareaRFID.Start();
  tareaTeclado.Start();
  Serial.println("Presenta una tarjeta.");
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

tareaIndicadores.Start();
inicioFranja = millis();
mostrarFranja();
tareaHorario.Start();
}

void loop() {
  tareaHorario.Update();
  tareaRFID.Update();
  tareaTeclado.Update();
  tareaIndicadores.Update();
}
