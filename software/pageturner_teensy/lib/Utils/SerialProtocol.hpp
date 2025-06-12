#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

#include <Arduino.h>

// --- Packet Framing Constants ---
#define PACKET_MAGIC_HI 0xAB
#define PACKET_MAGIC_LO 0xCD
#define PACKET_MAX_PAYLOAD 512

// --- CRC Functions ---
uint8_t compute_crc8(uint8_t cmd, uint16_t len, const uint8_t* data);
uint32_t compute_crc32(const uint8_t* data, uint32_t len);

// --- Framed Packet API ---
bool read_packet(Stream& s, uint8_t& cmd, uint8_t* payload, uint16_t& len);
void send_packet(Stream& s, uint8_t cmd, const uint8_t* payload, uint16_t len);

#endif // SERIAL_PROTOCOL_H