# ─── Nyx Installer for Windows ────────────────────────────────────────────────
# Usage: powershell -ExecutionPolicy Bypass -File install.ps1
# Or:    iwr https://nyx.nemesistech.ee/install.ps1 | iex
# ──────────────────────────────────────────────────────────────────────────────

$ErrorActionPreference = "Stop"

$NyxVersion = "1.0.0"
$InstallDir = "$env:LOCALAPPDATA\Nyx"
$BinDir = "$InstallDir"
$LibsDir = "$InstallDir\libs"

Write-Host ""
Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "  ║         Nyx Language Installer        ║" -ForegroundColor Cyan
Write-Host "  ║       Nemesis Technologies            ║" -ForegroundColor Cyan
Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# ─── Create directories ──────────────────────────────────────────────────────

Write-Host "[1/4] Creating directories..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
New-Item -ItemType Directory -Force -Path $LibsDir | Out-Null
Write-Host "      $InstallDir" -ForegroundColor Gray

# ─── Download binary ─────────────────────────────────────────────────────────

$BinaryUrl = "https://github.com/nemesis-security/nyx/releases/latest/download/nyx-windows-x64.exe"
$BinaryPath = "$BinDir\nyx.exe"

Write-Host "[2/4] Downloading nyx $NyxVersion..." -ForegroundColor Yellow

try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $BinaryUrl -OutFile $BinaryPath -UseBasicParsing
    Write-Host "      Downloaded to $BinaryPath" -ForegroundColor Gray
} catch {
    Write-Host ""
    Write-Host "  Download failed. You can install manually:" -ForegroundColor Red
    Write-Host "  1. Download nyx.exe from https://github.com/nemesis-security/nyx/releases" -ForegroundColor Gray
    Write-Host "  2. Copy it to $BinDir" -ForegroundColor Gray
    Write-Host "  3. Run this script again (it will set up PATH)" -ForegroundColor Gray
    Write-Host ""

    # If binary already exists from a previous install or manual copy, continue
    if (-not (Test-Path $BinaryPath)) {
        Write-Host "  No existing nyx.exe found. Aborting." -ForegroundColor Red
        exit 1
    }
    Write-Host "  Found existing nyx.exe, continuing with PATH setup..." -ForegroundColor Yellow
}

# ─── Set PATH ────────────────────────────────────────────────────────────────

Write-Host "[3/4] Updating PATH..." -ForegroundColor Yellow

$CurrentPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($CurrentPath -notlike "*$BinDir*") {
    [Environment]::SetEnvironmentVariable("Path", "$CurrentPath;$BinDir", "User")
    Write-Host "      Added $BinDir to user PATH" -ForegroundColor Gray
} else {
    Write-Host "      Already in PATH" -ForegroundColor Gray
}

# Also update current session
if ($env:Path -notlike "*$BinDir*") {
    $env:Path = "$env:Path;$BinDir"
}

# ─── Verify ──────────────────────────────────────────────────────────────────

Write-Host "[4/4] Verifying..." -ForegroundColor Yellow

try {
    $version = & "$BinaryPath" --version 2>&1
    Write-Host "      $version" -ForegroundColor Green
} catch {
    Write-Host "      Could not verify (you may need to restart your terminal)" -ForegroundColor Yellow
}

# ─── Done ────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "  Nyx installed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "  Location:  $BinDir\nyx.exe" -ForegroundColor Gray
Write-Host "  Libraries: $LibsDir" -ForegroundColor Gray
Write-Host ""
Write-Host "  Open a new terminal and run:" -ForegroundColor White
Write-Host "    nyx --version" -ForegroundColor Cyan
Write-Host "    nyx                    # start REPL" -ForegroundColor Cyan
Write-Host "    nyx script.nyx         # run a script" -ForegroundColor Cyan
Write-Host "    nyx install <package>  # install a package" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Docs: https://nyx.nemesistech.ee" -ForegroundColor Gray
Write-Host "  Built by Nemesis Security - https://nemesistech.ee" -ForegroundColor DarkGray
Write-Host ""
