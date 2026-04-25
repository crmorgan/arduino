#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <bluefruit.h>
#include <ezButton.h>

// --- Battery ---------------------------------------------------------------
#define VBAT_CHECK_MS      1000  // How often to sample VBAT
#define VBAT_CUTOFF         3.2f // Shut down below this (LiPo safety floor)
#define VBAT_CUTOFF_HITS   5     // Consecutive low samples before cutoff fires
#define STARTUP_GRACE_MS   5000  // Ignore cutoff for first N ms after boot
// ---------------------------------------------------------------------------

// --- Yaw-to-cursor ---------------------------------------------------------
#define YAW_GAIN          300  // Gyro Z (rad/s) contribution to x; tune to taste
// ---------------------------------------------------------------------------

// --- Sleep / power-off ----------------------------------------------------
#define SLEEP_BUTTON_PIN     PIN_BUTTON1
#define SLEEP_AFTER_MS       (10UL * 60UL * 1000UL)  // 10 min inactivity -> sleep
#define MOTION_ACCEL_DELTA   20     // Per-axis tick change that counts as motion
#define MOTION_GYRO_RAD      0.15f  // Yaw rate that counts as motion (~8.6 deg/s)
// ---------------------------------------------------------------------------

Adafruit_MPU6050 mpu;

BLEUart bleuart; // Create BLE UART service

ezButton sleepButton(SLEEP_BUTTON_PIN);

float latestVbat = 0.0f;
uint32_t lastVbatCheck = 0;

int16_t prevAx = 0;
int16_t prevAy = 0;
uint32_t lastMotionMs = 0;
bool motionBaselineSet = false;

float readBattery() {
  // Feather nRF52840 Express: VBAT -> 1M/1M divider -> PIN_VBAT,
  // read with 12-bit ADC against 3.0 V internal reference.
  float raw = (float)analogRead(PIN_VBAT);
  return raw * (3.0f / 4096.0f) * 2.0f;
}

bool onUsbPower() {
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

void enterSleep(const char* reason) {
  Serial.print("Power off: ");
  Serial.println(reason);
  delay(100);

  // Tell Bluefruit not to auto-restart advertising when we disconnect.
  Bluefruit.Advertising.restartOnDisconnect(false);

  if (Bluefruit.connected()) {
    Bluefruit.disconnect(Bluefruit.connHandle());
    delay(100);
  }
  Bluefruit.Advertising.stop();
  delay(200);

  // Wait for USER SW release before entering the sleep loop, otherwise
  // we'd wake immediately on the still-pressed button.
  pinMode(SLEEP_BUTTON_PIN, INPUT_PULLUP);
  while (digitalRead(SLEEP_BUTTON_PIN) == LOW) {
    delay(10);
  }
  delay(50);

  // System ON idle - CPU sleeps, softdevice timers keep ticking. Wake on
  // any softdevice event or interrupt, then re-check the button.
  while (digitalRead(SLEEP_BUTTON_PIN) == HIGH) {
    sd_app_evt_wait();
  }

  // Button pressed - debounce, wait for release, then resume.
  delay(50);
  while (digitalRead(SLEEP_BUTTON_PIN) == LOW) {
    delay(10);
  }
  delay(50);

  Serial.println("Resumed from sleep");
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0);

  // Drain any stale button events queued up during the wake transitions
  // so we don't immediately re-trigger sleep on the next loop iteration.
  for (int i = 0; i < 10; i++) {
    sleepButton.loop();
    delay(20);
  }
  (void)sleepButton.isPressed();
  (void)sleepButton.isReleased();
}

void setup() {
  Serial.begin(9600);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  pinMode(LED_BUILTIN, OUTPUT);

  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  analogRead(PIN_VBAT);  // Warm-up dummy read
  delay(1);

  sleepButton.setDebounceTime(50);
  lastMotionMs = millis();

  Bluefruit.begin();
  Bluefruit.setName("MPU6050_Sender");

  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      digitalWrite(LED_BUILTIN, HIGH); delay(100);
      digitalWrite(LED_BUILTIN, LOW);  delay(100);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);

  bleuart.begin(); // Initialize BLE UART service

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.addService(bleuart); // Add BLE UART service

  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.start(0); // 0 = Don't stop advertising

  Serial.println("MPU6050 and Bluefruit setup done.");
}

void loop() {
  static uint8_t stuckCount = 0;
  static uint8_t failedRecoveries = 0;

  sleepButton.loop();
  if (!onUsbPower() && sleepButton.isPressed()) {
    enterSleep("button");
  }

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  if (a.acceleration.x == 0.0f && a.acceleration.y == 0.0f && a.acceleration.z == 0.0f) {
    if (++stuckCount >= 10) {
      Serial.println("*********************************************************************");
      Serial.println("****************** MPU stuck - re-initializing **********************");
      Serial.println("*********************************************************************");
      stuckCount = 0;
      if (mpu.begin()) {
        mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
        mpu.setGyroRange(MPU6050_RANGE_250_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);
        Serial.println("MPU re-init ok");
        failedRecoveries = 0;
      } else {
        Serial.println("MPU re-init FAILED");
        if (++failedRecoveries >= 3) {
          Serial.println("Multiple recoveries failed - system reset");
          delay(100);
          NVIC_SystemReset();
        }
      }
    }
    delay(100);
    return;
  }
  stuckCount = 0;

  int16_t x = a.acceleration.x * 100;
  int16_t y = a.acceleration.y * 100;
  x -= (int16_t)(g.gyro.z * YAW_GAIN);

  if (onUsbPower()) {
    // Never auto-sleep while tethered - keep the timer fresh.
    lastMotionMs = millis();
  } else if (motionBaselineSet) {
    if (abs(x - prevAx) > MOTION_ACCEL_DELTA ||
        abs(y - prevAy) > MOTION_ACCEL_DELTA ||
        fabsf(g.gyro.z) > MOTION_GYRO_RAD) {
      lastMotionMs = millis();
    }
  }
  prevAx = x;
  prevAy = y;
  motionBaselineSet = true;

  if (!onUsbPower() && millis() - lastMotionMs > SLEEP_AFTER_MS) {
    enterSleep("inactivity timeout");
  }

  if (millis() - lastVbatCheck >= VBAT_CHECK_MS) {
    lastVbatCheck = millis();
    latestVbat = readBattery();

    static uint8_t cutoffHits = 0;
    bool lowReading = millis() > STARTUP_GRACE_MS
                      && latestVbat > 2.5f
                      && latestVbat < VBAT_CUTOFF;
    if (lowReading) {
      cutoffHits++;
    } else {
      cutoffHits = 0;
    }

    if (cutoffHits >= VBAT_CUTOFF_HITS) {
      Serial.print("Battery critical (");
      Serial.print(latestVbat, 2);
      Serial.println(" V) - shutting down for LiPo safety");
      Bluefruit.Advertising.stop();
      if (Bluefruit.connected()) {
        Bluefruit.disconnect(Bluefruit.connHandle());
      }
      while (1) {
        digitalWrite(LED_BUILTIN, HIGH); delay(500);
        digitalWrite(LED_BUILTIN, LOW);  delay(500);
        if (onUsbPower()) {
          NVIC_SystemReset();  // USB restored - reboot and resume
        }
      }
    }
  }

  if (Serial) {
    Serial.print(x);
    Serial.print(" ");
    Serial.print(y);
    Serial.print(" vbat=");
    Serial.println(latestVbat, 2);
  }

  char buf[24];
  snprintf(buf, sizeof(buf), "%d,%d,%.2f\n", x, y, latestVbat);
  bleuart.print(buf);

  delay(20);
}
