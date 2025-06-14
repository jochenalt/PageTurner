import serial
import serial.tools.list_ports
import os
import struct
import wave
import time
import msvcrt

# === Constants ===
PACKET_MAGIC_HI = 0xAB
PACKET_MAGIC_LO = 0xCD
PACKET_MAX_PAYLOAD = 512
CMD_AUDIO_SNIPPET = 0xA1
CMD_SAMPLE_COUNT = 0xA2
SAMPLE_RATE = 44100  # use Teensy's default
BYTES_PER_SAMPLE = 2
LABELS = ['weiter', 'next', 'zurueck', 'back', 'silence', 'background']

audio_data = bytearray()  # outside of main()

# === CRC ===
def compute_crc8(cmd, length, payload):
    crc = cmd ^ (length >> 8) ^ (length & 0xFF)
    for b in payload:
        crc ^= b
    return crc

# === Port Detection ===
def find_teensy_port():
    ports = list(serial.tools.list_ports.comports())
    matches = [p for p in ports if "Serielles USB-Gerät" in p.description or "USB Serial Device" in p.description]
    print("Available ports:")
    for p in ports:
        print(f" - {p.device} ({p.description})")
    if len(matches) == 1:
        return matches[0].device
    elif len(matches) == 0:
        print("❌ No Teensy port found (with 'Serielles USB-Gerät').")
    else:
        print("❌ Multiple matching ports found.")
    return None

# === Directory Setup ===
def create_dataset_dirs(base='dataset'):
    for label in LABELS:
        path = os.path.join(base, label)
        os.makedirs(path, exist_ok=True)

# === Save WAV File ===
def save_wav(samples, label):
    folder = os.path.join("dataset", label)
    files = os.listdir(folder)
    number = len([f for f in files if f.endswith(".wav")])
    path = os.path.join(folder, f"{label}{number+1}.wav")
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(samples)
    print(f"✅ Saved {path}")

# === Packet Parsing ===
def read_packet(ser):
    if ser.read() != bytes([PACKET_MAGIC_HI]):
        return None
    if ser.read() != bytes([PACKET_MAGIC_LO]):
        return None
    cmd = ser.read(1)[0]
    len_hi = ser.read(1)[0]
    len_lo = ser.read(1)[0]
    length = (len_hi << 8) | len_lo
    payload = bytearray(ser.read(length))
    crc = ser.read(1)[0]
    if compute_crc8(cmd, length, payload) != crc:
        print("❌ CRC Mismatch")
        return None
    return cmd, payload

# === Main Program ===

def main():
    create_dataset_dirs()
    port = find_teensy_port()
    if not port:
        return

    ser = serial.Serial(port, 115200, timeout=1)
    print(f"📡 Listening on {port}...")

    print("Commands:")
    for i, l in enumerate(LABELS):
        print(f" {i} - record label '{l}'")
    print(" h - help")
    print(" q - quit")

    label = None
    print("🎛️  Press number key (0–5) to select label. Press Teensy button to record.")

    while True:
        # check keyboard without blocking
        if msvcrt.kbhit():
            key = msvcrt.getch().decode('utf-8').lower()

            if key == 'q':
                print("👋 Exiting.")
                break
            elif key == 'h':
                print("Available commands:")
                for i, l in enumerate(LABELS):
                    print(f" {i} - record label '{l}'")
                continue
            elif key in [str(i) for i in range(len(LABELS))]:
                label = LABELS[int(key)]
                print(f"✅ Selected label: '{label}'")
                continue
            else:
                print("❓ Invalid key.")

        if label is None:
            continue  # wait for label

        # look for packets
        if ser.in_waiting:
            result = read_packet(ser)
            if not result:
                continue
            cmd, payload = result

            if cmd == CMD_AUDIO_SNIPPET:
                chunk_idx = payload[0]
                chunk_total = payload[1]
                audio_data.extend(payload[2:])
                print(f"🟡 Chunk {chunk_idx+1}/{chunk_total}")

            elif cmd == CMD_SAMPLE_COUNT:
                if len(payload) != 6:
                    print(f"❌ CMD_SAMPLE_COUNT payload length invalid: {len(payload)}")
                    continue

                sample_data = payload[2:6]
                total_samples = struct.unpack('<I', sample_data)[0]
                expected_bytes = total_samples * BYTES_PER_SAMPLE
                print(f"📦 Sample count: {total_samples} → {expected_bytes} bytes")

                if len(audio_data) == expected_bytes:
                    save_wav(audio_data, label)
                else:
                    print(f"❌ Incomplete data: got {len(audio_data)}, expected {expected_bytes}")
                audio_data.clear()  # prepare for next


if __name__ == "__main__":
    main()
