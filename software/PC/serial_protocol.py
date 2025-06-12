import serial
import struct
import crcmod
from collections import defaultdict

MAGIC = b'\xAB\xCD'
CRC_FUNC = crcmod.predefined.mkCrcFun('crc-8')

class SerialProtocol:
    def __init__(self, port, baudrate=115200, timeout=0.5):
        self.ser = serial.Serial(port, baudrate, timeout=timeout)
        self._chunk_buffers = defaultdict(dict)
        self._chunk_meta = {}

    def build_packet(self, cmd: int, payload: bytes = b'') -> bytes:
        length = len(payload)
        header = struct.pack('>2sBH', MAGIC, cmd, length)
        crc = CRC_FUNC(header[2:] + payload)
        return header + payload + bytes([crc])

    def send_packet(self, cmd: int, payload: bytes = b''):
        self.ser.write(self.build_packet(cmd, payload))

    def read_packet(self) -> tuple[int, bytes] | None:
        while True:
            sync = self.ser.read(2)
            if sync != MAGIC:
                continue

            header = self.ser.read(3)
            if len(header) < 3:
                continue
            cmd, length = struct.unpack('>BH', header)

            payload = self.ser.read(length)
            crc = self.ser.read(1)
            if len(payload) != length or len(crc) != 1:
                continue

            expected_crc = CRC_FUNC(header + payload)
            if expected_crc != crc[0]:
                print("[WARN] CRC mismatch")
                continue

            # Chunk structure: [chunk_index][total_chunks][chunk_payload]
            chunk_index = payload[0]
            total_chunks = payload[1]
            chunk_data = payload[2:]

            self._chunk_buffers[cmd][chunk_index] = chunk_data
            self._chunk_meta[cmd] = total_chunks

            if len(self._chunk_buffers[cmd]) == total_chunks:
                full_payload = b''.join(self._chunk_buffers[cmd][i] for i in range(total_chunks))
                del self._chunk_buffers[cmd]
                del self._chunk_meta[cmd]
                return cmd, full_payload
            else:
                continue  # wait for remaining chunks

    def flush_input(self):
        self.ser.reset_input_buffer()