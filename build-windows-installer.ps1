$BuildSpec = Get-Content -Path ./buildspec.json -Raw | ConvertFrom-Json
$ProductName = $BuildSpec.name
$ProductVersion = $BuildSpec.version

$OutputName = "${ProductName}-${ProductVersion}-windows-x64"

cmake --build build_x64 --config RelWithDebInfo --target ALL_BUILD --
cmake --install build_x64 --prefix release/Package --config RelWithDebInfo
iscc build_x64/installer-Windows.generated.iss /O"release" /F"${OutputName}-Installer-signed"
