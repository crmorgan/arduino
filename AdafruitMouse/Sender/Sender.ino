#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <bluefruit.h>

Adafruit_MPU6050 mpu;

BLEUart bleuart; // Create BLE UART service

void setup() {
  Serial.begin(9600);
  Bluefruit.begin();
  Bluefruit.setName("MPU6050_Sender");

  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1);
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
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  int16_t x = a.acceleration.x * 100;
  int16_t y = a.acceleration.y * 100;

  Serial.print(x);
  Serial.print(" ");
  Serial.print(y);
  Serial.println();

  bleuart.print(x);
  bleuart.print(",");
  bleuart.println(y);

  delay(100);
}
