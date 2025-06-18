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
import tensorflow as tf
from pydub import AudioSegment
import glob
import librosa
import pprint
from scipy.signal import resample_poly
import numpy as np
from collections import defaultdict
import re
import sounddevice as sd
from scipy.io.wavfile import write as wav_write
from pynput import keyboard
import threading

# === Constants ===
PACKET_MAGIC_HI    = 0xAB
PACKET_MAGIC_LO    = 0xCD
PACKET_MAX_PAYLOAD = 512

# === Command IDs from Teensy ===
CMD_AUDIO_RECORDING = 0xA1
CMD_SAMPLE_COUNT    = 0xA2
CMD_AUDIO_STREAM    = 0xA3

DATASET_DIR             = "dataset"
OPTIMISED_DATASET_DIR   = "optimised_dataset"
MODEL_OUTPUT_DIR        = "model"

RECORD_SECONDS    = 1      
SAMPLE_RATE       = 16000  # downsampled on teensy from 44.1kHz
BYTES_PER_SAMPLE  = 2

TARGET_SAMPLES    = RECORD_SECONDS * SAMPLE_RATE
TARGET_BYTES      = TARGET_SAMPLES * BYTES_PER_SAMPLE
DURATION_MS       = (RECORD_SECONDS*1000)


LABELS = ['weiter', 'next', 'zurueck', 'back', 'unknown','silence', 'background']
CMD_LABELS = ['weiter', 'next', 'zurueck', 'back']

audio_data = bytearray()  # outside of main()

# for mic mode:
mic_mode = False

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

def create_optimised_dataset_dirs(base=OPTIMISED_DATASET_DIR):
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

    from collections import deque

from collections import deque

# before mic_test_mode
STEP_MS         = 200    # distance of sliding windows for inference
STEP_S          = STEP_MS / 1000.0
WINDOW_SAMPLES  = TARGET_SAMPLES  # 1 s * 16 kHz
import python_speech_features as psf
from collections import deque

def mic_test_mode():
    print("🧪 Entering mic-test mode. Hold SPACE to start, release to stop.")
    ring = deque(maxlen=TARGET_SAMPLES)
    is_running = False
    last_inference_time = 0.0

    def audio_cb(indata, frames, time_, status):
        nonlocal is_running, last_inference_time
        try:
            ring.extend(indata[:,0])
            now = time.time()
            # only infer every 200ms once we have 1s of data
            if is_running and len(ring) == TARGET_SAMPLES \
               and now - last_inference_time >= STEP_S:
                buf = np.array(ring, dtype=np.int16)
                label, conf = run_inference(buf)
                print(f"🔊 → {label} ({conf:.2f})")
                last_inference_time = now
        except Exception as e:
            print("⚠️  Inference error:", e)

    def on_press(key):
        nonlocal is_running
        if key == keyboard.Key.space and not is_running:
            print("▶️  Inference started")
            is_running = True

    def on_release(key):
        nonlocal is_running
        if key == keyboard.Key.space:
            print("⏹️  Inference stopped")
            is_running = False
            return False  # stop listener

    # start the audio stream
    stream = sd.InputStream(
        samplerate=SAMPLE_RATE,
        channels=1,
        dtype='int16',
        callback=audio_cb
    )
    stream.start()

    # explicitly start & join the listener
    listener = keyboard.Listener(on_press=on_press, on_release=on_release)
    listener.start()
    listener.join()

    stream.stop()
    print("🧪 Exiting mic-test mode.")


def record_from_mic(label):
    fs = SAMPLE_RATE
    channels = 1
    dtype = 'int16'

    # show mic info
    default_input_index = sd.default.device[0]
    input_info = sd.query_devices(default_input_index)
    print(f"🎤 Using microphone: {input_info['name']}")

    # --- branch for command labels: fixed 1 second ---
    if label in CMD_LABELS:
        print("▶️  Recording exactly 1 second for command label…")
        audio = sd.rec(int(fs), samplerate=fs, channels=channels, dtype=dtype)
        sd.wait()
        print(f"✅ Stoped recording")

        # save exactly-1s snippet
        folder = os.path.join(DATASET_DIR, label)
        os.makedirs(folder, exist_ok=True)
        # find next index
        existing = [f for f in os.listdir(folder) 
                    if f.lower().endswith(".wav") and f.startswith(label)]
        max_num = 0
        for fn in existing:
            m = re.match(rf"{re.escape(label)}(\d+)\.wav$", fn)
            if m:
                num = int(m.group(1))
                if num > max_num:
                    max_num = num
        idx = max_num + 1

        path = os.path.join(folder, f"{label}{idx}.wav")
        wav_write(path, fs, audio)
        print(f"✅ Saved to {path}")
        return

    # --- otherwise: manual hold-SPACE-to-record mode ---
    recording = []
    is_recording = False
    lock = threading.Lock()

    print("🎙️  Press and hold SPACE to record. Release to stop…")

    def audio_callback(indata, frames, time, status):
        if is_recording:
            with lock:
                recording.append(indata.copy())

    def on_press(key):
        nonlocal is_recording
        if key == keyboard.Key.space and not is_recording:
            print("▶️  Recording…")
            is_recording = True
        elif key != keyboard.Key.space and not is_recording:
            # any non-space before starting should exit listener
            return False

    def on_release(key):
        nonlocal is_recording, recording
        if key == keyboard.Key.space:
            print("⏹️  Stopped. Saving…")
            is_recording = False
            with lock:
                if recording:
                    audio = np.concatenate(recording, axis=0)
                    folder = os.path.join(DATASET_DIR, label)
                    os.makedirs(folder, exist_ok=True)
                    # find next index
                    existing = [
                        f for f in os.listdir(folder)
                        if f.lower().endswith(".wav") and f.startswith(label)
                    ]
                    max_num = 0
                    for fn in existing:
                        m = re.match(rf"{re.escape(label)}(\d+)\.wav$", fn)
                        if m:
                            num = int(m.group(1))
                            if num > max_num:
                                max_num = num
                    idx = max_num + 1

                    path = os.path.join(folder, f"{label}{idx}.wav")
                    wav_write(path, fs, audio)
                    print(f"✅ Saved to {path}")
                    recording.clear()
            return False  # stop listener

    with sd.InputStream(samplerate=fs, channels=channels, dtype=dtype, callback=audio_callback):
        with keyboard.Listener(on_press=on_press, on_release=on_release) as listener:
            listener.join()

# === optimise data set ===

def count_files_per_label(dataset_dir=OPTIMISED_DATASET_DIR, extensions=(".wav", ".mp3")):
    label_counts = defaultdict(int)

    for root, dirs, files in os.walk(dataset_dir):
        label = os.path.basename(root)
        count = sum(1 for f in files if f.lower().endswith(extensions))
        if count > 0:
            label_counts[label] += count

    # Output nicely formatted
    print("📊 File counts per label:")
    for label, count in sorted(label_counts.items()):
        print(f"{label:<15} {count:>5}")




def optimise_dataset(dataset_dir="dataset", target_sample_rate=16000):
    # ── clean out previous optimised data ────────────────────────────────────────
    if os.path.isdir(OPTIMISED_DATASET_DIR):
        shutil.rmtree(OPTIMISED_DATASET_DIR)

    # ── prepare per-label counters ───────────────────────────────────────────────
    label_counters = {}

    for root, dirs, files in os.walk(dataset_dir):
        for file in files:
            if not (file.lower().endswith(".mp3") or file.lower().endswith(".wav")):
                continue

            src_label = os.path.basename(root)

            # merge 'piano' into 'background'
            out_label = 'background' if src_label == 'piano' else src_label

            # initialise counter for this output label if needed
            if out_label not in label_counters:
                label_counters[out_label] = 0

            # build matching folder under optimised_dataset
            relative_path = os.path.relpath(root, dataset_dir)
            # override piano → background in path
            if src_label == 'piano':
                relative_path = os.path.join(os.path.dirname(relative_path), 'background')
            output_dir = os.path.join(OPTIMISED_DATASET_DIR, relative_path)
            os.makedirs(output_dir, exist_ok=True)

            try:
                # load source
                if file.lower().endswith(".mp3"):
                    audio = AudioSegment.from_file(os.path.join(root, file), format="mp3")
                else:
                    audio = AudioSegment.from_file(os.path.join(root, file), format="wav")

                # normalize to mono, 16-bit, 16 kHz
                audio = (
                    audio
                    .set_channels(1)
                    .set_sample_width(2)
                    .set_frame_rate(target_sample_rate)
                )

                basename    = os.path.splitext(file)[0].replace(" ", "_")
                duration_ms = len(audio)

                # slice into 1s segments or keep as single
                segments = []
                if duration_ms > DURATION_MS:
                    for i in range(0, duration_ms, DURATION_MS):
                        chunk = audio[i:i + DURATION_MS]
                        if len(chunk) < DURATION_MS:
                            break
                        segments.append(chunk)
                else:
                    segments.append(audio)

                # export all segments, using a single counter per out_label
                for chunk in segments:
                    idx      = label_counters[out_label]
                    out_name = f"{out_label}_{basename}_{idx:04d}.wav"
                    out_path = os.path.join(output_dir, out_name)
                    if not os.path.exists(out_path):
                        chunk.export(out_path, format="wav")
                    label_counters[out_label] += 1

            except Exception as e:
                print(f"❌ Fehler bei {os.path.join(root, file)}: {e}")

def validate_dataset(dataset_dir=OPTIMISED_DATASET_DIR):
    invalid_files = []

    for root, dirs, files in os.walk(dataset_dir):
        for file in files:
            if not file.lower().endswith(".wav"):
                continue

            full_path = os.path.join(root, file)

            try:
                audio = AudioSegment.from_file(full_path, format="wav")
                duration_ms = len(audio)
                sample_rate = audio.frame_rate
                channels = audio.channels
                sample_width = audio.sample_width  # in bytes

                # Bedingungen prüfen
                if not (DURATION_MS-50 <= duration_ms <= DURATION_MS+50):
                    invalid_files.append((full_path, f"❌ Dauer: {duration_ms} ms"))
                elif sample_rate != 16000:
                    invalid_files.append((full_path, f"❌ Sample Rate: {sample_rate} Hz"))
                elif channels != 1:
                    invalid_files.append((full_path, f"❌ Channels: {channels}"))
                elif sample_width != 2:
                    invalid_files.append((full_path, f"❌ Sample Width: {sample_width * 8} bit"))
            except Exception as e:
                invalid_files.append((full_path, f"❌ Fehler beim Einlesen: {e}"))

    # Ausgabe
    if not invalid_files:
        print("✅ Alle .wav-Dateien sind gültige 1s-Snippets mit 16 kHz, mono, 16-bit.")
    else:
        print(f"⚠️  {len(invalid_files)} ungültige Dateien gefunden:\n")
        for path, reason in invalid_files:
            print(f"{reason:35} -> {path}")

import python_speech_features as psf

# ——————————————————————————————————————————————
# 1) Sliding-window normalization
def sliding_norm(mfcc_feats, win_size=151, eps=1e-6):
    T, D = mfcc_feats.shape
    normed = np.zeros_like(mfcc_feats, dtype=np.float32)
    # running sums
    csum   = np.vstack([np.zeros((1,D)), np.cumsum(mfcc_feats,   axis=0)])
    csqsum = np.vstack([np.zeros((1,D)), np.cumsum(mfcc_feats**2,axis=0)])
    for t in range(T):
        start = max(0, t - win_size + 1)
        count = t - start + 1
        s1 = csum[t+1]    - csum[start]
        s2 = csqsum[t+1]  - csqsum[start]
        mean = s1/count
        var  = (s2/count) - (mean**2)
        std  = np.sqrt(np.maximum(var,0.0)) + eps
        normed[t] = (mfcc_feats[t] - mean) / std
    return normed

# ——————————————————————————————————————————————
# 2) Load TFLite model once
interpreter = tf.lite.Interpreter(model_path="../model/model.tflite")
interpreter.allocate_tensors()
input_details  = interpreter.get_input_details()[0]
output_details = interpreter.get_output_details()[0]

# ——————————————————————————————————————————————
# 3) Single‐window inference

# —————————————————————————————————————————
# Create a 4th-order bandpass at module init
from scipy.signal import butter, lfilter
BP_ORDER = 4
BP_LOWER = 300.0
BP_UPPER = 3400.0
BP_B, BP_A = butter(
    BP_ORDER,
    [BP_LOWER/(0.5*SAMPLE_RATE), BP_UPPER/(0.5*SAMPLE_RATE)],
    btype="band"
)
def apply_bandpass(x: np.ndarray) -> np.ndarray:
    """4th-order butterworth bandpass at 300–3400 Hz."""
    return lfilter(BP_B, BP_A, x)


def run_inference(raw_int16: np.ndarray):
    """
    raw_int16: 1-D np.int16 array @16 kHz
    returns: (predicted_label, confidence)
    """
    # 1) Normalize to float32 in [–1..1]
    sig = raw_int16.astype(np.float32) / 32768.0

    # 2) Pre-filter to speech band (300–3400 Hz)
    sig = lfilter(BP_B, BP_A, sig)

    # 3) Compute MFCC exactly as in Edge Impulse
    mfcc_feats = psf.mfcc(
        sig,
        samplerate=SAMPLE_RATE,
        winlen=0.025,     # 25 ms
        winstep=0.020,    # 20 ms
        numcep=13,
        nfilt=32,
        nfft=512,
        lowfreq=80,
        highfreq=8000,
        preemph=0.98
    )
    mfcc_feats = sliding_norm(mfcc_feats, win_size=151)

    # 4) Truncate/pad to exactly match model’s flat‐MFCC length
    total_feats = input_details['shape'][1]       # e.g. 637
    ncep        = mfcc_feats.shape[1]            # should be 13
    exp_frames  = total_feats // ncep            # should be 49

    # truncate or pad frames
    if mfcc_feats.shape[0] > exp_frames:
        mfcc_feats = mfcc_feats[:exp_frames]
    elif mfcc_feats.shape[0] < exp_frames:
        pad = np.zeros((exp_frames - mfcc_feats.shape[0], ncep), dtype=mfcc_feats.dtype)
        mfcc_feats = np.vstack([mfcc_feats, pad])

    # 5) Flatten + batch
    inp = mfcc_feats.flatten()[np.newaxis, :].astype(np.float32)

    # 6) Quantize if needed
    dtype = input_details['dtype']
    if dtype != np.float32:
        scale, zp = input_details['quantization']
        q = np.round(inp / scale + zp)
        inp = np.clip(q, np.iinfo(dtype).min, np.iinfo(dtype).max).astype(dtype)

    # 7) Run inference
    interpreter.set_tensor(input_details['index'], inp)
    interpreter.invoke()
    out = interpreter.get_tensor(output_details['index'])

    # 8) Dequantize output if needed
    if output_details['dtype'] != np.float32:
        scale, zp = output_details['quantization']
        out = (out.astype(np.float32) - zp) * scale

    probs = np.squeeze(out)
    idx   = int(np.argmax(probs))

    # 9) Optional silence gate
    energy = np.mean(sig * sig)
    if energy < 1e-4 or probs[idx] < 0.6:
        return "silence", float(probs[idx])
    return LABELS[idx], float(probs[idx])

def augment_balance(
    optimised_dir=OPTIMISED_DATASET_DIR,
    cmd_labels=CMD_LABELS,
    bg_label='background',
    silence_label='silence',
    target_cmd=60,
    unknown_multiplier=4,
    bg_multiplier=2,
    target_silence=60,
    snr_range_db=(0, 20),            # SNR range for bg scaling
    global_gain_range_db=(-6, 6),    # global gain offset
    silence_gain_dB=-6,
    time_stretch_range=(0.9, 1.1),    # time-stretch factor range
    pitch_shift_steps=(-2, 2),       # pitch shift in semitones
    filter_cutoff_range=(300, 3000), # lowpass filter cutoff
    ir_dir=None,                     # directory with IR wav files for reverb
    noise_level_range=(0.0, 0.005),  # synthetic noise amplitude
    crop_range=(0.9, 1.0),           # random crop percentage
    rate_perturb_range=(0.97, 1.03), # resampling factor range
    soft_clipping_prob=0.5           # probability to apply soft clipping
):
    import numpy as np
    import random
    import os
    from pydub import AudioSegment
    import librosa
    from scipy.signal import butter, lfilter, resample
    from scipy.io import wavfile

    SAMPLE_RATE = 16000
    MIN_LENGTH_FOR_STFT = 2048

    # helper: convert AudioSegment <-> numpy float32 [-1..1]
    def seg_to_np(seg: AudioSegment) -> np.ndarray:
        arr = np.array(seg.get_array_of_samples()).astype(np.float32)
        arr /= float(2**15)
        return arr

    def np_to_seg(arr: np.ndarray) -> AudioSegment:
        # enforce exact 1s length
        if len(arr) > SAMPLE_RATE:
            arr = arr[:SAMPLE_RATE]
        elif len(arr) < SAMPLE_RATE:
            arr = np.pad(arr, (0, SAMPLE_RATE - len(arr)), mode='constant')
        arr = np.clip(arr, -1.0, 1.0)
        int16 = (arr * (2**15 - 1)).astype(np.int16)
        return AudioSegment(
            int16.tobytes(),
            frame_rate=SAMPLE_RATE,
            sample_width=2,
            channels=1
        )

    def wav_paths(label):
        folder = os.path.join(optimised_dir, label)
        return [os.path.join(folder, f) for f in os.listdir(folder) if f.endswith('.wav')]

    # augmentation primitives
    def apply_time_stretch(y):
        # skip if too short for STFT
        if len(y) < MIN_LENGTH_FOR_STFT:
            return y
        rate = random.uniform(*time_stretch_range)
        # use keyword args to avoid positional error
        return librosa.effects.time_stretch(y=y, rate=rate)

    def apply_pitch_shift(y):
        # skip if too short for STFT
        if len(y) < MIN_LENGTH_FOR_STFT:
            return y
        steps = random.uniform(*pitch_shift_steps)
        return librosa.effects.pitch_shift(y=y, sr=SAMPLE_RATE, n_steps=steps)

    def apply_filter(y):
        cutoff = random.uniform(*filter_cutoff_range)
        b, a = butter(N=2, Wn=cutoff/(0.5*SAMPLE_RATE), btype='low')
        return lfilter(b, a, y)

    def apply_reverb(y):
        if not ir_dir:
            return y
        ir_files = [os.path.join(ir_dir, f) for f in os.listdir(ir_dir) if f.endswith('.wav')]
        if not ir_files:
            return y
        ir_sr, ir = wavfile.read(random.choice(ir_files))
        ir = ir.astype(np.float32) / np.max(np.abs(ir))
        if ir_sr != SAMPLE_RATE:
            ir = resample(ir, int(len(ir) * SAMPLE_RATE / ir_sr))
        return np.convolve(y, ir, mode='same')

    def add_synthetic_noise(y):
        return y + np.random.randn(len(y)) * random.uniform(*noise_level_range)

    def random_crop_pad(y):
        pct = random.uniform(*crop_range)
        length = len(y)
        new_len = int(length * pct)
        start = random.randint(0, length - new_len) if new_len < length else 0
        out = y[start:start+new_len] if new_len < length else y
        if len(out) < length:
            pad = np.zeros(length, dtype=out.dtype)
            pad[random.randint(0, length-len(out)):][:len(out)] = out
            return pad
        return out

    def rate_perturb(y):
        factor = random.uniform(*rate_perturb_range)
        y_rs = resample(y, int(len(y)/factor))
        return resample(y_rs, len(y))

    def soft_clip(y):
        return np.tanh(y)

    def maybe_apply(func, y, prob=0.5):
        return func(y) if random.random() < prob else y

    # targets per class
    target_unknown = unknown_multiplier * target_cmd
    target_bg      = bg_multiplier * target_cmd

    # 1) COMMANDS
    for label in cmd_labels:
        paths = wav_paths(label)
        curr = len(paths)
        need = max(0, target_cmd - curr)
        counter = curr
        for _ in range(need):
            seg = AudioSegment.from_wav(random.choice(paths))
            ms = len(seg)
            y = seg_to_np(seg)
            bg = seg_to_np(AudioSegment.from_wav(random.choice(wav_paths(bg_label)))[:ms])
            sl = seg_to_np(AudioSegment.from_wav(random.choice(wav_paths(silence_label)))[:ms]) * (10**(silence_gain_dB/20))
            mlen = min(len(y), len(bg), len(sl))
            y, bg, sl = y[:mlen], bg[:mlen], sl[:mlen]
            snr = 10**(random.uniform(*snr_range_db)/20)
            bg *= (np.sqrt(np.mean(y**2))/(np.sqrt(np.mean(bg**2))*snr +1e-9))
            y = y + bg + sl
            y *= 10**(random.uniform(*global_gain_range_db)/20)
            for fn, p in [(rate_perturb,0.7),(apply_time_stretch,0.5),(apply_pitch_shift,0.5),
                         (apply_filter,0.5),(apply_reverb,0.3),(add_synthetic_noise,0.8),(random_crop_pad,0.5)]:
                y = maybe_apply(fn, y, prob=p)
            if random.random() < soft_clipping_prob: y = soft_clip(y)
            if np.max(np.abs(y))>1: y/=np.max(np.abs(y))
            out = np_to_seg(y)
            out.export(os.path.join(optimised_dir, label, f"{label}_aug_{counter:04d}.wav"), format='wav')
            counter+=1
        print(f"✔️ '{label}': {curr}→{counter}")

    # 2) UNKNOWN
    label='unknown'; paths=wav_paths(label)
    curr=len(paths); need=max(0,target_unknown-curr); counter=curr
    for _ in range(need):
        seg=AudioSegment.from_wav(random.choice(paths)); ms=len(seg)
        y=seg_to_np(seg)
        bg=seg_to_np(AudioSegment.from_wav(random.choice(wav_paths(bg_label)))[:ms])
        sl=seg_to_np(AudioSegment.from_wav(random.choice(wav_paths(silence_label)))[:ms])*(10**(silence_gain_dB/20))
        mlen=min(len(y),len(bg),len(sl)); y,bg,sl=y[:mlen],bg[:mlen],sl[:mlen]
        snr=10**(random.uniform(*snr_range_db)/20); bg*=(np.sqrt(np.mean(y**2))/(np.sqrt(np.mean(bg**2))*snr+1e-9)); y=y+bg+sl
        y*=10**(random.uniform(*global_gain_range_db)/20)
        for fn,p in [(rate_perturb,0.7),(apply_time_stretch,0.5),(apply_pitch_shift,0.5),
                    (apply_filter,0.5),(apply_reverb,0.3),(add_synthetic_noise,0.8),(random_crop_pad,0.5)]: y=maybe_apply(fn,y,prob=p)
        if random.random()<soft_clipping_prob: y=soft_clip(y)
        if np.max(np.abs(y))>1: y/=np.max(np.abs(y))
        out=np_to_seg(y); out.export(os.path.join(optimised_dir,label,f"{label}_aug_{counter:04d}.wav"),format='wav'); counter+=1
    print(f"✔️ 'unknown': {curr}→{counter}")

    # 3) BACKGROUND
    label=bg_label; paths=wav_paths(label)
    curr=len(paths); need=max(0,target_bg-curr); counter=curr
    for _ in range(need):
        seg=AudioSegment.from_wav(random.choice(paths)); ms=len(seg)
        y=seg_to_np(seg)
        sl=seg_to_np(AudioSegment.from_wav(random.choice(wav_paths(silence_label)))[:ms])*(10**(silence_gain_dB/20))
        mlen=min(len(y),len(sl)); y,sl=y[:mlen],sl[:mlen]
        y=y+sl; y*=10**(random.uniform(*global_gain_range_db)/20)
        for fn,p in [(rate_perturb,0.7),(apply_time_stretch,0.3),(apply_filter,0.5),
                    (add_synthetic_noise,0.8),(random_crop_pad,0.5)]: y=maybe_apply(fn,y,prob=p)
        if np.max(np.abs(y))>1: y/=np.max(np.abs(y))
        out=np_to_seg(y); out.export(os.path.join(optimised_dir,label,f"{label}_aug_{counter:04d}.wav"),format='wav'); counter+=1
    print(f"✔️ '{bg_label}': {curr}→{counter}")

    # 4) SILENCE
    label=silence_label; paths=wav_paths(label)
    curr=len(paths); need=max(0,target_silence-curr); counter=curr
    for _ in range(need):
        seg=AudioSegment.from_wav(random.choice(paths)); y=seg_to_np(seg)
        for fn,p in [(random_crop_pad,0.5),(add_synthetic_noise,0.5)]: y=maybe_apply(fn,y,prob=p)
        if np.max(np.abs(y))>1: y/=np.max(np.abs(y))
        out=np_to_seg(y); out.export(os.path.join(optimised_dir,label,f"{label}_aug_{counter:04d}.wav"),format='wav'); counter+=1
    print(f"✔️ '{silence_label}': {curr}→{counter}")


# === Main Program ===

def main():
    # Eval-Mode State
    audio_stream_buffer = []                # 1 s Fenster (16 000 Samples)
    last_inference_time = 0.0
    interpreter = None
    input_details = output_details = None
    mic_mode = False;

    create_dataset_dirs()
    create_optimised_dataset_dirs()
    port = find_teensy_port()
    ser = None

    if port:
        try:
            ser = serial.Serial(port, 115200, timeout=1)
            print(f"📡 Listening on {port}...")
        except serial.SerialException as e:
            print(f"⚠️ Failed to open serial port: {e}")
            ser = None
    else:
        print("⚠️ No Teensy connected, recording disabled.")


    if port:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"📡 Listening on {port}...")
    else:
        ser = None
        print("⚠️ No Teensy connected, recording disabled.")


    print(f"📡 Listening on {port}...")

    print("Commands:")
    for i, l in enumerate(LABELS):
        print(f" {i} - record label '{l}'")
    print(" o - optimise dataset")
    print(" m - use PC microphone to record a .wav file")
    print(" s - use PC microphone for inference test")
    print(" h - help")
    print(" q - quit")

    label = None
    print("🎛️  Press number key (0–5) to select label. Press Teensy button to record.")

    while True:
        # check keyboard without blocking
        if msvcrt.kbhit():
            key = msvcrt.getch().decode('utf-8').lower()
            if mic_mode and key != ' ':
                mic_mode = False

            if key == 'o':
                print("🚀 Starting dataset optimisation...")
                optimise_dataset()
                augment_balance()
                count_files_per_label();
                validate_dataset()
                print("✅ Optimisation complete.")
            elif key == 'm':
                if label is None:
                    print("❌ Please select a label (0–7) first.")
                    continue
                mic_mode = True
            elif key == 's':
                mic_test_mode()
                # when done, re-print the top-level menu:
                print("🎛️  Back to main menu. Select label or command:")
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
            elif key == ' ':
                print("ℹ️  Space pressed in main menu (no effect here).")
            else:
                print(f"❓ Invalid key: '{key}'")

        if mic_mode:
            if label is None:
                print("❌ Please select a label (0–7) first.")
                continue
            record_from_mic(label)
            if label in CMD_LABELS:
                mic_mode = False


        # look for packets
        if ser and ser.in_waiting:
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

if __name__ == "__main__":
    main()
