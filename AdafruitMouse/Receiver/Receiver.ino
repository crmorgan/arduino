#include <bluefruit.h>
#include <ezButton.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// --- Tuning ----------------------------------------------------------------
#define INPUT_MAX           200   // Approx max raw accel value (a * 100 at 2g)
#define DEADZONE_RAW         10   // Radial deadzone radius in raw units (noise)
#define ACCEL_DIVISOR     12000   // x^2.5 curve divisor; lower = faster
#define OUTPUT_CLAMP         50   // Max per-sample cursor delta
#define AUTO_CAL_MS        1500   // Averaging window for auto-calibrate on connect
#define VBAT_WARN            3.5f // Below this: NeoPixel goes yellow
#define VBAT_DANGER          3.3f // Below this: NeoPixel flashes red
#define BAD_PACKET_LOG_MS  5000   // Throttle "discarded BLE packets" log
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
volatile float latestVbat = 0.0f;
volatile bool dataValid = false;

int16_t xOffset = 0;
int16_t yOffset = 0;
bool mouseEnabled = false;

// Sub-pixel cursor accumulators. File-scope so we can zero them on every
// auto-cal start and not carry residual across sessions.
float dxAccum = 0.0f;
float dyAccum = 0.0f;

volatile bool calibrating = false;
uint32_t calStart = 0;
int32_t calSumX = 0;
int32_t calSumY = 0;
uint16_t calCount = 0;

volatile bool senderRequestedSleep = false;
uint32_t purpleUntil = 0;

// Counts BLE frames dropped for bad framing/prefix. Throttled-logged from loop.
volatile uint32_t badPacketCount = 0;
uint32_t lastBadPacketLog = 0;

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
    dxAccum = 0.0f;
    dyAccum = 0.0f;
    calibrating = true;
  } else {
    Serial.println("BLE UART service NOT found on peer. Disconnecting.");
    Bluefruit.disconnect(conn_handle);
  }
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  Serial.print("Disconnected, reason 0x");
  Serial.println(reason, HEX);
  if (senderRequestedSleep) {
    Serial.println("Sender went to sleep intentionally.");
    purpleUntil = millis() + 2000;
    senderRequestedSleep = false;
    // Sleep is "I'm done"; require explicit re-enable on wake.
    mouseEnabled = false;
  }
  // Transient drops keep mouseEnabled; reconnect auto-cal handles freshness.
  calibrating = false;
}

uint8_t vbatToPercent(float v) {
  // Piecewise linear approximation of LiPo discharge curve.
  static const struct { float volts; uint8_t pct; } curve[] = {
    {4.20f, 100}, {4.00f, 85}, {3.85f, 70}, {3.70f, 50},
    {3.60f, 30}, {3.50f, 15}, {3.40f, 5},  {3.30f, 0}
  };
  if (v >= curve[0].volts) return 100;
  for (size_t i = 0; i < sizeof(curve)/sizeof(curve[0]) - 1; i++) {
    if (v >= curve[i + 1].volts) {
      float vSpan  = curve[i].volts - curve[i + 1].volts;
      float vIn    = v - curve[i + 1].volts;
      uint8_t pSpan = curve[i].pct - curve[i + 1].pct;
      return curve[i + 1].pct + (uint8_t)(pSpan * vIn / vSpan);
    }
  }
  return 0;
}

void updatePixel() {
  static uint32_t lastColor = 0xFFFFFFFF;
  uint32_t color = 0;
  bool blinkPhase = ((millis() / 500) % 2) != 0;

  if (millis() < purpleUntil) {
    color = pixel.Color(180, 0, 200);  // Purple - Sender intentionally slept
  } else if (calibrating) {
    color = pixel.Color(0, 0, 255);  // Blue
  } else if (latestVbat > 0.0f && latestVbat < VBAT_DANGER) {
    // Danger: red blink. When mouse is enabled, off-phase becomes green so
    // the enabled cue is still visible while the user reaches for a charger.
    if (blinkPhase) {
      color = pixel.Color(255, 0, 0);
    } else {
      color = mouseEnabled ? pixel.Color(0, 255, 0) : 0;
    }
  } else if (latestVbat > 0.0f && latestVbat < VBAT_WARN) {
    // Warn: solid yellow when disabled; alternate yellow/green at 1 Hz when
    // enabled, so both battery state and enabled state are readable at once.
    if (mouseEnabled && blinkPhase) {
      color = pixel.Color(0, 255, 0);
    } else {
      color = pixel.Color(255, 255, 0);
    }
  } else if (mouseEnabled) {
    color = pixel.Color(0, 255, 0);  // Green
  }

  if (color != lastColor) {
    pixel.setPixelColor(0, color);
    pixel.show();
    lastColor = color;
  }
}

void bleuart_rx_callback(BLEClientUart& uart_svc) {
  static char rxbuf[32];
  static size_t rxlen = 0;

  while (uart_svc.available()) {
    char c = (char)uart_svc.read();
    if (c == '\n' || c == '\r') {
      if (rxlen == 0) continue;
      rxbuf[rxlen] = '\0';
      if (strcmp(rxbuf, "SLEEP") == 0) {
        senderRequestedSleep = true;
      } else if (rxbuf[0] == 'M' && rxlen > 1) {
        // Movement packet: M<x>,<y>,<vbat>
        char* body = rxbuf + 1;
        char* comma1 = strchr(body, ',');
        if (comma1) {
          *comma1 = '\0';
          int16_t newX = (int16_t)atoi(body);
          int16_t newY;
          float   newVbat = latestVbat;
          char* rest = comma1 + 1;
          char* comma2 = strchr(rest, ',');
          if (comma2) {
            *comma2 = '\0';
            newY = (int16_t)atoi(rest);
            newVbat = (float)atof(comma2 + 1);
          } else {
            newY = (int16_t)atoi(rest);
          }
          // Publish all three together so the main loop's snapshot sees a
          // coherent triple rather than a torn mix from two packets.
          noInterrupts();
          latestX = newX;
          latestY = newY;
          latestVbat = newVbat;
          dataValid = true;
          interrupts();
        } else {
          badPacketCount++;
        }
      } else {
        badPacketCount++;
      }
      rxlen = 0;
    } else if (rxlen < sizeof(rxbuf) - 1) {
      rxbuf[rxlen++] = c;
    } else {
      rxlen = 0;  // Overflow, discard.
      badPacketCount++;
    }
  }
}

// Atomically read the latest BLE packet. Returns true and clears dataValid
// when a fresh packet was consumed; false when no new data.
bool snapshotRx(int16_t& x, int16_t& y, float& vbat) {
  bool got;
  noInterrupts();
  got = dataValid;
  if (got) {
    x = latestX;
    y = latestY;
    vbat = latestVbat;
    dataValid = false;
  }
  interrupts();
  return got;
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
      dxAccum = 0.0f;
      dyAccum = 0.0f;
      calibrating = true;
      Serial.println("Mouse enabled: ON. Auto-calibrating - hold still...");
    } else {
      Serial.println("Mouse enabled: OFF");
    }
  }

  if (calibrateButton.isPressed()) {
    int16_t snapX, snapY;
    noInterrupts();
    snapX = latestX;
    snapY = latestY;
    interrupts();
    xOffset = snapX;
    yOffset = snapY;
    Serial.print("Calibrated - x: ");
    Serial.print(xOffset);
    Serial.print(" y: ");
    Serial.println(yOffset);
  }

  // Throttled report on dropped packets, so silent corruption surfaces.
  // Logs only when new bad packets have arrived since the last report.
  static uint32_t lastBadPacketCount = 0;
  uint32_t badNow;
  noInterrupts();
  badNow = badPacketCount;
  interrupts();
  if (Serial && badNow > lastBadPacketCount
      && millis() - lastBadPacketLog > BAD_PACKET_LOG_MS) {
    Serial.print("Discarded BLE packets (bad framing/prefix): ");
    Serial.print(badNow - lastBadPacketCount);
    Serial.print(" new, ");
    Serial.print(badNow);
    Serial.println(" total");
    lastBadPacketCount = badNow;
    lastBadPacketLog = millis();
  }

  int16_t curX = 0, curY = 0;
  float   curVbat = 0.0f;
  bool fresh = snapshotRx(curX, curY, curVbat);

  if (calibrating) {
    if (fresh) {
      calSumX += curX;
      calSumY += curY;
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
    }
    updatePixel();
    return;
  }

  if (fresh) {
    int16_t raw_x = curX - xOffset;
    int16_t raw_y = curY - yOffset;
    raw_x = constrain(raw_x, -INPUT_MAX, INPUT_MAX);
    raw_y = constrain(raw_y, -INPUT_MAX, INPUT_MAX);

    // Radial deadzone: prevents axis-aligned drift when one axis is just
    // inside its threshold and the other just outside.
    int32_t mag2 = (int32_t)raw_x * raw_x + (int32_t)raw_y * raw_y;
    if (mag2 < (int32_t)DEADZONE_RAW * DEADZONE_RAW) {
      raw_x = 0;
      raw_y = 0;
    }

    float absX = fabsf((float)raw_x);
    float absY = fabsf((float)raw_y);
    float magX = absX * absX * sqrtf(absX) / (float)ACCEL_DIVISOR;
    float magY = absY * absY * sqrtf(absY) / (float)ACCEL_DIVISOR;
    float step_x = raw_x < 0 ? -magX : magX;
    float step_y = raw_y < 0 ? -magY : magY;
    step_x = constrain(step_x, (float)-OUTPUT_CLAMP, (float)OUTPUT_CLAMP);
    step_y = constrain(step_y, (float)-OUTPUT_CLAMP, (float)OUTPUT_CLAMP);
    dxAccum += step_x;
    dyAccum += step_y;
    long dx = (long)dxAccum;
    long dy = (long)dyAccum;
    dxAccum -= dx;
    dyAccum -= dy;

    if (mouseEnabled && TinyUSBDevice.mounted() && usb_hid.ready()
        && (dx != 0 || dy != 0)) {
      usb_hid.mouseMove(0, (int8_t)dx, (int8_t)dy);
    }

    static uint8_t logCounter = 0;
    if (Serial && ++logCounter >= 10) {
      logCounter = 0;
      Serial.print("enabled=");
      Serial.print(mouseEnabled ? 1 : 0);
      Serial.print(" dx=");
      Serial.print(dx);
      Serial.print(" dy=");
      Serial.print(dy);
      Serial.print(" vbat=");
      Serial.print(curVbat, 2);
      Serial.print(" (");
      Serial.print(vbatToPercent(curVbat));
      Serial.println("%)");
    }
  }

  updatePixel();
}
