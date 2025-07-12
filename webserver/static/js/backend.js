// backend.js

// Add language mappings at the top of the file
const COMMAND_LABELS = {
    'English': ['Next',   'Back'],
    'Deutsch': ['Weiter', 'Zurück']
};

const LANGUAGE_LABELS = {
    'English': ['Music', 'Speech', 'Silence', 'Background'],
    'Deutsch': ['Musik', 'Sprache','Ruhe',   'Geräusche']
};

// Status message handler
function showStatus(message, isError = false) {
    const color = isError ? "#ff5c5c" : "#4CAF50";
    $$("message_box").setHTML(`<div style="color: ${color}">${message}</div>`);
}

// Update all comboboxes based on selected language
function updateComboboxes(language) {
    // Get the command and label names for the selected language
    const commands = COMMAND_LABELS[language] || COMMAND_LABELS['German'];
    const labels = LANGUAGE_LABELS[language] || LANGUAGE_LABELS['Deutsch'];
    
    // Update Recording Panel combobox
    $$("rec_label_filter").define("options", ["No label", ...commands, ...labels]);
    $$("rec_label_filter").refresh();
    
    // Update Audio Dataset Browser combobox
    $$("label_filter").define("options", ["All labels", ...commands, ...labels]);
    $$("label_filter").refresh();
}


// Load dataset overview with error handling
function loadDatasetOverview(language) {
    try {
        webix.ajax().get(`/api/dataset-overview?language=${language}`, {
            success: function(data, xml) {
                const response = xml.json();
                if (response.error) {
                    showStatus(response.message, true);
                    $$("dataset_table").clearAll();
                } else {
                    // Ensure all required fields exist and are numbers
                    const cleanData = response.map(item => ({
                        id: item.id,
                        label: item.label,
                        dataset_count: item.dataset_count || 0,
                        dataset_duration: item.dataset_duration || 0,
                        training_count: item.training_count || 0,
                        training_duration: item.training_duration || 0
                    }));
                    
                    $$("dataset_table").clearAll();
                    $$("dataset_table").parse(cleanData);
                    showStatus("Dataset overview loaded successfully");
                }
            },
            error: function(err) {
                showStatus("Failed to load dataset overview: " + (err.response?.json?.message || err.status), true);
                $$("dataset_table").clearAll();
            }
        });
    } catch (e) {
        console.error("Error in loadDatasetOverview:", e);
    }
}


// Global audio context and player variables
let audioContext = null;
let currentAudioBuffer = null;
let currentAudioSource = null;
let isPlaying = false;
let currentRowId = null;

// Initialize audio player functionality
function initAudioPlayer() {
    // Handle play button clicks
    webix.event($$("audio_table").getNode(), "click", function(e) {
        const target = e.target || e.srcElement;
        
        // Check if play button was clicked
        if (target.classList.contains("play-btn")) {
            const rowElement = target.closest(".audio-player");
            const rowId = rowElement.getAttribute("data-id");
            const item = $$("audio_table").getItem(rowId);
            
            if (item && item.playback) {
                if (isPlaying && currentRowId === rowId) {
                    stopAudio();
                } else {
                    currentRowId = rowId;
                    playAudio(item.playback, rowId);
                }
            }
        }
    });
}

function formatTime(seconds) {
    const mins = Math.floor(seconds / 60);
    const secs = Math.floor(seconds % 60);
    return `${mins}:${secs < 10 ? '0' : ''}${secs}`;
}

// Add this with your other utility functions
function updatePlayButton(rowId, isPlaying) {
    const playBtn = document.querySelector(`.audio-player[data-id="${rowId}"] .play-btn`);
    if (playBtn) {
        playBtn.textContent = isPlaying ? "Stop" : "Play";
        playBtn.style.backgroundColor = isPlaying ? "#f44336" : "#4CAF50";
        
        // Also update the title for accessibility
        playBtn.title = isPlaying ? "Stop playback" : "Play audio";
    }
}


function playAudio(url, rowId) {
    if (!audioContext) {
        try {
            audioContext = new (window.AudioContext || window.webkitAudioContext)();
        } catch (e) {
            showStatus("Web Audio API not supported in this browser", true);
            return;
        }
    }

    if (isPlaying) {
        stopAudio();
    }

    const filename = url.split('/').pop();
    showStatus(`Loading: ${filename}`);

    const audioUrl = `${url}?t=${Date.now()}`;
    
    // Create temporary player element if this is an automatic playback
    let playerElement, slider, timeDisplay;
    if (rowId) {
        const tempPlayer = document.createElement('div');
        tempPlayer.className = 'audio-player';
        tempPlayer.setAttribute('data-id', rowId);
        tempPlayer.innerHTML = `
            <input type="range" class="progress-slider" min="0" max="100" value="0" style="width: 120px; margin: 0 5px;">
            <span class="time-display" style="width: 50px; display: inline-block; text-align: center;">
                0:00 / 0:00
            </span>
        `;
        document.body.appendChild(tempPlayer);
        playerElement = tempPlayer;
        slider = tempPlayer.querySelector('.progress-slider');
        timeDisplay = tempPlayer.querySelector('.time-display');
    } else {
        playerElement = document.querySelector(`.audio-player[data-id="${rowId}"]`);
        slider = playerElement.querySelector('.progress-slider');
        timeDisplay = playerElement.querySelector('.time-display');
    }

    let animationFrameId;
    let startTime;
    let duration;

    fetch(audioUrl)
        .then(response => {
            if (!response.ok) throw new Error(`Server responded with ${response.status}`);
            return response.arrayBuffer();
        })
        .then(arrayBuffer => audioContext.decodeAudioData(arrayBuffer))
        .then(audioBuffer => {
            currentAudioBuffer = audioBuffer;
            currentAudioSource = audioContext.createBufferSource();
            currentAudioSource.buffer = audioBuffer;
            currentAudioSource.connect(audioContext.destination);
            
            duration = audioBuffer.duration;
            if (slider) slider.max = duration;
            if (timeDisplay) timeDisplay.textContent = `0:00 / ${formatTime(duration)}`;
            
            startTime = audioContext.currentTime;
            currentAudioSource.start(0);
            isPlaying = true;
            currentRowId = rowId;
            showStatus(`Playing: ${filename}`);
            
            function updateProgress() {
                if (!isPlaying) return;
                
                const elapsed = audioContext.currentTime - startTime;
                const progress = Math.min(elapsed, duration);
                
                if (slider) slider.value = progress;
                if (timeDisplay) timeDisplay.textContent = `${formatTime(progress)} / ${formatTime(duration)}`;
                
                if (progress < duration) {
                    animationFrameId = requestAnimationFrame(updateProgress);
                }
            }
            
            updateProgress();
            
            currentAudioSource.onended = () => {
                cancelAnimationFrame(animationFrameId);
                if (slider) slider.value = duration;
                if (timeDisplay) timeDisplay.textContent = `${formatTime(duration)} / ${formatTime(duration)}`;
                isPlaying = false;
                currentRowId = null;
                // Remove temporary player element if this was an automatic playback
                if (rowId && playerElement) {
                    playerElement.remove();
                }
            };
        })
        .catch(error => {
            showStatus(`Playback failed: ${error.message}`, true);
            console.error('Audio error:', error);
            // Remove temporary player element if there was an error
            if (rowId && playerElement) {
                playerElement.remove();
            }
        });
}

function stopAudio() {
    if (currentAudioSource) {
        currentAudioSource.stop();
        currentAudioSource = null;
        currentAudioBuffer = null;
        isPlaying = false;
        
        if (currentRowId) {
            const playerElement = document.querySelector(`.audio-player[data-id="${currentRowId}"]`);
            if (playerElement) {
                const slider = playerElement.querySelector('.progress-slider');
                const timeDisplay = playerElement.querySelector('.time-display');
                slider.value = 0;
                timeDisplay.textContent = `0:00 / ${formatTime(slider.max)}`;
            }
            updatePlayButton(currentRowId, false);
            currentRowId = null;
        }
    }
}

function loadAudioFiles() {
    const language = $$("language_filter").getValue();
    const labelFilter = $$("label_filter").getValue();
    
    showStatus("Loading audio files...");
    
    webix.ajax().get(`/api/audio-files?language=${encodeURIComponent(language)}&label=${encodeURIComponent(labelFilter)}`, {
        success: function(data, xml) {
            const response = xml.json();
            if (response.error) {
                showStatus(response.message, true);
                $$("audio_table").clearAll();
            } else {
                // Process and display all files (not just one page)
                const processedData = response.data.map(file => ({
                    id: webix.uid(),
                    name: file.name,
                    modified: file.modified,
                    samples: file.samples,
                    duration: file.duration || 0,
                    playback: file.audioSrc
                }));
                
                $$("audio_table").clearAll();
                $$("audio_table").parse(processedData);
                showStatus(`Loaded ${processedData.length} audio files`);
                
                // Adjust table height if needed
                if (processedData.length > 0) {
                    $$("audio_table").adjustRowHeight();
                }
            }
        },
        error: function(err) {
            showStatus("Failed to load audio files: " + (err.response?.json?.message || err.status), true);
            $$("audio_table").clearAll();
        }
    });
}

function initLanguageFilter() {
    $$("language_filter").attachEvent("onChange", function(newv) {
        updateDeviceLanguage(newv);
        loadDatasetOverview(newv);
        updateComboboxes(newv);
        loadAudioFiles();  // Also reload audio files when language changes
    });

    $$("label_filter").attachEvent("onChange", function(newv) {
        // updateDeviceLabel(newv);
        loadAudioFiles();  // This should call loadAudioFiles, not loadDatasetOverview
    });

    $$("rec_label_filter").attachEvent("onChange", function(newv) {
        updateDeviceLabel(newv);
    });
    
    // Load initial data
    const initialLanguage = $$("language_filter").getValue();
    loadDatasetOverview(initialLanguage);
    updateComboboxes(initialLanguage);
    loadAudioFiles();  // Load initial audio files
}

// Add this function to handle dataset optimization
function optimizeDataset() {
    showStatus("Starting dataset optimization...");
    
    const btn = $$("create_training_btn");
    btn.disable();
    btn.setValue("Processing...");
    
    // Set timeout fallback (5 minutes)
    const timeout = setTimeout(() => {
        showStatus("Optimization timed out", true);
        btn.enable();
        btn.setValue("Create training dataset");
    }, 300000); // 5 minutes
    
    webix.ajax().post("/api/optimize-dataset", {}, {
        success: function(data, xml) {
            clearTimeout(timeout); // Clear the timeout
            const response = xml.json();
            showStatus(response.message, !response.success);
            
            btn.enable();
            btn.setValue("Create training dataset");
            
            if (response.success) {
                setTimeout(() => {
                    loadDatasetOverview($$("language_filter").getValue());
                }, 1000);
            }
        },
        error: function(err) {
            clearTimeout(timeout); // Clear the timeout
            showStatus("Failed to optimize dataset: " + (err.response?.json?.message || err.status), true);
            btn.enable();
            btn.setValue("Create training dataset");
        }
    });
}


// Helper function to format date
function formatDate(isoString) {
    if (!isoString) return "Never";
    const date = new Date(isoString);
    return date.toLocaleString();
}

function formatBytes(bytes) {
    if (isNaN(bytes) || bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    // Fixed concatenation by using template literals
    return `${(bytes / Math.pow(k, i)).toFixed(2)} ${sizes[i]}`;
}

let globalWebSocket = null;  // For device list updates


function updateDeviceList(devices) {
    if (!$$("device_selector")) {
        console.error("device_selector not found");
        return;
    }

    // Create device options
    const deviceOptions = devices.map(device => {
        return {
            id: device.id,
            value: device.id
        };
    });

    // Update device selector
    $$("device_selector").define("options", deviceOptions);
    $$("device_selector").refresh();

    // if we have only one device, select that
    if (devices || devices.length == 1) {
        const singleDevice = devices[0];
        $$("device_selector").setValue(singleDevice.id);
        loadDeviceDetails(singleDevice.id);  // Load details immediately
        manageDeviceWebSocket(singleDevice.id);  // Connect WebSocket
        showStatus(`Auto-selected device: ${singleDevice.id}`);
    } 

    showStatus(`Device list updated (${devices.length} devices)`);
}

function connectGlobalWebSocket() {
    const wsProtocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    const wsUrl = wsProtocol + window.location.host + '/api/ws/device-updates';
    
    globalWebSocket = new WebSocket(wsUrl);
    console.log('Connecting to global WebSocket:', wsUrl);
    
    globalWebSocket.onopen = () => {
        console.log('Global WebSocket connected');
        // Identify as a client (not a device)
        globalWebSocket.send("client");
    };
    
    globalWebSocket.onmessage = (event) => {
        console.log('Global WebSocket message:', event.data);
        try {
            const data = JSON.parse(event.data);
            if (data.type === 'device_list') {
                updateDeviceList(data.devices);
            }
        } catch (e) {
            console.error('Error processing global WebSocket message:', e);
        }
    };
    
    globalWebSocket.onclose = (event) => {
        console.log('Global WebSocket closed, reconnecting...', event.code, event.reason);
        setTimeout(connectGlobalWebSocket, 3000);
    };
    
    globalWebSocket.onerror = (error) => {
        console.error('Global WebSocket error:', error);
    };
}


function updateDeviceInfo(device) {
    $$("device_owner").setValue(device.owner);
    $$("device_chip_id").setValue(device.id);
    $$("device_board").setValue(device.board);
    $$("device_flash").setValue(formatBytes(device.flash));
    $$("device_psram").setValue(formatBytes(device.psram));
    $$("device_heap").setValue(formatBytes(device.heap));
    $$("device_version").setValue(device.version );
    $$("device_lastseen").setValue(formatDate(device.last_seen) );
}

function getCurrentSettings() {
    return {
        language: $$("language_filter").getValue(),
        label: $$("label_filter").getValue()
    };
}


function loadDeviceDetails(deviceId) {
    showStatus(`Loading device ${deviceId}...`);
    const settings = getCurrentSettings();
    
    webix.ajax().headers({
        "X-Current-Language": settings.language,
        "X-Current-Label": settings.label
    }).get(`/api/device/device=${encodeURIComponent(deviceId)}`, {        
        success: function(data, xml) {
            const device = xml.json();
            if (device.error) {
                showStatus("Failed to load device: " + device.message, true);
                return;
            }
            
            updateDeviceInfo(device);
            showStatus(`Device ${deviceId} loaded`);
        },
        error: function(err) {
            const errorMsg = err.response?.json?.message || err.status;
            showStatus("Failed to load device details: " + errorMsg, true);
            console.error("Device load error:", err);
        }
    });
}

// Update language/label handlers to always specify device if available
function updateDeviceLanguage(language) {
    const deviceId = $$("device_chip_id").getValue();
    if (deviceId) {
        webix.ajax().post("/api/session/language", {
            chip_id: deviceId,
            language: language
        });
    }
}


function updateDeviceLabel(label) {
    const deviceId = $$("device_chip_id").getValue();
    if (deviceId) {
        webix.ajax().headers({
            "Content-Type": "application/json"
        }).post("/api/session/label", JSON.stringify({
            chip_id: deviceId,
            label: label
        }), {
            success: function(response) {
                showStatus("Label updated successfully");
            },
            error: function(err) {
                showStatus("Failed to update label: " + (err.response?.json?.message || err.status), true);
            }
        });
    } else {
        showStatus("Error: You need to select a device first", true);
    }
}

// Update the formatRecordingTimestamp function
function formatRecordingTimestamp(timestamp) {
    if (!timestamp) return "Never";
    try {
        const date = new Date(timestamp);
        return date.toLocaleString();
    } catch (e) {
        return "Invalid date";
    }
}


// Add this function to update recording history display
function updateRecordingHistory(deviceId) {
    if (!deviceId) {
        $$("last_recording_time").setValue("No device selected");
        $$("last_recording_path").setValue("");
        return;
    }

    webix.ajax().get(`/api/device/${deviceId}/recording-history`, {
        success: function(data, xml) {
            const history = xml.json();
            if (history.error) {
                showStatus("Failed to load recording history: " + history.message, true);
                return;
            }

            $$("last_recording_time").setValue(
                history.last_timestamp 
                    ? formatRecordingTimestamp(history.last_timestamp)
                    : "No recordings yet"
            );
            
            $$("last_recording_path").setValue(
                history.last_filename 
                    ? `./${history.last_filename}`
                    : ""
            );
        },
        error: function(err) {
            showStatus("Failed to load recording history", true);
        }
    });
}

// Add at the top of backend.js
let deviceWebSocket = null;

function connectWebSocket(deviceId) {
    const wsProtocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    const wsUrl = wsProtocol + window.location.host + '/api/ws/device-updates';
    

    deviceWebSocket = new WebSocket(wsUrl);
    console.log('Attempting WebSocket connection to:', wsUrl);
    deviceWebSocket.onopen = () => console.log('WebSocket connected successfully');
    deviceWebSocket.onerror = (error) => console.error('WebSocket error:', error);
    
    deviceWebSocket.onopen = () => {
        console.log('WebSocket connected for device:', deviceId);
        // Send device ID as first message
        deviceWebSocket.send(deviceId);
        
        // Add ping interval
        this.pingInterval = setInterval(() => {
            if (deviceWebSocket.readyState === WebSocket.OPEN) {
                deviceWebSocket.send('ping');
            }
        }, 30000);
    };
    
    deviceWebSocket.onmessage = (event) => {
        console.log('WebSocket message received:', event.data);
        try {
            const data = JSON.parse(event.data);
            console.log('Parsed WebSocket data:', data);
            if (data.type === 'device_update') {
                updateDeviceInfo(data.data);
                
                if (data.data.recording_history) {
                    $$("last_recording_time").setValue(
                        data.data.recording_history.last_timestamp 
                            ? formatRecordingTimestamp(data.data.recording_history.last_timestamp)
                            : "No recordings yet"
                    );
                    
                    $$("last_recording_path").setValue(
                        data.data.recording_history.last_filename 
                            ? `./${data.data.recording_history.last_filename}`
                            : ""
                    );

                    // Add automatic playback when new recording arrives
                    const alwaysPlay = $$("recording_form").getChildViews()[0].getValue();
                    if (alwaysPlay && data.data.recording_history.last_filename) {
                        const audioUrl = `/dataset/${data.data.recording_history.last_filename}`;
                        // Create a temporary ID for playback
                        const tempId = webix.uid();
                        playAudio(audioUrl, tempId);
                    }
                }
            }
        } catch (e) {
            console.error('Error processing WebSocket message:', e);
        }
    };
    
    deviceWebSocket.onclose = (event) => {
        console.log('WebSocket closed:', event.code, event.reason);
        clearInterval(this.pingInterval);
        if (event.code !== 1000) { // Don't reconnect if closed normally
            setTimeout(() => connectWebSocket(deviceId), 3000);
        }
    };
    
    deviceWebSocket.onerror = (error) => {
        console.error('WebSocket error:', error);
    };
}

function manageDeviceWebSocket(deviceId) {
    // Close any existing connection
    if (deviceWebSocket) {
        deviceWebSocket.close();
        deviceWebSocket = null;
    }
    
    if (deviceId) {
        connectWebSocket(deviceId);
    }
}



// Update the webix.ready function to use initLanguageFilter
webix.ready(function() {
    // First verify all components exist
    const components = [
        'language_filter', 
        'dataset_table',
        'message_box',
        'rec_label_filter',
        'label_filter'
    ];
    
    let allComponentsExist = true;
    components.forEach(id => {
        if (!$$(id)) {
            console.error(`Component ${id} not found!`);
            allComponentsExist = false;
        }
    });
    
    if (!allComponentsExist) {
        console.error("Missing required components - aborting initialization");
        return;
    }
    
    // Initialize WebSocket connections
    connectGlobalWebSocket();

    // Add button event handler
    $$("create_training_btn").attachEvent("onItemClick", function() {
        webix.confirm({
            title: "Confirm Optimization",
            text: "This will recreate the training dataset. Continue?",
            callback: function(result) {
                if (result) {
                    optimizeDataset();
                }
            }
        });
    })

    // Modify the device selection handler
    $$("device_selector").attachEvent("onChange", function(newv) {
        if (newv) {
            // Load details for the selected device
            $$("device_selector").setValue(newv);
            loadDeviceDetails(newv);
            manageDeviceWebSocket(newv); // Start WebSocket for this device
        } else {
            manageDeviceWebSocket(null); // Close WebSocket
            $$("last_recording_time").setValue("No device selected");
            $$("last_recording_path").setValue("");
        }
    });


    // Initialize language filter and all comboboxes
    initLanguageFilter();
    initAudioPlayer();
    loadAudioFiles();
});


