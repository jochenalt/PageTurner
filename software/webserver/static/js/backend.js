// backend.js

// Status message handler
// Status message handler
function showStatus(message, isError = false) {
    const color = isError ? "#ff5c5c" : "#4CAF50";
    $$("message_box").setHTML(`<div style="color: ${color}">${message}</div>`);
}


// Initialize language filter
function initLanguageFilter() {
    $$("language_filter").attachEvent("onChange", function(newv) {
        loadDatasetOverview(newv);
    });
    
    // Load initial data
    loadDatasetOverview($$("language_filter").getValue());
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

// backend.js
webix.ready(function() {
    // First verify all components exist
    const components = [
        'language_filter', 
        'dataset_table',
        'message_box'
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
    
    // Initialize language filter
    $$("language_filter").attachEvent("onChange", function(newv) {
        loadDatasetOverview(newv);
    });
    
    // Load initial data
    loadDatasetOverview($$("language_filter").getValue());
});