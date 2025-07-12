from datetime import datetime
from datetime import timedelta
import json

class DeviceSessionManager:
    def __init__(self, device_registry, client_connections):
        self.sessions = {}
        self.ws_connections = {}
        self.device_registry = device_registry
        self.client_connections = client_connections
    
    def update_recording_history(self, chip_id, filename):
        """Update the last recording info for a device"""
        if chip_id not in self.sessions:
            self.sessions[chip_id] = {
                'language': 'Deutsch',
                'label': 'All Labels',
                'last_active': datetime.now(),
                'last_recorded_filename': None,
                'last_recording_timestamp': None
            }
        
        self.sessions[chip_id].update({
            'last_recorded_filename': filename,
            'last_recording_timestamp': datetime.now().isoformat(),
            'last_active': datetime.now()
        })

    def get_or_create_session(self, chip_id):
        """Get existing session or create a new one"""
        if chip_id not in self.sessions:
            self.sessions[chip_id] = {
                'language': 'Deutsch',  # default
                'label': 'All Labels',  # default
                'last_active': datetime.now()
            }
        return self.sessions[chip_id]
    
    def update_language(self, chip_id, language):
        session = self.get_or_create_session(chip_id)
        session['language'] = language
        session['last_active'] = datetime.now()
    
    def update_label(self, chip_id, label):
        session = self.get_or_create_session(chip_id)
        session['label'] = label
        session['last_active'] = datetime.now()
    
    def cleanup_inactive_sessions(self, max_inactive_minutes=60):
        """Remove sessions that haven't been active for a while"""
        cutoff = datetime.now() - timedelta(minutes=max_inactive_minutes)
        inactive = [chip_id for chip_id, sess in self.sessions.items() 
                   if sess['last_active'] < cutoff]
        for chip_id in inactive:
            del self.sessions[chip_id]

    def register_ws_connection(self, device_id, ws):
        print(f"Registering WebSocket for device {device_id}")
        if device_id not in self.ws_connections:
            self.ws_connections[device_id] = set()
        self.ws_connections[device_id].add(ws)
        print(f"Current WebSocket connections: {len(self.ws_connections.get(device_id, set()))}")

    def unregister_ws_connection(self, device_id, ws):
        print(f"Unregistering WebSocket for device {device_id}")
        if device_id in self.ws_connections:
            self.ws_connections[device_id].discard(ws)

    # In broadcast_device_update method:
    def broadcast_device_update(self, device_id, data):
        if device_id in self.ws_connections:
            for ws in list(self.ws_connections[device_id]):
                try:
                    ws.send(json.dumps(data))
                except Exception as e:
                    print(f"Error sending WebSocket message: {str(e)}")
                    self.ws_connections[device_id].discard(ws)


    def broadcast_device_list(self):
        devices = []
        for chip_id, data in self.device_registry.items():
            devices.append({
                'id': chip_id,
                'owner': data.get('owner', 'Unknown')
            })
        
        devices.sort(key=lambda x: x['owner'].lower())
        
        update = {
            'type': 'device_list',
            'devices': devices
        }
        
        for ws in list(self.client_connections):
            try:
                ws.send(json.dumps(update))
            except:
                self.client_connections.discard(ws)