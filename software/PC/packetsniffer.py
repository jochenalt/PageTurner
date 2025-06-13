import serial
import struct

MAGIC_HI = 0xAB
MAGIC_LO = 0xCD

def compute_crc8(cmd, length, data):
    crc = cmd ^ (length >> 8) ^ (length & 0xFF)
    for byte in data:
        crc ^= byte
    return crc
def safe_read(ser, num_bytes):
    data = ser.read(num_bytes)
    if len(data) < num_bytes:
        raise Exception("Timeout or insufficient data")
    return data

def read_packet(ser):
    while True:
        byte = ser.read(1)
        if len(byte) == 0:
            continue  # nothing available yet
        if byte[0] != MAGIC_HI:
            continue
        byte = ser.read(1)
        if len(byte) == 0 or byte[0] != MAGIC_LO:
            continue

        cmd_bytes = safe_read(ser, 1)
        cmd = cmd_bytes[0]
        length_bytes = safe_read(ser, 2)
        length = struct.unpack(">H", length_bytes)[0]
        if length > 1024:
            print("Length too large")
            continue

        payload = safe_read(ser, length)
        crc_bytes = safe_read(ser, 1)
        crc = crc_bytes[0]

        calc_crc = compute_crc8(cmd, length, payload)
        if crc != calc_crc:
            print(f"CRC error! cmd={cmd}, length={length}, crc={crc}, calc={calc_crc}")
            continue

        print(f"✅ Valid packet received: cmd={cmd}, length={length}")
        return cmd, payload

if __name__ == "__main__":
    import serial.tools.list_ports
    ports = [p for p in serial.tools.list_ports.comports() if "Serial" in p.description]
    if len(ports) != 1:
        print("Please connect only one Teensy device.")
        for p in ports:
            print(f"{p.device}: {p.description}")
        exit()

    port = "COM3"
    print(f"Connecting to: {port}")
    ser = serial.Serial(port, 115200, timeout=1)

    while True:
        try:
            cmd, payload = read_packet(ser)
        except Exception as e:
            print("Error:", e)
