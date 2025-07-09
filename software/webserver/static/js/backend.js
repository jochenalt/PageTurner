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
    $$("rec_label_filter").define("options", ["All Labels", ...commands, ...labels]);
    $$("rec_label_filter").refresh();
    
    // Update Audio Dataset Browser combobox
    $$("label_filter").define("options", ["All Labels", ...commands, ...labels]);
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
                    const cleanData = response.map(item => ({
                        ...item,
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


// Play audio file
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
    const playerElement = document.querySelector(`.audio-player[data-id="${rowId}"]`);
    const slider = playerElement.querySelector('.progress-slider');
    const timeDisplay = playerElement.querySelector('.time-display');
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
            slider.max = duration;
            timeDisplay.textContent = `0:00 / ${formatTime(duration)}`;
            
            startTime = audioContext.currentTime;
            currentAudioSource.start(0);
            isPlaying = true;
            currentRowId = rowId;
            updatePlayButton(rowId, true);
            showStatus(`Playing: ${filename}`);
            
            // Animation loop to update slider
            function updateProgress() {
                if (!isPlaying) return;
                
                const elapsed = audioContext.currentTime - startTime;
                const progress = Math.min(elapsed, duration);
                
                slider.value = progress;
                timeDisplay.textContent = `${formatTime(progress)} / ${formatTime(duration)}`;
                
                if (progress < duration) {
                    animationFrameId = requestAnimationFrame(updateProgress);
                }
            }
            
            updateProgress();
            
            currentAudioSource.onended = () => {
                cancelAnimationFrame(animationFrameId);
                slider.value = duration;
                timeDisplay.textContent = `${formatTime(duration)} / ${formatTime(duration)}`;
                isPlaying = false;
                currentRowId = null;
                updatePlayButton(rowId, false);
            };
        })
        .catch(error => {
            showStatus(`Playback failed: ${error.message}`, true);
            console.error('Audio error:', error);
            updatePlayButton(rowId, false);
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
        loadDatasetOverview(newv);
        updateComboboxes(newv);
        loadAudioFiles();  // Also reload audio files when language changes
    });

    $$("label_filter").attachEvent("onChange", function(newv) {
        loadAudioFiles();  // This should call loadAudioFiles, not loadDatasetOverview
    });
    
    // Load initial data
    const initialLanguage = $$("language_filter").getValue();
    loadDatasetOverview(initialLanguage);
    updateComboboxes(initialLanguage);
    loadAudioFiles();  // Load initial audio files
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
    
    // Initialize language filter and all comboboxes
    initLanguageFilter();
    initAudioPlayer();
    loadAudioFiles();

});