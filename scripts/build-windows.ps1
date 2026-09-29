# Builds the Windows app (WinUI 3, C++) into build\windows\<arch>\ and zips it to
# build\ZarGUI-Windows-<arch>.zip. The app uses the Windows App Runtime 1.7 installed on the PC
# and offers to install it on first start if it is missing.
# Needs Visual Studio 2022 with C++ and Windows App SDK C++ tools, CMake and nuget
# (scripts\setup-windows.ps1 installs them).
# Usage: powershell -File scripts\build-windows.ps1 [-Arch x64|arm64|both]   (default: both)
param([ValidateSet('x64','arm64','both')][string]$Arch = 'both')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$b = Join-Path $root 'build'
$hostArm = $env:PROCESSOR_ARCHITECTURE -eq 'ARM64'
$project = Join-Path $root 'windows\ZarGUI\ZarGUI.vcxproj'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found. Run scripts\setup-windows.ps1 first.' }

nuget restore (Join-Path $root 'windows\ZarGUI\packages.config') -PackagesDirectory (Join-Path $root 'windows\packages')
if ($LASTEXITCODE) { throw 'nuget restore failed' }

function Build-One([string]$Target) {
    $platform = if ($Target -eq 'arm64') { 'ARM64' } else { 'x64' }

    # The core library and its tests.
    $core = Join-Path $b "windows-core-$Target"
    cmake -S $root -B $core -A $platform -DBUILD_TESTING=ON
    if ($LASTEXITCODE) { throw "CMake configure failed ($Target)" }
    cmake --build $core --config Release
    if ($LASTEXITCODE) { throw "Core build failed ($Target)" }
    if ($Target -eq 'x64' -or $hostArm) {  # arm64 binaries only run on ARM64 hosts
        ctest --test-dir $core -C Release --output-on-failure
        if ($LASTEXITCODE) { throw "Core tests failed ($Target)" }
    }

    # The app.
    $obj = Join-Path $b "windows-app-$Target\obj\"
    $bin = Join-Path $b "windows-app-$Target\bin\"
    & $msbuild $project /m /nologo /v:minimal /p:Configuration=Release /p:Platform=$platform /p:CppWinRTVerbosity=high `
        "/p:IntDir=$obj" "/p:OutDir=$bin" `
        "/p:ZarpackLib=$core\Release\zarpack.lib" "/p:ZstdLib=$core\_deps\zstd-build\lib\Release\zstd_static.lib"
    if ($LASTEXITCODE) { throw "App build failed ($Target)" }

    $out = Join-Path $b "windows\$Target"
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    New-Item -ItemType Directory -Force $out | Out-Null
    Get-ChildItem $bin -File | Where-Object { $_.Extension -notin '.pdb', '.lib', '.exp', '.winmd', '.ilk' } |
        Copy-Item -Destination $out
    Get-ChildItem $bin -Directory | Copy-Item -Destination $out -Recurse
    Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'THIRD_PARTY_NOTICES.md') $out

    $zip = Join-Path $b "ZarGUI-Windows-$Target.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip
    $size = (Get-ChildItem $out -Recurse -File | Measure-Object Length -Sum).Sum
    Write-Host "Built $out ($([math]::Round($size / 1KB)) KB, $((Get-ChildItem $out -Recurse -File).Count) files)"
    Get-ChildItem $out -Recurse -File | ForEach-Object { Write-Host "  $($_.Name)  $([math]::Round($_.Length / 1KB)) KB" }
}

$targets = if ($Arch -eq 'both') { @('x64', 'arm64') } else { @($Arch) }
foreach ($t in $targets) { Build-One $t }
