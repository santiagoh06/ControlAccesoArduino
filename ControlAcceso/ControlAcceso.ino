#include <SPI.h>
#include <MFRC522.h>
#include <AsyncTaskLib.h>
#include "StateMachineLib.h"
#include <EEPROM.h>
#include <Keypad.h>
#include <LiquidCrystal.h>
#include <string.h>

// LCD: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(32, 33, 34, 35, 36, 37);

// Lector RFID
const byte PIN_SELECCION = 53;
const byte PIN_RESET = 5;
MFRC522 lector(PIN_SELECCION, PIN_RESET);

// LED RGB y buzzer
const byte PIN_LED_VERDE = 30;
const byte PIN_LED_ROJO = 31;
const byte PIN_BUZZER = 8;

// Teclado
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

// Usuarios guardados en la EEPROM
struct Usuario {
  byte uid[4];
  char nombre[12];
  char rol[12];
  char pin[5];
};

const byte CANTIDAD_USUARIOS = 3;
const byte MARCA_USUARIOS_V3 = 0xA3;

const Usuario usuariosIniciales[CANTIDAD_USUARIOS] = {
  {{0x01, 0x02, 0x03, 0x04}, "Steven",   "Operario",    "1234"},
  {{0x11, 0x22, 0x33, 0x44}, "Sergio",   "Coordinador", "2468"},
  {{0x55, 0x66, 0x77, 0x88}, "Santiago", "Gerente",     "1357"}
};

Usuario usuarioActual;
byte indiceUsuarioActual = 0;
byte intentosFallidos[CANTIDAD_USUARIOS] = {0, 0, 0};
const byte MAX_INTENTOS = 3;

char pinIngresado[5];
byte digitosPIN = 0;

// Tiempos para la prueba en Wokwi
const unsigned long DURACION_FRANJA = 20000UL;
const unsigned long DURACION_LED = 2000UL;
const unsigned long DURACION_BLOQUEO = 10000UL;

const char rolesPorFranja[3][12] = {
  "Operario", "Coordinador", "Gerente"
};

byte franjaActual = 0;
unsigned long inicioFranja = 0;
unsigned long inicioAviso = 0;

// Los siete estados del acceso
enum EstadoAcceso {
  REPOSO,
  VALIDANDO_TARJETA,
  ESPERANDO_PIN,
  VALIDANDO_PIN,
  ACCESO_CONCEDIDO,
  ACCESO_DENEGADO,
  BLOQUEADO
};

// El motivo permite decidir qué mostrar y si se puede reintentar.
enum MotivoDenegacion {
  TARJETA_DESCONOCIDA,
  FUERA_DE_TURNO,
  PIN_INCORRECTO
};

MotivoDenegacion motivo = TARJETA_DESCONOCIDA;

StateMachine maquina(7, 13);  // 7 estados, 13 transiciones
byte estadoSolicitado = REPOSO;

// Condiciones que utiliza la librería para cambiar de estado
bool pideReposo()         { return estadoSolicitado == REPOSO; }
bool pideValidarTarjeta() { return estadoSolicitado == VALIDANDO_TARJETA; }
bool pideEsperarPIN()     { return estadoSolicitado == ESPERANDO_PIN; }
bool pideValidarPIN()     { return estadoSolicitado == VALIDANDO_PIN; }
bool pideConceder()       { return estadoSolicitado == ACCESO_CONCEDIDO; }
bool pideDenegar()        { return estadoSolicitado == ACCESO_DENEGADO; }
bool pideBloquear()       { return estadoSolicitado == BLOQUEADO; }

void cambiarEstado(byte nuevoEstado) {
  estadoSolicitado = nuevoEstado;
  maquina.Update();
}

// EEPROM
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

// Pantalla LCD
void mostrarMensaje(const char primera[], const char segunda[]) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(primera);
  lcd.setCursor(0, 1);
  lcd.print(segunda);
}

void mostrarRol() {
  mostrarMensaje("Rol habilitado:", rolesPorFranja[franjaActual]);
}

void mostrarPantallaPIN() {
  mostrarMensaje(usuarioActual.nombre, "Digite PIN: ");

  for (byte i = 0; i < digitosPIN; i++) {
    lcd.print('*');
  }
}

bool estaEnTurno() {
  return strcmp(usuarioActual.rol, rolesPorFranja[franjaActual]) == 0;
}

// Acciones al entrar en los estados
void entrarReposo() {
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_ROJO, LOW);
  digitosPIN = 0;
  mostrarRol();
}

void entrarEsperandoPIN() {
  digitalWrite(PIN_LED_ROJO, LOW);
  digitosPIN = 0;
  mostrarPantallaPIN();
}

void entrarConcedido() {
  Serial.print("PIN correcto para ");
  Serial.println(usuarioActual.nombre);

  mostrarMensaje("Bienvenido", usuarioActual.nombre);
  digitalWrite(PIN_LED_VERDE, HIGH);
  tone(PIN_BUZZER, 1500, 300);
  inicioAviso = millis();
}

void entrarDenegado() {
  if (motivo == PIN_INCORRECTO) {
    byte intentos = intentosFallidos[indiceUsuarioActual];

    Serial.print("PIN incorrecto. Intento ");
    Serial.print(intentos);
    Serial.println(" de 3.");

    mostrarMensaje("PIN incorrecto", "Intento ");
    lcd.print(intentos);
    lcd.print(" de 3");

  } else if (motivo == FUERA_DE_TURNO) {
    Serial.print("Fuera de turno para el rol ");
    Serial.println(usuarioActual.rol);
    mostrarMensaje("Fuera de turno", usuarioActual.rol);

  } else {
    Serial.println("Tarjeta desconocida.");
    mostrarMensaje("Tarjeta", "desconocida");
  }

  digitalWrite(PIN_LED_ROJO, HIGH);
  tone(PIN_BUZZER, 350, 300);
  inicioAviso = millis();
}

void entrarBloqueado() {
  Serial.println("Acceso bloqueado por 10 segundos.");
  mostrarMensaje("Acceso bloqueado", "Espere 10 s");
  digitalWrite(PIN_LED_ROJO, HIGH);
  tone(PIN_BUZZER, 350, 1000);
  inicioAviso = millis();
}

// Transiciones de la máquina de estados
void configurarMaquina() {
  maquina.AddTransition(REPOSO, VALIDANDO_TARJETA, pideValidarTarjeta);

  maquina.AddTransition(VALIDANDO_TARJETA, ESPERANDO_PIN, pideEsperarPIN);
  maquina.AddTransition(VALIDANDO_TARJETA, ACCESO_DENEGADO, pideDenegar);

  maquina.AddTransition(ESPERANDO_PIN, VALIDANDO_PIN, pideValidarPIN);
  maquina.AddTransition(ESPERANDO_PIN, REPOSO, pideReposo);

  maquina.AddTransition(VALIDANDO_PIN, ACCESO_CONCEDIDO, pideConceder);
  maquina.AddTransition(VALIDANDO_PIN, ACCESO_DENEGADO, pideDenegar);
  maquina.AddTransition(VALIDANDO_PIN, BLOQUEADO, pideBloquear);
  maquina.AddTransition(VALIDANDO_PIN, REPOSO, pideReposo);

  maquina.AddTransition(ACCESO_CONCEDIDO, REPOSO, pideReposo);

  maquina.AddTransition(ACCESO_DENEGADO, ESPERANDO_PIN, pideEsperarPIN);
  maquina.AddTransition(ACCESO_DENEGADO, REPOSO, pideReposo);

  maquina.AddTransition(BLOQUEADO, REPOSO, pideReposo);

  maquina.SetOnEntering(REPOSO, entrarReposo);
  maquina.SetOnEntering(ESPERANDO_PIN, entrarEsperandoPIN);
  maquina.SetOnEntering(ACCESO_CONCEDIDO, entrarConcedido);
  maquina.SetOnEntering(ACCESO_DENEGADO, entrarDenegado);
  maquina.SetOnEntering(BLOQUEADO, entrarBloqueado);

  maquina.SetState(REPOSO, false, true);
}

// Cambio automático de rol
void actualizarHorario() {
  if (millis() - inicioFranja < DURACION_FRANJA) {
    return;
  }

  inicioFranja += DURACION_FRANJA;
  franjaActual++;

  if (franjaActual == 3) {
    franjaActual = 0;
  }

  Serial.print("Rol habilitado: ");
  Serial.println(rolesPorFranja[franjaActual]);

  byte estado = maquina.GetState();

  if (estado == ESPERANDO_PIN || estado == VALIDANDO_PIN) {
    Serial.println("Cambio de turno. Presenta una tarjeta.");
    cambiarEstado(REPOSO);
  } else if (estado == REPOSO) {
    mostrarRol();
  }
}

// Lectura y validación de la tarjeta
void validarTarjeta() {
  actualizarHorario();

  for (byte i = 0; i < CANTIDAD_USUARIOS; i++) {
    EEPROM.get(direccionUsuario(i), usuarioActual);

    if (lector.uid.size == sizeof(usuarioActual.uid) &&
        memcmp(lector.uid.uidByte, usuarioActual.uid,
               sizeof(usuarioActual.uid)) == 0) {

      indiceUsuarioActual = i;

      if (estaEnTurno()) {
        Serial.print("Usuario reconocido: ");
        Serial.print(usuarioActual.nombre);
        Serial.print(" | ");
        Serial.println(usuarioActual.rol);
        Serial.println("Escribe tu PIN de 4 digitos y pulsa #.");

        cambiarEstado(ESPERANDO_PIN);
      } else {
        motivo = FUERA_DE_TURNO;
        cambiarEstado(ACCESO_DENEGADO);
      }
      return;
    }
  }

  motivo = TARJETA_DESCONOCIDA;
  cambiarEstado(ACCESO_DENEGADO);
}

void leerTarjeta() {
  if (maquina.GetState() != REPOSO) return;
  if (!lector.PICC_IsNewCardPresent()) return;
  if (!lector.PICC_ReadCardSerial()) return;

  cambiarEstado(VALIDANDO_TARJETA);

  Serial.print("UID leido: ");
  for (byte i = 0; i < lector.uid.size; i++) {
    if (i > 0) Serial.print(':');
    if (lector.uid.uidByte[i] < 0x10) Serial.print('0');
    Serial.print(lector.uid.uidByte[i], HEX);
  }
  Serial.println();

  validarTarjeta();
  lector.PICC_HaltA();
}

// Lectura y validación del PIN
void validarPIN() {
  actualizarHorario();

  // Si acaba de vencer el turno, actualizarHorario() cancela el ingreso.
  if (maquina.GetState() != VALIDANDO_PIN) return;

  pinIngresado[digitosPIN] = '\0';

  if (strcmp(pinIngresado, usuarioActual.pin) == 0) {
    intentosFallidos[indiceUsuarioActual] = 0;
    cambiarEstado(ACCESO_CONCEDIDO);
  } else {
    intentosFallidos[indiceUsuarioActual]++;

    if (intentosFallidos[indiceUsuarioActual] >= MAX_INTENTOS) {
      cambiarEstado(BLOQUEADO);
    } else {
      motivo = PIN_INCORRECTO;
      cambiarEstado(ACCESO_DENEGADO);
    }
  }
}

void leerTeclado() {
  char tecla = teclado.getKey();

  if (tecla == NO_KEY || maquina.GetState() != ESPERANDO_PIN) {
    return;
  }

  if (tecla == '*') {
    digitosPIN = 0;
    mostrarPantallaPIN();
    Serial.println("PIN borrado. Escribelo de nuevo.");

  } else if (tecla == '#') {
    if (digitosPIN == 4) {
      cambiarEstado(VALIDANDO_PIN);
      validarPIN();
    } else {
      mostrarMensaje("PIN incompleto", "Faltan digitos");
      Serial.println("Completa los 4 digitos.");
    }

  } else if (tecla >= '0' && tecla <= '9' && digitosPIN < 4) {
    pinIngresado[digitosPIN] = tecla;
    digitosPIN++;
    mostrarPantallaPIN();
  }
}

// Duración de los mensajes y del bloqueo
void revisarAvisos() {
  byte estado = maquina.GetState();
  unsigned long tiempoPasado = millis() - inicioAviso;

  if (estado == BLOQUEADO && tiempoPasado >= DURACION_BLOQUEO) {
    intentosFallidos[indiceUsuarioActual] = 0;
    Serial.println("Bloqueo terminado. Presenta una tarjeta.");
    cambiarEstado(REPOSO);

  } else if (estado == ACCESO_CONCEDIDO && tiempoPasado >= DURACION_LED) {
    cambiarEstado(REPOSO);

  } else if (estado == ACCESO_DENEGADO && tiempoPasado >= DURACION_LED) {
    actualizarHorario();

    if (motivo == PIN_INCORRECTO && estaEnTurno()) {
      cambiarEstado(ESPERANDO_PIN);
    } else {
      cambiarEstado(REPOSO);
    }
  }
}

// Tareas periódicas
AsyncTask tareaHorario(50, true, actualizarHorario);
AsyncTask tareaRFID(50, true, leerTarjeta);
AsyncTask tareaTeclado(20, true, leerTeclado);
AsyncTask tareaIndicadores(20, true, revisarAvisos);

void setup() {
  Serial.begin(9600);
  SPI.begin();
  lector.PCD_Init();
  lcd.begin(16, 2);

  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  prepararEEPROM();
  inicioFranja = millis();
  configurarMaquina();

  Serial.println("Presenta una tarjeta.");
  Serial.print("Rol habilitado: ");
  Serial.println(rolesPorFranja[franjaActual]);

  tareaHorario.Start();
  tareaRFID.Start();
  tareaTeclado.Start();
  tareaIndicadores.Start();
}

void loop() {
  tareaHorario.Update();
  tareaRFID.Update();
  tareaTeclado.Update();
  tareaIndicadores.Update();
}
