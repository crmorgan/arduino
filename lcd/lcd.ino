#include <LiquidCrystal.h>

// rs, enable, d4 - d7
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.print("Setup complete");
  delay(2000);
  lcd.clear();
}

void loop() {
  lcd.setCursor(0, 0);
  lcd.print("Hi Honey,");
  lcd.setCursor(0, 1);
  lcd.print("I love you!");
  delay(1000);
}
