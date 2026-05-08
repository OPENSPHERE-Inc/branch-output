Param (
    [switch]$installer
)

try {
    Push-Location $PSScriptRoot

    $BuildSpec = Get-Content -Path ./buildspec.json -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-x64"

    # Source-archive builds have no `.git`; fall back to a header existence check
    # so `git submodule status` is not invoked outside a working git tree.
    $hasGit = $null -ne (Get-Command git -ErrorAction SilentlyContinue)
    if ((Test-Path ".git") -and $hasGit) {
        # Detect submodule state via the leading status char.
        # ' ' = up to date, '-' = uninitialized, '+' = out of date, 'U' = merge conflict.
        $submoduleStatus = git submodule status lib/obs-websocket
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        if ($submoduleStatus) {
            $statusChar = $submoduleStatus[0]
            if ($statusChar -eq 'U') {
                Write-Error "lib/obs-websocket submodule has merge conflicts; resolve them before building."
                exit 1
            } elseif ($statusChar -eq '-' -or $statusChar -eq '+') {
                git submodule update --init lib/obs-websocket
                if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            }
        }
    } elseif (-not (Test-Path "lib/obs-websocket/lib/obs-websocket-api.h")) {
        if ((Test-Path ".git") -and -not $hasGit) {
            Write-Error "git was not found on PATH; install git to update the lib/obs-websocket submodule, or pre-populate lib/obs-websocket/lib/obs-websocket-api.h before running build.ps1."
        } else {
            Write-Error "lib/obs-websocket sources are missing and this tree is not a git checkout. Configure with -DENABLE_OBS_WEBSOCKET=OFF or build from a tree that includes the submodule."
        }
        exit 1
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
