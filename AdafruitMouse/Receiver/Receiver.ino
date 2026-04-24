#include <bluefruit.h>
#include <ezButton.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// --- Tuning ----------------------------------------------------------------
#define INPUT_MAX         200  // Approx max raw accel value (a * 100 at 2g)
#define DEADZONE_RAW       10  // Ignore tilts inside this raw range (noise)
#define ACCEL_DIVISOR     800  // Quadratic curve divisor; lower = faster
#define OUTPUT_CLAMP       50  // Max per-sample cursor delta
#define AUTO_CAL_MS      1500  // Averaging window for auto-calibrate on connect
// ---------------------------------------------------------------------------

uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_MOUSE() };
Adafruit_USBD_HID usb_hid;

BLEClientUart clientUart;

const uint8_t ENABLE_BUTTON_PIN = 9;
const uint8_t CALIBRATE_BUTTON_PIN = 10;

ezButton enableButton(ENABLE_BUTTON_PIN);
ezButton calibrateButton(CALIBRATE_BUTTON_PIN);

volatile int16_t latestX = 0;
volatile int16_t latestY = 0;
volatile bool dataValid = false;

int16_t xOffset = 0;
int16_t yOffset = 0;
bool mouseEnabled = false;

volatile bool calibrating = false;
uint32_t calStart = 0;
int32_t calSumX = 0;
int32_t calSumY = 0;
uint16_t calCount = 0;

void scan_callback(ble_gap_evt_adv_report_t* report) {
  char name[32] = {0};
  Bluefruit.Scanner.parseReportByType(
      report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME,
      (uint8_t*)name, sizeof(name) - 1);

  if (name[0] == '\0') {
    Bluefruit.Scanner.resume();
    return;
  }

  Serial.print("Saw: ");
  Serial.println(name);

  if (strcmp(name, "MPU6050_Sender") == 0) {
    Serial.println("Match - connecting...");
    Bluefruit.Central.connect(report);
  } else {
    Bluefruit.Scanner.resume();
  }
}

void connect_callback(uint16_t conn_handle) {
  Serial.println("Connected. Discovering BLE UART service...");
  if (clientUart.discover(conn_handle)) {
    Serial.println("BLE UART service found. Enabling TXD notify.");
    clientUart.enableTXD();
    Serial.println("Ready to receive data. Auto-calibrating - hold still...");
    calSumX = 0;
    calSumY = 0;
    calCount = 0;
    calStart = millis();
    calibrating = true;
    pixel.setPixelColor(0, pixel.Color(0, 0, 255));
    pixel.show();
  } else {
    Serial.println("BLE UART service NOT found on peer. Disconnecting.");
    Bluefruit.disconnect(conn_handle);
  }
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  Serial.print("Disconnected, reason 0x");
  Serial.println(reason, HEX);
}

void bleuart_rx_callback(BLEClientUart& uart_svc) {
  static char rxbuf[32];
  static size_t rxlen = 0;

  while (uart_svc.available()) {
    char c = (char)uart_svc.read();
    if (c == '\n' || c == '\r') {
      if (rxlen == 0) continue;
      rxbuf[rxlen] = '\0';
      char* comma = strchr(rxbuf, ',');
      if (comma) {
        *comma = '\0';
        latestX = (int16_t)atoi(rxbuf);
        latestY = (int16_t)atoi(comma + 1);
        dataValid = true;
      }
      rxlen = 0;
    } else if (rxlen < sizeof(rxbuf) - 1) {
      rxbuf[rxlen++] = c;
    } else {
      rxlen = 0;  // Overflow, discard.
    }
  }
}

void setup() {
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();

  Serial.begin(9600);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  enableButton.setDebounceTime(50);
  calibrateButton.setDebounceTime(50);

  pixel.begin();
  pixel.setBrightness(30);
  pixel.clear();
  pixel.show();

  Bluefruit.begin(0, 1);
  Bluefruit.setName("MPU6050_Receiver");

  clientUart.begin();
  clientUart.setRxCallback(bleuart_rx_callback);

  Bluefruit.Central.setConnectCallback(connect_callback);
  Bluefruit.Central.setDisconnectCallback(disconnect_callback);

  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160, 80);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0);

  Serial.println("Bluetooth receiver setup done. Scanning...");
}

void loop() {
  enableButton.loop();
  calibrateButton.loop();

  if (enableButton.isPressed() && !calibrating) {
    mouseEnabled = !mouseEnabled;
    if (mouseEnabled) {
      calSumX = 0;
      calSumY = 0;
      calCount = 0;
      calStart = millis();
      calibrating = true;
      pixel.setPixelColor(0, pixel.Color(0, 0, 255));
      pixel.show();
      Serial.println("Mouse enabled: ON. Auto-calibrating - hold still...");
    } else {
      pixel.setPixelColor(0, 0);
      pixel.show();
      Serial.println("Mouse enabled: OFF");
    }
  }

  if (calibrateButton.isPressed()) {
    xOffset = latestX;
    yOffset = latestY;
    Serial.print("Calibrated - x: ");
    Serial.print(xOffset);
    Serial.print(" y: ");
    Serial.println(yOffset);
  }

  if (calibrating) {
    if (dataValid) {
      dataValid = false;
      calSumX += latestX;
      calSumY += latestY;
      calCount++;
    }
    if (millis() - calStart >= AUTO_CAL_MS) {
      if (calCount > 0) {
        xOffset = calSumX / calCount;
        yOffset = calSumY / calCount;
        Serial.print("Auto-cal complete - x: ");
        Serial.print(xOffset);
        Serial.print(" y: ");
        Serial.println(yOffset);
      } else {
        Serial.println("Auto-cal window ended with no samples; offsets unchanged.");
      }
      calibrating = false;
      pixel.setPixelColor(0, mouseEnabled ? pixel.Color(0, 255, 0) : 0);
      pixel.show();
    }
    return;
  }

  if (dataValid) {
    dataValid = false;

    int16_t raw_x = latestX - xOffset;
    int16_t raw_y = latestY - yOffset;
    raw_x = constrain(raw_x, -INPUT_MAX, INPUT_MAX);
    raw_y = constrain(raw_y, -INPUT_MAX, INPUT_MAX);
    if (raw_x > -DEADZONE_RAW && raw_x < DEADZONE_RAW) raw_x = 0;
    if (raw_y > -DEADZONE_RAW && raw_y < DEADZONE_RAW) raw_y = 0;

    long dx = (long)raw_x * (raw_x < 0 ? -raw_x : raw_x) / ACCEL_DIVISOR;
    long dy = (long)raw_y * (raw_y < 0 ? -raw_y : raw_y) / ACCEL_DIVISOR;
    dx = constrain(dx, -OUTPUT_CLAMP, OUTPUT_CLAMP);
    dy = constrain(dy, -OUTPUT_CLAMP, OUTPUT_CLAMP);

    if (mouseEnabled && TinyUSBDevice.mounted() && usb_hid.ready()
        && (dx != 0 || dy != 0)) {
      usb_hid.mouseMove(0, (int8_t)dx, (int8_t)dy);
    }

    static uint8_t logCounter = 0;
    if (++logCounter >= 10) {
      logCounter = 0;
      Serial.print("enabled=");
      Serial.print(mouseEnabled ? 1 : 0);
      Serial.print(" dx=");
      Serial.print(dx);
      Serial.print(" dy=");
      Serial.println(dy);
    }
  }
}
