#include <SoftwareSerial.h>
// #include <Arduino_LED_Matrix.h>
// #include <ArduinoGraphics.h>

char c =' ';
//ArduinoLEDMatrix matrix;
//SoftwareSerial BTSerial(10, 11); // RX | TX

void setup()
{
  // put the HC-05 in AT mode by holding the button (or pulling EN pin high) while powering it on.
  //pinMode(9, OUTPUT);  // this pin will pull the HC-05 pin 34 (key pin) HIGH to switch module to AT mode
  //digitalWrite(9, HIGH);
  Serial.begin(750);
  while (!Serial)
    delay(50); 
 
  //matrix.begin();
 
  Serial.println("HC-05 Init");
  //Serial1.begin(38400); // HC-05 default speed in AT command mode
  Serial1.begin(230400); // HC-05 default speed in AT command mode
  while (!Serial1)
    delay(50);
  Serial.println("HC-05 found");
 
  Serial.println("Enter AT commands:");
}

void loop()
{
  // Keep reading from HC-05 and send to Arduino Serial Monitor
  if (Serial1.available())
  {
    // matrix.loadFrame(LEDMATRIX_BLUETOOTH);
    Serial.write(Serial1.read());
  }

  // Keep reading from Arduino Serial Monitor and send to HC-05
  if (Serial.available())
  {
    c = Serial.read();
    Serial1.write(c);
    // matrix.loadSequence(LEDMATRIX_ANIMATION_LOAD);
    // matrix.play(true);
  }
}