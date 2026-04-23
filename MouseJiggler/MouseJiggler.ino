#include <Mouse.h>
#include <ezButton.h>

const int ENABLE_BUTTON_PIN = 7;
bool mouseEnabled = false;

ezButton enableButton(ENABLE_BUTTON_PIN);

void setup() {
  // Serial.begin(9600);
  // while (!Serial)
  //   delay(50);

  Mouse.begin();
  enableButton.setDebounceTime(100);

  randomSeed(analogRead(0));

  //Serial.println("Setup complete");
  delay(2000);
}

void loop() {
  

  // if (enableButton.isPressed()) {
  //   Serial.print("Button pressed ");
  //   Serial.println(mouseEnabled);
  //   mouseEnabled = !mouseEnabled;
  // }

  // if (!mouseEnabled) return;
  int radius = random(1, 10);  // Random radius for the circle

  for (int i = 0; i < 360; i++) {  // Loop to complete a circle

    enableButton.loop();
    if (enableButton.isPressed()) {
      //Serial.print("Button pressed ");
      //Serial.println(mouseEnabled);
      mouseEnabled = !mouseEnabled;
    }

    if(!mouseEnabled) break;

    int x = radius * cos(degreesToRadians(i));
    int y = radius * sin(degreesToRadians(i));
    
    // Serial.print("x: ");
    // Serial.print(x);
    // Serial.print(" y: ");
    // Serial.println(y);

    Mouse.move(x, y);
    delay(10);
  }

  delay(500);
}

float degreesToRadians(int degrees) {
  return degrees * 3.14159 / 180;
}
