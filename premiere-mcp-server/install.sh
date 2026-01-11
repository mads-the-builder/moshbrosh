#!/bin/bash
# MoshBrosh MCP Bridge - Installation Script
# This installs the MCP server and CEP panel for Premiere Pro automation

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CEP_PANEL_DIR="$SCRIPT_DIR/cep-panel"
CEP_EXTENSIONS_DIR="$HOME/Library/Application Support/Adobe/CEP/extensions"

echo "=== MoshBrosh MCP Bridge Installation ==="
echo ""

# Check for Node.js
if ! command -v node &> /dev/null; then
    echo "ERROR: Node.js is not installed."
    echo ""
    echo "Please install Node.js first:"
    echo "  brew install node"
    echo ""
    echo "Or download from: https://nodejs.org/"
    exit 1
fi

echo "Found Node.js: $(node --version)"
echo ""

# Step 1: Enable unsigned CEP extensions (required for development)
echo "Step 1: Enabling unsigned CEP extensions..."
defaults write com.adobe.CSXS.12 PlayerDebugMode 1
defaults write com.adobe.CSXS.11 PlayerDebugMode 1
defaults write com.adobe.CSXS.10 PlayerDebugMode 1
defaults write com.adobe.CSXS.9 PlayerDebugMode 1
echo "  Done. (Set PlayerDebugMode to 1 for CSXS 9-12)"
echo ""

# Step 2: Install npm dependencies for MCP server
echo "Step 2: Installing MCP server dependencies..."
cd "$SCRIPT_DIR"
npm install
echo "  Done."
echo ""

# Step 3: Create CEP extensions directory if needed
echo "Step 3: Installing CEP panel..."
mkdir -p "$CEP_EXTENSIONS_DIR"

# Remove old installation if exists
rm -rf "$CEP_EXTENSIONS_DIR/com.moshbrosh.mcpbridge"

# Create symlink to development panel (allows editing without reinstalling)
ln -sf "$CEP_PANEL_DIR" "$CEP_EXTENSIONS_DIR/com.moshbrosh.mcpbridge"
echo "  Installed to: $CEP_EXTENSIONS_DIR/com.moshbrosh.mcpbridge"
echo ""

# Step 4: Create Claude Code MCP config
echo "Step 4: MCP Configuration for Claude Code..."
echo ""
echo "Add this to your Claude Code MCP settings (~/.claude/mcp_settings.json):"
echo ""
echo '{'
echo '  "mcpServers": {'
echo '    "premiere": {'
echo '      "command": "node",'
echo "      \"args\": [\"$SCRIPT_DIR/src/index.js\"]"
echo '    }'
echo '  }'
echo '}'
echo ""

# Step 5: Instructions
echo "=== Installation Complete ==="
echo ""
echo "To use the MCP bridge:"
echo ""
echo "1. Restart Premiere Pro (required after enabling debug mode)"
echo ""
echo "2. Open Window > Extensions > MoshBrosh MCP Bridge in Premiere"
echo "   (The panel should show 'Connected to MCP Server' when running)"
echo ""
echo "3. Start the MCP server:"
echo "   cd $SCRIPT_DIR"
echo "   npm start"
echo ""
echo "4. Configure Claude Code to use the MCP server (see config above)"
echo ""
echo "5. In Claude Code, you can now use premiere_* tools to control Premiere!"
echo ""
