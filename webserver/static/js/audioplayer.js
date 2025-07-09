// audioPlayer.js
function initAudioPlayer(rowElement, audioSrc) {
    if (!rowElement) {
        console.error("Row element is null");
        return;
    }
    
    const playerDiv = rowElement.querySelector(".audio-player");
    if (!playerDiv) {
        console.error("Audio player div not found in row");
        return;
    }
    

    playBtn.addEventListener("click", () => {
        if (isPlaying) {
            audio.pause();
            playBtn.innerHTML = '<span class="webix_icon wxi-play"></span>';
        } else {
            audio.play()
                .then(() => {
                    playBtn.innerHTML = '<span class="webix_icon wxi-pause"></span>';
                })
                .catch(e => {
                    showStatus("Playback failed: " + e.message, true);
                });
        }
        isPlaying = !isPlaying;
    });

    audio.addEventListener("timeupdate", () => {
        slider.value = (audio.currentTime / audio.duration) * 100 || 0;
    });

    slider.addEventListener("input", () => {
        audio.currentTime = (slider.value / 100) * audio.duration;
    });

    audio.addEventListener("loadedmetadata", () => {
        slider.disabled = false;
    });

    audio.addEventListener("ended", () => {
        playBtn.innerHTML = '<span class="webix_icon wxi-play"></span>';
        isPlaying = false;
    });
}

function attachAudioPlayers(tableId) {
    const table = $$(tableId);
    if (!table) {
        console.error(`Table ${tableId} not found`);
        return;
    }
    
    table.data.each(function(obj) {
        const rowElement = table.getItemNode(obj.id);
        if (rowElement && obj.audioSrc) {
            initAudioPlayer(rowElement, obj.audioSrc);
        }
    });
}