#include <Wire.h>
#include <I2Cdev.h>
#include <MPU6050.h>
#include <ezButton.h>
//#include "Mouse.h"

MPU6050 mpu;
int16_t ax;
int16_t ay;
int16_t az, gx, gy, gz;
int16_t axOffset, ayOffset;
const int ENABLE_BUTTON_PIN = 7;
const int CALIBRATE_BUTTON_PIN =8;
bool mouseEnabled = false;

ezButton enableButton(ENABLE_BUTTON_PIN);
ezButton calibrateButton(CALIBRATE_BUTTON_PIN);

void setup() {
  Serial.begin(9600);
  Wire.begin();
  mpu.initialize();
  //Mouse.begin();
  enableButton.setDebounceTime(50);
  calibrateButton.setDebounceTime(50);
  delay(8000);
  Serial.println("Ready");
}

void loop() {
  enableButton.loop();
  if(enableButton.isPressed()) {
    Serial.println("Button pressed");
    mouseEnabled = !mouseEnabled;
  }

  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  calibrateButton.loop();
  if(calibrateButton.isPressed())
  {
    axOffset = ax;
    ayOffset = ay;
    Serial.print("Offsets x:");
    Serial.print(axOffset);
    Serial.print(" y:");
    Serial.println(ayOffset);
  }

  long x = map(ax-axOffset, -17000, 17000, -100, 100);
  long y = map(ay-ayOffset, -17000, 17000, -100, 100);

  if(x > -3 && x < 3) x = 0;
  if(y > -3 && y < 3) y = 0;

  Serial.print("x: ");
  Serial.print(x);
  Serial.print(" y:");
  Serial.print(y);
  Serial.println("");

  if(mouseEnabled) {
    //Mouse.move(x, y);
  }

  delay(500);
}
