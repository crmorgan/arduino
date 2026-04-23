#include <Wire.h>
#include <I2Cdev.h>
#include <MPU6050.h>

MPU6050 mpu;
int16_t ax, ay, az, gx, gy, gz;

void setup() {
  Serial.begin(19200);
  Wire.begin();
  mpu.initialize();
  delay(8000);
  Serial.println("Ready");
}

void loop() {

  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  Serial.print("ax: ");
  Serial.print(ax);
  Serial.print(" ay:");
  Serial.print(ay);
  Serial.print(" gx:");
  Serial.print(gx);
  Serial.println("");

  delay(500);
  ax++;
}
