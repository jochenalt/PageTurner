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

# === Constants ===
PACKET_MAGIC_HI = 0xAB
PACKET_MAGIC_LO = 0xCD
PACKET_MAX_PAYLOAD = 512

# === Command IDs from Teensy ===
CMD_AUDIO_SNIPPET = 0xA1
CMD_SAMPLE_COUNT = 0xA2
CMD_AUDIO_STREAM  = 0xA3

DATASET_DIR         = "dataset"
AUGMENTED_DIR       = "dataset_augmented"
MODEL_OUTPUT_DIR    = "trained_model"

RECORD_SECONDS    = 1
SAMPLE_RATE       = 16000  # downsampled on teensy from 44.1kHz
BYTES_PER_SAMPLE  = 2
TARGET_SAMPLES    = RECORD_SECONDS * SAMPLE_RATE
TARGET_BYTES      = TARGET_SAMPLES * BYTES_PER_SAMPLE
DURATION_MS       = RECORD_SECONDS * (RECORD_SECONDS*1000)


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

def copy_original_dataset():
    for lbl in LABELS:
        src = os.path.join(DATASET_DIR, lbl)
        dst = os.path.join(AUGMENTED_DIR, lbl)
        if not os.path.isdir(src): 
            print(f"⚠️ Missing: {src}")
            continue
        for fn in os.listdir(src):
            if fn.endswith(".wav"):
                shutil.copy(os.path.join(src, fn), os.path.join(dst, fn))

from pydub import AudioSegment

# your constants
RECORD_SECONDS = 2
SAMPLE_RATE    = 16000
DURATION_MS    = RECORD_SECONDS * 1000

def pad_or_trim_np(samples: np.ndarray) -> np.ndarray:
    if len(samples) < TARGET_SAMPLES:
        return np.concatenate([
            samples,
            np.zeros(TARGET_SAMPLES - len(samples), dtype=samples.dtype)
        ])
    else:
        return samples[:TARGET_SAMPLES]

def copy_and_pad_original():
    """Read each original .wav, pad/trim to TARGET_SAMPLES, write into augmented."""
    for label in LABELS:
        src_dir = os.path.join(DATASET_DIR, label)
        dst_dir = os.path.join(AUGMENTED_DIR, label)
        os.makedirs(dst_dir, exist_ok=True)
        if not os.path.isdir(src_dir):
            continue
        for fn in os.listdir(src_dir):
            if not fn.endswith(".wav"): continue
            src = os.path.join(src_dir, fn)
            dst = os.path.join(dst_dir, fn)
            sr, data = wavfile.read(src)
            if data.ndim>1:
                data = data[:,0]
            if sr != SAMPLE_RATE:
                # fallback: use pydub to resample
                seg = AudioSegment.from_wav(src).set_frame_rate(SAMPLE_RATE).set_channels(1)
                data = np.array(seg.get_array_of_samples(), dtype=np.int16)
            data = pad_or_trim_np(data)
            wavfile.write(dst, SAMPLE_RATE, data)

def timeshift_wav_np(src_path, dst_path, max_shift_ms=150):
    sr, data = wavfile.read(src_path)
    if data.ndim>1: data = data[:,0]
    # random shift in samples
    shift_amt = int(random.uniform(-max_shift_ms, max_shift_ms) * sr / 1000.0)
    if shift_amt > 0:
        shifted = np.concatenate([data[shift_amt:], np.zeros(shift_amt, dtype=data.dtype)])
    else:
        shifted = np.concatenate([np.zeros(-shift_amt, dtype=data.dtype), data[:shift_amt]])
    shifted = pad_or_trim_np(shifted)
    wavfile.write(dst_path, sr, shifted)

def mix_with_background_np(word_path, bg_path, dst_path):
    # load both
    sr1, fg = wavfile.read(word_path)
    sr2, bg = wavfile.read(bg_path)
    assert sr1==sr2==SAMPLE_RATE
    if fg.ndim>1: fg = fg[:,0]
    if bg.ndim>1: bg = bg[:,0]
    # make both exactly target
    fg = pad_or_trim_np(fg)
    if len(bg) < TARGET_SAMPLES:
        times = TARGET_SAMPLES // len(bg) + 1
        bg = np.tile(bg, times)
    bg = pad_or_trim_np(bg)
    # choose random start
    start = random.randint(0, TARGET_SAMPLES - len(fg))
    bg_clip = bg[start:start+len(fg)]
    # random gains (in linear scale)
    fg_gain = 10**(random.uniform(-3,3)/20)   # ±3 dB
    bg_gain = 10**( -random.uniform(5,15)/20 )# −5…−15 dB
    mixed = (fg.astype(np.float32) * fg_gain + bg_clip.astype(np.float32)*bg_gain)
    # avoid clipping
    mixed = np.clip(mixed, -32768, 32767).astype(np.int16)
    wavfile.write(dst_path, SAMPLE_RATE, mixed)

def augment_files():
    for label in LABELS:
        if label in ('silence','background'): continue
        orig_dir = os.path.join(DATASET_DIR, label)
        out_dir  = os.path.join(AUGMENTED_DIR, label)
        bg_dir   = os.path.join(DATASET_DIR, 'background')
        bg_files = [os.path.join(bg_dir,f) for f in os.listdir(bg_dir) if f.endswith('.wav')]

        for fn in os.listdir(orig_dir):
            if not fn.endswith('.wav'): continue
            base = os.path.splitext(fn)[0]
            src  = os.path.join(orig_dir, fn)

            # time‐shift
            dst = os.path.join(out_dir, f"{base}_shifted.wav")
            timeshift_wav_np(src, dst)

            # mix with *each* background
            for bg in bg_files:
                bg_name = os.path.splitext(os.path.basename(bg))[0]
                dst     = os.path.join(out_dir, f"{base}_bg_{bg_name}.wav")
                mix_with_background_np(src, bg, dst)

def run_augmentation():
    if os.path.exists(AUGMENTED_DIR):
        shutil.rmtree(AUGMENTED_DIR)
    # make per‐label dirs
    for l in LABELS:
        os.makedirs(os.path.join(AUGMENTED_DIR, l), exist_ok=True)
    # first copy+pad originals
    copy_and_pad_original()
    # then augment
    augment_files()

def verify_augmented():
    bad = []
    for l in LABELS:
        for fn in os.listdir(os.path.join(AUGMENTED_DIR,l)):
            if not fn.endswith('.wav'): continue
            with wave.open(os.path.join(AUGMENTED_DIR,l,fn),'rb') as wf:
                n = wf.getnframes()
            if n != TARGET_SAMPLES:
                bad.append((l,fn,n))
    if bad:
        print("⚠️ Wrong frame counts:")
        for l,fn,n in bad:
            print(f" {l}/{fn}: {n}")
    else:
        print("✅ All clips are", TARGET_SAMPLES, "frames long.")

import wave, os

EXPECTED_FRAMES = SAMPLE_RATE * RECORD_SECONDS

def verify_dataset_lengths(folder=AUGMENTED_DIR):
    expected = RECORD_SECONDS * SAMPLE_RATE
    bad = []
    for lbl in LABELS:
        for fn in os.listdir(os.path.join(folder,lbl)):
            if not fn.endswith('.wav'): continue
            path = os.path.join(folder,lbl,fn)
            with wave.open(path,'rb') as wf:
                n = wf.getnframes()
            if n != expected:
                bad.append((path,n))
    if bad:
        print("⚠️ Wrong frame counts:")
        for p,n in bad: print(f" {p}: {n}")
    else:
        print("✅ All clips are exactly", expected, "frames.")


#  === Create training pipeline function ===
def get_next_model_version(base_dir):
    os.makedirs(base_dir, exist_ok=True)
    existing = [
        int(d.name.replace("model_", ""))
        for d in pathlib.Path(base_dir).iterdir()
        if d.is_dir() and d.name.startswith("model_") and d.name.replace("model_", "").isdigit()
    ]
    next_version = max(existing, default=0) + 1
    return os.path.join(base_dir, f"model_{next_version}")

from tflite_model_maker.audio_classifier import BrowserFftSpec, DataLoader, create
from tflite_model_maker.config import ExportFormat

import glob

def check_augmented():
    print("🔎 Checking augmented data contents…")
    for label in LABELS:
        pattern = os.path.join(AUGMENTED_DIR, label, "*.wav")
        n = len(glob.glob(pattern))
        print(f"  {label}: {n} file(s)")
    all_counts = [len(glob.glob(os.path.join(AUGMENTED_DIR, l, "*.wav"))) for l in LABELS]
    if sum(all_counts) == 0:
        raise RuntimeError(f"No .wav files found under {AUGMENTED_DIR}! Did augmentation run correctly?")

from tflite_model_maker import audio_classifier
from tflite_model_maker.audio_classifier import BrowserFftSpec, DataLoader
from tflite_model_maker.config import ExportFormat

def train_model():
    spec = BrowserFftSpec();

    # 2️⃣ Load your augmented data and split 80/20
    print("📚 Loading data…")
    loader = DataLoader.from_folder(spec, AUGMENTED_DIR, cache=True)
    train_data, test_data = loader.split(0.8)

    # 3️⃣ Log split sizes
    n_train, n_test = len(train_data), len(test_data)
    print(f"   ▶ train examples: {n_train}")
    print(f"   ▶ test  examples: {n_test}")

    # 4️⃣ Train (retry without validation if empty-dataset error)
    print("🎓 Training model…")
    try:
        model = create(
            train_data,
            spec,
            validation_data=test_data,
            batch_size=32,
            epochs=10
        )
    except ValueError as e:
        print("⚠️ Validation set error:", e)
        print("   → Retrying training *without* validation set")
        model = create(
            train_data,
            spec,
            batch_size=32,
            epochs=10
        )

    # 5️⃣ Evaluate on whichever set is non-empty
    eval_ds = test_data if n_test > 0 else train_data
    print("🧪 Evaluating model…")
    loss, acc = model.evaluate(eval_ds)
    print(f"✅ Accuracy: {acc:.1%}")

    # 6️⃣ Export to verhhsioned folder
    versioned_dir = get_next_model_version(MODEL_OUTPUT_DIR)
    print(f"💾 Exporting model to {versioned_dir}…")
    model.export(
        export_dir=versioned_dir,
        export_format=[ExportFormat.TFLITE, ExportFormat.LABEL]
    )
    print("✅ Model exported to", versioned_dir)

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
    print(" t - training")
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
            print(f">> Received packet cmd=0x{cmd:02X} len={len(payload)}")

            if cmd == CMD_AUDIO_STREAM and interpreter is not None:
                # rohes PCM16 in numpy-Array
                chunk = np.frombuffer(payload, dtype=np.int16)
                audio_stream_buffer.extend(chunk.tolist())
                # nur letzte 1 s behalten
                if len(audio_stream_buffer) > TARGET_SAMPLES:
                    audio_stream_buffer = audio_stream_buffer[-TARGET_SAMPLES:]

                # Sliding-Window Inferenz alle 250 ms
                now = time.time()
                if interpreter is not None and (now - last_inference_time) >= inference_stride \
                                           and len(audio_stream_buffer) >= TARGET_SAMPLES:
                    last_inference_time = now
                    # Normalisiertes Float-Signal
                    signal = np.array(audio_stream_buffer, dtype=np.float32) / 32768.0
                    # MFCC-Features (30 ms window, 10 ms stride, 13 coeff)
                    mfcc = librosa.feature.mfcc(
                       y=signal,
                       sr=SAMPLE_RATE,
                       n_mfcc=13,
                       n_fft=int(0.03*SAMPLE_RATE),
                       hop_length=int(0.01*SAMPLE_RATE)
                    ).T  # shape (time_steps, 13)
                    # Interpreter füttern (Formate können je nach Spec variieren)
                    input_tensor = np.expand_dims(mfcc, axis=0).astype(np.float32)
                    interpreter.set_tensor(input_details[0]['index'], input_tensor)
                    interpreter.invoke()
                    output = interpreter.get_tensor(output_details[0]['index'])[0]
                    idx    = np.argmax(output)
                    lbl    = LABELS[idx]
                    print(f"🔍 Inference → {lbl} ({output[idx]:.2f})")

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

            # → regulärer Schnipsel-Modus
            if cmd == CMD_AUDIO_SNIPPET:
                chunk_idx = payload[0]
                chunk_total = payload[1]
                audio_data.extend(payload[2:])
                # print(f"🟡 Chunk {chunk_idx+1}/{chunk_total}")

if __name__ == "__main__":
    main()
