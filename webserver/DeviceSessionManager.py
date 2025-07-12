from datetime import datetime
from datetime import timedelta

class DeviceSessionManager:
    def __init__(self):
        self.sessions = {}  # chip_id -> session data
    
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

