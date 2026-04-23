#include <SoftwareSerial.h>

char c =' ';

void setup()
{
  Serial.begin(9600);
  while (!Serial)
    delay(50); 
 
  Serial.println("HC-05 Init");
  Serial1.begin(9600);
  while (!Serial1)
    delay(50);
  Serial.println("HC-05 found");

  Serial.println("");
  Serial.println("Setup Complete");
  delay(2000);
}

void loop()
{
  Serial.println(Serial1.read());
  // Keep reading from HC-05 and send to Arduino Serial Monitor
  if (Serial1.available())
  {
    Serial.print("Data Received: ");
    Serial.println(Serial1.read());
  }
}