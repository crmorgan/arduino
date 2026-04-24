#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <bluefruit.h>

Adafruit_MPU6050 mpu;

BLEUart bleuart; // Create BLE UART service

void setup() {
  Serial.begin(9600);
  while (!Serial) delay(10);
  pinMode(LED_BUILTIN, OUTPUT);

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

  Serial.print(x);
  Serial.print(" ");
  Serial.print(y);
  Serial.println();

  char buf[16];
  snprintf(buf, sizeof(buf), "%d,%d\n", x, y);
  bleuart.print(buf);

  delay(20);
}
