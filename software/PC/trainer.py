import serial
import serial.tools.list_ports
import os
import struct
import wave
import time
import shutil
import random
import numpy as np
from scipy.io import wavfile
from scipy.ndimage import shift
import pathlib
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
import threading
from pynput import keyboard
from collections import deque
from platform import system

# === local classes ===
import kbhit
import model_interface

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


LABELS = model_interface.get_available_labels()
CMD_LABELS = model_interface.get_available_commands();

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
TEENSY_VID = 0x16C0
TEENSY_PID = 0x0483
def find_teensy_port():
    ports = list(serial.tools.list_ports.comports())
    print("Available ports:")
    for p in ports:
        print(f" - {p.device} | {p.description} | VID:PID={p.vid:04X}:{p.pid:04X}")
    # 1) match by VID/PID
    for p in ports:
        if p.vid == TEENSY_VID and p.pid == TEENSY_PID:
            return p.device
    # 2) fallback: match Linux naming (/dev/ttyACM* or /dev/ttyUSB*)
    for p in ports:
        if p.device.startswith(('/dev/ttyACM', '/dev/ttyUSB')):
            return p.device
    print("❌ No Teensy port found.")
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
def save_wav(samples_bytes, label):
    """
    Write out `samples_bytes` (raw 16-bit PCM little-endian) to
    DATASET_DIR/label/label{N}.wav, where N is one more than the
    highest existing index.
    """
    folder = os.path.join(DATASET_DIR, label)
    os.makedirs(folder, exist_ok=True)

    # find the highest existing index
    existing = [f for f in os.listdir(folder) if f.lower().endswith(".wav")]
    pattern = re.compile(rf"^{re.escape(label)}(\d+)\.wav$")
    max_idx = 0
    for fn in existing:
        m = pattern.match(fn)
        if m:
            idx = int(m.group(1))
            if idx > max_idx:
                max_idx = idx

    new_idx = max_idx + 1
    filename = f"{label}{new_idx}.wav"
    path = os.path.join(folder, filename)

    # now actually write the file
    with wave.open(path, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(samples_bytes)

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

import numpy as np

def compute_rms(samples):
    """
    samples: 1D int16-Array, unskaliert (–32768…+32767)
    threshold: RMS**2–Schwelle in (–1…1)²–Normierung
    """
    # 1) Normierung auf [–1…1]
    y = samples.astype(np.float32) / 32768.0
    # 2) RMS**2 (Average Power)
    power = np.mean(y*y)
    return power;

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
                    # nur Label + Punkt + Nummer (4-stellig), z.B. weiter.0001.wav
                    out_name = f"{out_label}.{basename}.{idx:04d}.wav"
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



def augment_balance(
    optimised_dir=OPTIMISED_DATASET_DIR,
    cmd_labels=CMD_LABELS,
    bg_label='background',
    silence_label='silence',
    target_cmd=100,
    unknown_multiplier=4,
    bg_multiplier=2,
    target_silence=100,
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
            out.export(os.path.join(optimised_dir, label, f"{label}.aug_{counter:04d}.wav"), format='wav')
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
        out=np_to_seg(y); out.export(os.path.join(optimised_dir,label,f"{label}.aug_{counter:04d}.wav"),format='wav'); counter+=1
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
        out=np_to_seg(y); out.export(os.path.join(optimised_dir,label,f"{label}.aug_{counter:04d}.wav"),format='wav'); counter+=1
    print(f"✔️ '{bg_label}': {curr}→{counter}")

    # 4) SILENCE
    label=silence_label; paths=wav_paths(label)
    curr=len(paths); need=max(0,target_silence-curr); counter=curr
    for _ in range(need):
        seg=AudioSegment.from_wav(random.choice(paths)); y=seg_to_np(seg)
        for fn,p in [(random_crop_pad,0.5),(add_synthetic_noise,0.5)]: y=maybe_apply(fn,y,prob=p)
        if np.max(np.abs(y))>1: y/=np.max(np.abs(y))
        out=np_to_seg(y); out.export(os.path.join(optimised_dir,label,f"{label}.aug_{counter:04d}.wav"),format='wav'); counter+=1
    print(f"✔️ '{silence_label}': {curr}→{counter}")


def run_pc_mic_mode(input_device=None,
        samplerate=16000, channels=1,
        window_duration=1.0, step_duration=0.05):
    """
    Continuously reads from the PC microphone and runs inference on
    a rolling window of `window_duration` seconds, updated every
    `step_duration` seconds. Prints stable detections (4 consecutive).
    Exit cleanly on ESC.
    """
    window_samples = int(samplerate * window_duration)
    step_samples   = int(samplerate * step_duration)
    ignore = {'silence','background','unknown'}

    print(f"🎙  PC‐mic inference: window={window_duration}s step={step_duration}s")
    print("Press ESC to exit.")

    # ring buffer for last `window_samples`
    buf = np.zeros(window_samples, dtype=np.int16)
    prev_label = None
    count = 0

    try:
        with sd.InputStream(device=input_device,
                             samplerate=samplerate,
                             channels=channels,
                             dtype='int16',
                             blocksize=step_samples) as stream:
            while True:
                # 1) Check for ESC key
                if kbhit.kbhit():
                    ch = kbhit.getch()
                    if ch and ord(ch) == 27:  # ESC
                        print("\n🚪 Exiting PC‐mic inference.")
                        break

                # 2) Read next audio block
                data, overflow = stream.read(step_samples)
                if overflow:
                    print("⚠️  Overflow")
                frame = data.flatten()

                # 3) Slide window
                buf[:-step_samples] = buf[step_samples:]
                buf[-step_samples:] = frame

                # 4) Run inference
                scores     = model_interface.classify(buf)
                pred_idx   = int(np.argmax(scores))
                pred_label = LABELS[pred_idx]

                # 5) Debounce and print
                if pred_label == prev_label:
                    count += 1
                else:
                    prev_label  = pred_label
                    count       = 1

                if count == 4 and pred_label not in ignore:
                    print(f"Detected: {pred_label} (score: {scores[pred_idx]:.4f})")

    finally:
        print("🛑 PC‐mic inference stopped.")
        # if you initialized kbhit in main(), you'll restore it there



def print_menu():
    """Show the list of commands to the user."""
    print("Commands:")
    for i, l in enumerate(LABELS):
        print(f" {i} - record label '{l}'")
    print(" o - optimise dataset")
    print(" m - use PC microphone to record a .wav file")
    print(" s - use PC microphone for inference test")
    print(" h - help")
    print(" q - quit")
    print("🎛️  Press number key (0–5) to select label. Press Teensy button to record.")


# === Main Program ===

def main():
    kbhit.init_kbhit()
    try:
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

        print_menu();

        label = None

        streaming = False
        stream_buffer = bytearray()
        stream_label = None
        last_stream_time = 0.0
        STREAM_TIMEOUT = 1.5  # seconds to wait for the “next” snippet
        stream_buffer.clear()
        while True:
            # check keyboard without blocking
            if kbhit.kbhit():
                key = kbhit.getch()
                if mic_mode and key != ' ':
                    mic_mode = False

                if key == 'o':
                    print("🚀 Starting dataset optimisation...")
                    optimise_dataset()
                    #augment_balance()
                    count_files_per_label();
                    validate_dataset()
                    print("✅ Optimisation complete.")
                elif key == 'm':
                    if label is None:
                        print("❌ Please select a label (0–7) first.")
                        continue
                    mic_mode = True
                elif key == 's':
                    # read the default input device once:
                    default_dev = sd.default.device[0]
                    run_pc_mic_mode(input_device=default_dev,
                                      samplerate=SAMPLE_RATE,
                                      channels=1,
                                      window_duration=1.0,
                                      step_duration=0.05)
                    print("🎛️  Back to main menu. Select label or command:")
                    continue
                elif key == 'q':
                    print("👋 Exiting.")
                    break
                elif key == 'h':
                    print_menu();
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

            if streaming and (time.time() - last_stream_time) > STREAM_TIMEOUT:
                print("ℹ️  Stream ended due to timeout")
                # write out the entire stream
                if stream_label is not None:
                    save_wav(stream_buffer, stream_label)
                    print(f"✅ Saved full {len(stream_buffer)//2}-sample stream under '{stream_label}'")
                else:
                    print("ℹ️  Stream ended, no label selected → dropping")
                # reset streaming state
                streaming = False
                stream_buffer.clear()
                stream_label = None


            # look for packets
            if ser and ser.in_waiting:
                result = read_packet(ser)
                if not result:
                    continue
                cmd, payload = result
                #print(f">> Received packet cmd=0x{cmd:02X} len={len(payload)}")

                if cmd == CMD_AUDIO_STREAM and interpreter is not None:
                        print(f"no model present");

                if cmd == CMD_AUDIO_STREAM:
                    if not streaming:
                        print("ℹ️  start receive stream")
                    print("ℹ️  received 1s of stream data")
                    print(f"ℹ️   adding  {len(audio_data)} samples to stream of {len(stream_buffer)} bytes")
                    stream_buffer.extend(audio_data)
                    audio_data.clear()


                    last_stream_time = time.time()
                    streaming = True
                    # remember current label (could be global `label` from keyboard)
                    stream_label = label
                    continue
                elif cmd == CMD_SAMPLE_COUNT:
                    # you can still handle final inference here if you need
                    # but for pure streaming saves, CMD_SAMPLE_COUNT means “end”
                    # trigger the same save logic immediately:
                    if streaming:
                        if stream_label is not None:
                            save_wav(audio_data, stream_label)
                            print(f"✅ Saved full {len(stream_buffer)//2}-sample stream under '{stream_label}'")
                        else:
                            print("ℹ️  Stream ended, no label selected → dropping")
                    streaming = False
                    stream_buffer.clear()
                    stream_label = None

                    print(f"❌ CMD_SAMPLE_COUNT payload length: {len(payload)}")
    
                    total_samples = struct.unpack('<I', payload[2:6])[0]
                    print(f"📦 total_samples: {total_samples}")
                    class_count = struct.unpack('<I', payload[6:10])[0]
                    print(f"📦 class_count: {class_count}")
                    teensy_scores = []
                    offset = 10
                    for i in range(class_count):
                        score = struct.unpack('<f', payload[offset:offset+4])[0]
                        teensy_scores.append(score)
                        offset += 4
                    pred_teensy_idx  = int(np.argmax(teensy_scores))
                    pred_teensy_label= LABELS[pred_teensy_idx]

                    print(f"Teensy scores: {', '.join(f'{label}={score:.5f}' for label, score in zip(LABELS, teensy_scores))} --> {pred_teensy_label}")

                    
                    expected_bytes = total_samples * BYTES_PER_SAMPLE
                    print(f"📦 Sample count: {total_samples} → {expected_bytes} bytes")

                    if len(audio_data) != expected_bytes:
                        print(f"❌ Incomplete data: got {len(audio_data)}, expected {expected_bytes}")
                        audio_data.clear()
                        continue

                    # 2) decode & pad/truncate
                    samples = np.frombuffer(audio_data, dtype=np.int16).copy()
                    if samples.size < TARGET_SAMPLES:
                        samples = np.pad(samples, (0, TARGET_SAMPLES - samples.size), 'constant')
                    elif samples.size > TARGET_SAMPLES:
                        samples = samples[:TARGET_SAMPLES]

                    # 3) Play back on PC
                    sd.play(samples, SAMPLE_RATE)
                    #sd.wait()

                    # 4) Run inference
                    rms    = compute_rms(samples);
                    scores    = model_interface.classify(samples)
                    pred_idx  = int(np.argmax(scores))
                    pred_label= LABELS[pred_idx]
                    print(f"C++ scores: {', '.join(f'{label}={score:.5f}' for label, score in zip(LABELS, scores))} (rms={rms})--> : {pred_label}")
 
                    # 5a) if the user pre-selected a ground-truth label, only save on mismatch
                    if label is not None:
                        if pred_teensy_label != label:
                            save_wav(audio_data, label)
                            print(f"✅ MISMATCH! Saved under ground-truth '{label}'")
                        else:
                            print("✅ MATCH: skipping save.")
                    # 5b) otherwise offer optional annotation
                    else:
                        print("Press 0–{n} to annotate & save, or wait 3 s to skip.".format(n=len(LABELS)-1))
                        for i,l in enumerate(LABELS):
                            print(f"  {i}: {l}")
                        start = time.time()
                        choice = None
                        while time.time() - start < 3 and not ser.in_waiting:
                            if kbhit.kbhit():
                                ch = kbhit.getch()
                                if ch.isdigit():
                                    i = int(ch)
                                    if 0 <= i < len(LABELS):
                                        choice = i
                                break
                        if  ser.in_waiting:
                            print(" no time, packet gets in")
                        if  time.time() - start >= 3:
                            print(" Timout, you missed your chance")

                        if choice is not None:
                            if choice != pred_idx:
                                save_wav(audio_data, LABELS[choice])
                                print(f"✅ Saved under '{LABELS[choice]}'")
                            else:
                                print("ℹ️ Choice matches inference, skipping save.")

                    audio_data.clear()  # prepare for next

                elif cmd == CMD_AUDIO_RECORDING:
                    # store audio recording in a buffer. It is only saved if the 
                    # following CMD_SAMPLE_COUNT gives the right number. Othrwise this
                    # buffer is overwritten next time
                    chunk_idx = payload[0]
                    chunk_total = payload[1]
                    audio_data.extend(payload[2:])

                    #print(f"received  {len(audio_data)} samples")
                else:
                    print(f"unknown command {cmd}")


    finally:
          kbhit.restore_kbhit()

if __name__ == "__main__":
    main()
