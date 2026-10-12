# Run named local ffmpeg receivers that copy what they receive into a file.
# usage:
#   receiver.ps1 start -StateDir <dir> -Name <name> -Url <url> -Out <file>
#   receiver.ps1 stop|alive -StateDir <dir> -Name <name>
#   receiver.ps1 stopall -StateDir <dir>
# An rtmp:// URL is opened as a listener (-listen 1: one connection, then ffmpeg exits). For SRT,
# put the mode in the URL (srt://127.0.0.1:<port>?mode=listener or ?mode=caller). <dir> holds the
# PID file and ffmpeg's stderr of each receiver; stop and stopall end only those PIDs.
param(
    [Parameter(Mandatory = $true)][ValidateSet("start", "stop", "alive", "stopall")][string]$Action,
    [Parameter(Mandatory = $true)][string]$StateDir,
    [string]$Name = "",
    [string]$Url = "",
    [string]$Out = ""
)

New-Item -ItemType Directory -Force $StateDir | Out-Null
$pidFile = Join-Path $StateDir "$Name.pid"

function Stop-Receiver([string]$file) {
    $id = Get-Content $file
    Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
    Remove-Item $file
    return $id
}

switch ($Action) {
    "start" {
        if (-not $Name -or -not $Url -or -not $Out) { Write-Error "start needs -Name, -Url and -Out"; exit 1 }
        $a = @("-v", "error", "-y")
        if ($Url -like "rtmp://*") { $a += @("-listen", "1") }
        $a += @("-i", "`"$Url`"", "-c", "copy", "`"$Out`"")
        $p = Start-Process -FilePath ffmpeg -ArgumentList $a -WindowStyle Hidden -PassThru `
            -RedirectStandardError (Join-Path $StateDir "$Name.err")
        $p.Id | Set-Content $pidFile
        Write-Output "receiver $Name pid=$($p.Id)"
    }
    "stop" {
        if (Test-Path $pidFile) { Write-Output "receiver $Name ($(Stop-Receiver $pidFile)) stopped" }
        else { Write-Output "receiver $Name not running" }
    }
    "alive" {
        if ((Test-Path $pidFile) -and (Get-Process -Id (Get-Content $pidFile) -ErrorAction SilentlyContinue)) {
            Write-Output "alive"
        } else {
            Write-Output "dead"
        }
    }
    "stopall" {
        Get-ChildItem $StateDir -Filter *.pid | ForEach-Object { [void](Stop-Receiver $_.FullName) }
        Write-Output "all receivers stopped"
    }
}
