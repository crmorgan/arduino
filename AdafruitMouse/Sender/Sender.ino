#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <bluefruit.h>
#include <ezButton.h>
#include <Adafruit_NeoPixel.h>

// --- Battery ---------------------------------------------------------------
#define VBAT_CHECK_MS       1000   // How often to sample VBAT
#define VBAT_CUTOFF          3.2f  // Shut down below this (LiPo safety floor)
#define VBAT_CUTOFF_HITS    5      // Consecutive low samples before cutoff fires
#define STARTUP_GRACE_MS    5000   // Ignore cutoff for first N ms after boot
#define VBAT_EMA_ALPHA       0.25f // New-sample weight in running mean (~4-tap)
// ---------------------------------------------------------------------------

// --- Yaw-to-cursor ---------------------------------------------------------
#define YAW_GAIN          300  // Gyro Z (rad/s) contribution to x; tune to taste
// ---------------------------------------------------------------------------

// --- Sleep / power-off ----------------------------------------------------
#define SLEEP_BUTTON_PIN          PIN_BUTTON1
#define SLEEP_AFTER_MS            (10UL * 60UL * 1000UL)  // 10 min inactivity -> sleep
#define MOTION_ACCEL_DELTA        20     // Per-axis tick change that counts as motion
#define MOTION_GYRO_RAD           0.15f  // Yaw rate that counts as motion (~8.6 deg/s)
#define SLEEP_RELEASE_TIMEOUT_MS  5000   // Reboot if USER SW stuck low this long
// ---------------------------------------------------------------------------

Adafruit_MPU6050 mpu;

BLEUart bleuart; // Create BLE UART service

Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

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

  // MPU into ~5 uA sleep before we lose I2C control via softdevice disable.
  mpu.enableSleep(true);

  // Cleanly stop BLE so the disconnect propagates to the Receiver.
  Bluefruit.Advertising.restartOnDisconnect(false);
  if (Bluefruit.connected()) {
    // Send a sentinel so the Receiver knows this disconnect was intentional.
    bleuart.print("SLEEP\n");
    delay(50);
    Bluefruit.disconnect(Bluefruit.connHandle());
    delay(100);
  }
  Bluefruit.Advertising.stop();
  delay(200);

  // Wait for USER SW release before configuring the sense source,
  // otherwise the still-pressed button triggers immediate wake.
  pinMode(SLEEP_BUTTON_PIN, INPUT_PULLUP);
  uint32_t releaseStart = millis();
  while (digitalRead(SLEEP_BUTTON_PIN) == LOW) {
    if (millis() - releaseStart > SLEEP_RELEASE_TIMEOUT_MS) {
      // Stuck button: reboot so the user isn't locked in a hung sleep path.
      Serial.println("Sleep button held too long; rebooting");
      delay(50);
      NVIC_SystemReset();
    }
    delay(10);
  }
  delay(50);

  // Disable softdevice and USB - stops all BLE / FreeRTOS / USB overhead.
  // We will cold-boot on wake to re-initialize them via setup().
  sd_softdevice_disable();
  NRF_USBD->ENABLE = 0;

  // Configure USER SW as GPIO sense wake source.
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[SLEEP_BUTTON_PIN],
                           NRF_GPIO_PIN_PULLUP,
                           NRF_GPIO_PIN_SENSE_LOW);
  // Clear stale latches so a prior LOW state doesn't trigger immediate wake.
  NRF_P0->LATCH = NRF_P0->LATCH;
  NRF_P1->LATCH = NRF_P1->LATCH;
  // Clear bootloader retention so reset boots app, not DFU.
  NRF_POWER->GPREGRET  = 0;
  NRF_POWER->GPREGRET2 = 0;
  NRF_POWER->RESETREAS = 0xFFFFFFFF;

  // Raw WFE sleep loop. CPU halts until GPIO sense fires.
  while (digitalRead(SLEEP_BUTTON_PIN) == HIGH) {
    __WFE();
    __SEV();
    __WFE();
  }

  // Button pressed - cold-boot to re-init everything cleanly.
  delay(50);  // brief debounce

  // Tell the Adafruit bootloader to skip its DFU check and boot the app.
  // 0x6D == DFU_MAGIC_SKIP; the double-tap magic at 0x20007F7C also gets
  // cleared so a stale value doesn't override.
  NRF_POWER->GPREGRET = 0x6D;
  *(volatile uint32_t*)0x20007F7C = 0;

  NVIC_SystemReset();
  while (1);  // unreachable
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

  // Clear any leftover bootloader NeoPixel state. Do it twice with a
  // small delay - the first show() right after begin() can miss the
  // first reset window on some boards.
  pixel.begin();
  pixel.setBrightness(30);
  pixel.setPixelColor(0, 0, 0, 0);
  pixel.show();
  delay(10);
  pixel.setPixelColor(0, 0, 0, 0);
  pixel.show();

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
        ++failedRecoveries;
        Serial.print("MPU re-init FAILED (failedRecoveries=");
        Serial.print(failedRecoveries);
        Serial.println(")");
        if (failedRecoveries >= 3) {
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
    float vbatSample = readBattery();
    if (latestVbat == 0.0f) {
      latestVbat = vbatSample;  // Seed: avoid slow ramp from 0.
    } else {
      latestVbat = latestVbat * (1.0f - VBAT_EMA_ALPHA)
                 + vbatSample * VBAT_EMA_ALPHA;
    }

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

  static uint8_t logCounter = 0;
  if (Serial && ++logCounter >= 10) {
    logCounter = 0;
    Serial.print(x);
    Serial.print(" ");
    Serial.print(y);
    Serial.print(" vbat=");
    Serial.println(latestVbat, 2);
  }

  // 'M' prefix lets the Receiver discard non-movement junk (BLE flake, partial
  // frames, future protocol drift) instead of feeding atoi() = 0 to the cursor.
  char buf[25];
  snprintf(buf, sizeof(buf), "M%d,%d,%.2f\n", x, y, latestVbat);
  bleuart.print(buf);

  delay(20);
}
