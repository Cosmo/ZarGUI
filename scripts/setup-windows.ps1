# Installs whatever is missing (VS 2022 Build Tools with C++ incl. ARM64, CMake, .NET SDK) via winget,
# then runs the build. Windows may show an admin (UAC) prompt for the Visual Studio install.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\setup-windows.ps1 [-Arch x64|arm64|both]
param([ValidateSet('x64','arm64','both')][string]$Arch = 'both')
$ErrorActionPreference = 'Stop'

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget not found. Install 'App Installer' from the Microsoft Store (or update Windows), then run this again."
}

function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}
function Install([string]$Id, [string]$Override) {
    Write-Host "==> Installing $Id"
    $args = @('install', '--id', $Id, '-e', '--accept-source-agreements', '--accept-package-agreements')
    if ($Override) { $args += @('--override', $Override) }
    winget @args
    # 0 = installed; -1978335189 = already installed / no applicable update
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { throw "winget failed for $Id (exit $LASTEXITCODE)" }
}

# Visual Studio C++ tools (x64 + ARM64)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
function Has-Component([string]$Component) {
    (Test-Path $vswhere) -and [bool](& $vswhere -latest -products * -requires $Component -property installationPath)
}
$needArm = $Arch -ne 'x64'
$haveVC = Has-Component 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
$haveArm = -not $needArm -or (Has-Component 'Microsoft.VisualStudio.Component.VC.Tools.ARM64')
if (-not ($haveVC -and $haveArm)) {
    $ov = '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.ARM64 --includeRecommended'
    Install 'Microsoft.VisualStudio.2022.BuildTools' $ov
} else { Write-Host 'Visual Studio C++ tools: OK' }

# CMake
Refresh-Path
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Install 'Kitware.CMake' $null; Refresh-Path } else { Write-Host 'CMake: OK' }

# .NET SDK 8 or newer
$dotnetOk = $false
if (Get-Command dotnet -ErrorAction SilentlyContinue) {
    $v = (dotnet --version) 2>$null
    if ($v -match '^(\d+)\.' -and [int]$Matches[1] -ge 8) { $dotnetOk = $true }
}
if (-not $dotnetOk) { Install 'Microsoft.DotNet.SDK.8' $null; Refresh-Path } else { Write-Host '.NET SDK: OK' }

foreach ($tool in 'cmake', 'dotnet') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool was installed but isn't on PATH yet. Close this window, open a new PowerShell, and run the script again."
    }
}

& (Join-Path $PSScriptRoot 'build-windows.ps1') -Arch $Arch
