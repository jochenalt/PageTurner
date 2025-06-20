# ─────────────────────────────────────────────────────────────────────────────
# ► Add this right after your other imports in trainer.py:
import ctypes
import numpy as np
import sounddevice as sd
import kbhit

# 1) Load the shared inference library
_lib = ctypes.CDLL('./libPageTurnerInference.so')

# 2) Model‐size getters
_lib.get_input_frame_size.restype = ctypes.c_uint32
_lib.get_label_count.     restype = ctypes.c_uint32

# 3) Inference function binding
_lib.run_pageturner_inference_c.argtypes = [
    ctypes.POINTER(ctypes.c_int16),  # input buffer
    ctypes.c_uint32,                 # input size
    ctypes.POINTER(ctypes.c_float),  # output probs
    ctypes.POINTER(ctypes.c_uint32), # output label index
]
_lib.run_pageturner_inference_c.restype = ctypes.c_int

# 4) Category list binding
_lib.get_category_count.restype = ctypes.c_uint32
_lib.get_categories.restype     = ctypes.POINTER(ctypes.c_char_p)
_lib.get_category.argtypes      = [ctypes.c_uint32]
_lib.get_category.restype       = ctypes.c_char_p

# 5) Fetch categories into a Python list
_category_count = _lib.get_category_count()
_cats_ptr       = _lib.get_categories()
categories      = [ _cats_ptr[i].decode('utf-8') for i in range(_category_count) ]

# 6) Check matching label count
_model_count = _lib.get_label_count()
assert _model_count == _category_count, \
    f"Model outputs ({_model_count}) != category list ({_category_count})"

# 7) Build the set of labels to ignore
_ignore = {"silence", "background", "unknown"}
ignore_labels = {c for c in categories if c in _ignore}

# 7) Prepare buffers
_INPUT_SIZE = _lib.get_input_frame_size()
_PROBS     = np.zeros(_model_count, dtype=np.float32)
_LABEL_IDX = ctypes.c_uint32()

def get_available_labels() -> list[str]:
    """
    Return a copy of all the labels that the inference model can emit.
    """
    return categories.copy()

def get_available_commands() -> list[str]:
    """
    Return all the model’s labels except the special ones
    'background', 'silence' and 'unknown'.
    """
    return [lbl for lbl in categories if lbl not in ignore_labels]

def classify(buffer_int16: np.ndarray) -> np.ndarray:
    """Run one inference over exactly INPUT_SIZE int16 samples."""
    if buffer_int16.dtype != np.int16 or buffer_int16.size != _INPUT_SIZE:
        raise ValueError(f"classify() expects { _INPUT_SIZE } int16 samples")
    # ensure contiguous
    arr = np.ascontiguousarray(buffer_int16)
    res = _lib.run_pageturner_inference_c(
        arr.ctypes.data_as(ctypes.POINTER(ctypes.c_int16)),
        ctypes.c_uint32(_INPUT_SIZE),
        _PROBS.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
        ctypes.byref(_LABEL_IDX)
    )
    if res != 0:
        raise RuntimeError(f"Inference failed ({res})")
    return _PROBS
