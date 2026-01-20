/**
 * MoshBrosh MCP Bridge - CEP Panel
 * Connects to MCP server and executes commands in Premiere Pro
 */

const MCP_SERVER_URL = "ws://localhost:8847";
const HEARTBEAT_INTERVAL = 2000;

let ws = null;
let csInterface = null;
let heartbeatTimer = null;
let reconnectTimer = null;

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
        };

        ws.onclose = () => {
            setStatus("disconnected", "Disconnected from MCP Server");
            log("Disconnected");
            stopHeartbeat();
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
    log("Opening test project...");
    const result = await evalScript("openOrCreateTestProject()");
    return result;
}

async function renderFrame(frameNum) {
    log(`Rendering frame ${frameNum}...`);
    const result = await evalScript(`renderFrameToBase64(${frameNum})`);
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

// Test button
function testRender() {
    executeCommand("render_frame", { frame: 10 })
        .then(r => log(`Test result: ${JSON.stringify(r).slice(0, 100)}`))
        .catch(e => log(`Test error: ${e.message}`));
}
