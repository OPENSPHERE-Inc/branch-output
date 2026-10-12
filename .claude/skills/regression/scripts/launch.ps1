# Start a portable OBS instance and print its PID.
# usage: launch.ps1 -Root <root> [-ObsArgs "--profile BORegression --collection BORegression"]
# <root> is the extracted OBS folder that contains bin\64bit\obs64.exe. -ObsArgs is split on
# spaces. --portable and --disable-updater are always passed, with bin\64bit as the working directory.
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$ObsArgs = ""
)

$bin = Join-Path $Root "bin\64bit"
$exe = Join-Path $bin "obs64.exe"
if (-not (Test-Path $exe)) {
    Write-Error "$exe not found"
    exit 1
}
$argList = @("--portable", "--disable-updater") + $ObsArgs.Split(" ", [StringSplitOptions]::RemoveEmptyEntries)
$p = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory $bin -PassThru
Write-Output "pid=$($p.Id)"
