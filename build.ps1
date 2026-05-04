Param (
    [switch]$installer
)

try {
    Push-Location $PSScriptRoot

    $BuildSpec = Get-Content -Path ./buildspec.json -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-x64"

    if (-not (Test-Path "lib/obs-websocket/lib/obs-websocket-api.h")) {
        git submodule update --init --recursive lib/obs-websocket
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    cmake --fresh -S . -B build_x64 -Wdev -Wdeprecated -DCMAKE_SYSTEM_VERSION="10.0.18363.657" -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build build_x64 --config RelWithDebInfo --target ALL_BUILD --
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --install build_x64 --prefix release/Package --config RelWithDebInfo
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    if ($installer) {
        iscc build_x64/installer-Windows.generated.iss /O"release" /F"${OutputName}-Installer-signed"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
} finally {
    Pop-Location
}