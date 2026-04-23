#include <SoftwareSerial.h>

SoftwareSerial BTSerial(10, 11); // RX, TX

void setup() {
  Serial.begin(9600);
  BTSerial.begin(9600);
  delay(1000);
  Serial.println("HC-05 Init with SoftwareSerial");
}

void loop() {
  if (Serial.available()) {
    char data = Serial.read();
    Serial.print("tx: ");
    Serial.println(data);
    BTSerial.write(data);
  }

  if (BTSerial.available()) {
    char received = BTSerial.read();
    Serial.print("rx: ");
    Serial.println(received);
  }
}
