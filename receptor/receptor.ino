/*
 * RECEPTOR LoRa - Estación terrena Hidrochallenge 2026
 * ESP32 DevKit + LoRa Ra-01 (SX1278 433 MHz)
 *
 * Imprime cada paquete válido en el mismo formato del transmisor:
 *   millis|etapa|temp|hum|presion|altitud|lat|lon|ax|ay|az|gx|gy|gz|mx|my|mz
 *
 * LIBRERÍA: LoRa (Sandeep Mistry)
 */

#include <SPI.h>
#include <LoRa.h>

// ======================= PINES (mismos que el transmisor) =======================
#define LORA_SCK    18
#define LORA_MISO   19
#define LORA_MOSI   23
#define LORA_SS     5
#define LORA_RST    14
#define LORA_DIO0   26

// ======================= PARÁMETROS (deben coincidir con el transmisor) =======
const long LORA_FREQ     = 433E6;
const int  LORA_SF       = 8;
const long LORA_BW       = 125E3;
const int  LORA_CR       = 5;
const int  LORA_SYNCWORD = 0xF3;

const int NUM_FIELDS = 17;   // campos separados por "|"

// Tiempo máximo entre paquetes antes de contar pérdidas (transmisor: 4 s)
const uint32_t GAP_LIMIT_MS = 6000;

// ======================= ESTADO =======================
uint8_t  lastStage    = 255;
uint32_t lastRxMillis = 0;
uint32_t recibidos    = 0;
uint32_t perdidos     = 0;
uint32_t descartados  = 0;

// ======================= FUNCIONES =======================
int countFields(const String& s) {
  int n = 1;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (s[i] == '|') n++;
  }
  return n;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
LoRa.setPins(LORA_SS, -1, -1); // -1 deshabilita RST y DIO0 por completo

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println(F("# ERROR: LoRa no inicializó"));
    while (true) delay(1000);
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setSyncWord(LORA_SYNCWORD);
  LoRa.enableCrc();

  Serial.println(F("# Receptor LoRa listo"));
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;

  String pkt = "";
  while (LoRa.available()) {
    pkt += (char)LoRa.read();
  }

  // Validar estructura: si no tiene 17 campos, se descarta
  if (countFields(pkt) != NUM_FIELDS) {
    descartados++;
    Serial.print(F("# DESCARTADO: "));
    Serial.println(pkt);
    return;
  }

  // Contar pérdidas por hueco de tiempo
  uint32_t now = millis();
  if (lastRxMillis != 0 && (now - lastRxMillis) > GAP_LIMIT_MS) {
    uint32_t gap = (now - lastRxMillis) / 4000;
    if (gap > 1) perdidos += gap - 1;
  }
  lastRxMillis = now;
  recibidos++;

  // Detectar cambio de etapa (campo 2, entre el primer y segundo '|')
  int p1 = pkt.indexOf('|');
  int p2 = pkt.indexOf('|', p1 + 1);
  uint8_t etapa = pkt.substring(p1 + 1, p2).toInt();

  if (etapa != lastStage) {
    if (lastStage != 255) {
      Serial.print(F("# *** CAMBIO DE ETAPA: "));
      Serial.print(lastStage);
      Serial.print(F(" -> "));
      Serial.println(etapa);
    }
    lastStage = etapa;
  }

  // Imprimir el paquete tal cual llegó
  Serial.println(pkt);
}