#include <bluefruit.h>
#include "Adafruit_TinyUSB.h"

// BLE
BLEClientBas clientBas;    // battery client
BLEClientDis clientDis;    // device information client
BLEClientUart clientUart;  // bleuart client

// Mouse
Adafruit_USBD_HID usb_hid;

uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_MOUSE()
};

void setup() {
  Serial.begin(9600);
  while (!Serial) delay(10);

  Serial.println("Morgan Mouse Setup");

  // BLE
  Bluefruit.begin(0, 1);
  Bluefruit.setName("Bluefruit52 Central");
  clientBas.begin();
  clientDis.begin();
  clientUart.begin();
  clientUart.setRxCallback(bleuart_rx_callback);
  Bluefruit.setConnLedInterval(250);
  Bluefruit.Central.setConnectCallback(connect_callback);
  Bluefruit.Central.setDisconnectCallback(disconnect_callback);
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.setInterval(160, 80);  // in unit of 0.625 ms
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0);  // 0 = Don't stop scanning after n seconds
  Serial.println("BLE setup complete");

  // Mouse
  usb_hid.setBootProtocol(HID_ITF_PROTOCOL_MOUSE);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("Morgan Air Mouse");
  usb_hid.begin();

  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
  Serial.println("Mouse setup complete");
}

void loop() {
  // BLE
  if (Bluefruit.Central.connected()) {
    // Not discovered yet
    if (clientUart.discovered()) {
      // Discovered means in working state
      // Get Serial input and send to Peripheral
      if (Serial.available()) {
        delay(2);  // delay a bit for all characters to arrive

        char str[20 + 1] = { 0 };
        Serial.readBytes(str, 20);

        clientUart.print(str);
      }
    }
  }

// Mouse
#ifdef TINYUSB_NEED_POLLING_TASK
  // Manual call tud_task since it isn't called by Core's background
  TinyUSBDevice.task();
  Serial.println("tud_task called");
#endif

  // not enumerated()/mounted() yet: nothing to do
  if (!TinyUSBDevice.mounted()) {
    return;
  }
}

/**
 * Callback invoked when scanner pick up an advertising data
 */
void scan_callback(ble_gap_evt_adv_report_t* report) {
  // Check if advertising contain BleUart service
  if (Bluefruit.Scanner.checkReportForService(report, clientUart)) {
    Serial.print("BLE UART service detected. Connecting ... ");

    // Connect to device with bleuart service in advertising
    Bluefruit.Central.connect(report);
  } else {
    // For Softdevice v6: after received a report, scanner will be paused
    // We need to call Scanner resume() to continue scanning
    Bluefruit.Scanner.resume();
  }
}

/**
 * Callback invoked when an connection is established
 */
void connect_callback(uint16_t conn_handle) {
  Serial.println("Connected");

  Serial.print("Dicovering Device Information ... ");
  if (clientDis.discover(conn_handle)) {
    Serial.println("Found it");
    char buffer[32 + 1];

    // read and print out Manufacturer
    memset(buffer, 0, sizeof(buffer));
    if (clientDis.getManufacturer(buffer, sizeof(buffer))) {
      Serial.print("Manufacturer: ");
      Serial.println(buffer);
    }

    // read and print out Model Number
    memset(buffer, 0, sizeof(buffer));
    if (clientDis.getModel(buffer, sizeof(buffer))) {
      Serial.print("Model: ");
      Serial.println(buffer);
    }

    Serial.println();
  } else {
    Serial.println("Found NONE");
  }

  Serial.print("Dicovering Battery ... ");
  if (clientBas.discover(conn_handle)) {
    Serial.println("Found it");
    Serial.print("Battery level: ");
    Serial.print(clientBas.read());
    Serial.println("%");
  } else {
    Serial.println("Found NONE");
  }

  Serial.print("Discovering BLE Uart Service ... ");
  if (clientUart.discover(conn_handle)) {
    Serial.println("Found it");

    Serial.println("Enable TXD's notify");
    clientUart.enableTXD();

    Serial.println("Ready to receive from peripheral");
  } else {
    Serial.println("Found NONE");

    // disconnect since we couldn't find bleuart service
    Bluefruit.disconnect(conn_handle);
  }
}

/**
 * Callback invoked when a connection is dropped
 * @param conn_handle
 * @param reason is a BLE_HCI_STATUS_CODE which can be found in ble_hci.h
 */
void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;

  Serial.print("Disconnected, reason = 0x");
  Serial.println(reason, HEX);
}

/**
 * Callback invoked when uart received data
 * @param uart_svc Reference object to the service where the data 
 * arrived. In this example it is clientUart
 */
void bleuart_rx_callback(BLEClientUart& uart_svc) {
  Serial.print("RX: ");

  int8_t x, y;
  String data = "";
  char receivedChar;

  while (uart_svc.available()) {
    data += (char)uart_svc.read();
  }
  
  Serial.println(data);

  int separatorIndex = data.indexOf(',');  // Find the position of the comma

  if (separatorIndex != -1) {
    // Extract and convert the x and y values from the string
    x = data.substring(0, separatorIndex).toInt();
    y = data.substring(separatorIndex + 1).toInt();
  }

  process_hid(x, y);
}

void process_hid(int8_t x, int8_t y) {
  if (TinyUSBDevice.suspended()) {
    TinyUSBDevice.remoteWakeup();
    Serial.println("Remote Wakeup called");
  }

  if (usb_hid.ready()) {
    usb_hid.mouseMove(0, x, y);
  }
}