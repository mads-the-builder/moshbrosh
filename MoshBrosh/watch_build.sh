#!/bin/bash
# MoshBrosh - Build, Install, and Watch Debug Log
# Usage: ./watch_build.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/Mac"
DEBUG_LOG="$HOME/Desktop/moshbrosh_debug.log"
USER_PLUGIN_DIR="$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"

# Create user plugin dir if needed
mkdir -p "$USER_PLUGIN_DIR"

# Clear old log
echo "=== Build started at $(date) ===" > "$DEBUG_LOG"

echo "Building MoshBrosh..."
cd "$PROJECT_DIR"
xcodebuild -project MoshBrosh.xcodeproj \
    -scheme MoshBrosh \
    -configuration Debug \
    AE_SDK_BASE_PATH=/Users/mads/coding/moshbrosh/AfterEffectsSDK_25.6_61_mac/ae25.6_61.64bit.AfterEffectsSDK \
    2>&1 | grep -E "(error:|warning:|BUILD|Linking|Compiling)" || true

# Check if build succeeded
if [ -d "$HOME/Library/Developer/Xcode/DerivedData/MoshBrosh-"*/Build/Products/Debug/MoshBrosh.plugin ]; then
    echo ""
    echo "Copying to user plugin folder..."
    rm -rf "$USER_PLUGIN_DIR/MoshBrosh.plugin"
    cp -R "$HOME/Library/Developer/Xcode/DerivedData/MoshBrosh-"*/Build/Products/Debug/MoshBrosh.plugin "$USER_PLUGIN_DIR/"

    echo ""
    echo "============================================"
    echo "BUILD SUCCESSFUL!"
    echo "Plugin installed to: $USER_PLUGIN_DIR"
    echo "Debug log: $DEBUG_LOG"
    echo "============================================"
    echo ""
    echo "Now restart Premiere and test."
    echo "Watch the log with: tail -f $DEBUG_LOG"
    echo ""
    echo "Starting log tail..."
    echo ""
    tail -f "$DEBUG_LOG"
else
    echo ""
    echo "BUILD FAILED - check errors above"
    exit 1
fi
