#include <bluefruit.h>
#include <Adafruit_NeoPixel.h>

BLEUart bleuart; // Create BLE UART service

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  Bluefruit.begin(0, 1);
  Bluefruit.setName("MPU6050_Receiver");

  bleuart.begin(); // Initialize BLE UART service

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.addService(bleuart); // Add BLE UART service

  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0); // 0 = Don't stop advertising

  Serial1.println("Bluetooth receiver setup done.");
}

void loop() {
  if (bleuart.available()) {
    String data = bleuart.readStringUntil('\n');
    int commaIndex = data.indexOf(',');
    int16_t x = data.substring(0, commaIndex).toInt();
    int16_t y = data.substring(commaIndex + 1).toInt();

    Serial1.print("x: ");
    Serial1.print(x);
    Serial1.print(" y: ");
    Serial1.println(y);
}
