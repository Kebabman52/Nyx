#!/usr/bin/env bash

set -euo pipefail

NYX_REPO="Nemesis-Security/Nyx"
INSTALL_ROOT="${HOME}/.nyx"
BIN_DIR="${INSTALL_ROOT}/bin"
LIBS_DIR="${INSTALL_ROOT}/libs"
BINARY_PATH="${BIN_DIR}/nyx"
STATUS_FILE="${TMPDIR:-/tmp}/nyx_installer_status.json"
WATCHER_SCRIPT="${TMPDIR:-/tmp}/nyx_installer_watcher.sh"

color() {
    local code="$1"
    shift
    printf "\033[%sm%s\033[0m\n" "$code" "$*"
}

info()  { color "90" "  $*"; }
good()  { color "32" "  $*"; }
warn()  { color "33" "  $*"; }
fail()  { color "31" "  $*"; }
cyan()  { color "36" "$*"; }
white() { color "97" "$*"; }

show_banner() {
    clear 2>/dev/null || true
    echo
    color "36" "======================================"
    color "96" "            Nyx Installer"
    color "90" "        Nemesis Technologies"
    color "36" "======================================"
    echo
}

set_status() {
    local percent="$1"
    local text="$2"
    local state="${3:-running}"

    cat > "$STATUS_FILE" <<EOF
{"percent":$percent,"status":"$text","state":"$state"}
EOF
}

draw_progress() {
    local percent="$1"
    local text="$2"
    local width=30
    local filled=$((percent * width / 100))
    local empty=$((width - filled))

    printf "\r\033[2K"
    printf "  ["
    printf "%0.s#" $(seq 1 "$filled")
    printf "%0.s-" $(seq 1 "$empty")
    printf "] %3d%%  %s" "$percent" "$text"
}

detect_os() {
    case "$(uname -s)" in
        Darwin) echo "darwin" ;;
        Linux)  echo "linux" ;;
        *)
            fail "Unsupported OS: $(uname -s)"
            exit 1
            ;;
    esac
}

detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64) echo "x64" ;;
        arm64|aarch64) echo "arm64" ;;
        *)
            fail "Unsupported architecture: $(uname -m)"
            exit 1
            ;;
    esac
}

get_asset_url() {
    local os="$1"
    local arch="$2"
    local api_url="https://api.github.com/repos/${NYX_REPO}/releases/latest"

    local pattern=""
    case "$os" in
        linux)  pattern="nyx-linux-${arch}$|nyx-linux-${arch}[.]tar[.]gz$|nyx-linux-${arch}[.]zip$" ;;
        darwin) pattern="nyx-macos-${arch}$|nyx-darwin-${arch}$|nyx-macos-${arch}[.]tar[.]gz$|nyx-darwin-${arch}[.]tar[.]gz$|nyx-macos-${arch}[.]zip$|nyx-darwin-${arch}[.]zip$" ;;
    esac

    curl -fsSL "$api_url" \
        | grep -Eo '"browser_download_url":[[:space:]]*"[^"]+"' \
        | sed -E 's/"browser_download_url":[[:space:]]*"([^"]+)"/\1/' \
        | grep -Ei "$pattern" \
        | head -n 1
}

write_watcher_script() {
    cat > "$WATCHER_SCRIPT" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

STATUS_FILE="$1"

bar() {
    local percent="$1"
    local width=24
    local filled=$((percent * width / 100))
    local empty=$((width - filled))

    printf "["
    printf "%0.s#" $(seq 1 "$filled")
    printf "%0.s-" $(seq 1 "$empty")
    printf "]"
}

while :; do
    clear 2>/dev/null || true
    echo
    printf "\033[36m======================================\033[0m\n"
    printf "\033[96m          Nyx Installing...\033[0m\n"
    printf "\033[36m======================================\033[0m\n"
    echo

    if [ -f "$STATUS_FILE" ]; then
        raw="$(cat "$STATUS_FILE" 2>/dev/null || true)"

        percent="$(printf '%s' "$raw" | sed -n 's/.*"percent":\([0-9][0-9]*\).*/\1/p')"
        status="$(printf '%s' "$raw" | sed -n 's/.*"status":"\([^"]*\)".*/\1/p')"
        state="$(printf '%s' "$raw" | sed -n 's/.*"state":"\([^"]*\)".*/\1/p')"

        percent="${percent:-0}"
        status="${status:-starting...}"
        state="${state:-running}"

        printf "\033[32m  %s %s%%\033[0m\n" "$(bar "$percent")" "$percent"
        echo
        printf "  %s\n" "$status"

        if [ "$state" != "running" ]; then
            sleep 0.6
            exit 0
        fi
    else
        printf "\033[33m  waiting for installer...\033[0m\n"
    fi

    sleep 0.2
done
EOF

    chmod +x "$WATCHER_SCRIPT"
}

start_status_window() {
    write_watcher_script

    if [ "$(detect_os)" = "darwin" ] && command -v osascript >/dev/null 2>&1; then
        osascript <<EOF >/dev/null 2>&1 || true
tell application "Terminal"
    do script "bash '$WATCHER_SCRIPT' '$STATUS_FILE'; exit"
    activate
end tell
EOF
        return
    fi

    if command -v x-terminal-emulator >/dev/null 2>&1; then
        x-terminal-emulator -e bash "$WATCHER_SCRIPT" "$STATUS_FILE" >/dev/null 2>&1 &
        return
    fi

    if command -v gnome-terminal >/dev/null 2>&1; then
        gnome-terminal -- bash -c "bash '$WATCHER_SCRIPT' '$STATUS_FILE'" >/dev/null 2>&1 &
        return
    fi

    if command -v konsole >/dev/null 2>&1; then
        konsole -e bash "$WATCHER_SCRIPT" "$STATUS_FILE" >/dev/null 2>&1 &
        return
    fi

    if command -v xfce4-terminal >/dev/null 2>&1; then
        xfce4-terminal --command="bash '$WATCHER_SCRIPT' '$STATUS_FILE'" >/dev/null 2>&1 &
        return
    fi

    if command -v xterm >/dev/null 2>&1; then
        xterm -e bash "$WATCHER_SCRIPT" "$STATUS_FILE" >/dev/null 2>&1 &
        return
    fi
}

extract_or_install_binary() {
    local download_url="$1"
    local temp_dir="$2"
    local file_name
    file_name="$(basename "$download_url")"
    local downloaded_file="${temp_dir}/${file_name}"

    curl -fL "$download_url" -o "$downloaded_file"

    case "$downloaded_file" in
        *.tar.gz)
            tar -xzf "$downloaded_file" -C "$temp_dir"
            ;;
        *.zip)
            unzip -oq "$downloaded_file" -d "$temp_dir"
            ;;
    esac

    local found=""
    found="$(find "$temp_dir" -type f \( -name "nyx" -o -name "nyx.exe" \) | head -n 1 || true)"

    if [ -z "$found" ]; then
        fail "Could not find nyx binary in downloaded asset."
        exit 1
    fi

    cp "$found" "$BINARY_PATH"
    chmod +x "$BINARY_PATH"
}

shell_rc_file() {
    if [ -n "${ZSH_VERSION:-}" ]; then
        echo "${HOME}/.zshrc"
        return
    fi

    if [ -n "${BASH_VERSION:-}" ]; then
        if [ "$(detect_os)" = "darwin" ]; then
            echo "${HOME}/.bash_profile"
        else
            echo "${HOME}/.bashrc"
        fi
        return
    fi

    if [ -f "${HOME}/.zshrc" ]; then
        echo "${HOME}/.zshrc"
    elif [ -f "${HOME}/.bashrc" ]; then
        echo "${HOME}/.bashrc"
    else
        echo "${HOME}/.profile"
    fi
}

add_to_path() {
    local export_line='export PATH="$HOME/.nyx/bin:$PATH"'
    local rc_file
    rc_file="$(shell_rc_file)"

    mkdir -p "$(dirname "$rc_file")"
    touch "$rc_file"

    if ! grep -Fq "$export_line" "$rc_file"; then
        printf '\n%s\n' "$export_line" >> "$rc_file"
        good "Added Nyx to PATH in $rc_file"
    else
        info "Nyx PATH entry already exists in $rc_file"
    fi

    case ":$PATH:" in
        *":$BIN_DIR:"*) ;;
        *) export PATH="$BIN_DIR:$PATH" ;;
    esac
}

main() {
    show_banner
    set_status 0 "starting installer..."
    start_status_window

    local os arch download_url temp_dir version_out
    temp_dir="$(mktemp -d)"

    trap 'rm -rf "$temp_dir" "$STATUS_FILE" "$WATCHER_SCRIPT" >/dev/null 2>&1 || true' EXIT

    draw_progress 5 "detecting platform..."
    set_status 5 "detecting platform..."
    os="$(detect_os)"
    arch="$(detect_arch)"

    draw_progress 12 "creating directories..."
    set_status 12 "creating directories..."
    mkdir -p "$BIN_DIR" "$LIBS_DIR"

    draw_progress 28 "resolving latest release..."
    set_status 28 "resolving latest release..."
    download_url="$(get_asset_url "$os" "$arch" || true)"

    if [ -z "$download_url" ]; then
        set_status 100 "failed to find a matching release asset" "failed"
        echo
        fail "Could not find a matching Nyx release for ${os}-${arch}."
        info "Check the GitHub releases page and update the asset naming match if needed."
        exit 1
    fi

    draw_progress 52 "downloading nyx..."
    set_status 52 "downloading nyx..."
    extract_or_install_binary "$download_url" "$temp_dir"

    draw_progress 76 "updating path..."
    set_status 76 "updating path..."
    add_to_path

    draw_progress 91 "verifying installation..."
    set_status 91 "verifying installation..."
    if version_out="$("$BINARY_PATH" --version 2>/dev/null)"; then
        :
    else
        version_out="installed"
    fi

    draw_progress 100 "installation complete"
    set_status 100 "installation complete" "done"

    echo
    echo
    color "32" "======================================"
    color "92" "         Nyx installed successfully"
    color "32" "======================================"
    echo
    info "Executable: $BINARY_PATH"
    info "Libraries : $LIBS_DIR"
    info "Version   : $version_out"
    echo
    white "  Try in a new terminal:"
    cyan "    nyx --version"
    cyan "    nyx"
    cyan "    nyx script.nyx"
    cyan "    nyx install <package>"
    echo
}

main "$@"
