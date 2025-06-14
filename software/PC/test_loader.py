from tflite_model_maker.audio_classifier import DataLoader
from pathlib import Path

data = DataLoader.from_folder(Path("samples"))  # passe ggf. den Pfad an
print("✅ Data loaded:", data.size)