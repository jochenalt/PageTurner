from flask import Flask, request, render_template_string, send_file, redirect, url_for
import os, io, wave
import simpleaudio as sa
from datetime import datetime

app = Flask(__name__)

# directory to store individual snippets
SNIPPET_DIR      = "snippets"
os.makedirs(SNIPPET_DIR, exist_ok=True)

# audio parameters (must match ESP32 client)
SAMPLE_RATE      = 16000   # Hz
NUM_CHANNELS     = 1       # mono
BYTES_PER_SAMPLE = 2       # 16-bit

# upload endpoint: save chunk as its own file + play immediately
@app.route("/upload", methods=["POST"])
def upload():
    chunk = request.data
    # 1) play immediately (non-blocking)
    try:
        sa.play_buffer(chunk,
                       num_channels=NUM_CHANNELS,
                       bytes_per_sample=BYTES_PER_SAMPLE,
                       sample_rate=SAMPLE_RATE)
    except Exception as e:
        app.logger.error(f"Playback error: {e}")

    # 2) save to its own file named by UTC timestamp
    fname = datetime.utcnow().strftime("%Y%m%d-%H%M%S-%f") + ".raw"
    path = os.path.join(SNIPPET_DIR, fname)
    try:
        with open(path, "wb") as f:
            f.write(chunk)
    except Exception as e:
        app.logger.error(f"File write error: {e}")
        return "Error writing file", 500

    app.logger.info(f"Received {len(chunk)} bytes → saved as {fname}")
    return "OK", 200

# homepage: list, play, delete
@app.route("/")
def index():
    files = sorted(os.listdir(SNIPPET_DIR))
    # build list of dicts with metadata
    snippets = []
    for fn in files:
        full = os.path.join(SNIPPET_DIR, fn)
        size = os.path.getsize(full)
        # parse timestamp back out of filename for display
        ts = fn.rstrip(".raw")
        snippets.append({"name": fn, "size": size, "ts": ts})
    return render_template_string("""
<!doctype html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Tiny Turner</title>
    <style>
      body { font-family: sans-serif; margin: 2em; }
      table { border-collapse: collapse; width: 100%; }
      th, td { padding: 0.5em; border: 1px solid #ccc; text-align: left; }
      th { background: #f0f0f0; }
      audio { width: 200px; }
      form { display: inline; }
    </style>
  </head>
  <body>
    <h1>Tiny Turner</h1>
    <p>Available snippets: {{ snippets|length }}</p>
    <table>
      <tr><th>Timestamp</th><th>Size (bytes)</th><th>Play</th><th>Delete</th></tr>
    {% for s in snippets %}
      <tr>
        <td>{{ s.ts }}</td>
        <td>{{ s.size }}</td>
        <td>
          <audio controls 
                 src="{{ url_for('get_wav', filename=s.name) }}">
            Your browser does not support audio.
          </audio>
        </td>
        <td>
          <form action="{{ url_for('delete', filename=s.name) }}" method="post">
            <button type="submit">🗑️</button>
          </form>
        </td>
      </tr>
    {% endfor %}
    </table>
  </body>
</html>
""", snippets=snippets)

# serve each .raw as a WAV stream
@app.route("/snippet/<filename>")
def get_wav(filename):
    path = os.path.join(SNIPPET_DIR, filename)
    if not os.path.exists(path):
        return "Not found", 404

    raw = open(path, "rb").read()
    buf = io.BytesIO()
    wf = wave.open(buf, "wb")
    wf.setnchannels(NUM_CHANNELS)
    wf.setsampwidth(BYTES_PER_SAMPLE)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(raw)
    wf.close()
    buf.seek(0)

    return send_file(
        buf,
        mimetype="audio/wav",
        as_attachment=False,
        download_name=filename.replace(".raw", ".wav")
    )

# delete endpoint
@app.route("/delete/<filename>", methods=["POST"])
def delete(filename):
    path = os.path.join(SNIPPET_DIR, filename)
    if os.path.exists(path):
        os.remove(path)
    return redirect(url_for("index"))

if __name__ == "__main__":
    # in production, keep this bound to localhost and front with nginx
    app.run(host="0.0.0.0", port=8000)
