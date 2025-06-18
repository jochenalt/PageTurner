import ctypes
import numpy as np
import sounddevice as sd
from enum import Enum

# 1) Define class labels as enumeration
class Labels(Enum):
    BACK       = "back"
    BACKGROUND = "background"
    NEXT       = "next"
    SILENCE    = "silence"
    UNKNOWN    = "unknown"
    WEITER     = "weiter"
    ZURUECK    = "zurück"

labels = [label.value for label in Labels]

# Audio parameters
samplerate      = 16000
channels        = 1
window_duration = 1.0   # inference window in seconds
step_duration   = 0.05  # time between buffer updates (seconds)

window_samples = int(samplerate * window_duration)
step_samples   = int(samplerate * step_duration)

print("Available audio devices:\n", sd.query_devices())
input_device = 1  # adjust as needed

# 4) Load the shared library
lib = ctypes.CDLL('./libPageTurnerInference.so')
lib.get_input_frame_size.restype = ctypes.c_uint32
lib.get_label_count.restype      = ctypes.c_uint32


# Bind the new int16 version:
lib.run_pageturner_inference_c.argtypes = [
    ctypes.POINTER(ctypes.c_int16),  # raw int16 buffer
    ctypes.c_uint32,                 # input size
    ctypes.POINTER(ctypes.c_float),  # output probs buffer
    ctypes.POINTER(ctypes.c_uint32), # output label
]
lib.run_pageturner_inference_c.restype = ctypes.c_int

# 5) Fetch model parameters
input_size  = lib.get_input_frame_size()
model_count = lib.get_label_count()

if len(labels) != model_count:
    raise ValueError(f"Enum length ({len(labels)}) does not match model label count ({model_count}).")

# 6) Classification helper function
def classify(raw_int16: np.ndarray) -> np.ndarray:
    if raw_int16.dtype != np.int16:
        raise ValueError(f"Expected int16 buffer, got {raw_int16.dtype}")
    if raw_int16.size != input_size:
        raise ValueError(f"Expected {input_size} samples, got {raw_int16.size}")
    buf = np.ascontiguousarray(raw_int16)
    out_probs = np.zeros(model_count, dtype=np.float32)
    out_label = ctypes.c_uint32()
    res = lib.run_pageturner_inference_c(
        buf.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        ctypes.c_uint32(input_size),
        out_probs.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
        ctypes.byref(out_label)
    )
    if res != 0:
        raise RuntimeError(f"Inference failed with code {res}")
    return out_probs

# 7) Continuous inference
if __name__ == "__main__":
    print(f"Starting inference every {int(step_duration*1000)} ms (Ctrl-C to stop)…")
    buffer = np.zeros(window_samples, dtype=np.int16)
    prev_label = None
    count = 0
    ignore_labels = {Labels.SILENCE.value, Labels.BACKGROUND.value, Labels.UNKNOWN.value}

    try:
        with sd.InputStream(device=input_device,
                             samplerate=samplerate,
                             channels=channels,
                             dtype='int16',
                             blocksize=step_samples) as stream:
            while True:
                data, overflow = stream.read(step_samples)
                if overflow:
                    print("Warning: overflow detected")
                frame = data.flatten()  # int16 array of length=step_samples

                # slide window
                buffer[:-step_samples] = buffer[step_samples:]
                buffer[-step_samples:] = frame

                # inference on raw PCM
                scores = classify(buffer)
                pred_idx   = int(np.argmax(scores))
                pred_label = labels[pred_idx]

                # simple debouncing
                if pred_label == prev_label:
                    count += 1
                else:
                    prev_label = pred_label
                    count = 1

                if count == 4 and pred_label not in ignore_labels:
                    print(f"Detected: {pred_label} (score: {scores[pred_idx]:.4f})")
    except KeyboardInterrupt:
        print("\nStopping.")
