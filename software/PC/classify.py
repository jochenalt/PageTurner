import ctypes
import numpy as np
import sounddevice as sd

# Audio parameters
samplerate      = 16000
channels        = 1
window_duration = 1.0   # inference window in seconds
step_duration   = 0.05  # time between buffer updates (seconds)

window_samples = int(samplerate * window_duration)
step_samples   = int(samplerate * step_duration)

print("Available audio devices:\n", sd.query_devices())
input_device = 1  # adjust as needed

# 1) Load the shared library
lib = ctypes.CDLL('./libPageTurnerInference.so')

# 2) Bind model-size getters
lib.get_input_frame_size.restype = ctypes.c_uint32
lib.get_label_count.restype      = ctypes.c_uint32

# 3) Bind inference fn (int16 → float internally)
lib.run_pageturner_inference_c.argtypes = [
    ctypes.POINTER(ctypes.c_int16),  # raw PCM buffer
    ctypes.c_uint32,                 # input size
    ctypes.POINTER(ctypes.c_float),  # output probs buffer
    ctypes.POINTER(ctypes.c_uint32), # output label
]
lib.run_pageturner_inference_c.restype = ctypes.c_int

# 4) Bind category‐list getters
lib.get_category_count.restype = ctypes.c_uint32
lib.get_categories.restype     = ctypes.POINTER(ctypes.c_char_p)
lib.get_category.argtypes      = [ctypes.c_uint32]
lib.get_category.restype       = ctypes.c_char_p

# 5) Fetch categories from the C library
category_count = lib.get_category_count()
cats_ptr       = lib.get_categories()
categories     = [cats_ptr[i].decode('utf-8') for i in range(category_count)]
print("Categories:", categories)

# 6) Fetch model parameters
input_size  = lib.get_input_frame_size()
model_count = lib.get_label_count()

if category_count != model_count:
    raise ValueError(
        f"Category count ({category_count}) != model output count ({model_count})"
    )

# 7) Build the set of labels to ignore
_ignore = {"silence", "background", "unknown"}
ignore_labels = {c for c in categories if c in _ignore}

# 8) Classification helper
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

# 9) Continuous inference loop
if __name__ == "__main__":
    print(f"Starting inference every {int(step_duration*1000)} ms (Ctrl-C to stop)…")
    buffer = np.zeros(window_samples, dtype=np.int16)
    prev_label = None
    count = 0

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
                frame = data.flatten()  # int16 array

                # slide window
                buffer[:-step_samples] = buffer[step_samples:]
                buffer[-step_samples:] = frame

                # run inference
                scores     = classify(buffer)
                pred_idx   = int(np.argmax(scores))
                pred_label = categories[pred_idx]

                # debouncing
                if pred_label == prev_label:
                    count += 1
                else:
                    prev_label = pred_label
                    count = 1

                # print on stable, non-ignored label
                if count == 4 and pred_label not in ignore_labels:
                    print(f"Detected: {pred_label} (score: {scores[pred_idx]:.4f})")

    except KeyboardInterrupt:
        print("\nStopping.")
