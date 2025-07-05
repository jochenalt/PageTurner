#include "bluetooth.h"
#include "constants.h"
#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_UART.h"


// Hardware UART  
Adafruit_BluefruitLE_UART bluetoothHID(BLUEFRUIT_HWSERIAL_NAME);
bool bluetoothIsActive = false;

// Configure the baudrate of the Bluetooth module
void initBluetoothBaudrate() {
    // Tell the Bluetooth board to go faster than 9600 baud
    bluetoothHID.println("AT+BAUDRATE=115200");

    // Stop Serial4; it will be reconfigured later at higher speed
    // Give the board 100ms to reboot its serial
    BLUEFRUIT_HWSERIAL_NAME.end();
}

// Initialise the Bluetooth module
void initBluetooth() {
    // Setup serial port
    BLUEFRUIT_HWSERIAL_NAME.begin(BLUETOOTH_BAUDRATE);

    // Try to initialise the module
    if (!bluetoothHID.begin(false, true)) {
        println("Couldn't find Bluefruit! Make sure it's in Command mode & check wiring?");
        return;
    }

    // Perform factory reset if enabled
    if (FACTORYRESET_ENABLE) {
        if (!bluetoothHID.factoryReset()) {
            println("Couldn't factory reset bluetooth device");
            return;
        }
    }

    // Change the device name
    if (!bluetoothHID.sendCommandCheckOK(F("AT+GAPDEVNAME=Page Turner"))) {
        println("Could not set device name!");
    }

    // Enable HID keyboard service
    if (!bluetoothHID.sendCommandCheckOK(F("AT+BLEHIDEN=On"))) {
        println("Could not enable HID Keyboard");
    }

    // Reset the Bluetooth module to apply changes
    if (!bluetoothHID.reset()) {
        println("Couldn't reset bluetooth device");
        return;
    }
    bluetoothIsActive = true;
}

static uint32_t key_pressed_ms = 0;

// Send a keypress to the connected Bluetooth HID host
void sendBluetoothKey(uint16_t key) {
    if (!bluetoothIsActive)
        return ;
    if (key == KEY_PAGE_DOWN) {
        bluetoothHID.sendCommandCheckOK("AT+BLEKEYBOARDCODE=00-00-4E-00-00-00-00");
        key_pressed_ms = millis();
    }
    else if (key == KEY_PAGE_UP) {
        bluetoothHID.sendCommandCheckOK("AT+BLEKEYBOARDCODE=00-00-4B-00-00-00-00");
        key_pressed_ms = millis();
    }
    else {
        println("unknown key %i", key);
    }
}

// To be called in loop(), releases the pressed key after 100ms
void updateBluetoothRelease() {
    if (!bluetoothIsActive)
        return ;

    if ((key_pressed_ms != 0) && (millis() - key_pressed_ms > 100)) {
        bluetoothHID.sendCommandCheckOK("AT+BLEKEYBOARDCODE=00-00-00-00-00-00-00"); // release key
        key_pressed_ms = 0;
    }
}
