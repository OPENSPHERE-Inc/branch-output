Param (
    [switch]$installer
)

$BuildSpec = Get-Content -Path ./buildspec.json -Raw | ConvertFrom-Json
$ProductName = $BuildSpec.name
$ProductVersion = $BuildSpec.version

$OutputName = "${ProductName}-${ProductVersion}-windows-x64"

if (-not (Test-Path "lib/obs-websocket/lib/obs-websocket-api.h")) {
    git submodule update --init --recursive lib/obs-websocket
    if (-not $?) { exit 1 }
}

cmake --fresh -S . -B build_x64 -Wdev -Wdeprecated -DCMAKE_SYSTEM_VERSION="10.0.18363.657" -G "Visual Studio 17 2022" -A x64
cmake --build build_x64 --config RelWithDebInfo --target ALL_BUILD --
cmake --install build_x64 --prefix release/Package --config RelWithDebInfo

if ($installer) {
    iscc build_x64/installer-Windows.generated.iss /O"release" /F"${OutputName}-Installer-signed"
}