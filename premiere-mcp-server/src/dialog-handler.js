/**
 * Dialog Handler - Automatically dismisses Premiere crash/recovery dialogs
 * Uses AppleScript to interact with system dialogs
 */

import { execSync, exec } from "child_process";

// Dismiss the macOS crash reporter dialog by clicking "Ignore"
export function dismissCrashReporter() {
  try {
    // Look for crash reporter dialog and click "Ignore" (not Reopen)
    const script = `
      tell application "System Events"
        set foundDialog to false
        repeat with proc in (every process whose name contains "UserNotification" or name contains "Problem Report" or name contains "crash")
          try
            set foundDialog to true
            tell proc
              -- Try "Ignore" first, then "Don't Send", then "OK"
              try
                click button "Ignore" of window 1
              on error
                try
                  click button "Don't Send" of window 1
                on error
                  try
                    click button "OK" of window 1
                  end try
                end try
              end try
            end tell
          end try
        end repeat
        return foundDialog
      end tell
    `;
    const result = execSync(`osascript -e '${script}'`, { encoding: "utf8", timeout: 5000 });
    return result.trim() === "true";
  } catch (e) {
    return false;
  }
}

// Dismiss Premiere's "Reopen project" recovery dialog by pressing Escape or clicking Don't Reopen
export function dismissRecoveryDialog() {
  try {
    // First try to find and click "Don't Reopen" or similar button
    const script = `
      tell application "System Events"
        tell process "Adobe Premiere Pro 2025"
          set foundDialog to false
          try
            -- Look for recovery dialog
            repeat with w in (every window)
              if (name of w contains "quit unexpectedly" or name of w contains "Recover") then
                set foundDialog to true
                -- Try to find and click the dismiss button
                repeat with b in (every button of w)
                  set btnName to name of b
                  if btnName contains "Don't" or btnName contains "Cancel" or btnName contains "No" then
                    click b
                    return true
                  end if
                end repeat
                -- If no dismiss button found, press Escape
                key code 53
                return true
              end if
            end repeat

            -- Also check for sheets (modal dialogs)
            repeat with w in (every window)
              repeat with s in (every sheet of w)
                set foundDialog to true
                repeat with b in (every button of s)
                  set btnName to name of b
                  if btnName contains "Don't" or btnName contains "Cancel" or btnName contains "No" then
                    click b
                    return true
                  end if
                end repeat
              end repeat
            end repeat
          end try
          return foundDialog
        end tell
      end tell
    `;
    const result = execSync(`osascript -e '${script}'`, { encoding: "utf8", timeout: 5000 });
    return result.trim() === "true";
  } catch (e) {
    return false;
  }
}

// Check if Premiere has any modal dialog open
export function hasModalDialog() {
  try {
    const script = `
      tell application "System Events"
        tell process "Adobe Premiere Pro 2025"
          set dialogCount to 0
          repeat with w in (every window)
            if subrole of w is "AXDialog" or subrole of w is "AXSystemDialog" then
              set dialogCount to dialogCount + 1
            end if
            set dialogCount to dialogCount + (count of sheets of w)
          end repeat
          return dialogCount
        end tell
      end tell
    `;
    const result = execSync(`osascript -e '${script}'`, { encoding: "utf8", timeout: 5000 });
    return parseInt(result.trim()) > 0;
  } catch (e) {
    return false;
  }
}

// Press Escape to dismiss any dialog
export function pressEscape() {
  try {
    const script = `
      tell application "System Events"
        tell process "Adobe Premiere Pro 2025"
          key code 53
        end tell
      end tell
    `;
    execSync(`osascript -e '${script}'`, { timeout: 2000 });
    return true;
  } catch (e) {
    return false;
  }
}

// Click a button by name in Premiere
export function clickButton(buttonName) {
  try {
    const script = `
      tell application "System Events"
        tell process "Adobe Premiere Pro 2025"
          repeat with w in (every window)
            try
              click button "${buttonName}" of w
              return true
            end try
            repeat with s in (every sheet of w)
              try
                click button "${buttonName}" of s
                return true
              end try
            end repeat
          end repeat
          return false
        end tell
      end tell
    `;
    const result = execSync(`osascript -e '${script}'`, { encoding: "utf8", timeout: 5000 });
    return result.trim() === "true";
  } catch (e) {
    return false;
  }
}

// Wait for Premiere to be ready (no dialogs, main window available)
export async function waitForPremiereReady(maxWaitMs = 60000) {
  const startTime = Date.now();

  while (Date.now() - startTime < maxWaitMs) {
    // First dismiss any crash reporter
    dismissCrashReporter();

    // Check if Premiere is running
    try {
      execSync('pgrep -f "Adobe Premiere Pro"', { encoding: "utf8" });
    } catch {
      // Premiere not running yet
      await sleep(1000);
      continue;
    }

    // Try to dismiss recovery dialog
    dismissRecoveryDialog();

    // Check if we have modal dialogs
    if (!hasModalDialog()) {
      // No dialogs - check if main window is ready
      try {
        const script = `
          tell application "System Events"
            tell process "Adobe Premiere Pro 2025"
              return (count of windows) > 0
            end tell
          end tell
        `;
        const result = execSync(`osascript -e '${script}'`, { encoding: "utf8", timeout: 5000 });
        if (result.trim() === "true") {
          return true;
        }
      } catch {}
    }

    await sleep(1000);
  }

  return false;
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

// Start a background watcher that continuously dismisses dialogs
let dialogWatcherInterval = null;

export function startDialogWatcher() {
  if (dialogWatcherInterval) return;

  console.error("[DialogHandler] Starting dialog watcher");

  dialogWatcherInterval = setInterval(() => {
    dismissCrashReporter();
    dismissRecoveryDialog();
  }, 2000);
}

export function stopDialogWatcher() {
  if (dialogWatcherInterval) {
    clearInterval(dialogWatcherInterval);
    dialogWatcherInterval = null;
    console.error("[DialogHandler] Stopped dialog watcher");
  }
}
