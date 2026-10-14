# Close the main window of the obs64 started from <root> (the window's close button, which exits
# OBS and writes the shutdown log) and wait for it to exit. Other obs64 processes are left alone.
# usage: quit.ps1 -Root <root> [-TimeoutSec 60]
# <root> is the extracted OBS folder that contains bin\64bit\obs64.exe.
# Prints "exited", or "still running after N s" when a process has not exited: a dialog (for
# example a confirmation to stop OBS's own stream) may be waiting, so take a screenshot first.
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [int]$TimeoutSec = 60
)

Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class ObsQuit {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
 [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@

$exe = [IO.Path]::GetFullPath((Join-Path $Root "bin\64bit\obs64.exe"))
if (-not (Test-Path $exe)) {
    Write-Error "$exe not found"
    exit 1
}
$procs = @(Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe })
if ($procs.Count -eq 0) {
    Write-Output "not running"
    exit 0
}
$ids = $procs | ForEach-Object { [uint32]$_.Id }
$script:sent = 0
[ObsQuit]::EnumWindows({
        param($h, $l)
        $procId = [uint32]0
        [void][ObsQuit]::GetWindowThreadProcessId($h, [ref]$procId)
        $sb = New-Object System.Text.StringBuilder 512
        [void][ObsQuit]::GetWindowText($h, $sb, 512)
        if ($ids -contains $procId -and $sb.ToString() -match '^OBS (Studio )?\d') {
            # WM_CLOSE
            [void][ObsQuit]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
            $script:sent++
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
if ($script:sent -eq 0) {
    Write-Output "main window not found"
    exit 1
}
$exited = $procs | ForEach-Object { $_.WaitForExit($TimeoutSec * 1000) }
if ($exited -contains $false) {
    Write-Output "still running after $TimeoutSec s"
    exit 2
}
Write-Output "exited"
