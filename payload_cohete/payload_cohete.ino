/*
 * CARGA ÚTIL COHETE - Hidrochallenge 2026
 * ESP32 DevKit V1 + BME280 + MPU9250 + GPS Neo-6M + LoRa Ra-01 (SX1278 433 MHz) + servo
 *
 * LIBRERÍAS (Arduino IDE > Library Manager):
 *   - Adafruit BME280 Library   (instala también Adafruit Unified Sensor / BusIO)
 *   - TinyGPSPlus               (Mikal Hart)
 *   - LoRa                      (Sandeep Mistry)
 *   - ESP32Servo                (Kevin Harrington)
 *   - MPU9250                   (hideakitai)
 *
 * FORMATO DEL PAQUETE (separado por "|"), un paquete cada 4 s (0.25 Hz):
 *   0  millis        ms desde el encendido
 *   1  etapa         0 = en rampa | 1 = ascenso | 2 = apogeo/descenso (servo activado)
 *   2  temperatura   °C
 *   3  humedad       %
 *   4  presion       hPa
 *   5  altitud       m sobre el suelo (barométrica, referencia = presión al encender)
 *   6  latitud       grados
 *   7  longitud      grados
 *   8-10  acelerómetro  ax|ay|az  (g)
 *   11-13 giroscopio    gx|gy|gz  (°/s)
 *   14-16 magnetómetro  mx|my|mz  (mG)
 */

#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <Adafruit_BME280.h>
#include <ESP32Servo.h>
#include "MPU9250.h"

// ======================= PINES =======================
#define PIN_SDA     21
#define PIN_SCL     22
#define GPS_RX      16   // RX2 del ESP32  <- TX del GPS
#define GPS_TX      17   // TX2 del ESP32  -> RX del GPS
#define PIN_SERVO   33

#define LORA_SCK    18
#define LORA_MISO   19
#define LORA_MOSI   23   // MOSI por defecto de VSPI
#define LORA_SS     5    // NSS / CS
#define LORA_RST    14
#define LORA_DIO0   26

// ======================= CONFIGURACIÓN =======================
// LoRa (el receptor debe usar EXACTAMENTE los mismos parámetros)
const long    LORA_FREQ       = 433E6;
const int     LORA_SF         = 8;       // 7 = más rápido, 12 = más alcance
const long    LORA_BW         = 125E3;
const int     LORA_CR         = 5;       // 4/5
const int     LORA_TX_POWER   = 17;      // dBm (máx. 20 en Ra-01)
const int     LORA_SYNCWORD   = 0xF3;

// Tiempos
const uint32_t TX_PERIOD_MS     = 500;  // 1/4 Hz
const uint32_t SENSOR_PERIOD_MS = 50;    // 20 Hz para detectar el apogeo

// Servo (ajusta según tu mecanismo)
const int SERVO_LOCK_ANGLE = 0;          // posición de seguro
const int SERVO_OPEN_ANGLE = 90;         // posición de liberación
<
// Detección de apogeo
const float    LAUNCH_ALT_M      = 15.0;   // altitud sobre suelo para armar (considera despegue)
const float    APOGEE_DROP_M     = 3.0;    // caída respecto al máximo para declarar apogeo
const int      APOGEE_CONFIRM    = 5;      // muestras consecutivas (5 x 50 ms = 0.25 s)
const float    ALT_FILTER_ALPHA  = 0.30;   // 0-1, menor = más filtrado
const uint32_t APOGEE_TIMEOUT_MS = 60000;  // respaldo: libera X ms después del despegue

// ======================= OBJETOS =======================
Adafruit_BME280 bme;
MPU9250         mpu;
TinyGPSPlus     gps;
Servo           servo;

// ======================= ESTADO =======================
bool bmeOk = false;
bool mpuOk = false;

float groundPressure = 1013.25;   // hPa, se calibra al encender
float temperature = 0, humidity = 0, pressure = 0, altitude = 0, altFilt = 0;
float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0, mx = 0, my = 0, mz = 0;

uint8_t  stage = 0;               // 0 rampa, 1 ascenso, 2 apogeo/descenso
float    maxAlt = 0;
int      dropCount = 0;
uint32_t launchTime = 0;

uint32_t lastSensor = 0;
uint32_t lastTx = 0;

// ======================= FUNCIONES =======================
void deployServo() {
  servo.write(SERVO_OPEN_ANGLE);
  stage = 2;
  Serial.println(F(">>> APOGEO: servo activado"));
}

void readSensors() {
  if (bmeOk) {
    temperature = bme.readTemperature();
    humidity    = bme.readHumidity();
    pressure    = bme.readPressure() / 100.0F;
    altitude    = 44330.0F * (1.0F - powf(pressure / groundPressure, 0.1903F));
    altFilt     = ALT_FILTER_ALPHA * altitude + (1.0F - ALT_FILTER_ALPHA) * altFilt;
  }
  if (mpuOk && mpu.update()) {
    ax = mpu.getAccX();  ay = mpu.getAccY();  az = mpu.getAccZ();
    gx = mpu.getGyroX(); gy = mpu.getGyroY(); gz = mpu.getGyroZ();
    mx = mpu.getMagX();  my = mpu.getMagY();  mz = mpu.getMagZ();
  }
}

void updateFlightStage(uint32_t now) {
  if (stage == 0) {
    if (bmeOk && altFilt > LAUNCH_ALT_M) {
      stage = 1;
      launchTime = now;
      maxAlt = altFilt;
      dropCount = 0;
      Serial.println(F(">>> DESPEGUE detectado"));
    }
  } else if (stage == 1) {
    if (bmeOk) {
      if (altFilt > maxAlt) {
        maxAlt = altFilt;
        dropCount = 0;
      } else if ((maxAlt - altFilt) > APOGEE_DROP_M) {
        dropCount++;
      } else {
        dropCount = 0;
      }
      if (dropCount >= APOGEE_CONFIRM) deployServo();
    }
    // Respaldo por tiempo
    if (stage == 1 && (now - launchTime) > APOGEE_TIMEOUT_MS) {
      Serial.println(F(">>> Timeout de apogeo"));
      deployServo();
    }
  }
}

void sendPacket(uint32_t now) {
  double lat = gps.location.isValid() ? gps.location.lat() : 0.0;
  double lon = gps.location.isValid() ? gps.location.lng() : 0.0;

  char buf[200];
  snprintf(buf, sizeof(buf),
           "%lu|%u|%.2f|%.2f|%.2f|%.1f|%.6f|%.6f|%.3f|%.3f|%.3f|%.2f|%.2f|%.2f|%.1f|%.1f|%.1f",
           (unsigned long)now, stage,
           temperature, humidity, pressure, altFilt,
           lat, lon,
           ax, ay, az, gx, gy, gz, mx, my, mz);

  if (LoRa.beginPacket()) {       // devuelve 0 si aún está transmitiendo
    LoRa.print(buf);
    LoRa.endPacket(true);         // asíncrono: no bloquea la detección de apogeo
  }
  Serial.println(buf);
}

// ======================= SETUP =======================
void setup() {
  Serial.begin(115200);
  delay(300);

  // Servo (primero, para dejarlo en seguro)
  servo.setPeriodHertz(50);
  servo.attach(PIN_SERVO, 500, 2400);
  servo.write(SERVO_LOCK_ANGLE);

  // I2C
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  // GPS
  Serial2.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  // BME280 (prueba 0x76 y 0x77)
  bmeOk = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);
  if (bmeOk) {
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,    // temperatura
                    Adafruit_BME280::SAMPLING_X8,    // presión
                    Adafruit_BME280::SAMPLING_X1,    // humedad
                    Adafruit_BME280::FILTER_X4,
                    Adafruit_BME280::STANDBY_MS_0_5);
    delay(100);
    float sum = 0;
    for (int i = 0; i < 30; i++) { sum += bme.readPressure() / 100.0F; delay(20); }
    groundPressure = sum / 30.0F;
    Serial.printf("BME280 OK | P0 = %.2f hPa\n", groundPressure);
  } else {
    Serial.println(F("ERROR: BME280 no encontrado"));
  }

  // MPU9250
  MPU9250Setting setting;
  setting.accel_fs_sel     = ACCEL_FS_SEL::A16G;
  setting.gyro_fs_sel      = GYRO_FS_SEL::G2000DPS;
  setting.mag_output_bits  = MAG_OUTPUT_BITS::M16BITS;
  setting.fifo_sample_rate = FIFO_SAMPLE_RATE::SMPL_200HZ;
  setting.gyro_fchoice     = 0x03;
  setting.gyro_dlpf_cfg    = GYRO_DLPF_CFG::DLPF_41HZ;
  setting.accel_fchoice    = 0x01;
  setting.accel_dlpf_cfg   = ACCEL_DLPF_CFG::DLPF_45HZ;
  mpuOk = mpu.setup(0x68, setting, Wire);
  Serial.println(mpuOk ? F("MPU9250 OK") : F("ERROR: MPU9250 no encontrado"));

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
LoRa.setPins(LORA_SS, -1, -1); // -1 deshabilita RST y DIO0 por completo
  if (LoRa.begin(LORA_FREQ)) {
    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW);
    LoRa.setCodingRate4(LORA_CR);
    LoRa.setTxPower(LORA_TX_POWER);
    LoRa.setSyncWord(LORA_SYNCWORD);
    LoRa.enableCrc();
    Serial.println(F("LoRa OK"));
  } else {
    Serial.println(F("ERROR: LoRa no inicializó"));
  }

  lastSensor = lastTx = millis();
}

// ======================= LOOP =======================
void loop() {
  // Alimentar el parser del GPS en cada vuelta
  while (Serial2.available()) gps.encode(Serial2.read());

  uint32_t now = millis();

  if (now - lastSensor >= SENSOR_PERIOD_MS) {
    lastSensor = now;
    readSensors();
    updateFlightStage(now);
  }

  if (now - lastTx >= TX_PERIOD_MS) {
    lastTx = now;
    sendPacket(now);
  }
}
