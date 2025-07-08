from flask import Flask, request, render_template_string, send_file, redirect, url_for, abort, flash
import os, io, wave
import simpleaudio as sa
from datetime import datetime
import glob

app = Flask(__name__)
app.secret_key = "change_this_to_a_random_secret"

# Corrected path to the dataset directory
DATASET_BASE = os.path.abspath(os.path.join(os.path.dirname(__file__), '../dataset'))

# allowable labels
ALLOWED_LABELS = [
    "back", "next", "weiter", "zurück",
    "metronome", "silence", "background", "speech"
]
# current label state
current_label = None

# audio params
SAMPLE_RATE      = 16000
NUM_CHANNELS     = 1
BYTES_PER_SAMPLE = 2

@app.route("/", methods=["GET"])
def index():
    # Define both dataset paths
    DATASET_PATHS = {
        "dataset": os.path.abspath(os.path.join(os.path.dirname(__file__), '../dataset')),
        "trainingdataset": os.path.abspath(os.path.join(os.path.dirname(__file__), '../trainingdataset'))
    }
    
    # Initialize data structures
    snippets = []
    dataset_stats = {}
    
    # First get all label directories from the main dataset
    label_dirs = [d for d in os.listdir(DATASET_PATHS['dataset']) 
                 if os.path.isdir(os.path.join(DATASET_PATHS['dataset'], d))]
    
    # Process both datasets for statistics
    for dataset_name, dataset_path in DATASET_PATHS.items():
        label_counts = {}
        total_files = 0
        
        # Count files for each label
        for label in ALLOWED_LABELS:
            label_dir = os.path.join(dataset_path, label)
            if os.path.exists(label_dir):
                wav_files = glob.glob(os.path.join(label_dir, "*.wav"))
                mp3_files = glob.glob(os.path.join(label_dir, "*.mp3"))
                label_counts[label] = len(wav_files) + len(mp3_files)
                total_files += label_counts[label]
            else:
                label_counts[label] = 0
        
        dataset_stats[dataset_name] = {
            'label_counts': label_counts,
            'total_files': total_files
        }
    
    # If a label is selected, only scan that directory in the main dataset
    scan_labels = [current_label] if current_label else label_dirs
    
    for label in scan_labels:
        # Get all audio files for this label from main dataset
        label_dir = os.path.join(DATASET_PATHS['dataset'], label)
        if not os.path.exists(label_dir):
            continue
            
        # Find all audio files in this directory
        audio_files = glob.glob(os.path.join(label_dir, "*.wav")) + \
                      glob.glob(os.path.join(label_dir, "*.mp3"))
        
        for file_path in audio_files:
            filename = os.path.basename(file_path)
            try:
                snippets.append({
                    "path": file_path,
                    "name": filename,
                    "label": label,
                    "size": os.path.getsize(file_path),
                    "ts": os.path.getmtime(file_path)
                })
            except OSError as e:
                app.logger.error(f"Error reading {file_path}: {str(e)}")
    
    # Sort by timestamp (most recent first)
    snippets.sort(key=lambda x: x['ts'], reverse=True)
    
    # Convert timestamps to readable format
    for s in snippets:
        s['ts'] = datetime.fromtimestamp(s['ts']).strftime("%Y-%m-%d %H:%M:%S")
    
    # Render the template
    return render_template_string("""
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Tiny Turner</title>
    <style>
        :root {
            --primary: #4361ee;
            --secondary: #3f37c9;
            --success: #4cc9f0;
            --danger: #f72585;
            --light: #f8f9fa;
            --dark: #212529;
            --gray: #6c757d;
            --border: #dee2e6;
            --shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        
        body {
            background-color: #f5f7fb;
            color: var(--dark);
            line-height: 1.6;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }
        
        header {
            text-align: center;
            margin-bottom: 30px;
            padding-bottom: 20px;
            border-bottom: 1px solid var(--border);
        }
        
        h1 {
            color: var(--primary);
            font-size: 2.5rem;
            margin-bottom: 10px;
        }
        
        .status-bar {
            background-color: var(--light);
            padding: 15px;
            border-radius: 8px;
            margin-bottom: 25px;
            box-shadow: var(--shadow);
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        
        .status-message {
            font-weight: 500;
            color: var(--gray);
        }
        
        .status-message.error {
            color: var(--danger);
        }
        
        .control-panel {
            display: flex;
            flex-wrap: wrap;
            gap: 20px;
            margin-bottom: 30px;
        }
        
        .panel-card {
            background: white;
            border-radius: 10px;
            padding: 25px;
            box-shadow: var(--shadow);
            flex: 1;
            min-width: 300px;
        }
        
        .panel-title {
            font-size: 1.2rem;
            color: var(--secondary);
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 1px solid var(--border);
        }
        
        .label-selector {
            display: flex;
            flex-direction: column;
            gap: 15px;
        }
        
        .label-display {
            font-size: 1.1rem;
            padding: 10px;
            background-color: #e9ecef;
            border-radius: 5px;
            font-weight: 600;
        }
        
        .label-display.active {
            background-color: #d1e7dd;
            color: #0a3622;
        }
        
        .combo-container {
            display: flex;
            gap: 10px;
        }
        
        select {
            flex: 1;
            padding: 10px;
            border: 1px solid var(--border);
            border-radius: 5px;
            font-size: 1rem;
            background-color: white;
        }
        
        button {
            padding: 10px 20px;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 1rem;
            font-weight: 600;
            transition: all 0.3s ease;
        }
        
        .btn-primary {
            background-color: var(--primary);
            color: white;
        }
        
        .btn-primary:hover {
            background-color: var(--secondary);
        }
        
        .btn-outline {
            background-color: transparent;
            border: 1px solid var(--primary);
            color: var(--primary);
        }
        
        .btn-outline:hover {
            background-color: var(--primary);
            color: white;
        }
        
        .btn-danger {
            background-color: var(--danger);
            color: white;
        }
        
        .btn-danger:hover {
            background-color: #d1146a;
        }
        
        .action-buttons {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin-top: 15px;
        }
        
        .snippets-container {
            background: white;
            border-radius: 10px;
            padding: 25px;
            box-shadow: var(--shadow);
            margin-bottom: 30px;
            display: flex;
            flex-direction: column;
            height: 600px;
        }
        
        .snippets-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
            flex-shrink: 0;
        }
        
        .snippets-window {
            flex: 1;
            overflow-y: auto;
            border: 1px solid var(--border);
            border-radius: 8px;
        }
        
        .table-container {
            position: relative;
            overflow: hidden;
            width: 100%;
        }
        
        table {
            width: 100%;
            border-collapse: collapse;
        }
        
        thead {
            position: sticky;
            top: 0;
            z-index: 10;
        }
        
        th, td {
            padding: 12px 15px;
            text-align: left;
            border-bottom: 1px solid var(--border);
        }
        
        th {
            background-color: #f1f3f9;
            color: var(--secondary);
            font-weight: 600;
        }
        
        tr:hover {
            background-color: #f8f9fa;
        }
        
        audio {
            width: 100%;
            max-width: 250px;
        }
        
        .action-cell {
            display: flex;
            gap: 5px;
        }
        
        .badge {
            display: inline-block;
            padding: 3px 8px;
            border-radius: 12px;
            font-size: 0.8rem;
            font-weight: 600;
        }
        
        .badge-primary {
            background-color: #e0e7ff;
            color: var(--primary);
        }
        
        .badge-success {
            background-color: #d1fae5;
            color: #047857;
        }
        
        .badge-danger {
            background-color: #fee2e2;
            color: #b91c1c;
        }
        
        .badge-info {
            background-color: #dbeafe;
            color: #1d4ed8;
        }
        
        @media (max-width: 768px) {
            .control-panel {
                flex-direction: column;
            }
            
            .combo-container {
                flex-direction: column;
            }
            
            th, td {
                padding: 8px 10px;
            }
            
            .snippets-container {
                height: auto;
                max-height: 70vh;
            }
        }

        .label-counts {
            display: flex;
            gap: 10px;
            flex-wrap: wrap;
            margin: 0 15px;
            flex-grow: 1;
        }

        .label-count {
            font-size: 0.9rem;
            color: var(--dark);
            background-color: #e9ecef;
            padding: 2px 8px;
            border-radius: 4px;
        }

        .label-count:nth-child(1) { background-color: #e0e7ff; }
        .label-count:nth-child(2) { background-color: #d1fae5; }
        .label-count:nth-child(3) { background-color: #fee2e2; }
        .label-count:nth-child(4) { background-color: #ede9fe; }
        .label-count:nth-child(5) { background-color: #fce7f3; }
        .label-count:nth-child(6) { background-color: #e0f2fe; }
        .label-count:nth-child(7) { background-color: #f0fdf4; }
        .label-count:nth-child(8) { background-color: #fef2f2; }

        .status-bar {
            /* Update existing status-bar style */
            flex-wrap: wrap;
            gap: 10px;
        }
        .dataset-stats {
            flex-grow: 1;
            margin-right: 15px;
        }

        .dataset-line {
            margin-bottom: 5px;
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            gap: 5px;
        }

        .dataset-line strong {
            margin-right: 10px;
            min-width: 120px;
        }

        .label-count {
            font-size: 0.85rem;
            color: var(--dark);
            background-color: #e9ecef;
            padding: 2px 6px;
            border-radius: 4px;
            white-space: nowrap;
        }

        .total-count {
            margin-left: auto;
            font-weight: bold;
            font-size: 0.9rem;
            color: var(--primary);
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Tiny Turner</h1>
            <p>Jochen Alt</p>
        </header>
        
        <div class="status-bar">
            <div class="dataset-stats">
                <div class="dataset-line">
                    <strong>Labels in dataset:</strong>
                    {% for label in allowed %}
                        {% if dataset_stats['dataset']['label_counts'].get(label, 0) > 0 %}
                            <span class="label-count">{{ label }}({{ dataset_stats['dataset']['label_counts'].get(label, 0) }})</span>
                        {% endif %}
                    {% endfor %}
                    <span class="total-count">{{ dataset_stats['dataset']['total_files'] }} files</span>
                </div>
                <div class="dataset-line">
                    <strong>Labels in training:</strong>
                    {% for label in allowed %}
                        {% if dataset_stats['trainingdataset']['label_counts'].get(label, 0) > 0 %}
                            <span class="label-count">{{ label }}({{ dataset_stats['trainingdataset']['label_counts'].get(label, 0) }})</span>
                        {% endif %}
                    {% endfor %}
                    <span class="total-count">{{ dataset_stats['trainingdataset']['total_files'] }} files</span>
                </div>
            </div>
            <div class="snippet-count">
                <span class="badge badge-info">{{ snippets|length }} shown</span>
            </div>
        </div>
        
        <div class="control-panel">
            <div class="panel-card">
                <h2 class="panel-title">Label Selection</h2>
                <div class="label-selector">
                    <div class="label-display {{ 'active' if current_label else '' }}">
                        Current Label: {{ current_label or 'None selected' }}
                    </div>
                    
                    <div class="combo-container">
                        <select name="label" id="label" onchange="window.location.href='{{ url_for('set_label') }}?label='+this.value">
                            <option value="">-- Select a label --</option>
                            {% for lbl in allowed %}
                                <option value="{{ lbl }}" {% if lbl == current_label %}selected{% endif %}>
                                    {{ lbl }}
                                </option>
                            {% endfor %}
                        </select>
                    </div>
                </div>
    
                {% if current_label %}
                <div class="action-buttons">
                    <form action="{{ url_for('clear_label') }}" method="post">
                        <button type="submit" class="btn-outline">Clear Label</button>
                    </form>
                </div>
                {% endif %}
            </div>
        </div>
        
        <div class="snippets-container">
            <div class="snippets-header">
                <h2 class="panel-title">Dataset Audio Files</h2>
                <div class="snippet-count">
                    <span class="badge badge-primary">{{ total_files }} files</span>
                    <span class="badge badge-info">{{ snippets|length }} shown</span>
                </div>
            </div>
            
            <div class="snippets-window">
                {% if snippets %}
                <div class="table-container">
                    <table>
                        <thead>
                            <tr>
                                <th>Label</th>
                                <th>Filename</th>
                                <th>Modified</th>
                                <th>Size</th>
                                <th>Playback</th>
                            </tr>
                        </thead>
                        <tbody>
                            {% for s in snippets %}
                                <tr>
                                    <td><span class="badge badge-primary">{{ s.label }}</span></td>
                                    <td>{{ s.name }}</td>
                                    <td>{{ s.ts }}</td>
                                    <td>{{ (s.size / 1024)|round(2) }} KB</td>
                                    <td>
                                        <audio controls preload="none">
                                            <source src="{{ url_for('get_audio', filename=s.label + '/' + s.name) }}" type="{% if s.name.endswith('.mp3') %}audio/mpeg{% else %}audio/wav{% endif %}">
                                            Your browser does not support the audio element.
                                        </audio>
                                    </td>
                                </tr>
                            {% endfor %}
                        </tbody>
                    </table>
                </div>
                {% else %}
                <div style="display: flex; justify-content: center; align-items: center; height: 100%;">
                    <p>No audio files found in dataset directory. Check your directory structure.</p>
                </div>
                {% endif %}
            </div>
        </div>
    </div>
    
    <script>
        // No need for the change handler anymore since we're using onchange in the select
    </script>
</body>
</html>
""", snippets=snippets, current_label=current_label, 
        allowed=ALLOWED_LABELS, dataset_stats=dataset_stats,
        dataset_path=DATASET_PATHS['dataset'])

@app.route("/set_label", methods=["GET"])
def set_label():
    global current_label
    lbl = request.args.get("label", "").strip()
    if lbl and lbl not in ALLOWED_LABELS:
        flash(f"Invalid label: {lbl}", "error")
    else:
        current_label = lbl if lbl else None
        if lbl:
            flash(f"Label set to: {lbl}", "success")
        else:
            flash("Label cleared", "success")
    return redirect(url_for("index"))

@app.route("/clear_label", methods=["POST"])
def clear_label():
    global current_label
    current_label = None
    flash("Label cleared", "success")
    return redirect(url_for("index"))

@app.route("/upload", methods=["POST"])
def upload():
    global current_label
    if not current_label:
        return "Error: no label selected, snippet ignored", 400

    chunk = request.data
    if not chunk:
        return "Error: empty payload", 400

    # play it back
    try:
        sa.play_buffer(chunk,
                       num_channels=NUM_CHANNELS,
                       bytes_per_sample=BYTES_PER_SAMPLE,
                       sample_rate=SAMPLE_RATE)
    except Exception as e:
        app.logger.error(f"Playback error: {e}")

    # Save to the appropriate dataset directory
    label_dir = os.path.join(DATASET_BASE, current_label)
    os.makedirs(label_dir, exist_ok=True)
    
    # Create filename with timestamp
    ts = datetime.utcnow().strftime("%Y%m%d-%H%M%S-%f")
    fname = f"{ts}.wav"
    path = os.path.join(label_dir, fname)

    try:
        with wave.open(path, "wb") as wf:
            wf.setnchannels(NUM_CHANNELS)
            wf.setsampwidth(BYTES_PER_SAMPLE)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(chunk)
    except Exception as e:
        app.logger.error(f"File write error: {e}")
        return "Error writing file", 500

    app.logger.info(f"Saved snippet to dataset: {path}")
    return "OK", 200
@app.route("/audio/<path:filename>")
def get_audio(filename):
    # Security check to prevent path traversal
    safe_path = os.path.abspath(os.path.join(DATASET_BASE, filename))
    dataset_base = os.path.abspath(DATASET_BASE)
    
    if not safe_path.startswith(dataset_base):
        app.logger.error(f"Attempted access outside dataset: {filename}")
        abort(403, "Access denied")
    
    if not os.path.isfile(safe_path):
        app.logger.error(f"File not found: {filename}")
        abort(404)
    
    # Determine MIME type based on file extension
    if safe_path.lower().endswith('.mp3'):
        mimetype = 'audio/mpeg'
    else:
        mimetype = 'audio/wav'
    
    # Add headers to prevent caching issues
    response = send_file(
        safe_path,
        mimetype=mimetype,
        as_attachment=False,
        conditional=True
    )
    response.headers['Cache-Control'] = 'no-cache, no-store, must-revalidate'
    response.headers['Pragma'] = 'no-cache'
    response.headers['Expires'] = '0'
    return response

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000)