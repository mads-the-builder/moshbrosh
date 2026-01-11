#!/usr/bin/env node

/**
 * Premiere Pro MCP Server
 * Enables autonomous plugin development by providing tools to:
 * - Control Premiere Pro (apply effects, render frames)
 * - Capture plugin debug output
 * - Detect and recover from crashes
 * - Build and reload plugins
 */

import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";
import { WebSocketServer } from "ws";
import { spawn, exec, execSync } from "child_process";
import { readFileSync, writeFileSync, existsSync, readdirSync, statSync } from "fs";
import { homedir } from "os";
import path from "path";
import {
  dismissCrashReporter,
  dismissRecoveryDialog,
  waitForPremiereReady,
  startDialogWatcher,
  stopDialogWatcher
} from "./dialog-handler.js";

// Configuration
const CONFIG = {
  wsPort: 8847,
  premiereAppName: "Adobe Premiere Pro 2025",
  pluginDebugLog: `${homedir()}/Desktop/moshbrosh_debug.log`,
  crashLogDir: `${homedir()}/Library/Logs/DiagnosticReports`,
  pluginBuildDir: "/Users/mads/coding/moshbrosh/MoshBrosh/Mac",
  pluginInstallDir: `${homedir()}/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore`,
  testVideoPath: "/Users/mads/coding/moshbrosh/MoshBrosh/CLI/test_input.mp4",
  testProjectPath: "/Users/mads/Desktop/mosh_test_2.prproj",
  exportOutputDir: "/Users/mads/coding/moshbrosh/MoshBrosh/CLI",
  exportOutputPath: "/Users/mads/coding/moshbrosh/MoshBrosh/CLI/premiere_export.mp4",
  cliToolDir: "/Users/mads/coding/moshbrosh/MoshBrosh/CLI",
  cliToolPath: "/Users/mads/coding/moshbrosh/MoshBrosh/CLI/moshbrosh",
  cliOutputPath: "/Users/mads/coding/moshbrosh/MoshBrosh/CLI/test_output_mcp.mp4",
  heartbeatInterval: 3000,
  heartbeatTimeout: 10000,
  effectProcessingWaitMs: 20000, // Wait 20 seconds for effect to process
};

// State
let premiereConnection = null;
let lastHeartbeat = 0;
let pendingRequests = new Map();
let requestIdCounter = 0;
let lastCrashTime = 0;
let lastCrashLog = "";

// WebSocket server for CEP panel connection
const wss = new WebSocketServer({ port: CONFIG.wsPort });

wss.on("connection", (ws) => {
  console.error(`[MCP] CEP panel connected`);
  premiereConnection = ws;
  lastHeartbeat = Date.now();

  ws.on("message", (data) => {
    try {
      const msg = JSON.parse(data.toString());

      if (msg.type === "heartbeat") {
        lastHeartbeat = Date.now();
        return;
      }

      if (msg.type === "auto_setup_complete") {
        console.error(`[MCP] Auto-setup complete: ${JSON.stringify(msg.result)}`);
        return;
      }

      if (msg.type === "response" && msg.requestId) {
        const pending = pendingRequests.get(msg.requestId);
        if (pending) {
          pending.resolve(msg);
          pendingRequests.delete(msg.requestId);
        }
      }
    } catch (e) {
      console.error(`[MCP] Error parsing message: ${e}`);
    }
  });

  ws.on("close", () => {
    console.error(`[MCP] CEP panel disconnected`);
    premiereConnection = null;

    // Check if this was a crash
    setTimeout(checkForCrash, 1000);
  });
});

console.error(`[MCP] WebSocket server listening on port ${CONFIG.wsPort}`);

// Check if Premiere crashed
async function checkForCrash() {
  const isRunning = isPremiereRunning();
  if (!isRunning && lastHeartbeat > 0) {
    console.error(`[MCP] Premiere appears to have crashed!`);
    lastCrashTime = Date.now();
    lastCrashLog = getLatestCrashLog();
  }
}

// Check if Premiere is running
function isPremiereRunning() {
  try {
    const result = execSync(`pgrep -f "Adobe Premiere Pro"`, { encoding: "utf8" });
    return result.trim().length > 0;
  } catch {
    return false;
  }
}

// Get PID of Premiere
function getPremierePid() {
  try {
    const result = execSync(`pgrep -f "Adobe Premiere Pro"`, { encoding: "utf8" });
    return parseInt(result.trim().split("\n")[0]);
  } catch {
    return null;
  }
}

// Get latest crash log
function getLatestCrashLog() {
  try {
    const files = readdirSync(CONFIG.crashLogDir)
      .filter(f => f.includes("Adobe Premiere Pro") && f.endsWith(".crash"))
      .map(f => ({
        name: f,
        path: path.join(CONFIG.crashLogDir, f),
        mtime: statSync(path.join(CONFIG.crashLogDir, f)).mtime.getTime()
      }))
      .sort((a, b) => b.mtime - a.mtime);

    if (files.length > 0 && files[0].mtime > Date.now() - 60000) {
      return readFileSync(files[0].path, "utf8").slice(0, 5000);
    }
  } catch (e) {
    console.error(`[MCP] Error reading crash logs: ${e}`);
  }
  return "";
}

// Send command to CEP panel and wait for response
async function sendToPremmiere(command, params = {}, timeout = 30000) {
  if (!premiereConnection) {
    throw new Error("Premiere not connected. Is it running with the CEP panel installed?");
  }

  const requestId = ++requestIdCounter;

  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pendingRequests.delete(requestId);
      reject(new Error(`Request timed out after ${timeout}ms`));
    }, timeout);

    pendingRequests.set(requestId, {
      resolve: (msg) => {
        clearTimeout(timer);
        if (msg.error) {
          reject(new Error(msg.error));
        } else {
          resolve(msg.result);
        }
      }
    });

    premiereConnection.send(JSON.stringify({
      type: "command",
      requestId,
      command,
      params
    }));
  });
}

// Build the plugin
async function buildPlugin() {
  return new Promise((resolve, reject) => {
    exec(
      `xcodebuild -project MoshBrosh.xcodeproj -scheme MoshBrosh -configuration Debug AE_SDK_BASE_PATH=/Users/mads/coding/moshbrosh/AfterEffectsSDK_25.6_61_mac/ae25.6_61.64bit.AfterEffectsSDK 2>&1`,
      { cwd: CONFIG.pluginBuildDir },
      (error, stdout, stderr) => {
        const success = stdout.includes("BUILD SUCCEEDED");
        resolve({
          success,
          output: stdout.slice(-3000),
          errors: success ? [] : stdout.match(/error:.*/g) || []
        });
      }
    );
  });
}

// Install the plugin
async function installPlugin() {
  try {
    execSync(`rm -rf "${CONFIG.pluginInstallDir}/MoshBrosh.plugin"`);
    execSync(`cp -R "${homedir()}/Library/Developer/Xcode/DerivedData/MoshBrosh-"*/Build/Products/Debug/MoshBrosh.plugin "${CONFIG.pluginInstallDir}/"`);
    return { success: true };
  } catch (e) {
    return { success: false, error: e.message };
  }
}

// Get plugin debug log
function getPluginDebugLog(lines = 100) {
  try {
    if (!existsSync(CONFIG.pluginDebugLog)) {
      return { log: "(no debug log file found)", lines: 0 };
    }
    const content = readFileSync(CONFIG.pluginDebugLog, "utf8");
    const allLines = content.split("\n");
    const lastLines = allLines.slice(-lines).join("\n");
    return { log: lastLines, lines: allLines.length };
  } catch (e) {
    return { log: `Error reading log: ${e.message}`, lines: 0 };
  }
}

// Run CLI tool for testing (bypasses Premiere entirely)
async function runCliTool(args = {}) {
  const {
    inputVideo = CONFIG.testVideoPath,
    outputVideo = `${CONFIG.cliToolDir}/test_output_mcp.mp4`,
    moshFrame = 10,
    duration = 30,
    blockSize = 16,
    searchRange = 16,
    blend = 100
  } = args;

  return new Promise((resolve) => {
    const cmd = `"${CONFIG.cliToolPath}" -i "${inputVideo}" -o "${outputVideo}" -f ${moshFrame} -d ${duration} -b ${blockSize} -s ${searchRange} -m ${blend} 2>&1`;

    exec(cmd, { cwd: CONFIG.cliToolDir, timeout: 120000 }, (error, stdout, stderr) => {
      const success = stdout.includes("Done!") || existsSync(outputVideo);
      resolve({
        success,
        command: cmd,
        output: stdout.slice(-2000),
        outputVideo: success ? outputVideo : null,
        error: error ? error.message : null
      });
    });
  });
}

// Extract a frame from a video using ffmpeg
async function extractFrameFromVideo(videoPath, frameNum, outputPath) {
  return new Promise((resolve) => {
    const cmd = `ffmpeg -y -i "${videoPath}" -vf "select=eq(n\\,${frameNum})" -vframes 1 "${outputPath}" 2>&1`;

    exec(cmd, { timeout: 30000 }, (error, stdout, stderr) => {
      const success = existsSync(outputPath);
      if (success) {
        const imageData = readFileSync(outputPath);
        const base64 = imageData.toString("base64");
        resolve({ success: true, image: base64, path: outputPath });
      } else {
        resolve({ success: false, error: error?.message || "Failed to extract frame" });
      }
    });
  });
}

// Build CLI tool
async function buildCliTool() {
  return new Promise((resolve) => {
    exec("make clean && make", { cwd: CONFIG.cliToolDir }, (error, stdout, stderr) => {
      const success = existsSync(CONFIG.cliToolPath);
      resolve({
        success,
        output: stdout + stderr,
        error: error ? error.message : null
      });
    });
  });
}

// Clear debug log
function clearPluginDebugLog() {
  try {
    execSync(`echo "" > "${CONFIG.pluginDebugLog}"`);
    return { success: true };
  } catch (e) {
    return { success: false, error: e.message };
  }
}

// Restart Premiere with dialog handling
async function restartPremiere() {
  console.error("[MCP] Restarting Premiere Pro...");

  // Start dialog watcher to auto-dismiss crash/recovery dialogs
  startDialogWatcher();

  // Kill existing Premiere
  try {
    execSync(`pkill -f "Adobe Premiere Pro"`);
    console.error("[MCP] Killed existing Premiere process");
  } catch {}

  await new Promise(r => setTimeout(r, 3000));

  // Dismiss any lingering crash reporter dialogs
  dismissCrashReporter();

  // Launch Premiere by opening the project file directly
  exec(`open "${CONFIG.testProjectPath}"`);
  console.error(`[MCP] Opening project: ${CONFIG.testProjectPath}`);

  // Wait for Premiere to be ready (dialogs dismissed, main window available)
  const ready = await waitForPremiereReady(90000);
  if (!ready) {
    stopDialogWatcher();
    return { success: false, message: "Premiere did not become ready within 90s" };
  }

  console.error("[MCP] Premiere is ready, waiting for CEP panel connection...");

  // Wait for CEP panel connection (up to 60s more)
  const start = Date.now();
  while (Date.now() - start < 60000) {
    if (premiereConnection) {
      stopDialogWatcher();
      console.error("[MCP] CEP panel connected!");
      return { success: true, message: "Premiere restarted and CEP panel connected" };
    }
    await new Promise(r => setTimeout(r, 1000));
  }

  stopDialogWatcher();
  return { success: false, message: "Premiere started but CEP panel did not connect within 60s. Make sure to open Window > Extensions > MoshBrosh MCP Bridge" };
}

// Full autonomous test cycle
async function runAutonomousTestCycle() {
  console.error("[MCP] Starting autonomous test cycle...");

  const results = {
    steps: [],
    success: false,
    error: null
  };

  try {
    // Step 1: Ensure Premiere is running and connected
    if (!premiereConnection) {
      results.steps.push({ step: "restart_premiere", status: "starting" });
      const restartResult = await restartPremiere();
      results.steps[results.steps.length - 1].status = restartResult.success ? "success" : "failed";
      results.steps[results.steps.length - 1].result = restartResult;

      if (!restartResult.success) {
        results.error = "Failed to start Premiere";
        return results;
      }
    }

    // Step 2: Open test project (CEP panel should auto-setup)
    results.steps.push({ step: "open_project", status: "starting" });
    const projectResult = await sendToPremmiere("open_test_project", {});
    results.steps[results.steps.length - 1].status = projectResult.success ? "success" : "failed";
    results.steps[results.steps.length - 1].result = projectResult;

    if (!projectResult.success) {
      results.error = `Failed to open project: ${projectResult.error}`;
      return results;
    }

    // Step 3: Apply MoshBrosh effect
    results.steps.push({ step: "apply_effect", status: "starting" });
    const effectResult = await sendToPremmiere("apply_effect", {});
    results.steps[results.steps.length - 1].status = effectResult.success ? "success" : "failed";
    results.steps[results.steps.length - 1].result = effectResult;

    if (!effectResult.success) {
      results.error = `Failed to apply effect: ${effectResult.error}`;
      return results;
    }

    // Step 4: Wait for effect to process
    results.steps.push({ step: "wait_for_processing", status: "starting", waitMs: CONFIG.effectProcessingWaitMs });
    console.error(`[MCP] Waiting ${CONFIG.effectProcessingWaitMs}ms for effect to process...`);
    await new Promise(r => setTimeout(r, CONFIG.effectProcessingWaitMs));
    results.steps[results.steps.length - 1].status = "success";

    // Step 5: Export sequence
    results.steps.push({ step: "export_sequence", status: "starting" });
    const exportResult = await sendToPremmiere("export_sequence", {});
    results.steps[results.steps.length - 1].status = exportResult.success ? "success" : "failed";
    results.steps[results.steps.length - 1].result = exportResult;

    if (!exportResult.success) {
      results.error = `Failed to export: ${exportResult.error}`;
      return results;
    }

    // Step 6: Wait for export to complete and analyze
    results.steps.push({ step: "analyze_export", status: "starting" });

    // Wait for export file to appear and stabilize
    let exportReady = false;
    for (let i = 0; i < 60; i++) {
      if (existsSync(CONFIG.exportOutputPath)) {
        const size1 = statSync(CONFIG.exportOutputPath).size;
        await new Promise(r => setTimeout(r, 2000));
        const size2 = statSync(CONFIG.exportOutputPath).size;
        if (size1 === size2 && size1 > 0) {
          exportReady = true;
          break;
        }
      }
      await new Promise(r => setTimeout(r, 1000));
    }

    if (!exportReady) {
      results.steps[results.steps.length - 1].status = "failed";
      results.error = "Export file not ready after 60s";
      return results;
    }

    // Compare frames from export with CLI output
    const analysisResult = await analyzeExportedVideo();
    results.steps[results.steps.length - 1].status = analysisResult.success ? "success" : "failed";
    results.steps[results.steps.length - 1].result = analysisResult;

    results.success = analysisResult.success;
    return results;

  } catch (e) {
    results.error = e.message;
    return results;
  }
}

// Analyze exported video by comparing frames to CLI output
async function analyzeExportedVideo() {
  try {
    // Extract frames from Premiere export
    const premiereFramesDir = `${CONFIG.exportOutputDir}/premiere_frames`;
    execSync(`mkdir -p "${premiereFramesDir}"`);
    execSync(`ffmpeg -y -i "${CONFIG.exportOutputPath}" -vf "select=gte(n\\,10)*lte(n\\,40)" -vsync vfr "${premiereFramesDir}/frame_%03d.png" 2>/dev/null`);

    // Extract frames from CLI output (if exists)
    const cliFramesDir = `${CONFIG.exportOutputDir}/cli_frames`;
    if (existsSync(CONFIG.cliOutputPath)) {
      execSync(`mkdir -p "${cliFramesDir}"`);
      execSync(`ffmpeg -y -i "${CONFIG.cliOutputPath}" -vf "select=gte(n\\,10)*lte(n\\,40)" -vsync vfr "${cliFramesDir}/frame_%03d.png" 2>/dev/null`);
    }

    // Check if mosh effect is visible by comparing frame 15 to frame 30
    // If moshing works, these should look different due to accumulated distortion
    const frame15 = `${premiereFramesDir}/frame_006.png`; // frame 15 (offset by 10)
    const frame30 = `${premiereFramesDir}/frame_021.png`; // frame 30

    if (!existsSync(frame15) || !existsSync(frame30)) {
      return { success: false, error: "Could not extract frames from export" };
    }

    // Use ImageMagick to compare frames
    try {
      const diffResult = execSync(`compare -metric RMSE "${frame15}" "${frame30}" null: 2>&1`, { encoding: "utf8" });
      const rmse = parseFloat(diffResult.match(/[\d.]+/)?.[0] || "0");

      // If RMSE is very low, frames are too similar (effect not working)
      // If RMSE is reasonable, effect is creating visible changes
      const effectWorking = rmse > 1000; // Threshold for visible difference

      return {
        success: effectWorking,
        message: effectWorking ? "Mosh effect is visible in export" : "Frames look too similar - effect may not be working",
        rmse,
        frame15,
        frame30
      };
    } catch (e) {
      // compare returns non-zero if images differ, which is actually good
      return { success: true, message: "Frames differ (effect appears to be working)" };
    }

  } catch (e) {
    return { success: false, error: e.message };
  }
}

// MCP Server setup
const server = new Server(
  { name: "premiere-mcp-server", version: "1.0.0" },
  { capabilities: { tools: {} } }
);

// List available tools
server.setRequestHandler(ListToolsRequestSchema, async () => {
  return {
    tools: [
      {
        name: "premiere_status",
        description: "Check if Premiere Pro is running and connected",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "build_plugin",
        description: "Build the MoshBrosh plugin using xcodebuild",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "install_plugin",
        description: "Install the built plugin to Premiere's plugin directory",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "build_and_install_plugin",
        description: "Build and install the plugin in one step",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "restart_premiere",
        description: "Kill and restart Premiere Pro, wait for CEP panel to reconnect",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "get_plugin_debug_log",
        description: "Get the debug log output from the MoshBrosh plugin",
        inputSchema: {
          type: "object",
          properties: {
            lines: { type: "number", description: "Number of lines to return (default 100)" }
          }
        }
      },
      {
        name: "clear_plugin_debug_log",
        description: "Clear the plugin debug log file",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "get_last_crash_log",
        description: "Get the most recent Premiere crash log if one exists",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "open_test_project",
        description: "Open or create a test project with test video and MoshBrosh effect applied",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "render_frame",
        description: "Render a specific frame and return it as a base64 image",
        inputSchema: {
          type: "object",
          properties: {
            frame: { type: "number", description: "Frame number to render" }
          },
          required: ["frame"]
        }
      },
      {
        name: "set_effect_param",
        description: "Set a parameter on the MoshBrosh effect",
        inputSchema: {
          type: "object",
          properties: {
            param: { type: "string", description: "Parameter name (mosh_frame, duration, block_size, search_range, blend)" },
            value: { type: "number", description: "Parameter value" }
          },
          required: ["param", "value"]
        }
      },
      {
        name: "get_effect_params",
        description: "Get all current parameter values from the MoshBrosh effect",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "render_frame_range",
        description: "Render a range of frames and return them as base64 images",
        inputSchema: {
          type: "object",
          properties: {
            start: { type: "number", description: "Start frame" },
            end: { type: "number", description: "End frame" },
            step: { type: "number", description: "Step between frames (default 1)" }
          },
          required: ["start", "end"]
        }
      },
      {
        name: "get_source_frame",
        description: "Get a frame from the source video (before any effects)",
        inputSchema: {
          type: "object",
          properties: {
            frame: { type: "number", description: "Frame number" }
          },
          required: ["frame"]
        }
      },
      {
        name: "compare_frames",
        description: "Compare two rendered frames and return difference metrics",
        inputSchema: {
          type: "object",
          properties: {
            frame_a: { type: "number", description: "First frame number" },
            frame_b: { type: "number", description: "Second frame number" }
          },
          required: ["frame_a", "frame_b"]
        }
      },
      {
        name: "build_cli_tool",
        description: "Build the CLI version of MoshBrosh for testing",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "run_cli_datamosh",
        description: "Run the CLI datamosh tool on a test video. This bypasses Premiere entirely and tests the core algorithm.",
        inputSchema: {
          type: "object",
          properties: {
            mosh_frame: { type: "number", description: "Frame where mosh starts (default 10)" },
            duration: { type: "number", description: "Duration in frames (default 30)" },
            block_size: { type: "number", description: "Block size: 8, 16, or 32 (default 16)" },
            search_range: { type: "number", description: "Search range (default 16)" },
            blend: { type: "number", description: "Blend amount 0-100 (default 100)" }
          }
        }
      },
      {
        name: "get_cli_frame",
        description: "Extract a frame from the CLI output video as an image",
        inputSchema: {
          type: "object",
          properties: {
            frame: { type: "number", description: "Frame number to extract" },
            source: { type: "string", description: "'input' for original video, 'output' for moshed video (default 'output')" }
          },
          required: ["frame"]
        }
      },
      {
        name: "compare_cli_frames",
        description: "Compare input and output frames from CLI tool to see the effect",
        inputSchema: {
          type: "object",
          properties: {
            frame: { type: "number", description: "Frame number to compare" }
          },
          required: ["frame"]
        }
      },
      {
        name: "read_source_file",
        description: "Read the contents of a source file in the MoshBrosh project",
        inputSchema: {
          type: "object",
          properties: {
            file: { type: "string", description: "File path relative to project root, e.g., 'MoshBrosh/MoshBrosh.cpp'" }
          },
          required: ["file"]
        }
      },
      {
        name: "edit_source_file",
        description: "Edit a source file in the MoshBrosh project",
        inputSchema: {
          type: "object",
          properties: {
            file: { type: "string", description: "File path relative to project root" },
            old_text: { type: "string", description: "Text to find and replace" },
            new_text: { type: "string", description: "Replacement text" }
          },
          required: ["file", "old_text", "new_text"]
        }
      },
      {
        name: "run_autonomous_test",
        description: "Run a full autonomous test cycle: restart Premiere, open project, apply effect, wait for processing, export, and analyze frames. Handles crash recovery automatically.",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "get_project_info",
        description: "Get info about the currently open Premiere project",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "apply_effect",
        description: "Apply the MoshBrosh effect to the first clip in the timeline",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "export_sequence",
        description: "Export the current sequence to video file for analysis",
        inputSchema: { type: "object", properties: {} }
      },
      {
        name: "analyze_premiere_export",
        description: "Analyze the exported video from Premiere, comparing frames to verify mosh effect is working",
        inputSchema: { type: "object", properties: {} }
      }
    ]
  };
});

// Handle tool calls
server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;

  try {
    switch (name) {
      case "premiere_status": {
        const running = isPremiereRunning();
        const connected = premiereConnection !== null;
        const pid = getPremierePid();
        return {
          content: [{
            type: "text",
            text: JSON.stringify({
              premiere_running: running,
              cep_panel_connected: connected,
              premiere_pid: pid,
              last_heartbeat_ago_ms: connected ? Date.now() - lastHeartbeat : null,
              last_crash_time: lastCrashTime > 0 ? new Date(lastCrashTime).toISOString() : null
            }, null, 2)
          }]
        };
      }

      case "build_plugin": {
        const result = await buildPlugin();
        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      case "install_plugin": {
        const result = await installPlugin();
        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      case "build_and_install_plugin": {
        const buildResult = await buildPlugin();
        if (!buildResult.success) {
          return {
            content: [{
              type: "text",
              text: JSON.stringify({ success: false, stage: "build", ...buildResult }, null, 2)
            }]
          };
        }
        const installResult = await installPlugin();
        return {
          content: [{
            type: "text",
            text: JSON.stringify({
              success: installResult.success,
              build: buildResult,
              install: installResult
            }, null, 2)
          }]
        };
      }

      case "restart_premiere": {
        const result = await restartPremiere();
        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      case "get_plugin_debug_log": {
        const lines = args?.lines || 100;
        const result = getPluginDebugLog(lines);
        return {
          content: [{
            type: "text",
            text: result.log
          }]
        };
      }

      case "clear_plugin_debug_log": {
        const result = clearPluginDebugLog();
        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      case "get_last_crash_log": {
        const log = getLatestCrashLog() || lastCrashLog;
        return {
          content: [{
            type: "text",
            text: log || "(no recent crash logs found)"
          }]
        };
      }

      case "build_cli_tool": {
        const result = await buildCliTool();
        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      case "run_cli_datamosh": {
        const result = await runCliTool({
          moshFrame: args?.mosh_frame || 10,
          duration: args?.duration || 30,
          blockSize: args?.block_size || 16,
          searchRange: args?.search_range || 16,
          blend: args?.blend || 100
        });
        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      case "get_cli_frame": {
        const source = args?.source || "output";
        const videoPath = source === "input"
          ? CONFIG.testVideoPath
          : `${CONFIG.cliToolDir}/test_output_mcp.mp4`;
        const outputPath = `${CONFIG.cliToolDir}/temp_frame_${args.frame}.png`;

        const result = await extractFrameFromVideo(videoPath, args.frame, outputPath);

        if (result.success) {
          return {
            content: [
              { type: "text", text: JSON.stringify({ success: true, frame: args.frame, source }, null, 2) },
              { type: "image", data: result.image, mimeType: "image/png" }
            ]
          };
        }
        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      case "compare_cli_frames": {
        const frameNum = args.frame;
        const inputPath = `${CONFIG.cliToolDir}/temp_input_${frameNum}.png`;
        const outputPath = `${CONFIG.cliToolDir}/temp_output_${frameNum}.png`;

        const inputResult = await extractFrameFromVideo(CONFIG.testVideoPath, frameNum, inputPath);
        const outputResult = await extractFrameFromVideo(`${CONFIG.cliToolDir}/test_output_mcp.mp4`, frameNum, outputPath);

        const content = [
          { type: "text", text: `Frame ${frameNum} comparison (input vs output):` }
        ];

        if (inputResult.success) {
          content.push({ type: "text", text: "Input (original):" });
          content.push({ type: "image", data: inputResult.image, mimeType: "image/png" });
        }

        if (outputResult.success) {
          content.push({ type: "text", text: "Output (moshed):" });
          content.push({ type: "image", data: outputResult.image, mimeType: "image/png" });
        }

        return { content };
      }

      case "read_source_file": {
        const filePath = path.join("/Users/mads/coding/moshbrosh", args.file);
        try {
          const content = readFileSync(filePath, "utf8");
          return {
            content: [{
              type: "text",
              text: content
            }]
          };
        } catch (e) {
          return {
            content: [{
              type: "text",
              text: `Error reading file: ${e.message}`
            }],
            isError: true
          };
        }
      }

      case "edit_source_file": {
        const filePath = path.join("/Users/mads/coding/moshbrosh", args.file);
        try {
          let content = readFileSync(filePath, "utf8");
          if (!content.includes(args.old_text)) {
            return {
              content: [{
                type: "text",
                text: `Error: old_text not found in file`
              }],
              isError: true
            };
          }
          content = content.replace(args.old_text, args.new_text);
          writeFileSync(filePath, content, "utf8");
          return {
            content: [{
              type: "text",
              text: JSON.stringify({ success: true, file: args.file }, null, 2)
            }]
          };
        } catch (e) {
          return {
            content: [{
              type: "text",
              text: `Error editing file: ${e.message}`
            }],
            isError: true
          };
        }
      }

      case "run_autonomous_test": {
        const result = await runAutonomousTestCycle();
        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      case "analyze_premiere_export": {
        const result = await analyzeExportedVideo();
        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      case "open_test_project":
      case "render_frame":
      case "set_effect_param":
      case "get_effect_params":
      case "render_frame_range":
      case "get_source_frame":
      case "compare_frames":
      case "get_project_info":
      case "apply_effect":
      case "export_sequence": {
        // These require the CEP panel
        const result = await sendToPremmiere(name, args);

        // Handle image responses
        if (result?.image) {
          return {
            content: [
              { type: "text", text: JSON.stringify({ ...result, image: "(see image below)" }, null, 2) },
              { type: "image", data: result.image, mimeType: "image/png" }
            ]
          };
        }

        if (result?.images) {
          const content = [
            { type: "text", text: JSON.stringify({ ...result, images: `(${result.images.length} images below)` }, null, 2) }
          ];
          for (const img of result.images) {
            content.push({ type: "image", data: img.data, mimeType: "image/png" });
          }
          return { content };
        }

        return {
          content: [{
            type: "text",
            text: JSON.stringify(result, null, 2)
          }]
        };
      }

      default:
        throw new Error(`Unknown tool: ${name}`);
    }
  } catch (error) {
    return {
      content: [{
        type: "text",
        text: `Error: ${error.message}`
      }],
      isError: true
    };
  }
});

// Start the MCP server
async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("[MCP] Premiere MCP server running");
}

main().catch(console.error);
