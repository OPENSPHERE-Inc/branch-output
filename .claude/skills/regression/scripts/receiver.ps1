# Run named local ffmpeg receivers that copy what they receive into a file.
# usage:
#   receiver.ps1 start -StateDir <dir> -Name <name> -Url <url> -Out <file.ts>
#   receiver.ps1 stop|alive -StateDir <dir> -Name <name>
#   receiver.ps1 stopall -StateDir <dir>
# An rtmp:// URL is opened as a listener (-listen 1: one connection, then ffmpeg exits). For SRT,
# put the mode in the URL (srt://127.0.0.1:<port>?mode=listener or ?mode=caller). <dir> holds the
# PID file (PID and start time) and ffmpeg's stderr of each receiver; stop, stopall and alive act
# only on the ffmpeg process a PID file records. start refuses a name whose receiver is running.
# <file.ts> must be a new file for each connection, and MPEG-TS because stop and stopall kill ffmpeg
# before it writes a trailer (MP4 / MOV need one to be readable, MKV / FLV to record their duration).
param(
    [Parameter(Mandatory = $true)][ValidateSet("start", "stop", "alive", "stopall")][string]$Action,
    [Parameter(Mandatory = $true)][string]$StateDir,
    [string]$Name = "",
    [string]$Url = "",
    [string]$Out = ""
)

New-Item -ItemType Directory -Force $StateDir | Out-Null
$pidFile = Join-Path $StateDir "$Name.pid"

# Returns the ffmpeg process a PID file records, or $null when it has exited: Windows reuses PIDs,
# so the PID alone may name another process.
function Get-Receiver([string]$file) {
    $text = Get-Content -LiteralPath $file -Raw -ErrorAction SilentlyContinue
    $fields = "$text".Trim() -split '\s+'
    $id = 0
    $ticks = [long]0
    if ($fields.Count -ne 2 -or -not [int]::TryParse($fields[0], [ref]$id) -or
        -not [long]::TryParse($fields[1], [ref]$ticks)) {
        return $null
    }
    $p = Get-Process -Id $id -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq "ffmpeg" -and $p.StartTime -and
        $p.StartTime.ToUniversalTime().Ticks -eq $ticks) {
        return $p
    }
    return $null
}

# Stops the receiver a PID file records and deletes the file. Returns the stopped PID, or $null
# when the recorded ffmpeg had already exited.
function Stop-Receiver([string]$file) {
    $p = Get-Receiver $file
    if ($p) { $p | Stop-Process -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $file -ErrorAction SilentlyContinue
    if ($p) { return $p.Id }
    return $null
}

switch ($Action) {
    "start" {
        if (-not $Name -or -not $Url -or -not $Out) { Write-Error "start needs -Name, -Url and -Out"; exit 1 }
        $running = Get-Receiver $pidFile
        if ($running) { Write-Output "receiver $Name already running pid=$($running.Id)"; exit 1 }
        if ([IO.Path]::GetExtension($Out) -ne ".ts") { Write-Error "-Out must end in .ts"; exit 1 }
        if (Test-Path -LiteralPath $Out) {
            Write-Error "$Out already exists: use a new file for each connection"; exit 1
        }
        $a = @("-v", "error", "-y")
        if ($Url -like "rtmp://*") { $a += @("-listen", "1") }
        $a += @("-i", "`"$Url`"", "-c", "copy", "`"$Out`"")
        $p = Start-Process -FilePath ffmpeg -ArgumentList $a -WindowStyle Hidden -PassThru `
            -RedirectStandardError (Join-Path $StateDir "$Name.err")
        "$($p.Id) $($p.StartTime.ToUniversalTime().Ticks)" | Set-Content $pidFile
        Write-Output "receiver $Name pid=$($p.Id)"
    }
    "stop" {
        $id = Stop-Receiver $pidFile
        if ($id) { Write-Output "receiver $Name ($id) stopped" }
        else { Write-Output "receiver $Name not running" }
    }
    "alive" {
        if (Get-Receiver $pidFile) {
            Write-Output "alive"
        } else {
            Remove-Item -LiteralPath $pidFile -ErrorAction SilentlyContinue
            Write-Output "dead"
        }
    }
    "stopall" {
        Get-ChildItem $StateDir -Filter *.pid | ForEach-Object { [void](Stop-Receiver $_.FullName) }
        Write-Output "all receivers stopped"
    }
}
