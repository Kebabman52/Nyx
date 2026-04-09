# Nyx Installer for Windows
# Usage:
#   powershell -ExecutionPolicy Bypass -File install.ps1
#   iwr https://nyx.nemesistech.ee/install.ps1 | iex

$ErrorActionPreference = "Stop"

$NyxVersion = "1.0.0"
$InstallDir = Join-Path $env:LOCALAPPDATA "Nyx"
$LibsDir    = Join-Path $InstallDir "libs"
$BinaryPath = Join-Path $InstallDir "nyx.exe"
$BinaryUrl  = "https://github.com/nemesis-security/nyx/releases/latest/download/nyx-windows-x64.exe"
$StatusFile = Join-Path $env:TEMP "nyx_installer_status.json"

function Show-Banner {
    Clear-Host
    Write-Host ""
    Write-Host "======================================" -ForegroundColor DarkCyan
    Write-Host "            Nyx Installer" -ForegroundColor Cyan
    Write-Host "        Nemesis Technologies" -ForegroundColor DarkGray
    Write-Host "======================================" -ForegroundColor DarkCyan
    Write-Host ""
}

function Write-Info($Text) {
    Write-Host "  $Text" -ForegroundColor Gray
}

function Write-Ok($Text) {
    Write-Host "  $Text" -ForegroundColor Green
}

function Write-Warn($Text) {
    Write-Host "  $Text" -ForegroundColor Yellow
}

function Write-Fail($Text) {
    Write-Host "  $Text" -ForegroundColor Red
}

function Update-InstallStatus {
    param(
        [int]$Percent,
        [string]$Status,
        [string]$State = "running"
    )

    $payload = @{
        percent = $Percent
        status  = $Status
        state   = $State
    } | ConvertTo-Json -Compress

    Set-Content -Path $StatusFile -Value $payload -Encoding UTF8 -Force
    Write-Progress -Activity "Installing Nyx" -Status $Status -PercentComplete $Percent
}

function Start-StatusWindow {
    $popupScript = @"
`$statusFile = '$($StatusFile -replace "'", "''")'
`$Host.UI.RawUI.WindowTitle = 'Nyx Installer'

function Render-Bar {
    param([int]`$Percent)

    `$total = 20
    `$filled = [math]::Floor((`$Percent / 100) * `$total)
    if (`$filled -lt 0) { `$filled = 0 }
    if (`$filled -gt `$total) { `$filled = `$total }

    `$empty = `$total - `$filled
    return ('#' * `$filled) + ('-' * `$empty)
}

while (`$true) {
    Clear-Host
    Write-Host ""
    Write-Host "======================================" -ForegroundColor DarkCyan
    Write-Host "          Nyx Installing..." -ForegroundColor Cyan
    Write-Host "======================================" -ForegroundColor DarkCyan
    Write-Host ""

    if (Test-Path `$statusFile) {
        try {
            `$data = Get-Content `$statusFile -Raw | ConvertFrom-Json
            `$bar = Render-Bar -Percent `$data.percent

            Write-Host ("  [{0}] {1}%%" -f `$bar, `$data.percent) -ForegroundColor Green
            Write-Host ""
            Write-Host ("  {0}" -f `$data.status) -ForegroundColor White

            if (`$data.state -ne 'running') {
                Start-Sleep -Milliseconds 500
                exit
            }
        }
        catch {
            Write-Host "  Waiting for installer..." -ForegroundColor Yellow
        }
    }
    else {
        Write-Host "  Starting..." -ForegroundColor Yellow
    }

    Start-Sleep -Milliseconds 200
}
"@

    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($popupScript))

    Start-Process powershell -ArgumentList @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-EncodedCommand", $encoded
    ) | Out-Null
}

function Add-ToUserPath {
    param([string]$PathToAdd)

    $current = [Environment]::GetEnvironmentVariable("Path", "User")
    $parts = @()

    if ($current) {
        $parts = $current -split ';' | Where-Object { $_.Trim() -ne "" }
    }

    if ($parts -notcontains $PathToAdd) {
        $newPath = ($parts + $PathToAdd) -join ';'
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        Write-Ok "Added to PATH: $PathToAdd"
    }
    else {
        Write-Info "Already in PATH: $PathToAdd"
    }

    if (($env:Path -split ';') -notcontains $PathToAdd) {
        $env:Path += ";$PathToAdd"
    }
}

Show-Banner
Update-InstallStatus -Percent 0 -Status "Starting installer..."
Start-StatusWindow

try {
    Update-InstallStatus -Percent 10 -Status "Creating directories..."
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    New-Item -ItemType Directory -Force -Path $LibsDir | Out-Null
    Write-Info "Install directory: $InstallDir"

    Update-InstallStatus -Percent 35 -Status "Downloading nyx.exe..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    try {
        Invoke-WebRequest -Uri $BinaryUrl -OutFile $BinaryPath -UseBasicParsing
        Write-Ok "Downloaded: $BinaryPath"
    }
    catch {
        if (-not (Test-Path $BinaryPath)) {
            throw "Download failed and no existing nyx.exe was found."
        }
        Write-Warn "Download failed, but an existing nyx.exe was found. Continuing..."
    }

    Update-InstallStatus -Percent 65 -Status "Updating PATH..."
    Add-ToUserPath -PathToAdd $InstallDir

    Update-InstallStatus -Percent 85 -Status "Verifying installation..."
    try {
        $version = & $BinaryPath --version 2>&1
        Write-Ok "$version"
    }
    catch {
        Write-Warn "Installed, but verification could not complete in this terminal."
    }

    Update-InstallStatus -Percent 100 -Status "Installation complete." -State "done"

    Write-Host ""
    Write-Host "  Nyx installed successfully." -ForegroundColor Green
    Write-Host ""
    Write-Info "Executable: $BinaryPath"
    Write-Info "Libraries : $LibsDir"
    Write-Host ""
    Write-Host "  Try in a new terminal:" -ForegroundColor White
    Write-Host "    nyx --version" -ForegroundColor Cyan
    Write-Host "    nyx" -ForegroundColor Cyan
    Write-Host "    nyx script.nyx" -ForegroundColor Cyan
    Write-Host "    nyx install <package>" -ForegroundColor Cyan
    Write-Host ""
}
catch {
    Update-InstallStatus -Percent 100 -Status "Installation failed." -State "failed"
    Write-Host ""
    Write-Fail $_.Exception.Message
    Write-Host ""
    exit 1
}
finally {
    Start-Sleep -Milliseconds 700
    Remove-Item $StatusFile -Force -ErrorAction SilentlyContinue
}
