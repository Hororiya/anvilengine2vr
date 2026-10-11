[CmdletBinding()]
param([int]$WaitSeconds = 15)

$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sessionPath = Join-Path $packageRoot 'state\session.json'

if (-not (Test-Path -LiteralPath $sessionPath)) {
    Write-Host 'No packaged Shadows VR session state was found.'
    return
}

$session = Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json
$ownershipFailures = @()
$eventPrefixes = @{
    camera = 'Local\AnvilShadowsVR.Camera.Stop.'
    lod = 'Local\AnvilShadowsVR.ModelLodNative.Stop.'
    history = 'Local\AnvilShadowsVR.TemporalReset.Stop.'
    consumer = 'Local\AnvilShadowsVR.Stop.'
    bootstrap = 'Local\AnvilShadowsVR.Stop.'
}

function Signal-OwnedComponent {
    param([string]$Name, [object]$Component)
    if ($null -eq $Component -or -not $Component.pid -or -not $Component.stopEvent) { return }
    if ($Component.stopped) { return }
    $process = Get-CimInstance Win32_Process -Filter "ProcessId=$($Component.pid)" -ErrorAction SilentlyContinue
    if (-not $process) {
        Write-Host "$Name already stopped (PID $($Component.pid))."
        return
    }
    $expectedEvent = $eventPrefixes[$Name] + [string]$Component.pid
    $creationUtc = $process.CreationDate.ToUniversalTime().ToString('o')
    $nativeProcess = Get-Process -Id $Component.pid -ErrorAction SilentlyContinue
    $nativeCreationUtc = if ($nativeProcess) { $nativeProcess.StartTime.ToUniversalTime().ToString('o') } else { $null }
    $markerMatches = [string]$process.CommandLine -match [regex]::Escape([string]$Component.commandMarker)
    $eventMatches = [string]$Component.stopEvent -ceq $expectedEvent
    $gameMatches = $true
    if ($Name -in @('camera','lod','history')) {
        $pidPattern = '--pid\s+' + [regex]::Escape([string]$session.gamePid) + '(?:\s|$)'
        $gameMatches = [string]$process.CommandLine -match $pidPattern -and [int]$Component.gamePid -eq [int]$session.gamePid
    }
    if ($Component.identityPending) {
        $storedNativeUtc = if ($Component.nativeProcessCreationTimeUtc -is [datetime]) { $Component.nativeProcessCreationTimeUtc.ToUniversalTime().ToString('o') } else { [string]$Component.nativeProcessCreationTimeUtc }
        $nativeMatches = $storedNativeUtc -and $nativeCreationUtc -eq $storedNativeUtc
        if ($markerMatches -and $nativeMatches -and $eventMatches -and $gameMatches) {
            $Component | Add-Member -NotePropertyName creationTimeUtc -NotePropertyValue $creationUtc -Force
            $Component | Add-Member -NotePropertyName identityPending -NotePropertyValue $false -Force
            $creationMatches = $true
        } else {
            $creationMatches = $false
        }
    } else {
        $storedCreationUtc = if ($Component.creationTimeUtc -is [datetime]) { $Component.creationTimeUtc.ToUniversalTime().ToString('o') } else { [string]$Component.creationTimeUtc }
        $creationMatches = $storedCreationUtc -and $creationUtc -eq $storedCreationUtc
    }
    if (-not ($markerMatches -and $creationMatches -and $eventMatches -and $gameMatches)) {
        $script:ownershipFailures += "${Name}=$($Component.pid): ownership identity mismatch"
        Write-Warning "Skipping PID $($Component.pid): recorded $Name ownership identity does not match."
        return
    }
    try {
        $event = [Threading.EventWaitHandle]::OpenExisting([string]$Component.stopEvent)
        try { $event.Set() | Out-Null } finally { $event.Dispose() }
        Write-Host "Stop requested: $Name (PID $($Component.pid))."
    } catch [Threading.WaitHandleCannotBeOpenedException] {
        Write-Warning "$Name is running but its recorded stop event is unavailable: $($Component.stopEvent)"
    }
}

# Stop controllers first so their hooks restore/detach before the camera publisher
# and sole OpenXR consumer leave. Never terminates the game or force-kills a host.
foreach ($name in @('history', 'lod', 'camera', 'consumer', 'bootstrap')) {
    Signal-OwnedComponent -Name $name -Component $session.$name
}

$deadline = (Get-Date).AddSeconds([Math]::Max(0, $WaitSeconds))
$remaining = @()
do {
    $remaining = @()
    foreach ($name in @('history', 'lod', 'camera', 'consumer', 'bootstrap')) {
        $component = $session.$name
        if ($null -ne $component -and -not $component.stopped -and $component.pid -and (Get-Process -Id $component.pid -ErrorAction SilentlyContinue)) {
            $remaining += "$name=$($component.pid)"
        }
    }
    if ($remaining.Count -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
} while ($remaining.Count -and (Get-Date) -lt $deadline)

$session | Add-Member -NotePropertyName stoppedRequested -NotePropertyValue (Get-Date).ToString('o') -Force
$session | Add-Member -NotePropertyName remaining -NotePropertyValue @($remaining) -Force
$session | Add-Member -NotePropertyName ownershipFailures -NotePropertyValue @($ownershipFailures) -Force
$temporary = "$sessionPath.tmp.$PID"
$session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding UTF8
Move-Item -LiteralPath $temporary -Destination $sessionPath -Force

if ($remaining.Count) {
    Write-Warning "Graceful stop timed out; no process was killed. Remaining: $($remaining -join ', ')"
} else {
    Write-Host 'Packaged Shadows VR components stopped gracefully. The game was not closed.'
}
if ($ownershipFailures.Count) {
    throw "Some recorded components were skipped after ownership verification: $($ownershipFailures -join '; ')"
}
