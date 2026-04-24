#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <bluefruit.h>

// --- Battery ---------------------------------------------------------------
#define VBAT_CHECK_MS    1000  // How often to sample VBAT
#define VBAT_CUTOFF       3.2f // Shut down below this (LiPo safety floor)
// ---------------------------------------------------------------------------

Adafruit_MPU6050 mpu;

BLEUart bleuart; // Create BLE UART service

float latestVbat = 0.0f;
uint32_t lastVbatCheck = 0;

float readBattery() {
  // Feather nRF52840 Express: VBAT -> 1M/1M divider -> PIN_VBAT,
  // read with 12-bit ADC against 3.0 V internal reference.
  float raw = (float)analogRead(PIN_VBAT);
  return raw * (3.0f / 4096.0f) * 2.0f;
}

void setup() {
  Serial.begin(9600);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  pinMode(LED_BUILTIN, OUTPUT);

  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  analogRead(PIN_VBAT);  // Warm-up dummy read
  delay(1);

  Bluefruit.begin();
  Bluefruit.setName("MPU6050_Sender");

  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH); delay(100);
      digitalWrite(LED_BUILTIN, LOW);  delay(100);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);

  bleuart.begin(); // Initialize BLE UART service

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.addService(bleuart); // Add BLE UART service

  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0); // 0 = Don't stop advertising

  Serial.println("MPU6050 and Bluefruit setup done.");
}

void loop() {
  static uint8_t stuckCount = 0;
  static uint8_t failedRecoveries = 0;
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  if (a.acceleration.x == 0.0f && a.acceleration.y == 0.0f && a.acceleration.z == 0.0f) {
    if (++stuckCount >= 10) {
      Serial.println("*********************************************************************");
      Serial.println("****************** MPU stuck - re-initializing **********************");
      Serial.println("*********************************************************************");
      stuckCount = 0;
      if (mpu.begin()) {
        mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
        mpu.setGyroRange(MPU6050_RANGE_250_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);
        Serial.println("MPU re-init ok");
        failedRecoveries = 0;
      } else {
        Serial.println("MPU re-init FAILED");
        if (++failedRecoveries >= 3) {
          Serial.println("Multiple recoveries failed - system reset");
          delay(100);
          NVIC_SystemReset();
        }
      }
    }
    delay(100);
    return;
  }
  stuckCount = 0;

  int16_t x = a.acceleration.x * 100;
  int16_t y = a.acceleration.y * 100;

  if (millis() - lastVbatCheck >= VBAT_CHECK_MS) {
    lastVbatCheck = millis();
    latestVbat = readBattery();
    if (latestVbat > 2.5f && latestVbat < VBAT_CUTOFF) {
      Serial.print("Battery critical (");
      Serial.print(latestVbat, 2);
      Serial.println(" V) - shutting down for LiPo safety");
      Bluefruit.Advertising.stop();
      if (Bluefruit.connected()) {
        Bluefruit.disconnect(Bluefruit.connHandle());
      }
      while (1) {
        digitalWrite(LED_BUILTIN, HIGH); delay(500);
        digitalWrite(LED_BUILTIN, LOW);  delay(500);
      }
    }
  }

  if (Serial) {
    Serial.print(x);
    Serial.print(" ");
    Serial.print(y);
    Serial.print(" vbat=");
    Serial.println(latestVbat, 2);
  }

  char buf[24];
  snprintf(buf, sizeof(buf), "%d,%d,%.2f\n", x, y, latestVbat);
  bleuart.print(buf);

  delay(20);
}
