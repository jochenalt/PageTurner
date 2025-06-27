#include "bluetooth.h"

// hardware serial, which does not need the RTS/CTS pins. Uncomment this line 
// Adafruit_BluefruitLE_UART ble(BLUEFRUIT_HWSERIAL_NAME, BLUEFRUIT_UART_MODE_PIN);

/* ...hardware SPI, using SCK/MOSI/MISO hardware SPI pins and then user selected CS/IRQ/RST */
Adafruit_BluefruitLE_UART bluetoothHID(BLUEFRUIT_HWSERIAL_NAME);


void init_bluetooth_baudrate() {
    // tell bluetooth board to go faster than 9600 baud
    bluetoothHID.println("AT+BAUDRATE=115200");
    // stop Serial4, later on it is reconfigured to go faster. But give the board 100ms to reboot its serial.
    BLUEFRUIT_HWSERIAL_NAME.end();
}

void init_bluetooth() {
    // turn board into command mode
    // pinMode(BLUETOOTH_CMD_MODE_PIN, OUTPUT);  
    // digitalWrite(BLUETOOTH_CMD_MODE_PIN, HIGH);  
    BLUEFRUIT_HWSERIAL_NAME.begin(115200);

    if ( !bluetoothHID.begin(true, true) )
    {
        println("Couldn't find Bluefruit! Make sure it's in Command mode & check wiring?");
        return;
    }

    if ( FACTORYRESET_ENABLE )
    {
        /* Perform a factory reset to make sure everything is in a known state */
        println(("Performing a factory reset: "));
        if ( ! bluetoothHID.factoryReset() ){
            println(("Couldn't factory reset bluetooth device"));
            return;
        }
  }

  /* Disable command echo from Bluefruit */
  bluetoothHID.echo(false);

  println("Requesting Bluefruit info:");
  /* Print Bluefruit information */
  bluetoothHID.info();

  /* Change the device name to make it easier to find */
  println(("Setting device name to 'Bluefruit Keyboard': "));
  if (! bluetoothHID.sendCommandCheckOK(F( "AT+GAPDEVNAME=Bluefruit Keyboard" )) ) {
    println("Could not set device name?");
  }

  /* Enable HID Service */
  println("Enable HID Service (including Keyboard): ");
  if ( bluetoothHID.isVersionAtLeast(MINIMUM_FIRMWARE_VERSION) )
  {
    if ( !bluetoothHID.sendCommandCheckOK(F( "AT+BleHIDEn=On" ))) {
      println("Could not enable Keyboard");
    }
  }else
  {
    if (! bluetoothHID.sendCommandCheckOK(F( "AT+BleKeyboardEn=On"  ))) {
      println(("Could not enable Keyboard"));
    }
  }
  /* Add or remove service requires a reset */
  println("Performing a SW reset (service changes require a reset): ");
  if (! bluetoothHID.reset() ) {
    println(("Couldn't reset bluetooth device"));
    return;
  }

  println("Bluettooth initialised" );

}

static uint32_t key_pressed_ms = 0;

void send_bluetooth_command(int16_t key) {
    bluetoothHID.print("AT+BleKeyboard=");
    if (key == KEY_PAGE_DOWN) {
        bluetoothHID.println("00-00-4E-00-00-00-00");
    }
    else if (key == KEY_PAGE_UP) {
        bluetoothHID.println("00-00-4B-00-00-00-00");
    }
    else {
        println("unknown key %i", key);
    }
}

// to be called in loop(), releases the pressed key after 100ms
void update_bluetooth_release() {
  if (millis() - key_pressed_ms < 100) {
    bluetoothHID.println("AT+BleKeyboard=00-00-00-00-00-00-00"); // Page Down
    key_pressed_ms = 0;
  }
}