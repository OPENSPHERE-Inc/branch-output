# Operate on the windows of the running obs64 processes.
# usage:
#   window.ps1 list                                  list visible windows with their process ID and title
#   window.ps1 front                                 restore the main window and bring it to the front
#   window.ps1 maximize                              maximize the main window and bring it to the front
#   window.ps1 resize -Title <part> [-X 0 -Y 0 -W 1400 -H 1000]
#                                                    move and resize a visible window whose title contains <part>
# The main window is the one whose title starts with "OBS <digit>".
param(
    [Parameter(Mandatory = $true)][ValidateSet("list", "front", "maximize", "resize")][string]$Action,
    [string]$Title = "",
    [int]$X = 0,
    [int]$Y = 0,
    [int]$W = 1400,
    [int]$H = 1000
)

Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class ObsWindow {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
 [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
}
"@

$ids = @(Get-Process obs64 -ErrorAction SilentlyContinue | ForEach-Object { [uint32]$_.Id })
if ($ids.Count -eq 0) {
    Write-Output "obs64 is not running"
    exit 1
}
$script:windows = @()
[ObsWindow]::EnumWindows({
        param($h, $l)
        $procId = [uint32]0
        [void][ObsWindow]::GetWindowThreadProcessId($h, [ref]$procId)
        if ($ids -contains $procId -and [ObsWindow]::IsWindowVisible($h)) {
            $sb = New-Object System.Text.StringBuilder 512
            [void][ObsWindow]::GetWindowText($h, $sb, 512)
            $script:windows += [pscustomobject]@{ Handle = $h; ProcessId = $procId; Title = $sb.ToString() }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null

switch ($Action) {
    "list" {
        $script:windows | ForEach-Object { "[$($_.ProcessId)] '$($_.Title)'" }
    }
    "resize" {
        $target = $script:windows | Where-Object { $_.Title -like "*$Title*" } | Select-Object -First 1
        if (-not $target) { Write-Output "no window titled *$Title*"; exit 1 }
        [void][ObsWindow]::MoveWindow($target.Handle, $X, $Y, $W, $H, $true)
        [void][ObsWindow]::SetForegroundWindow($target.Handle)
        Write-Output "resized '$($target.Title)'"
    }
    default {
        $main = $script:windows | Where-Object { $_.Title -match '^OBS \d' } | Select-Object -First 1
        if (-not $main) { Write-Output "main window not found"; exit 1 }
        $show = 9
        if ($Action -eq "maximize") { $show = 3 }
        [void][ObsWindow]::ShowWindow($main.Handle, $show)
        [void][ObsWindow]::SetForegroundWindow($main.Handle)
        Write-Output "$Action '$($main.Title)'"
    }
}
