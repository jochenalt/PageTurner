from datetime import datetime
import math
from flask import Flask, render_template, jsonify, request, send_from_directory, send_file
import os
import librosa
from threading import Thread
import time
import shutil
from pydub import AudioSegment
from datetime import datetime


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

# Add this near the top with other global variables
device_registry = {}  # Global dictionary to store all devices by chip_id


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


def optimise_dataset():
    """Copy and convert files from ./dataset to ./trainingdataset, splitting >1s files into 1s WAV segments"""
    
    # Constants
    SEGMENT_LENGTH_MS = 1000  # 1 second in milliseconds
    TARGET_SAMPLE_RATE = 16000

    # Clear existing training dataset
    if os.path.isdir(TRAINING_DIR):
        shutil.rmtree(TRAINING_DIR)
    
    # Get unique folders from FOLDER_MAPPING
    unique_folders = {}
    for folder in FOLDER_MAPPING['label']:
        unique_folders[folder] = True  # Using dict keys for uniqueness
    
    # Process each unique label
    for label in unique_folders.keys():
        src_dir = os.path.join(DATASET_DIR, label)
        dest_dir = os.path.join(TRAINING_DIR, label)
        
        app.logger.info(f"Processing {src_dir} -> {dest_dir}")

        if not os.path.exists(src_dir):
            app.logger.info(f"Skipping non-existent source directory: {src_dir}")
            continue
            
        os.makedirs(dest_dir, exist_ok=True)
        file_counter = 0  # Counter for naming split files
        
        # Process each audio file
        for filename in os.listdir(src_dir):
            src_path = os.path.join(src_dir, filename)
            base_name = os.path.splitext(filename)[0]
            
            try:
                # Load audio file (handles both WAV and MP3)
                if filename.lower().endswith('.mp3'):
                    audio = AudioSegment.from_mp3(src_path)
                elif filename.lower().endswith('.wav'):
                    audio = AudioSegment.from_wav(src_path)
                else:
                    continue  # Skip non-audio files
                
                # Standardize format: mono, 16-bit, 16kHz
                audio = audio.set_channels(1)
                audio = audio.set_sample_width(2)  # 16-bit
                audio = audio.set_frame_rate(TARGET_SAMPLE_RATE)
                
                duration_ms = len(audio)
                
                # Split into 1-second segments if longer than 1 second
                if duration_ms > SEGMENT_LENGTH_MS:
                    num_segments = int(duration_ms / SEGMENT_LENGTH_MS)
                    for i in range(num_segments):
                        start = i * SEGMENT_LENGTH_MS
                        end = start + SEGMENT_LENGTH_MS
                        segment = audio[start:end]
                        
                        # Save as WAV with timestamp and counter
                        timestamp = datetime.now().strftime("%Y%m%d%H%M%S")
                        segment_path = os.path.join(dest_dir, 
                                                  f"{label}.{timestamp}.{file_counter:04d}.wav")
                        segment.export(segment_path, format="wav")
                        file_counter += 1
                else:
                    # Save as WAV if <= 1 second
                    timestamp = datetime.now().strftime("%Y%m%d%H%M%S")
                    segment_path = os.path.join(dest_dir, 
                                              f"{label}.{timestamp}.{file_counter:04d}.wav")
                    audio.export(segment_path, format="wav")
                    file_counter += 1
                
                
            except Exception as e:
                app.logger.error(f"Error processing {src_path}: {str(e)}")
                continue
    
    # Count and log results
    total_files = sum([len(files) for r, d, files in os.walk(TRAINING_DIR)])
    app.logger.info(f"Optimization complete. Created {total_files} WAV segments in {TRAINING_DIR}")

    # After processing, update the folder mapping
    populate_folder_mapping_stats()
    return True

@app.route('/api/optimize-dataset', methods=['POST'])
def api_optimize_dataset():
    try:
        # Run optimization
        success = optimise_dataset()
        if success:
            return jsonify({
                'success': True,
                'message': 'Dataset optimization completed successfully'
            })
        else:
            return jsonify({
                'success': False,
                'message': 'Dataset optimization failed'
            }), 500
    except Exception as e:
        return jsonify({
            'success': False,
            'message': f'Error during optimization: {str(e)}'
        }), 500

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
            'training_duration': round(training_duration) 
        })
    
    return jsonify(data)

@app.route('/api/audio-files')
def audio_files():
    try:
        language = request.args.get('language', 'Deutsch')
        label = request.args.get('label', 'All Labels')
        
        # Get all valid labels for the current language
        commands = COMMAND_LABELS.get(language, COMMAND_LABELS['Deutsch'])
        labels = LANGUAGE_LABELS.get(language, LANGUAGE_LABELS['Deutsch'])
        all_labels = commands + labels
        
        files = []
        if label == 'All Labels':
            # Get files from all folders in FOLDER_MAPPING that match the current language
            valid_folders = []
            for display_name, folder_name in zip(FOLDER_MAPPING['name'], FOLDER_MAPPING['label']):
                if display_name in all_labels:
                    valid_folders.append(folder_name)
            
            # Remove duplicates while preserving order
            valid_folders = list(dict.fromkeys(valid_folders))
            
            for folder in valid_folders:
                folder_path = os.path.join(DATASET_DIR, folder)
                if os.path.exists(folder_path):
                    for file in os.listdir(folder_path):
                        if file.lower().endswith(('.wav', '.mp3')):
                            file_path = os.path.join(folder_path, file)
                            stat = os.stat(file_path)
                            duration = librosa.get_duration(path=file_path)
                            files.append({
                                'name': file,
                                'modified': datetime.fromtimestamp(stat.st_mtime).strftime('%d.%m.%y %H:%M:%S'),
                                'samples': str(int(duration*16))+'k',
                                'duration': str(round(duration, 2))+'s',
                                'audioSrc': f'/dataset/{folder}/{file}'
                            })
        else:
            # Find the corresponding folder name in FOLDER_MAPPING
            folder_name = None
            for display_name, folder in zip(FOLDER_MAPPING['name'], FOLDER_MAPPING['label']):
                if display_name == label:
                    folder_name = folder
                    break
            
            if folder_name:
                folder_path = os.path.join(DATASET_DIR, folder_name)

                if os.path.exists(folder_path):
                    for file in os.listdir(folder_path):
                        if file.lower().endswith(('.wav', '.mp3')):
                            file_path = os.path.join(folder_path, file)
                            stat = os.stat(file_path)
                            duration = librosa.get_duration(path=file_path)
                            files.append({
                                'name': file,
                                'modified': datetime.fromtimestamp(stat.st_mtime).strftime('%d.%m.%y %H:%M:%S'),
                                'samples': str(int(duration*16))+'k',
                                'duration': str(round(duration, 2))+'s',
                                'audioSrc': f'/dataset/{folder_name}/{file}'
                            })
        
        # Sort by modification date (newest first)
        files.sort(key=lambda x: x['modified'], reverse=True)
        
        return jsonify({
            'data': files,
            'total': len(files)
        })
    
    except Exception as e:
        return jsonify({
            'error': True,
            'message': f"Error loading audio files: {str(e)}"
        }), 500


@app.route('/dataset/<path:folder>/<path:filename>')
def serve_audio(folder, filename):
    try:
        # Security check to prevent directory traversal
        if '..' in folder or '..' in filename:
            raise ValueError("Invalid path")
            
        file_path = os.path.join(DATASET_DIR, folder, filename)
        
        # Check if file exists and is an audio file
        if not os.path.exists(file_path) or not filename.lower().endswith(('.wav', '.mp3')):
            return "File not found", 404
            
        return send_file(file_path)
        
    except Exception as e:
        return str(e), 500

@app.route('/api/status')
def status():
    message = request.args.get('message', '')
    is_error = request.args.get('is_error', 'false') == 'true'
    return jsonify({
        'message': message,
        'is_error': is_error
    })

# service to add device information
@app.route('/api/device-info', methods=['POST'])
def handle_device_info():
    global device_registry

    try:
        # Get raw binary data
        raw_data = request.data

        
        # First byte is command, rest is the message
        if len(raw_data) < 1:
            return jsonify({'error': 'No data received'}), 400
            
        command = raw_data[0]
        message = raw_data[1:].decode('utf-8', errors='replace').strip()
        
        # Temporary dict for this update
        device_data = {}
        current_key = None
        current_value = None
        in_quote = False
        
        # Parse key:"value" pairs
        i = 0
        while i < len(message):
            if message[i] == '"' and not in_quote:
                in_quote = True
                current_value = ""
                i += 1
            elif message[i] == '"' and in_quote:
                in_quote = False
                if current_key:
                    device_data[current_key] = current_value
                i += 1
            elif in_quote:
                current_value += message[i]
                i += 1
            else:
                if message[i] == ' ':
                    i += 1
                    continue
                    
                key_end = message.find(':', i)
                if key_end == -1:
                    break
                    
                current_key = message[i:key_end].strip()
                i = key_end + 1

        # Ensure we have a chip ID (use MAC if missing)
        chip_id = device_data.get('chipid')
        if not chip_id:
            return jsonify({'error': 'No device identifier found'}), 400
        
        # Add timestamp
        device_data['last_seen'] = datetime.now().isoformat()
        
        # Update or create device entry
        if chip_id not in device_registry:
            device_registry[chip_id] = device_data
            print(f"New device registered: {chip_id}")
        else:
            device_registry[chip_id].update(device_data)
        print(f"registerd")
        
        # Log the update
        print(f"\n=== Device Update [{chip_id}] ===")
        print(f"Command: {command}")
        for key, value in device_data.items():
            print(f"{key}: {value}")
        print(f"Total devices registered: {len(device_registry)}")
        print("=================================")
        
        return jsonify({
            'success': True,
            'message': 'Device info received',
            'device_id': chip_id,
            'data': device_data
        })
        
    except Exception as e:
        print(f"Error processing device info: {str(e)}")
        return jsonify({
            'error': True,
            'message': f'Error processing device info: {str(e)}'
        }), 500

@app.route('/api/devices')
def get_devices():
    try:
        # Create list of devices with owner and chip_id
        devices = []
        for chip_id, data in device_registry.items():
            devices.append({
                'id': chip_id,  # Internal identifier
                'owner': data.get('owner', 'Unknown')
            })
        
        # Sort by owner name
        devices.sort(key=lambda x: x['owner'].lower())
        
        print("cmd: {devices}")
        return jsonify({
            'success': True,
            'devices': devices
        })
    except Exception as e:
        return jsonify({
            'error': True,
            'message': f'Failed to get devices: {str(e)}'
        }), 500
    

# Full device details (called when selected)
@app.route('/api/device/device=<device_id>')
def get_device_details(device_id):
    try:
        device = device_registry.get(device_id)
        if not device:
            print(f"device {device_id} not found")
            return jsonify({'error: Device not found ': device_id }), 404
            
        s =   jsonify({
            'id': device_id,
            'owner': device.get('owner', 'Unknown'),
            'board': device.get('board', 'Unknown'),
            'flash': device.get('flash', 'Unknown'),
            'psram': device.get('psram', 'Unknown'),
            'heap': device.get('freeheap', 'Unknown'),
            'version': device.get('version', 'Unknown'),
            'last_seen': device.get('last_seen')
        })
        print(f"device: {s}")
        return s
    except Exception as e:
        return jsonify({'error': str(e)}), 500



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