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
    BLUEFRUIT_HWSERIAL_NAME.begin(BLUETOOTH_BAUDRATE);

    if ( !bluetoothHID.begin(false, true) )
    {
        println("Couldn't find Bluefruit! Make sure it's in Command mode & check wiring?");
        return;
    }

    if ( FACTORYRESET_ENABLE )
    {
        /* Perform a factory reset to make sure everything is in a known state */
        if ( ! bluetoothHID.factoryReset() ){
            println(("Couldn't factory reset bluetooth device"));
            return;
        }
  }

  /* Print Bluefruit information */
  // bluetoothHID.info();

  /* Change the device name to make it easier to find */
  println("Baptising device name to 'Page Turner'");  if (! bluetoothHID.sendCommandCheckOK(F( "AT+GAPDEVNAME=Page Turner" )) ) {
    println("Could not set device name!");
  }

  /* Enable HID Service */
  if ( !bluetoothHID.sendCommandCheckOK(F( "AT+BLEHIDEN=On" ))) {
      println("Could not enable HID Keyboard");
  }
  if (! bluetoothHID.reset()) {
      println("Couldn't reset bluetooth device");
      return;
  } 
}

static uint32_t key_pressed_ms = 0;

void send_bluetooth_command(uint16_t key) {
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

// to be called in loop(), releases the pressed key after 100ms
void update_bluetooth_release() {
  if (millis() - key_pressed_ms > 100) {
     bluetoothHID.sendCommandCheckOK("AT+BLEKEYBOARDCODE=00-00-00-00-00-00-00"); // release
     key_pressed_ms = 0;
  };
}