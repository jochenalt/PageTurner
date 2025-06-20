
#include <SerialProtocol.hpp>
 
uint8_t compute_crc8(uint8_t cmd, uint16_t len, const uint8_t* data) {
  uint8_t crc = 0;
  crc ^= cmd;
  crc ^= (len >> 8);
  crc ^= (len & 0xFF);
  for (uint16_t i = 0; i < len; ++i) {
    crc ^= data[i];
  }
  return crc;
}

// Standard CRC-32 implementation (polynomial 0xEDB88320)
uint32_t compute_crc32(const uint8_t* data, uint32_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xEDB88320;
      else
        crc >>= 1;
    }
  }
  return ~crc;
}

bool read_packet(Stream& s, uint8_t& cmd, uint8_t* payload, uint16_t& len) {
  if (s.available() < 6) return false;

  if (s.read() != PACKET_MAGIC_HI) return false;
  if (s.read() != PACKET_MAGIC_LO) return false;

  cmd = s.read();
  len = (s.read() << 8) | s.read();
  if (len > PACKET_MAX_PAYLOAD + 2) return false;

  for (uint16_t i = 0; i < len; ++i) {
    while (!s.available());
    payload[i] = s.read();
  }

  while (!s.available());
  uint8_t crc = s.read();
  uint8_t calc = compute_crc8(cmd, len, payload);
  return crc == calc;
}

void send_packet(Stream& s, uint8_t cmd, const uint8_t* payload, uint16_t len) {
  const uint16_t MAX_CHUNK_SIZE = PACKET_MAX_PAYLOAD;
  uint16_t bytes_sent = 0;
  uint8_t chunk_index = 0;
  uint8_t total_chunks = (len + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;

  while (bytes_sent < len) {
    uint16_t remaining = len - bytes_sent;
    uint16_t chunk_len = min(remaining, MAX_CHUNK_SIZE);

    // Payload format: [chunk_index][total_chunks][chunk_data]
    uint8_t frame[MAX_CHUNK_SIZE + 2];
    frame[0] = chunk_index;
    frame[1] = total_chunks;
    memcpy(frame + 2, payload + bytes_sent, chunk_len);

    uint16_t final_len = chunk_len + 2;

    // Write header
    s.write(PACKET_MAGIC_HI);
    s.write(PACKET_MAGIC_LO);
    s.write(cmd);
    s.write((final_len >> 8) & 0xFF);
    s.write(final_len & 0xFF);

    // Write payload + CRC
    s.write(frame, final_len);
    s.write(compute_crc8(cmd, final_len, frame));

    if (cmd == CMD_SAMPLE_COUNT) {
      LOGSerial.print("CMD_SAMPLECOUNT");

      LOGSerial.print(PACKET_MAGIC_HI, HEX);      
      LOGSerial.print(PACKET_MAGIC_LO, HEX);      LOGSerial.print("  ");
      LOGSerial.print(cmd, HEX);      LOGSerial.print("  ");
      LOGSerial.print((final_len >> 8) & 0xFF, HEX);      LOGSerial.print(' ');
      LOGSerial.print(final_len & 0xFF, HEX);      LOGSerial.print("  "); 
      for (int i = 0;i<final_len;i++) {
        LOGSerial.print( frame[i], HEX);      LOGSerial.print(' ');
      }
      LOGSerial.print(compute_crc8(cmd, final_len, frame), HEX);
      LOGSerial.println();

    }

    bytes_sent += chunk_len;
    chunk_index++;
  }
}