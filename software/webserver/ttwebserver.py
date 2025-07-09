from datetime import datetime
import math
from flask import Flask, render_template, jsonify, request, send_from_directory
import os
import librosa
from threading import Thread
import time


app = Flask(__name__)

# Configuration
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATASET_DIR = os.path.join(BASE_DIR, '../dataset')
TRAINING_DIR = os.path.join(BASE_DIR, '../trainingdataset')

# Check if dataset directory exists
if not os.path.exists(DATASET_DIR):
    raise FileNotFoundError(f"Dataset directory not found at {DATASET_DIR}. Please create it.")



app = Flask(__name__, static_folder='static')

# Add these routes to serve JS files
@app.route('/js/<path:filename>')
def serve_js(filename):
    return send_from_directory(os.path.join(app.static_folder, 'js'), filename)

@app.route('/')
def index():
    # Verify dataset directory exists before rendering
    if not os.path.exists(DATASET_DIR):
        return render_template('error.html', 
                            message=f"Dataset directory not found at {DATASET_DIR}"), 500
    return render_template('index.html')

@app.route('/favicon.ico')
def favicon():
    return send_from_directory(os.path.join(app.root_path, 'static'),
                               'favicon.ico', mimetype='image/vnd.microsoft.icon')

# Language mappings
LANGUAGE_LABELS = {
    'English': ['Music', 'Speech',  'Silence', 'Background'],
    'Deutsch': ['Musik', 'Sprache', 'Ruhe',    'Geräusche']
}

COMMAND_LABELS = {
    'Deutsch':  ['Weiter', 'Zurück'],
    'English': ['Next',   'Back'],
}

FOLDER_MAPPING = {
     'name':   ['Next', 'Back','Music', 'Speech', 'Silence', 'Background','Weiter', 'Zurück','Musik', 'Sprache', 'Ruhe',   'Geräusche'],
     'label':  ['next', 'back','piano', 'speech', 'silence', 'background','weiter', 'zurück','piano', 'speech',  'silence','background'] ,
     'data_files':  [0]*12,
     'data_length': [0]*12,
     'training_files':  [0]*12,
     'training_length': [0]*12
}

folder_mapping_updated = False

def populate_folder_mapping_stats():
    global FOLDER_MAPPING

    # Create a dictionary to store stats for each unique folder label
    folder_stats = {}
    
    # First pass: collect stats for each unique folder
    for i, folder_label in enumerate(FOLDER_MAPPING['label']):
        if folder_label not in folder_stats:
            # Initialize stats for this folder
            stats = {
                'data_files': 0,
                'data_length': 0,
                'training_files': 0,
                'training_length': 0
            }
            
            # Process dataset files
            dataset_path = os.path.join(DATASET_DIR, folder_label)
            if os.path.exists(dataset_path):
                for filename in os.listdir(dataset_path):
                    if filename.lower().endswith(('.wav', '.mp3')):
                        try:
                            filepath = os.path.join(dataset_path, filename)
                            duration = librosa.get_duration(path=filepath)
                            stats['data_files'] += 1
                            stats['data_length'] += duration
                        except Exception as e:
                            print(f"Error processing {filepath}: {str(e)}")
                            continue
            
            # Process training files (1 second snippets)
            training_path = os.path.join(TRAINING_DIR, folder_label)
            if os.path.exists(training_path):
                file_count = len([f for f in os.listdir(training_path) 
                               if f.lower().endswith('.wav')])
                stats['training_files'] = file_count
                stats['training_length'] = file_count  # 1 second per file
            
            folder_stats[folder_label] = stats
    
    # Second pass: populate all entries in FOLDER_MAPPING
    for i, folder_label in enumerate(FOLDER_MAPPING['label']):
        stats = folder_stats.get(folder_label, {
            'data_files': 0,
            'data_length': 0,
            'training_files': 0,
            'training_length': 0
        })
        
        FOLDER_MAPPING['data_files'][i] = stats['data_files']
        FOLDER_MAPPING['data_length'][i] = stats['data_length']
        FOLDER_MAPPING['training_files'][i] = stats['training_files']
        FOLDER_MAPPING['training_length'][i] = stats['training_length']

    folder_mapping_updated = True;
    print(f"Dataset population done")

    return 

@app.route('/api/dataset-overview')
def dataset_overview():
   
    language = request.args.get('language', 'Deutsch')
    data = []
    
    # Get the correct labels for the requested language
    commands = COMMAND_LABELS.get(language, COMMAND_LABELS['Deutsch'])

    labels = LANGUAGE_LABELS.get(language, LANGUAGE_LABELS['Deutsch'])

    # Combine labels while preserving order (command labels first)
    labels_to_show = commands + labels

    # Create mapping dictionaries
    name_to_folder = dict(zip(FOLDER_MAPPING['name'], FOLDER_MAPPING['label']))
    label_to_index = {label: i for i, label in enumerate(FOLDER_MAPPING['label'])}


    for label in labels_to_show:
        # Get corresponding folder name from mapping
        folder_name = name_to_folder.get(label, label.lower())
        
        # Find the index in FOLDER_MAPPING for this label
        index = label_to_index.get(folder_name)
        if index is None:
            # If label not found, use default values
            dataset_count = 0
            dataset_duration = 0
            training_count = 0
            training_duration = 0
        else:
            # Get pre-calculated values from FOLDER_MAPPING
            dataset_count = FOLDER_MAPPING['data_files'][index]
            dataset_duration = FOLDER_MAPPING['data_length'][index]
            training_count = FOLDER_MAPPING['training_files'][index]
            training_duration = FOLDER_MAPPING['training_length'][index]
        
        data.append({
            'id': len(data) + 1,
            'label': label,
            'dataset_count': dataset_count,
            'dataset_duration': round(dataset_duration),  
            'training_count': training_count,
            'training_duration': round(training_duration)  # 60 files = 1 minute
        })
    
    print(data)
    return jsonify(data)

@app.route('/api/audio-files')
def audio_files():
    if not os.path.exists(DATASET_DIR):
        return jsonify({
            'error': True,
            'message': f"Dataset directory not found at {DATASET_DIR}"
        }), 500

    try:
        label = request.args.get('label', 'All Labels')
        page = int(request.args.get('page', 1))
        page_size = int(request.args.get('page_size', 10))
        
        files = []
        if label == 'All Labels':
            for label_dir in os.listdir(DATASET_DIR):
                label_path = os.path.join(DATASET_DIR, label_dir)
                if os.path.isdir(label_path):
                    for file in os.listdir(label_path):
                        if file.lower().endswith(('.wav', '.mp3')):
                            stat = os.stat(os.path.join(label_path, file))
                            files.append({
                                'name': file,
                                'modified': datetime.fromtimestamp(stat.st_mtime).strftime('%d.%m.%y %H:%M:%S'),
                                'samples': '16000',
                                'audioSrc': f'/dataset/{label_dir}/{file}'
                            })
        else:
            label_path = os.path.join(DATASET_DIR, label)
            if os.path.exists(label_path):
                for file in os.listdir(label_path):
                    if file.lower().endswith(('.wav', '.mp3')):
                        stat = os.stat(os.path.join(label_path, file))
                        files.append({
                            'name': file,
                            'modified': datetime.fromtimestamp(stat.st_mtime).strftime('%d.%m.%y %H:%M:%S'),
                            'samples': '16000',
                            'audioSrc': f'/dataset/{label}/{file}'
                        })
        
        total = len(files)
        pages = math.ceil(total / page_size)
        start = (page - 1) * page_size
        end = start + page_size
        
        return jsonify({
            'data': files[start:end],
            'total': total,
            'pages': pages,
            'page': page
        })
    
    except Exception as e:
        return jsonify({
            'error': True,
            'message': f"Error loading audio files: {str(e)}"
        }), 500

@app.route('/api/status')
def status():
    message = request.args.get('message', '')
    is_error = request.args.get('is_error', 'false') == 'true'
    return jsonify({
        'message': message,
        'is_error': is_error
    })

if __name__ == '__main__':
    try:
        # Check directories exist
        if not os.path.exists(DATASET_DIR):
            print(f"ERROR: Dataset directory not found at {DATASET_DIR}")
            print("Please create the directory and populate it with audio files")
        
        if not os.path.exists(TRAINING_DIR):
            print(f"WARNING: Training directory not found at {TRAINING_DIR}")
            print("Some features may not work without training data")
        
        # calculate the content
        Thread(target=populate_folder_mapping_stats, daemon=True).start()

        app.run(host="0.0.0.0", port=8000, debug=True)
    except Exception as e:
        print(f"Failed to start server: {str(e)}")