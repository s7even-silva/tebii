#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

// Reconstruccion, NO copia recuperada del programa utilizado en la sesion.
// ESP32 clasico: editar estos pines para que coincidan con el montaje real.
constexpr uint8_t PIN_DOUT = 19;
constexpr uint8_t PIN_SCK = 18;
// 25 pulsos = entrada diferencial a 10 Hz; 27 = diferencial a 40 Hz.
// NO usar 26: en HX710B selecciona DVDD-AVDD, no el puente de presion.
constexpr uint8_t PULSOS = 25;
constexpr uint32_t TIMEOUT_MS = 2000;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

double patm = 101.325, v0 = 1.00, vm = 0.76, pmax = 35.00;
int nLecturas = 100, ciclo = 1;
double vj = 0.98;
String sentido = "COMPRESION", linea;
bool puntoSeleccionado = false, descartarLinea = false;

struct Estadistica {
  double media = 0, m2 = 0;
  int n = 0, rails = 0;
  int32_t minimo = INT32_MAX, maximo = INT32_MIN;
  void agregar(int32_t x) {
    ++n;
    double d = x - media;
    media += d / n;
    m2 += d * (x - media);
    if (x < minimo) minimo = x;
    if (x > maximo) maximo = x;
    if (x == -8388608 || x == 8388607) ++rails;
  }
  double sd() const { return n > 1 ? sqrt(m2 / (n - 1)) : 0; }
};

bool leerHX(int32_t &valor, uint32_t &bits) {
  uint32_t inicio = millis();
  while (digitalRead(PIN_DOUT) == HIGH) {
    if ((uint32_t)(millis() - inicio) >= TIMEOUT_MS) return false;
    delay(1);
  }
  bits = 0;
  // Evita que una interrupcion prolongue SCK alto hasta power-down.
  // El intervalo real se debe comprobar en el osciloscopio del montaje.
  portENTER_CRITICAL(&mux);
  delayMicroseconds(1);
  for (int i = 0; i < 24; ++i) {
    digitalWrite(PIN_SCK, HIGH);
    delayMicroseconds(1);
    bits = (bits << 1) | (digitalRead(PIN_DOUT) == HIGH ? 1u : 0u);
    digitalWrite(PIN_SCK, LOW);
    delayMicroseconds(1);
  }
  for (int i = 24; i < PULSOS; ++i) {
    digitalWrite(PIN_SCK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PIN_SCK, LOW);
    delayMicroseconds(1);
  }
  portEXIT_CRITICAL(&mux);
  // Extension de signo de complemento a dos de 24 bits.
  valor = (bits & 0x800000u) ? (int32_t)((int64_t)bits - 16777216LL)
                               : (int32_t)bits;
  return true;
}

bool adquirir(Estadistica &s, bool detalle = false) {
  // Vaciar la conversion pendiente para iniciar con una lectura nueva.
  int32_t x; uint32_t bits;
  if (!leerHX(x, bits)) {
    Serial.println("ERROR: HX710B no esta listo. Revisar conexion/alimentacion.");
    return false;
  }
  for (int i = 0; i < nLecturas; ++i) {
    if (!leerHX(x, bits)) {
      Serial.println("ERROR: timeout; adquisicion incompleta, no se emite CSV.");
      return false;
    }
    s.agregar(x);
    if (detalle) Serial.printf("Lectura %d/%d: %ld\n", i + 1, nLecturas, (long)x);
  }
  return true;
}

void diagnostico(const Estadistica &s) {
  Serial.printf("Minimo = %ld; Maximo = %ld; Lecturas en rail ADC = %d/%d\n",
    (long)s.minimo, (long)s.maximo, s.rails, s.n);
  if (s.rails) Serial.println("AVISO: se detectaron codigos extremos del ADC.");
}

void config() {
  Serial.println("\n================================================");
  Serial.println("           LABORATORIO N 2 - TEB II");
  Serial.println("================================================");
  Serial.printf("PATM = %.3f kPa\nV0 = %.2f mL\nVM = %.2f mL\nPMAX = %.2f kPa\nN = %d\n",
    patm, v0, vm, pmax, nLecturas);
  double minimo = (patm * v0 - pmax * vm) / (patm + pmax);
  Serial.printf("Vj,min = %.3f mL\n", minimo);
  Serial.println("Marca TEORICA; no verifica rango efectivo ni controla el embolo.");
  Serial.printf("Entrada diferencial, %d Hz; cuentas con signo.\n", PULSOS == 25 ? 10 : 40);
}

void medir() {
  if (!puntoSeleccionado) { Serial.println("Primero enviar 1,compresion,0.98"); return; }
  const double pref = patm * (v0 - vj) / (vj + vm);
  Serial.println("Tomando medicion...");
  Serial.printf("Ciclo: %d\nSentido: %s\nVj: %.2f mL\nPref: %.4f kPa\n",
    ciclo, sentido.c_str(), vj, pref);
  Estadistica s;
  if (!adquirir(s)) return;
  Serial.printf("Resultado:\nMedia = %.3f\nDesviacion estandar = %.3f\nCSV:\n",
    s.media, s.sd());
  Serial.printf("%d,%s,%.2f,%.4f,%.3f,%.3f,%d\n", ciclo, sentido.c_str(),
    vj, pref, s.media, s.sd(), s.n);
  diagnostico(s);
}

bool numero(String texto, double &x) {
  texto.trim();
  if (!texto.length()) return false;
  char *fin;
  x = strtod(texto.c_str(), &fin);
  return *fin == '\0' && isfinite(x);
}

void comando(String cmd) {
  cmd.trim(); cmd.toLowerCase();
  if (cmd == "ayuda") {
    Serial.println("Comandos: cero | cero final | config | raw | medir | ayuda");
    Serial.println("Punto: ciclo,compresion,volumen o ciclo,descompresion,volumen");
    Serial.println("Ejemplo: 1,compresion,0.98 (selecciona y mide). Enter repite.");
    Serial.println("Cambiar constantes: patm=101.325 v0=1 vm=0.76 pmax=35 n=100");
    Serial.println("Enviar UNA asignacion por linea; usar punto decimal y Nueva linea.");
    Serial.println("Esperar equilibrio ANTES del comando. 100 lecturas a 10 Hz ~10 s.");
    return;
  }
  if (cmd == "config") { config(); return; }
  if (cmd == "cero" || cmd == "cero final") {
    Serial.println("Medir cero solo con puerto abierto a atmosfera.");
    Estadistica s;
    if (!adquirir(s, true)) return;
    Serial.println(cmd == "cero" ? "CERO INICIAL" : "CERO FINAL");
    Serial.printf("N = %d\nMedia C0,%s = %.3f\nDesviacion estandar = %.3f\n",
      s.n, cmd == "cero" ? "i" : "f", s.media, s.sd());
    Serial.println("HX710B RESPONDE; esto no demuestra calibracion correcta.");
    diagnostico(s); return;
  }
  if (cmd == "raw") {
    int32_t x; uint32_t bits;
    if (leerHX(x, bits)) Serial.printf("signed=%ld hex=0x%06lX offset_binary=%lu\n",
      (long)x, (unsigned long)bits, (unsigned long)(bits ^ 0x800000u));
    else Serial.println("ERROR: timeout.");
    return;
  }
  if (cmd == "medir" || !cmd.length()) { medir(); return; }
  int igual = cmd.indexOf('=');
  if (igual >= 0) {
    String clave = cmd.substring(0, igual); clave.trim();
    double x;
    if (!numero(cmd.substring(igual + 1), x)) { Serial.println("Valor invalido."); return; }
    if (clave == "patm" && x > 0) patm = x;
    else if (clave == "v0" && x > 0) v0 = x;
    else if (clave == "vm" && x >= 0) vm = x;
    else if (clave == "pmax" && x > 0) pmax = x;
    else if (clave == "n" && x >= 2 && x <= 10000 && floor(x) == x) nLecturas = (int)x;
    else { Serial.println("Parametro o rango invalido."); return; }
    puntoSeleccionado = false; config(); return;
  }
  int a = cmd.indexOf(','), b = cmd.indexOf(',', a + 1);
  if (a < 1 || b < a + 2 || cmd.indexOf(',', b + 1) >= 0) {
    Serial.println("Comando invalido. Enviar ayuda."); return;
  }
  double c, v;
  String dir = cmd.substring(a + 1, b); dir.trim();
  if (!numero(cmd.substring(0, a), c) || c < 1 || c > 100000 || floor(c) != c ||
      !numero(cmd.substring(b + 1), v) || v <= 0 ||
      (dir != "compresion" && dir != "descompresion")) {
    Serial.println("Ciclo, sentido o volumen invalido."); return;
  }
  if (v + vm <= 0) { Serial.println("Volumen total invalido."); return; }
  ciclo = (int)c; vj = v; sentido = dir; sentido.toUpperCase(); puntoSeleccionado = true;
  medir();
}

void setup() {
  Serial.begin(115200);
  digitalWrite(PIN_SCK, LOW);
  pinMode(PIN_SCK, OUTPUT); pinMode(PIN_DOUT, INPUT);
  delay(500);
  config(); comando("ayuda");
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (!descartarLinea) comando(linea);
      linea = ""; descartarLinea = false;
    } else if (!descartarLinea) {
      if (linea.length() < 120) linea += c;
      else { linea = ""; descartarLinea = true; Serial.println("Linea demasiado larga."); }
    }
  }
  delay(1);
}
