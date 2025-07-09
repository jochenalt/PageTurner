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

// Add this to backend.js after the existing functions

// Function to load audio files based on current filter
function loadAudioFiles() {
    const language = $$("language_filter").getValue();
    const labelFilter = $$("label_filter").getValue();
    
    try {
        webix.ajax().get(`/api/audio-files?language=${language}&label=${labelFilter}`, {
            success: function(data, xml) {
                const response = xml.json();
                if (response.error) {
                    showStatus(response.message, true);
                    $$("audio_table").clearAll();
                } else {
                    // Process each file to include duration if available
                    const processedData = response.data.map(file => {
                        return {
                            id: webix.uid(),
                            name: file.name,
                            modified: file.modified,
                            samples: file.samples,
                            duration: file.duration || 0,
                            playback: file.audioSrc
                        };
                    });
                    
                    $$("audio_table").clearAll();
                    $$("audio_table").parse(processedData);
                    showStatus(`Loaded ${processedData.length} audio files`);
                }
            },
            error: function(err) {
                showStatus("Failed to load audio files: " + (err.response?.json?.message || err.status), true);
                $$("audio_table").clearAll();
            }
        });
    } catch (e) {
        console.error("Error in loadAudioFiles:", e);
    }
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

    loadAudioFiles();

});