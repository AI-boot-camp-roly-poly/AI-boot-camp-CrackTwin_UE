# CrackTwin Unreal Editor start/stop/status for Codex and humans.
# Usage: Tools\editor.cmd status | start [-WaitMcp] | stop [-Force]
# Codex runs Tools\editor.cmd outside its sandbox (~/.codex/rules/default.rules), so the
# editor runs as the desktop user and Codex Computer Use can see its window.
param(
    [Parameter(Position = 0, Mandatory = $true)][ValidateSet('status', 'start', 'stop')][string]$Action,
    [switch]$WaitMcp,
    [switch]$Force,
    [int]$TimeoutSec = 180
)

$ErrorActionPreference = 'Stop'
$Engine = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Project = 'C:\AIBootcamp\CrackTwin\CrackTwin.uproject'
$McpPort = 18000

function Get-CrackTwinEditors {
    Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe'" |
        ForEach-Object {
            # CommandLine is empty for an elevated (Run as administrator) editor; fall back to the window title.
            $title = (Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue).MainWindowTitle
            $elevated = -not $_.CommandLine
            if (($_.CommandLine -like '*CrackTwin.uproject*') -or ($elevated -and $title -like 'CrackTwin*')) {
                $owner = Invoke-CimMethod -InputObject $_ -MethodName GetOwner
                [pscustomobject]@{ Id = [int]$_.ProcessId; User = "$($owner.Domain)\$($owner.User)"; Elevated = $elevated }
            }
        }
}

function Test-Mcp {
    $client = New-Object System.Net.Sockets.TcpClient
    try { return $client.ConnectAsync('127.0.0.1', $McpPort).Wait(1000) -and $client.Connected }
    catch { return $false }
    finally { $client.Dispose() }
}

function Show-Status {
    $editors = @(Get-CrackTwinEditors)
    if ($editors.Count -eq 0) { Write-Output 'EDITOR: not running' }
    foreach ($e in $editors) {
        Write-Output ("EDITOR: running pid={0} user={1}{2}" -f $e.Id, $e.User, $(if ($e.Elevated) { ' elevated(likely; Computer Use cannot drive it)' } else { '' }))
    }
    Write-Output ("MCP {0}: {1}" -f $McpPort, $(if (Test-Mcp) { 'listening' } else { 'not listening' }))
}

if ($Action -eq 'status') { Show-Status; exit 0 }

if ($Action -eq 'start') {
    $editors = @(Get-CrackTwinEditors)
    if ($editors.Count -gt 0) {
        foreach ($e in $editors) { Write-Output ("ALREADY RUNNING: pid={0} user={1}" -f $e.Id, $e.User) }
        Write-Output 'Not starting a second editor on the same project.'
        exit 2
    }
    $p = Start-Process -FilePath $Engine -ArgumentList "`"$Project`"" -PassThru
    Write-Output ("STARTED: pid={0}" -f $p.Id)
    if ($WaitMcp) {
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-Date) -lt $deadline) {
            if ($p.HasExited) { Write-Output ("EXITED EARLY: code={0}" -f $p.ExitCode); exit 1 }
            if (Test-Mcp) { Write-Output ("MCP {0}: listening" -f $McpPort); exit 0 }
            Start-Sleep -Seconds 2
        }
        Write-Output ("TIMEOUT: MCP {0} not listening after {1}s" -f $McpPort, $TimeoutSec)
        exit 1
    }
    exit 0
}

if ($Action -eq 'stop') {
    $editors = @(Get-CrackTwinEditors)
    if ($editors.Count -eq 0) { Write-Output 'EDITOR: not running'; exit 0 }
    $failed = $false
    foreach ($e in $editors) {
        $proc = Get-Process -Id $e.Id -ErrorAction SilentlyContinue
        if (-not $proc) { continue }
        try {
            if ($Force) {
                Stop-Process -Id $e.Id -Force
                Write-Output ("KILLED: pid={0} user={1} (unsaved changes are lost)" -f $e.Id, $e.User)
                continue
            }
            # WM_CLOSE: the editor asks to save dirty packages instead of losing them.
            if (-not $proc.CloseMainWindow()) {
                Write-Output ("NO WINDOW: pid={0} user={1}. Window is in another session or hidden; use -Force only with user approval." -f $e.Id, $e.User)
                $failed = $true
                continue
            }
            if ($proc.WaitForExit($TimeoutSec * 1000)) {
                Write-Output ("CLOSED: pid={0} user={1}" -f $e.Id, $e.User)
            } else {
                Write-Output ("STILL RUNNING: pid={0} after {1}s. A save/PIE dialog is probably open; ask the user." -f $e.Id, $TimeoutSec)
                $failed = $true
            }
        } catch {
            Write-Output ("FAILED: pid={0} user={1}: {2}" -f $e.Id, $e.User, $_.Exception.Message)
            $failed = $true
        }
    }
    exit $(if ($failed) { 1 } else { 0 })
}
