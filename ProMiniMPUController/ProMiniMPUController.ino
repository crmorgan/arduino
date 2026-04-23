#include <Wire.h>
#include <I2Cdev.h>
#include <MPU6050.h>
#include <Mouse.h>
#include <ezButton.h>

MPU6050 mpu;
int16_t ax, ay, az;
int16_t gx, gy, gz;
long d = 100;
const int BUTTON_PIN = 7;
bool mouseEnabled = false;

ezButton button(BUTTON_PIN);

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Mouse.begin();
  mpu.initialize();
  button.setDebounceTime(50);
}

void loop() {

  button.loop();

  if(button.isPressed()) {
    Serial.println("Button pressed");
    mouseEnabled = !mouseEnabled;
  }

  if(mouseEnabled) {
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    long x = map(ax, -17000, 17000, -5, 5);
    long y = map(ay, -17000, 17000, -5, 5);

    // Serial.print("x: ");
    // Serial.print(x);
    // Serial.print(" y:");
    // Serial.print(y);
    // Serial.println("");
    delay(10);

    if(x == 0 && y == 0) {
      Serial.println("center");
    }
    else {
      signed char mouseX = static_cast<signed char>(x);
      signed char mouseY = static_cast<signed char>(y);

      Serial.print("x: ");
      Serial.print(mouseX);
      Serial.print(" y:");
      Serial.print(mouseY);
      Serial.println("");

      Mouse.move(mouseX, mouseY, 0);
      delay(d);
    }
  }
}