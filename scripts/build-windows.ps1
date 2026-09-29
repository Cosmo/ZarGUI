# Builds the Windows app: one self-contained build\windows\<arch>\ZarGUI.exe, zipped to
# build\ZarGUI-Windows-<arch>.zip. Needs Visual Studio 2022 C++ tools and CMake
# (scripts\setup-windows.ps1 installs them).
# Usage: powershell -File scripts\build-windows.ps1 [-Arch x64|arm64|both]   (default: both)
param([ValidateSet('x64','arm64','both')][string]$Arch = 'both')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$b = Join-Path $root 'build'
$hostArm = $env:PROCESSOR_ARCHITECTURE -eq 'ARM64'

function Build-One([string]$Target) {
    $core = Join-Path $b "windows-core-$Target"
    $cmakeArch = if ($Target -eq 'arm64') { 'ARM64' } else { 'x64' }
    cmake -S $root -B $core -A $cmakeArch -DBUILD_TESTING=ON
    if ($LASTEXITCODE) { throw "CMake configure failed ($Target)" }
    cmake --build $core --config Release
    if ($LASTEXITCODE) { throw "Build failed ($Target)" }

    # Tests only run where the binary can run: x64 anywhere (ARM64 Windows 11 emulates it), arm64 on ARM64 hosts.
    if ($Target -eq 'x64' -or $hostArm) {
        ctest --test-dir $core -C Release --output-on-failure
        if ($LASTEXITCODE) { throw "Core tests failed ($Target)" }
    }

    $out = Join-Path $b "windows\$Target"
    New-Item -ItemType Directory -Force $out | Out-Null
    Copy-Item (Join-Path $core 'Release\ZarGUI.exe') $out
    Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'THIRD_PARTY_NOTICES.md') $out

    $zip = Join-Path $b "ZarGUI-Windows-$Target.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip
    Write-Host "Built $out\ZarGUI.exe ($([math]::Round((Get-Item "$out\ZarGUI.exe").Length / 1KB)) KB)"
    Write-Host "Zipped $zip"
}

$targets = if ($Arch -eq 'both') { @('x64', 'arm64') } else { @($Arch) }
foreach ($t in $targets) { Build-One $t }
