#!/bin/sh
# ─── Nyx Installer for Linux/macOS ──────────────────────────────────────────
# Usage: curl -sSf https://nyx.nemesistech.ee/install.sh | sh
# Or:    chmod +x install.sh && ./install.sh
# ─────────────────────────────────────────────────────────────────────────────

set -e

NYX_VERSION="1.0.0"
INSTALL_DIR="$HOME/.nyx"
BIN_DIR="$INSTALL_DIR"
LIBS_DIR="$INSTALL_DIR/libs"

# Colors
CYAN='\033[36m'
YELLOW='\033[33m'
GREEN='\033[32m'
RED='\033[31m'
GRAY='\033[90m'
WHITE='\033[37m'
RESET='\033[0m'

echo ""
echo "${CYAN}  ══════════════════════════════════════${RESET}"
echo "${CYAN}         Nyx Language Installer${RESET}"
echo "${CYAN}       Nemesis Technologies${RESET}"
echo "${CYAN}  ══════════════════════════════════════${RESET}"
echo ""

# ─── Detect OS and architecture ──────────────────────────────────────────────

OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Linux*)   PLATFORM="linux" ;;
    Darwin*)  PLATFORM="macos" ;;
    *)        echo "${RED}Unsupported OS: $OS${RESET}"; exit 1 ;;
esac

case "$ARCH" in
    x86_64|amd64)  ARCH="x64" ;;
    aarch64|arm64) ARCH="arm64" ;;
    *)             echo "${RED}Unsupported architecture: $ARCH${RESET}"; exit 1 ;;
esac

BINARY_NAME="nyx-${PLATFORM}-${ARCH}"
BINARY_URL="https://github.com/nemesis-security/nyx/releases/latest/download/${BINARY_NAME}"

echo "${YELLOW}[1/4] Creating directories...${RESET}"
mkdir -p "$BIN_DIR"
mkdir -p "$LIBS_DIR"
echo "${GRAY}      $INSTALL_DIR${RESET}"

# ─── Download binary ─────────────────────────────────────────────────────────

echo "${YELLOW}[2/4] Downloading nyx $NYX_VERSION ($PLATFORM-$ARCH)...${RESET}"

BINARY_PATH="$BIN_DIR/nyx"

if command -v curl >/dev/null 2>&1; then
    curl -sL "$BINARY_URL" -o "$BINARY_PATH" || true
elif command -v wget >/dev/null 2>&1; then
    wget -q "$BINARY_URL" -O "$BINARY_PATH" || true
else
    echo "${RED}  Neither curl nor wget found. Install one and retry.${RESET}"
    exit 1
fi

if [ -f "$BINARY_PATH" ] && [ -s "$BINARY_PATH" ]; then
    chmod +x "$BINARY_PATH"
    echo "${GRAY}      Downloaded to $BINARY_PATH${RESET}"
else
    echo ""
    echo "${RED}  Download failed. You can install manually:${RESET}"
    echo "${GRAY}  1. Build nyx from source: https://github.com/nemesis-security/nyx${RESET}"
    echo "${GRAY}  2. Copy the binary to $BIN_DIR/nyx${RESET}"
    echo "${GRAY}  3. Run this script again (it will set up PATH)${RESET}"
    echo ""

    if [ ! -f "$BINARY_PATH" ]; then
        echo "${RED}  No existing nyx binary found. Aborting.${RESET}"
        exit 1
    fi
    echo "${YELLOW}  Found existing binary, continuing with PATH setup...${RESET}"
fi

# ─── Set PATH ────────────────────────────────────────────────────────────────

echo "${YELLOW}[3/4] Updating PATH...${RESET}"

add_to_path() {
    local rc_file="$1"
    if [ -f "$rc_file" ]; then
        if ! grep -q "$BIN_DIR" "$rc_file" 2>/dev/null; then
            echo "" >> "$rc_file"
            echo "# Nyx language" >> "$rc_file"
            echo "export PATH=\"\$PATH:$BIN_DIR\"" >> "$rc_file"
            echo "${GRAY}      Updated $rc_file${RESET}"
            return 0
        else
            echo "${GRAY}      Already in $rc_file${RESET}"
            return 0
        fi
    fi
    return 1
}

PATH_ADDED=false

# Try common shell configs
if [ -n "$ZSH_VERSION" ] || [ -f "$HOME/.zshrc" ]; then
    add_to_path "$HOME/.zshrc" && PATH_ADDED=true
fi

if [ -f "$HOME/.bashrc" ]; then
    add_to_path "$HOME/.bashrc" && PATH_ADDED=true
fi

if [ -f "$HOME/.bash_profile" ]; then
    add_to_path "$HOME/.bash_profile" && PATH_ADDED=true
elif [ -f "$HOME/.profile" ]; then
    add_to_path "$HOME/.profile" && PATH_ADDED=true
fi

if [ "$PATH_ADDED" = false ]; then
    echo "${YELLOW}      Could not detect shell config. Add this to your shell profile:${RESET}"
    echo "${WHITE}      export PATH=\"\$PATH:$BIN_DIR\"${RESET}"
fi

# Update current session
export PATH="$PATH:$BIN_DIR"

# ─── Verify ──────────────────────────────────────────────────────────────────

echo "${YELLOW}[4/4] Verifying...${RESET}"

if "$BINARY_PATH" --version >/dev/null 2>&1; then
    VERSION=$("$BINARY_PATH" --version 2>&1)
    echo "${GREEN}      $VERSION${RESET}"
else
    echo "${YELLOW}      Could not verify (you may need to restart your shell)${RESET}"
fi

# ─── Done ────────────────────────────────────────────────────────────────────

echo ""
echo "${GREEN}  Nyx installed successfully!${RESET}"
echo ""
echo "${GRAY}  Location:  $BIN_DIR/nyx${RESET}"
echo "${GRAY}  Libraries: $LIBS_DIR${RESET}"
echo ""
echo "${WHITE}  Open a new terminal and run:${RESET}"
echo "${CYAN}    nyx --version${RESET}"
echo "${CYAN}    nyx                    # start REPL${RESET}"
echo "${CYAN}    nyx script.nyx         # run a script${RESET}"
echo "${CYAN}    nyx install <package>  # install a package${RESET}"
echo ""
echo "${GRAY}  Docs: https://nyx.nemesistech.ee${RESET}"
echo "${GRAY}  Built by Nemesis Security - https://nemesistech.ee${RESET}"
echo ""
