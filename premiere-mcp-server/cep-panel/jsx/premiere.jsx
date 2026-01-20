/**
 * MoshBrosh MCP Bridge - Premiere Pro ExtendScript
 * Controls Premiere Pro for automated plugin testing
 */

// Configuration
var TEST_VIDEO_PATH = "/Users/mads/coding/moshbrosh/MoshBrosh/CLI/test_input.mp4";
var TEST_PROJECT_PATH = "/Users/mads/coding/moshbrosh/MoshBrosh/test_project.prproj";
var FRAME_EXPORT_PATH = "/Users/mads/coding/moshbrosh/MoshBrosh/temp_frame.png";

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

// Open or create a test project with test video and MoshBrosh effect
function openOrCreateTestProject() {
    try {
        // Check if project is already open with our test clip
        var seq = getActiveSequence();
        if (seq && seq.name === "MoshBrosh Test") {
            return jsonResult({
                success: true,
                message: "Test project already open",
                sequence: seq.name
            });
        }

        // Check if test project exists
        var testProjectFile = new File(TEST_PROJECT_PATH);
        if (testProjectFile.exists) {
            app.openDocument(TEST_PROJECT_PATH);
            // Wait a moment for project to load
            $.sleep(2000);

            seq = getActiveSequence();
            if (seq) {
                return jsonResult({
                    success: true,
                    message: "Opened existing test project",
                    sequence: seq.name
                });
            }
        }

        // Create new project
        // Note: Premiere's ExtendScript can't directly create projects
        // We need to have a template project or the user creates one

        return jsonResult({
            success: false,
            message: "No test project found. Please create a project with: 1) Import test_input.mp4, 2) Create sequence 'MoshBrosh Test', 3) Add clip to timeline, 4) Apply MoshBrosh effect, 5) Save as test_project.prproj"
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Render a frame to PNG and return as base64
function renderFrameToBase64(frameNum) {
    try {
        var seq = getActiveSequence();
        if (!seq) {
            return jsonResult({ success: false, error: "No active sequence" });
        }

        // Move playhead to frame
        var fps = seq.getSettings().videoFrameRate.seconds;
        if (fps === 0) fps = 1/24; // default 24fps
        var timeInSeconds = frameNum * fps;

        // Set player position
        seq.setPlayerPosition(timeInSeconds.toString());

        // Export frame using Premiere's built-in export frame function
        // This uses the current program monitor view
        var exportFile = new File(FRAME_EXPORT_PATH);

        // Use the sequence's exportFrameToFile method if available
        // Note: This may not be available in all Premiere versions
        if (seq.exportFramePNG) {
            seq.exportFramePNG(timeInSeconds, exportFile.fsName);
        } else {
            // Fallback: Use app.encoder or manual method
            // For now, return an error asking to use Export Frame manually
            return jsonResult({
                success: false,
                error: "exportFramePNG not available. Premiere version may not support this.",
                workaround: "Use File > Export > Media and select PNG sequence"
            });
        }

        // Read the exported file and convert to base64
        if (exportFile.exists) {
            exportFile.encoding = "BINARY";
            exportFile.open("r");
            var binaryData = exportFile.read();
            exportFile.close();

            // Note: ExtendScript doesn't have native base64, we'd need a workaround
            // For now, return the file path
            return jsonResult({
                success: true,
                frame: frameNum,
                imagePath: FRAME_EXPORT_PATH,
                message: "Frame exported to file"
            });
        }

        return jsonResult({
            success: false,
            error: "Failed to export frame"
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message, stack: e.stack });
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
            if (prop.displayName.toLowerCase().replace(/\s+/g, "_") === paramName.toLowerCase() ||
                prop.displayName.toLowerCase() === paramName.toLowerCase()) {

                prop.setValue(value, true);

                return jsonResult({
                    success: true,
                    param: paramName,
                    value: value
                });
            }
        }

        return jsonResult({
            success: false,
            error: "Parameter not found: " + paramName
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

// Render a range of frames
function renderFrameRange(startFrame, endFrame, step) {
    try {
        var results = [];
        for (var f = startFrame; f <= endFrame; f += step) {
            var result = JSON.parse(renderFrameToBase64(f));
            results.push({
                frame: f,
                success: result.success,
                imagePath: result.imagePath || null,
                error: result.error || null
            });
        }

        return jsonResult({
            success: true,
            frames: results
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Get source frame (before effects)
function getSourceFrame(frameNum) {
    // This would require disabling effects, rendering, then re-enabling
    // For now, return not implemented
    return jsonResult({
        success: false,
        error: "getSourceFrame not yet implemented"
    });
}

// Compare two frames
function compareFrames(frameA, frameB) {
    // Would need to render both frames and compare
    // For now, return not implemented
    return jsonResult({
        success: false,
        error: "compareFrames not yet implemented"
    });
}

// Apply MoshBrosh effect to the first clip
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
                message: "MoshBrosh effect already applied"
            });
        }

        // Find MoshBrosh in the effects list
        // Note: This requires knowing the effect's matchName or bin path
        // Premiere's ExtendScript doesn't have a direct "apply effect by name" method
        // This typically requires using QE (Quality Engineering) DOM which is more complex

        return jsonResult({
            success: false,
            error: "Cannot programmatically apply effects in Premiere ExtendScript. Please apply MoshBrosh manually from Effects panel."
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}

// Get project info
function getProjectInfo() {
    try {
        if (!app.project) {
            return jsonResult({ success: false, error: "No project open" });
        }

        var seq = getActiveSequence();
        var clip = getFirstVideoClip();
        var effect = clip ? findEffectOnClip(clip, "MoshBrosh") : null;

        return jsonResult({
            success: true,
            projectPath: app.project.path,
            sequenceName: seq ? seq.name : null,
            hasClip: clip !== null,
            hasMoshBroshEffect: effect !== null
        });

    } catch (e) {
        return jsonResult({ success: false, error: e.message });
    }
}
