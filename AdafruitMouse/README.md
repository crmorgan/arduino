# AdafruitMouse

Hands-free head-mouse. A head-worn Sender reads head motion from an
MPU-6050 and transmits orientation over Bluetooth LE to a Receiver
that plugs into the host PC and acts as a standard USB HID mouse.

## Hardware

### Sender (worn)
- Adafruit Feather nRF52840
- Adafruit MPU-6050 6 DoF Accel and Gyro Sensor - STEMMA QT Qwiic
- Adafruit LiIon/LiPoly Backpack + battery

Connect MPU-6050 to the Feather over STEMMA QT (I2C, address 0x68).

### Receiver (USB dongle)
- Adafruit Feather nRF52840 Express
- Two momentary pushbuttons to GND
  - Enable/disable: pin D9
  - Calibrate (zero current tilt as neutral): pin D10
- Buttons use internal pull-ups (press = LOW), debounced in firmware.

## Libraries

### Sender
- Adafruit MPU6050
- Adafruit Unified Sensor

### Receiver
- Adafruit TinyUSB Library
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
3. LEDs on both boards go solid blue when paired.
4. Put the Sender in your neutral head position, press the calibrate
   button on the Receiver. This snapshots the current accelerometer
   reading as the zero point.
5. Press the enable button to start sending mouse movements.
   Press again to stop.

## Behavior
- Sender transmits accelerometer X/Y at 50 Hz as comma-separated text
  over BLE UART. If the MPU stops responding, the Sender auto-retries
  `mpu.begin()` and logs the recovery to USB serial.
- Receiver parses each line, subtracts the calibration offset, maps to
  signed 8-bit deltas with a +/-3-unit deadzone, and sends HID reports
  only while enabled.

## Upload quirks
Uploads via Arduino IDE use serial DFU. If upload fails with
"No data received on serial port", double-tap the reset button on the
target board to force it into the UF2 bootloader, reselect the new COM
port in Tools -> Port, then upload.
