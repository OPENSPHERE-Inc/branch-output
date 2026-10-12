# Operate on the windows of the obs64 started from <root>. Other obs64 processes are left alone.
# usage:
#   window.ps1 list -Root <root>       list visible windows with their process ID and title
#   window.ps1 front -Root <root>      bring the main window to the front, restoring it if minimized
#   window.ps1 maximize -Root <root>   maximize the main window and bring it to the front
#   window.ps1 resize -Root <root> -Title <part> [-X 0 -Y 0 -W 1400 -H 1000]
#                                      move and resize a visible window whose title contains <part>
# <root> is the extracted OBS folder that contains bin\64bit\obs64.exe. The main window is the one
# whose title starts with "OBS <digit>" ("OBS Studio <digit>" in Studio Mode). front and maximize
# exit with 1 when Windows keeps another window in the front.
param(
    [Parameter(Mandatory = $true)][ValidateSet("list", "front", "maximize", "resize")][string]$Action,
    [Parameter(Mandatory = $true)][string]$Root,
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
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
}
"@

function Get-ForegroundProcessId {
    $procId = [uint32]0
    [void][ObsWindow]::GetWindowThreadProcessId([ObsWindow]::GetForegroundWindow(), [ref]$procId)
    return $procId
}

$exe = [IO.Path]::GetFullPath((Join-Path $Root "bin\64bit\obs64.exe"))
$ids = @(Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } |
    ForEach-Object { [uint32]$_.Id })
if ($ids.Count -eq 0) {
    Write-Output "obs64 is not running from $Root"
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
        $main = $script:windows | Where-Object { $_.Title -match '^OBS (Studio )?\d' } |
            Select-Object -First 1
        if (-not $main) { Write-Output "main window not found"; exit 1 }
        # SW_RESTORE (9) also un-maximizes a window, so front uses it only on a minimized one and
        # SW_SHOW (5) otherwise. maximize uses SW_MAXIMIZE (3).
        $show = 5
        if ($Action -eq "maximize") {
            $show = 3
        } elseif ([ObsWindow]::IsIconic($main.Handle)) {
            $show = 9
        }
        [void][ObsWindow]::ShowWindow($main.Handle, $show)
        # Windows may refuse SetForegroundWindow from a process that is not in the foreground.
        [void][ObsWindow]::SetForegroundWindow($main.Handle)
        $deadline = (Get-Date).AddSeconds(1)
        while ((Get-ForegroundProcessId) -ne $main.ProcessId) {
            if ((Get-Date) -ge $deadline) {
                Write-Output "could not bring '$($main.Title)' to the front"
                exit 1
            }
            Start-Sleep -Milliseconds 50
        }
        Write-Output "$Action '$($main.Title)'"
    }
}
