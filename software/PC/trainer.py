import serial
import serial.tools.list_ports
import os
import struct
import wave
import time
import msvcrt
import shutil
import random
import numpy as np
from scipy.io import wavfile
from scipy.ndimage import shift
import pathlib
from tflite_model_maker import audio_classifier
from tflite_model_maker.config import ExportFormat
import tensorflow as tf
from pydub import AudioSegment
from tflite_model_maker import model_spec
from tflite_model_maker import audio_classifier as ac
import glob
import librosa
import pprint
from scipy.signal import resample_poly
import numpy as np

# === Constants ===
PACKET_MAGIC_HI    = 0xAB
PACKET_MAGIC_LO    = 0xCD
PACKET_MAX_PAYLOAD = 512

# === Command IDs from Teensy ===
CMD_AUDIO_RECORDING = 0xA1
CMD_SAMPLE_COUNT    = 0xA2
CMD_AUDIO_STREAM    = 0xA3

DATASET_DIR         = "dataset"
MODEL_OUTPUT_DIR    = "model"

RECORD_SECONDS    = 1      
SAMPLE_RATE       = 16000  # downsampled on teensy from 44.1kHz
BYTES_PER_SAMPLE  = 2

TARGET_SAMPLES    = RECORD_SECONDS * SAMPLE_RATE
TARGET_BYTES      = TARGET_SAMPLES * BYTES_PER_SAMPLE
DURATION_MS       = RECORD_SECONDS * (RECORD_SECONDS*1000)


LABELS = ['weiter', 'next', 'zurueck', 'back', 'unknown','silence', 'background']

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
def create_dataset_dirs(base=DATASET_DIR):
    for label in LABELS:
        path = os.path.join(base, label)
        os.makedirs(path, exist_ok=True)

# === Save WAV File ===
def save_wav(samples, label):
    folder = os.path.join(DATASET_DIR, label)
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

# === Augment .wav files ===

def create_augmented_dirs():
    for lbl in LABELS:
        os.makedirs(os.path.join(AUGMENTED_DIR, lbl), exist_ok=True)


# === Main Program ===

def main():
    # Eval-Mode State
    audio_stream_buffer = []                # 1 s Fenster (16 000 Samples)
    last_inference_time = 0.0
    inference_stride   = 0.25               # 250 ms
    interpreter = None
    input_details = output_details = None

    create_dataset_dirs()
    port = find_teensy_port()
    if not port:
        return

    model_dirs = sorted(glob.glob(os.path.join(MODEL_OUTPUT_DIR, 'model_*')))
    if model_dirs:
        latest = model_dirs[-1]
        tflite_path = os.path.join(latest, 'model.tflite')
        interpreter = tf.lite.Interpreter(model_path=tflite_path)
        interpreter.allocate_tensors()
        input_details  = interpreter.get_input_details()
        output_details = interpreter.get_output_details()
        print("▶ model input shape:", input_details[0]['shape'])
        print("  dtype:", input_details[0]['dtype'])

        print(f"✅ Loaded model {latest}")
    else:
        interpreter = None
        input_details = output_details = None
        print("⚠️ No model found yet — training mit 't' möglich.")

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
            if key == 't':
                verify_dataset_lengths(folder=DATASET_DIR);

                print("🚀 Starting augmentation...")
                run_augmentation()
                check_augmented();
                verify_dataset_lengths(folder=AUGMENTED_DIR);
                print("🔍 Augmented dir contents:")
                for lbl in LABELS:
                    n = len(list(pathlib.Path(AUGMENTED_DIR, lbl).glob("*.wav")))
                    print(f"  {lbl}: {n} file(s)")
                print("✅ Augmentation done.")
                print("🚀 Starting training...")
                train_model()
                print("✅ Training done.")

                continue
            elif key == 'q':
                print("👋 Exiting.")
                break
            elif key == 'h':
                print("Available commands:")
                for i, l in enumerate(LABELS):
                    print(f" {i} - record label '{l}'")
                print(" t - training")

                continue
            elif key in [str(i) for i in range(len(LABELS))]:
                label = LABELS[int(key)]
                print(f"✅ Selected label: '{label}'")
                continue
            else:
                print("❓ Invalid key.")

        # look for packets
        if ser.in_waiting:
            result = read_packet(ser)
            if not result:
                continue
            cmd, payload = result
            #print(f">> Received packet cmd=0x{cmd:02X} len={len(payload)}")

            if cmd == CMD_AUDIO_STREAM and interpreter is not None:
                    print(f"no model present");

            if cmd == CMD_SAMPLE_COUNT:
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

            if label is None:
                continue  # wait for label

            if cmd == CMD_AUDIO_RECORDING:
                # store audio recording in a buffer. It is only saved if the 
                # following CMD_SAMPLE_COUNT gives the right number. Otehrwise this
                # buffer is overwritten next time
                chunk_idx = payload[0]
                chunk_total = payload[1]
                audio_data.extend(payload[2:])
                # print(f"🟡 Chunk {chunk_idx+1}/{chunk_total}")

if __name__ == "__main__":
    main()
