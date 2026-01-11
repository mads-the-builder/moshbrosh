/**
 * MoshBrosh MCP Bridge - CEP Panel
 * Connects to MCP server and executes commands in Premiere Pro
 * AUTO-OPENS test project when connected
 */

const MCP_SERVER_URL = "ws://localhost:8847";
const HEARTBEAT_INTERVAL = 2000;
const AUTO_SETUP_DELAY = 3000; // Wait 3 seconds after connect before auto-setup

let ws = null;
let csInterface = null;
let heartbeatTimer = null;
let reconnectTimer = null;
let autoSetupDone = false;

// Initialize
document.addEventListener("DOMContentLoaded", () => {
    csInterface = new CSInterface();
    log("CEP Panel initialized");
    connect();
});

function log(msg) {
    const logEl = document.getElementById("log");
    const time = new Date().toLocaleTimeString();
    logEl.textContent = `[${time}] ${msg}\n` + logEl.textContent.slice(0, 2000);
    console.log(`[MCP Bridge] ${msg}`);
}

function setStatus(status, message) {
    const statusEl = document.getElementById("status");
    statusEl.className = `status ${status}`;
    statusEl.textContent = message;
}

function connect() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        return;
    }

    setStatus("connecting", "Connecting to MCP Server...");
    log(`Connecting to ${MCP_SERVER_URL}`);

    try {
        ws = new WebSocket(MCP_SERVER_URL);

        ws.onopen = () => {
            setStatus("connected", "Connected to MCP Server");
            log("Connected!");
            startHeartbeat();

            // Just notify that we're connected - don't auto-apply effects
            // Effects should only be applied when explicitly requested
            log("Ready - waiting for commands");
        };

        ws.onclose = () => {
            setStatus("disconnected", "Disconnected from MCP Server");
            log("Disconnected");
            stopHeartbeat();
            autoSetupDone = false; // Reset so we auto-setup on reconnect
            scheduleReconnect();
        };

        ws.onerror = (err) => {
            log(`WebSocket error: ${err.message || "unknown"}`);
        };

        ws.onmessage = (event) => {
            handleMessage(event.data);
        };
    } catch (e) {
        log(`Connection error: ${e.message}`);
        scheduleReconnect();
    }
}

function reconnect() {
    if (ws) {
        ws.close();
    }
    autoSetupDone = false;
    connect();
}

function scheduleReconnect() {
    if (reconnectTimer) return;
    log("Will reconnect in 5 seconds...");
    reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connect();
    }, 5000);
}

function startHeartbeat() {
    stopHeartbeat();
    heartbeatTimer = setInterval(() => {
        if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: "heartbeat" }));
        }
    }, HEARTBEAT_INTERVAL);
}

function stopHeartbeat() {
    if (heartbeatTimer) {
        clearInterval(heartbeatTimer);
        heartbeatTimer = null;
    }
}

function sendResponse(requestId, result, error = null) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({
            type: "response",
            requestId,
            result,
            error
        }));
    }
}

// Auto-setup project when panel connects
async function autoSetupProject() {
    if (autoSetupDone) {
        log("Auto-setup already completed");
        return;
    }

    log("Auto-setting up test project...");
    setStatus("connected", "Setting up test project...");

    try {
        const result = await evalScript("checkAndSetupProject()");
        autoSetupDone = true;

        if (result.success) {
            log(`Auto-setup complete: ${result.action || "ready"}`);
            setStatus("connected", `Ready: ${result.action || "project loaded"}`);

            // Notify MCP server that we're ready
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({
                    type: "auto_setup_complete",
                    result: result
                }));
            }
        } else {
            log(`Auto-setup issue: ${result.error}`);
            setStatus("connected", `Setup issue: ${result.error}`);
        }
    } catch (e) {
        log(`Auto-setup error: ${e.message}`);
        setStatus("connected", "Connected (setup failed)");
    }
}

async function handleMessage(data) {
    try {
        const msg = JSON.parse(data);

        if (msg.type !== "command") return;

        const { requestId, command, params } = msg;
        log(`Received command: ${command}`);

        try {
            const result = await executeCommand(command, params);
            sendResponse(requestId, result);
        } catch (e) {
            log(`Command error: ${e.message}`);
            sendResponse(requestId, null, e.message);
        }
    } catch (e) {
        log(`Parse error: ${e.message}`);
    }
}

async function executeCommand(command, params) {
    switch (command) {
        case "open_test_project":
            return await openTestProject();

        case "get_project_info":
            return await getProjectInfo();

        case "apply_effect":
            return await applyEffect();

        case "render_frame":
            return await renderFrame(params.frame);

        case "set_effect_param":
            return await setEffectParam(params.param, params.value);

        case "get_effect_params":
            return await getEffectParams();

        case "render_frame_range":
            return await renderFrameRange(params.start, params.end, params.step || 1);

        case "get_source_frame":
            return await getSourceFrame(params.frame);

        case "compare_frames":
            return await compareFrames(params.frame_a, params.frame_b);

        case "export_sequence":
            return await exportSequence();

        case "refresh_timeline":
            return await refreshTimeline();

        case "save_project":
            return await saveProject();

        default:
            throw new Error(`Unknown command: ${command}`);
    }
}

// Execute ExtendScript and return result
function evalScript(script) {
    return new Promise((resolve, reject) => {
        csInterface.evalScript(script, (result) => {
            if (result === "EvalScript error.") {
                reject(new Error("ExtendScript error"));
            } else {
                try {
                    resolve(JSON.parse(result));
                } catch {
                    resolve(result);
                }
            }
        });
    });
}

// Command implementations
async function openTestProject() {
    log("Opening/setting up test project...");
    const result = await evalScript("checkAndSetupProject()");
    return result;
}

async function getProjectInfo() {
    log("Getting project info...");
    const result = await evalScript("getProjectInfo()");
    return result;
}

async function applyEffect() {
    log("Applying MoshBrosh effect...");
    const result = await evalScript("applyMoshBroshEffect()");
    return result;
}

async function renderFrame(frameNum) {
    log(`Rendering frame ${frameNum}...`);
    const result = await evalScript(`renderFrameToFile(${frameNum})`);
    return result;
}

async function setEffectParam(param, value) {
    log(`Setting ${param} = ${value}...`);
    const result = await evalScript(`setMoshBroshParam("${param}", ${value})`);
    return result;
}

async function getEffectParams() {
    log("Getting effect params...");
    const result = await evalScript("getMoshBroshParams()");
    return result;
}

async function renderFrameRange(start, end, step) {
    log(`Rendering frames ${start}-${end}...`);
    const result = await evalScript(`renderFrameRange(${start}, ${end}, ${step})`);
    return result;
}

async function getSourceFrame(frameNum) {
    log(`Getting source frame ${frameNum}...`);
    const result = await evalScript(`getSourceFrame(${frameNum})`);
    return result;
}

async function compareFrames(frameA, frameB) {
    log(`Comparing frames ${frameA} and ${frameB}...`);
    const result = await evalScript(`compareFrames(${frameA}, ${frameB})`);
    return result;
}

async function exportSequence() {
    log("Exporting sequence...");
    const result = await evalScript("exportSequence()");
    return result;
}

async function refreshTimeline() {
    log("Refreshing timeline...");
    const result = await evalScript("refreshTimeline()");
    return result;
}

async function saveProject() {
    log("Saving project...");
    const result = await evalScript("saveProject()");
    return result;
}

// Manual test buttons
function testSetup() {
    openTestProject()
        .then(r => log(`Setup result: ${JSON.stringify(r)}`))
        .catch(e => log(`Setup error: ${e.message}`));
}

function testRender() {
    executeCommand("render_frame", { frame: 15 })
        .then(r => log(`Render result: ${JSON.stringify(r).slice(0, 200)}`))
        .catch(e => log(`Render error: ${e.message}`));
}

function testInfo() {
    getProjectInfo()
        .then(r => log(`Project info: ${JSON.stringify(r)}`))
        .catch(e => log(`Info error: ${e.message}`));
}
