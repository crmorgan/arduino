# AdafruitMouse

Hands-free head-mouse. A head-worn Sender reads head motion from an
MPU-6050 and transmits it over Bluetooth LE to a Receiver that plugs
into the host PC and acts as a standard USB HID mouse.

## Hardware

### Sender (worn)
- Adafruit Feather nRF52840 Express
- Adafruit MPU-6050 6 DoF Accel and Gyro Sensor breakout
- 3.7 V LiPo battery (e.g. EEMB LP403048, 500 mAh) plugged into the
  Feather's onboard JST connector
- Optional: switched JST inline cable for true off / battery preservation

The MPU-6050 connects to the Feather's I2C lines (SDA, SCL, 3V, GND).
A STEMMA QT cable works if you have one (Adafruit #4209: STEMMA QT to
male headers); breadboard jumpers also work but are more prone to
intermittent disconnects on a head-worn device. I2C address is `0x68`.
The USER SW button on the Sender is used as the sleep/wake button.

### Receiver (USB dongle)
- Adafruit Feather nRF52840 Express
- Two momentary pushbuttons to GND
  - Enable/disable HID output: pin `D9`
  - Calibrate (zero current tilt as neutral): pin `D10`
- Buttons use internal pull-ups (press = LOW), debounced in firmware.

## Libraries

### Sender
- Adafruit MPU6050
- Adafruit Unified Sensor
- Adafruit NeoPixel
- ezButton

### Receiver
- Adafruit TinyUSB Library
- Adafruit NeoPixel
- ezButton

Bluefruit is bundled with the Adafruit nRF52 BSP -- no separate install.

## Arduino IDE setup
1. File -> Preferences -> Additional Boards Manager URLs:
   `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`
2. Tools -> Board -> Boards Manager -> install "Adafruit nRF52 by Adafruit".
3. Tools -> Board -> Adafruit nRF52 Boards -> Adafruit Feather nRF52840 Express.
4. BSP 1.7.0+ uses TinyUSB as the only USB stack; no "USB Stack" menu exists.

## Usage
1. Power the Sender (battery or USB).
2. Plug Receiver into the host PC. It enumerates as a composite CDC
   (serial debug) + HID mouse device.
3. The Sender's blue LED blinks while advertising and goes solid when
   the Receiver pairs to it.
4. The Receiver's NeoPixel goes blue for ~1.5 s while it auto-calibrates
   on connect. Hold your head in the neutral position during this.
5. Press the **enable button** (`D9`) on the Receiver to start sending
   mouse movements. Pressing it kicks off another auto-calibration
   first, so each enable starts from a fresh neutral.
6. Press the **calibrate button** (`D10`) at any time to re-zero the
   current head position.

## Sleep / power-off

The Sender has two sleep paths, both gated to battery operation only
(while on USB it stays awake so you can debug):

- **USER SW button on the Sender**: single press to sleep, single press
  to wake. Wake takes ~1-2 seconds (cold boot).
- **Inactivity**: 10 minutes with no head motion or yaw -> auto-sleep.

When sleeping, the Sender disables BLE / softdevice / USB peripherals
and uses a `__WFE` loop with the USER SW configured as a GPIO sense
wake source. The MPU-6050 is also placed in its own ~5 uA sleep mode.
Standby current is ~1.5 mA (limited by the Feather's regulator
quiescent and the MPU breakout's hardwired power LED). With a 500 mAh
battery, that's ~14 days standby.

The Sender sends a "SLEEP" sentinel before disconnecting so the
Receiver knows the disconnect was intentional. The Receiver's
NeoPixel briefly flashes purple to acknowledge.

For longer-term off (weeks), use the optional switched JST inline
cable to physically disconnect the battery. The firmware sleep is for
day-to-day "I'm done for now" use.

## Battery monitoring

The Sender reads `VBAT` once per second and includes it in every BLE
packet. The Receiver displays the voltage in its throttled debug log
and adjusts the NeoPixel color based on level:

- **Green / off / blue**: state colors (enabled / disabled / calibrating)
- **Yellow**: battery warning (below 3.5 V, ~15% remaining)
- **Blinking red**: battery danger (below 3.3 V, ~5% remaining)
- **Purple flash**: Sender just intentionally slept (vs unexpected drop)

Below 3.2 V the Sender enters a hardware-safe cutoff: stops BLE,
slow-blinks `LED_BUILTIN`, and waits for USB power to recover. Plug in
USB and the Sender auto-reboots.

Active runtime is roughly 80 hours (~3 days) on a full 500 mAh charge.
Sleep extends this dramatically when not actively in use.

## Receiver NeoPixel state colors

| Color | Meaning |
|-------|---------|
| Off | Mouse disabled / idle |
| Blue | Auto-calibrating (1.5 s after BLE connect or enable press) |
| Green | Mouse enabled |
| Yellow | Battery warning (< 3.5 V) |
| Blinking red | Battery danger (< 3.3 V) |
| Purple | Sender just intentionally slept (~2 s flash) |

## Upload quirks
Uploads via Arduino IDE use serial DFU. If upload fails with
"No data received on serial port", double-tap the reset button on the
target board to force it into the UF2 bootloader, reselect the new COM
port in Tools -> Port, then upload.
