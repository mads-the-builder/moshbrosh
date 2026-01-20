/**
 * MoshBrosh MCP Bridge - Premiere Pro ExtendScript
 * Controls Premiere Pro for automated plugin testing
 */

// Configuration
var TEST_VIDEO_PATH = "/Users/mads/coding/moshbrosh/MoshBrosh/CLI/test_input.mp4";
var TEST_PROJECT_PATH = "/Users/mads/coding/moshbrosh/MoshBrosh/test_project.prproj";
var FRAME_EXPORT_PATH = "/Users/mads/coding/moshbrosh/MoshBrosh/CLI/temp_frame.png";
var RENDER_OUTPUT_PATH = "/Users/mads/coding/moshbrosh/MoshBrosh/CLI/premiere_render.mp4";

// Helper: Return JSON string
function jsonResult(obj) {
    return JSON.stringify(obj);
}

// Helper: Find effect by name on a clip
function findEffectOnClip(clip, effectName) {
    if (!clip || !clip.components) return null;

    for (var i = 0; i < clip.components.numItems; i++) {
        var component = clip.components[i];
        if (component.displayName.toLowerCase().indexOf(effectName.toLowerCase()) >= 0) {
            return component;
        }
    }
    return null;
}

// Helper: Get active sequence
function getActiveSequence() {
    if (!app.project) return null;
    return app.project.activeSequence;
}

// Helper: Get first video clip in sequence
function getFirstVideoClip() {
    var seq = getActiveSequence();
    if (!seq) return null;

    for (var t = 0; t < seq.videoTracks.numTracks; t++) {
        var track = seq.videoTracks[t];
        if (track.clips.numItems > 0) {
            return track.clips[0];
        }
    }
    return null;
}

// Enable QE DOM for advanced operations
function enableQE() {
    try {
        app.enableQE();
        return true;
    } catch (e) {
        return false;
    }
}

// Check project status and auto-setup if needed
function checkAndSetupProject() {
    try {
        // Check if any project is open
        if (!app.project || !app.project.path) {
            // No project open - try to open test project
            var testProjectFile = new File(TEST_PROJECT_PATH);
            if (testProjectFile.exists) {
                app.openDocument(TEST_PROJECT_PATH);
                $.sleep(3000); // Wait for project to load
                return jsonResult({
                    success: true,
                    action: "opened_project",
                    project: TEST_PROJECT_PATH
                });
            } else {
                // Create a new project
                return createTestProject();
            }
        }

        // Project is open - check if it has our sequence
        var seq = getActiveSequence();
        if (!seq) {
            // No sequence - need to set one up
            return setupTestSequence();
        }

        // Check if MoshBrosh effect is applied
        var clip = getFirstVideoClip();
        if (clip) {
            var effect = findEffectOnClip(clip, "MoshBrosh");
            if (!effect) {
                // Apply the effect
                return applyMoshBroshEffect();
            }
        }

        return jsonResult({
            success: true,
            action: "ready",
            project: app.project.path,
            sequence: seq.name,
            hasEffect: true
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Create a new test project
function createTestProject() {
    try {
        // Create new project
        app.newProject(TEST_PROJECT_PATH);
        $.sleep(1000);

        // Import test video
        var importFiles = [TEST_VIDEO_PATH];
        var importSuccess = app.project.importFiles(importFiles, true, app.project.rootItem, false);

        if (!importSuccess) {
            return jsonResult({ success: false, error: "Failed to import test video" });
        }

        $.sleep(1000);

        // Create sequence from clip
        var rootItem = app.project.rootItem;
        var videoItem = null;

        for (var i = 0; i < rootItem.children.numItems; i++) {
            var item = rootItem.children[i];
            if (item.name.indexOf("test_input") >= 0) {
                videoItem = item;
                break;
            }
        }

        if (!videoItem) {
            return jsonResult({ success: false, error: "Could not find imported video" });
        }

        // Create sequence from clip
        app.project.createNewSequenceFromClips("MoshBrosh Test", [videoItem]);
        $.sleep(2000);

        // Save project
        app.project.save();

        return jsonResult({
            success: true,
            action: "created_project",
            project: TEST_PROJECT_PATH
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Setup test sequence if project exists but no sequence
function setupTestSequence() {
    try {
        // Find or import test video
        var rootItem = app.project.rootItem;
        var videoItem = null;

        for (var i = 0; i < rootItem.children.numItems; i++) {
            var item = rootItem.children[i];
            if (item.name.indexOf("test_input") >= 0) {
                videoItem = item;
                break;
            }
        }

        if (!videoItem) {
            // Import test video
            app.project.importFiles([TEST_VIDEO_PATH], true, app.project.rootItem, false);
            $.sleep(1000);

            for (var i = 0; i < rootItem.children.numItems; i++) {
                var item = rootItem.children[i];
                if (item.name.indexOf("test_input") >= 0) {
                    videoItem = item;
                    break;
                }
            }
        }

        if (!videoItem) {
            return jsonResult({ success: false, error: "Could not find or import test video" });
        }

        // Create sequence
        app.project.createNewSequenceFromClips("MoshBrosh Test", [videoItem]);
        $.sleep(2000);

        return jsonResult({
            success: true,
            action: "created_sequence"
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Apply MoshBrosh effect using QE DOM
function applyMoshBroshEffect() {
    try {
        var clip = getFirstVideoClip();
        if (!clip) {
            return jsonResult({ success: false, error: "No video clip found" });
        }

        // Check if already applied
        var existing = findEffectOnClip(clip, "MoshBrosh");
        if (existing) {
            return jsonResult({
                success: true,
                action: "effect_already_applied"
            });
        }

        // Enable QE DOM
        if (!enableQE()) {
            return jsonResult({ success: false, error: "Could not enable QE DOM" });
        }

        // Get QE sequence and clip
        var qeSeq = qe.project.getActiveSequence();
        if (!qeSeq) {
            return jsonResult({ success: false, error: "No QE sequence" });
        }

        // Get first video track and clip
        var qeTrack = qeSeq.getVideoTrackAt(0);
        if (!qeTrack) {
            return jsonResult({ success: false, error: "No QE video track" });
        }

        var qeClip = qeTrack.getItemAt(0);
        if (!qeClip) {
            return jsonResult({ success: false, error: "No QE clip" });
        }

        // Add effect by name - MoshBrosh should be in Stylize category
        // The effect matchName is typically the plugin name
        var effectAdded = qeClip.addVideoEffect(qe.project.getVideoEffectByName("MoshBrosh"));

        if (effectAdded) {
            return jsonResult({
                success: true,
                action: "effect_applied"
            });
        } else {
            // Try alternate names
            effectAdded = qeClip.addVideoEffect(qe.project.getVideoEffectByName("MoshBrosh Datamosh"));
            if (effectAdded) {
                return jsonResult({
                    success: true,
                    action: "effect_applied"
                });
            }

            return jsonResult({
                success: false,
                error: "Could not find or apply MoshBrosh effect. Is the plugin installed?"
            });
        }

    } catch (e) {
        return jsonResult({ success: false, error: e.message, stack: e.stack });
    }
}

// Open test project (called from panel)
function openOrCreateTestProject() {
    return checkAndSetupProject();
}

// Export sequence to video file for testing
function exportSequence() {
    try {
        var seq = getActiveSequence();
        if (!seq) {
            return jsonResult({ success: false, error: "No active sequence" });
        }

        // Use Adobe Media Encoder preset
        // Export to MP4 for easy frame extraction
        var outputFile = new File(RENDER_OUTPUT_PATH);

        // Get first available export preset
        var presetPath = "";

        // Try to find H.264 preset
        var presetsFolder = new Folder(Folder.appData.fsName + "/Adobe/Adobe Media Encoder/25.0/Presets");
        if (presetsFolder.exists) {
            var presets = presetsFolder.getFiles("*.epr");
            if (presets.length > 0) {
                presetPath = presets[0].fsName;
            }
        }

        // Queue export in AME
        app.encoder.launchEncoder();
        $.sleep(2000);

        var exportSuccess = app.encoder.encodeSequence(
            seq,
            outputFile.fsName,
            presetPath,
            app.encoder.ENCODE_WORKAREA,
            true // remove on completion
        );

        if (exportSuccess) {
            return jsonResult({
                success: true,
                action: "export_queued",
                outputPath: RENDER_OUTPUT_PATH
            });
        } else {
            return jsonResult({
                success: false,
                error: "Failed to queue export"
            });
        }

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Render specific frame by moving playhead and exporting frame
function renderFrameToFile(frameNum) {
    try {
        var seq = getActiveSequence();
        if (!seq) {
            return jsonResult({ success: false, error: "No active sequence" });
        }

        // Get frame rate
        var seqSettings = seq.getSettings();
        var fps = 1.0 / seqSettings.videoFrameRate.seconds;

        // Calculate time in ticks (Premiere uses ticks internally)
        var timeInSeconds = frameNum / fps;
        var ticks = timeInSeconds * 254016000000; // Ticks per second

        // Move playhead
        seq.setPlayerPosition(ticks.toString());

        // Use exportFramePNG if available (Premiere 2020+)
        var outputFile = FRAME_EXPORT_PATH;

        // Try to export frame
        if (typeof seq.exportFramePNG === "function") {
            seq.exportFramePNG(ticks.toString(), outputFile);
            return jsonResult({
                success: true,
                frame: frameNum,
                path: outputFile
            });
        } else {
            // Fallback: need to use export
            return jsonResult({
                success: false,
                error: "exportFramePNG not available",
                suggestion: "Use exportSequence and extract frame with ffmpeg"
            });
        }

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Set a parameter on the MoshBrosh effect
function setMoshBroshParam(paramName, value) {
    try {
        var clip = getFirstVideoClip();
        if (!clip) {
            return jsonResult({ success: false, error: "No video clip found" });
        }

        var effect = findEffectOnClip(clip, "MoshBrosh");
        if (!effect) {
            return jsonResult({ success: false, error: "MoshBrosh effect not found on clip" });
        }

        // Find the parameter
        for (var i = 0; i < effect.properties.numItems; i++) {
            var prop = effect.properties[i];
            var propName = prop.displayName.toLowerCase().replace(/\s+/g, "_");
            var searchName = paramName.toLowerCase().replace(/\s+/g, "_");

            if (propName === searchName || prop.displayName.toLowerCase() === paramName.toLowerCase()) {
                prop.setValue(value, true);
                return jsonResult({
                    success: true,
                    param: paramName,
                    value: value
                });
            }
        }

        // List available params for debugging
        var availableParams = [];
        for (var i = 0; i < effect.properties.numItems; i++) {
            availableParams.push(effect.properties[i].displayName);
        }

        return jsonResult({
            success: false,
            error: "Parameter not found: " + paramName,
            availableParams: availableParams
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Get all MoshBrosh effect parameters
function getMoshBroshParams() {
    try {
        var clip = getFirstVideoClip();
        if (!clip) {
            return jsonResult({ success: false, error: "No video clip found" });
        }

        var effect = findEffectOnClip(clip, "MoshBrosh");
        if (!effect) {
            return jsonResult({ success: false, error: "MoshBrosh effect not found on clip" });
        }

        var params = {};
        for (var i = 0; i < effect.properties.numItems; i++) {
            var prop = effect.properties[i];
            try {
                params[prop.displayName] = prop.getValue();
            } catch (e) {
                params[prop.displayName] = "(unreadable)";
            }
        }

        return jsonResult({
            success: true,
            effectName: effect.displayName,
            params: params
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Get project info
function getProjectInfo() {
    try {
        if (!app.project) {
            return jsonResult({
                success: true,
                projectOpen: false,
                project: null
            });
        }

        var seq = getActiveSequence();
        var clip = getFirstVideoClip();
        var effect = clip ? findEffectOnClip(clip, "MoshBrosh") : null;

        return jsonResult({
            success: true,
            projectOpen: true,
            projectPath: app.project.path || "(unsaved)",
            sequenceName: seq ? seq.name : null,
            hasClip: clip !== null,
            hasMoshBroshEffect: effect !== null
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Force refresh the timeline (helps trigger re-renders)
function refreshTimeline() {
    try {
        var seq = getActiveSequence();
        if (!seq) {
            return jsonResult({ success: false, error: "No active sequence" });
        }

        // Move playhead slightly to force refresh
        var currentPos = seq.getPlayerPosition();
        seq.setPlayerPosition((parseInt(currentPos) + 1).toString());
        $.sleep(100);
        seq.setPlayerPosition(currentPos.toString());

        return jsonResult({ success: true, action: "refreshed" });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Save project
function saveProject() {
    try {
        if (app.project) {
            app.project.save();
            return jsonResult({ success: true, action: "saved" });
        }
        return jsonResult({ success: false, error: "No project open" });
    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}
