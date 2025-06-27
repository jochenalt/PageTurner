#pragma once

#include "constants.h"
#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_UART.h"

void init_bluetooth_baudrate();
void init_bluetooth();
void send_bluetooth_command(int16_t key);
void update_bluetooth_release();