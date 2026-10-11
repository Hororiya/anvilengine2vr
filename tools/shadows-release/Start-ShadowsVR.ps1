[CmdletBinding()]
param(
    [int]$GamePid = 0,
    [string]$SettingsPath = '',
    [string]$PythonPath = '',
    [Alias('DryRun')][switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$toolsRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = Split-Path -Parent $toolsRoot
$stateDirectory = Join-Path $packageRoot 'state'
$logDirectory = Join-Path $packageRoot 'logs'
$sessionPath = Join-Path $stateDirectory 'session.json'
$defaultSettingsPath = Join-Path $PSScriptRoot 'settings.default.json'
$userSettingsPath = Join-Path $packageRoot 'settings.json'

function Resolve-FirstPath {
    param([Parameter(Mandatory)][string[]]$Candidates, [Parameter(Mandatory)][string]$Label)
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw "$Label was not found. Expected one of: $($Candidates -join '; ')"
}

function Quote-Argument([string]$Value) { '"' + ($Value -replace '"', '\"') + '"' }

function Write-JsonAtomic {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$Value)
    $temporary = "$Path.tmp.$PID"
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Signal-Event([string]$Name) {
    try {
        $event = [Threading.EventWaitHandle]::OpenExisting($Name)
        try { $event.Set() | Out-Null } finally { $event.Dispose() }
    } catch [Threading.WaitHandleCannotBeOpenedException] { }
}

function New-ComponentRecord {
    param([Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)]$Process,
          [Parameter(Mandatory)][string]$CommandMarker, [Parameter(Mandatory)][string]$StopEvent)
    $cim = $null
    $deadline = (Get-Date).AddSeconds(2)
    do {
        $cim = Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)" -ErrorAction SilentlyContinue
        if (-not $cim) { Start-Sleep -Milliseconds 50 }
    } while (-not $cim -and (Get-Date) -lt $deadline)
    if (-not $cim) { throw "Started $Name PID $($Process.Id), but its creation identity was not observable." }
    [ordered]@{
        pid = $Process.Id
        commandMarker = $CommandMarker
        creationTimeUtc = $cim.CreationDate.ToUniversalTime().ToString('o')
        stopEvent = $StopEvent
        gamePid = $GamePid
    }
}

function Save-ProvisionalComponent {
    param([Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)]$Process,
          [Parameter(Mandatory)][string]$CommandMarker, [Parameter(Mandatory)][string]$StopEvent)
    $nativeCreationUtc = $Process.StartTime.ToUniversalTime().ToString('o')
    $session[$Name] = [ordered]@{
        pid = $Process.Id
        commandMarker = $CommandMarker
        identityPending = $true
        nativeProcessCreationTimeUtc = $nativeCreationUtc
        stopEvent = $StopEvent
        gamePid = $GamePid
    }
    Write-JsonAtomic $sessionPath $session
}

$usingImplicitSettings = -not $SettingsPath
if ($usingImplicitSettings) {
    $SettingsPath = if (Test-Path -LiteralPath $userSettingsPath) { $userSettingsPath } else { $defaultSettingsPath }
}
$settings = Get-Content -LiteralPath $SettingsPath -Raw | ConvertFrom-Json
if ($settings.schemaVersion -ne 1) { throw "Unsupported settings schema: $($settings.schemaVersion)" }
$forward = [double]$settings.cameraForwardGameUnits
$lodBias = [double]$settings.geometryLodBias
if ([double]::IsNaN($forward) -or [double]::IsInfinity($forward) -or $forward -lt -2 -or $forward -gt 3) { throw 'cameraForwardGameUnits is outside finite range [-2,3].' }
if ([double]::IsNaN($lodBias) -or [double]::IsInfinity($lodBias) -or $lodBias -lt -0.5 -or $lodBias -gt 0) { throw 'geometryLodBias is outside finite range [-0.5,0].' }
if ([int]$settings.sourceWidth -ne 3840 -or [int]$settings.sourceHeight -ne 2160) { throw 'This release supports sourceWidth=3840 and sourceHeight=2160 only.' }
if (-not $settings.nativeStereo -or -not $settings.pairedEyes -or -not $settings.hmdProjection) { throw 'This release launcher requires nativeStereo, pairedEyes, and hmdProjection.' }
if ($settings.historyResetEnabled -and $settings.historyResetMode -ne 'diagnostic-every-frame') { throw 'Unknown history reset mode.' }

$venvPython = Join-Path $packageRoot '.venv\Scripts\python.exe'
if ($PythonPath) {
    if (-not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) { throw "PythonPath was not found: $PythonPath" }
    $python = (Resolve-Path -LiteralPath $PythonPath).Path
} elseif (Test-Path -LiteralPath $venvPython -PathType Leaf) {
    $python = (Resolve-Path -LiteralPath $venvPython).Path
} else {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if (-not $pythonCommand) { throw 'Python was not found in .venv or on PATH.' }
    $python = $pythonCommand.Source
}

$fridaVersion = (& $python -c "import importlib.metadata as m; print(m.version('frida'))" 2>$null)
if ($LASTEXITCODE -ne 0) { throw 'Python package frida is missing. Install tools/shadows-release/requirements.txt.' }
$requiredFridaLine = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'requirements.txt') | Where-Object { $_ -match '^frida==' } | Select-Object -First 1
if (-not $requiredFridaLine) { throw 'requirements.txt does not pin frida.' }
$requiredFrida = $requiredFridaLine -replace '^frida==',''
if ($fridaVersion.Trim() -ne $requiredFrida.Trim()) { throw "frida $requiredFrida is required; found $fridaVersion." }

$cameraScript = Resolve-FirstPath -Label 'Camera script' -Candidates @((Join-Path $toolsRoot 'shadows_scene_camera.py'))
$windowScript = Resolve-FirstPath -Label '4K window script' -Candidates @((Join-Path $toolsRoot 'shadows_window_4k.py'))
$lodScript = Resolve-FirstPath -Label 'Native LOD script' -Candidates @((Join-Path $toolsRoot 'shadows_model_lod_native.py'))
$historyScript = Resolve-FirstPath -Label 'History diagnostic script' -Candidates @((Join-Path $toolsRoot 'shadows_native_history_reset.py'))
$consumerExe = Resolve-FirstPath -Label 'Native stereo executable' -Candidates @((Join-Path $packageRoot 'bin\ShadowsVisionPro.exe'))
$cameraMath = Resolve-FirstPath -Label 'Camera math bridge' -Candidates @((Join-Path $packageRoot 'bin\shadows_camera_math_bridge.dll'))
$producer = Resolve-FirstPath -Label 'Native color publisher' -Candidates @((Join-Path $packageRoot 'bin\shadows_native_color_bridge.dll'))
$receiver = Resolve-FirstPath -Label 'Native color receiver' -Candidates @((Join-Path $packageRoot 'bin\ShadowsColorReceiver.dll'))
$manifestPath = Resolve-FirstPath -Label 'Native build manifest' -Candidates @((Join-Path $packageRoot 'native-build-manifest.json'),(Join-Path $packageRoot 'bin\native-build-manifest.json'))
$nativeManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($nativeManifest.schema_version -ne 1 -or $nativeManifest.configuration -ne 'Release' -or $nativeManifest.architecture -ne 'x64') { throw 'Unsupported native build manifest.' }
if ($nativeManifest.openxr_loader_linkage -ne 'static') { throw 'This package requires a statically linked OpenXR loader.' }
if ($settings.openxrLoaderLinkage -ne 'static') { throw 'settings.json must declare openxrLoaderLinkage=static.' }
$openXrLoader = '<statically linked>'

$preflight = [ordered]@{
    packageRoot = $packageRoot
    settings = (Resolve-Path -LiteralPath $SettingsPath).Path
    python = $python
    frida = $fridaVersion.Trim()
    cameraScript = $cameraScript
    windowScript = $windowScript
    lodScript = $lodScript
    historyScript = $historyScript
    consumer = $consumerExe
    cameraMath = $cameraMath
    publisher = $producer
    receiver = $receiver
    openXrLoader = $openXrLoader
    nativeBuildManifest = $manifestPath
    diagnosticHistoryReset = [bool]$settings.historyResetEnabled
    diagnosticNotice = [string]$settings.historyResetNotice
}

$detectedGamePid = $GamePid
if (-not $detectedGamePid) {
    $detectedGames = @(Get-Process ACShadows -ErrorAction SilentlyContinue)
    if ($detectedGames.Count -eq 1) { $detectedGamePid = $detectedGames[0].Id }
}
$activeConflicts = @()
if ($detectedGamePid) {
    $targetPidPattern = '--pid\s+' + [regex]::Escape([string]$detectedGamePid) + '(?:\s|$)'
    foreach ($candidate in @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)) {
        $line = [string]$candidate.CommandLine
        $isController = $line -match 'shadows_(scene_camera|model_lod_native|native_history_reset)[.]py' -and $line -match $targetPidPattern
        $isConsumer = $line -match '(ShadowsVisionPro|ShadowsNativeGazeCandidate)[.]exe' -and $line -match '--(native-stereo|head-locked)'
        if ($isController -or $isConsumer) { $activeConflicts += [ordered]@{ pid=$candidate.ProcessId; command=$line } }
    }
}
$preflight.activeConflicts = $activeConflicts

if ($ValidateOnly) {
    Write-Host 'Validation passed. No process was attached or started.'
    $preflight | ConvertTo-Json -Depth 5
    return
}

if (-not $GamePid) {
    $games = @(Get-Process ACShadows -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Supply -GamePid when exactly one running game process cannot be identified.' }
    $GamePid = $games[0].Id
}
$game = Get-Process -Id $GamePid -ErrorAction Stop
if ($game.ProcessName -ne 'ACShadows') { throw 'The supplied PID is not the supported game process.' }

if (Test-Path -LiteralPath $sessionPath) {
    $previous = Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json
    foreach ($componentName in @('camera','lod','history','consumer','bootstrap')) {
        $component = $previous.$componentName
        if ($null -ne $component -and -not $component.stopped -and $component.pid -and (Get-Process -Id $component.pid -ErrorAction SilentlyContinue)) {
            throw "A packaged session component is already running: $componentName PID $($component.pid). Stop it first."
        }
    }
}

New-Item -ItemType Directory -Force -Path $stateDirectory, $logDirectory, (Join-Path $packageRoot 'artifact\shadows-native\clarity') | Out-Null
if ($usingImplicitSettings -and -not (Test-Path -LiteralPath $userSettingsPath)) {
    Copy-Item -LiteralPath $defaultSettingsPath -Destination $userSettingsPath
}
if ($usingImplicitSettings) {
    $SettingsPath = $userSettingsPath
    $preflight.settings = $userSettingsPath
}

if ($activeConflicts.Count) {
    throw "Another Shadows VR controller/consumer is active for this game. Stop the existing developer or packaged session first. PIDs: $($activeConflicts.pid -join ', ')"
}
$cameraControl = Join-Path $stateDirectory 'camera-control.json'
$lodControl = Join-Path $stateDirectory 'lod-control.json'
Write-JsonAtomic $cameraControl ([ordered]@{ forward_game_units = [double]$settings.cameraForwardGameUnits; updated = (Get-Date).ToString('o') })
Write-JsonAtomic $lodControl ([ordered]@{ geometry_lod_bias = [double]$settings.geometryLodBias; updated = (Get-Date).ToString('o') })

$bootstrap = $null; $camera = $null; $consumer = $null; $lod = $null; $history = $null
$session = [ordered]@{ schemaVersion=1; sessionNonce=[guid]::NewGuid().ToString('N'); gamePid=$GamePid; started=(Get-Date).ToString('o'); settings=$preflight.settings; diagnosticHistoryReset=[bool]$settings.historyResetEnabled }
try {
    $hwnd = [Int64]$game.MainWindowHandle
    if (-not $hwnd) { throw 'The game main window handle is not available.' }
    & $python $windowScript '--pid' $GamePid '--hwnd' ('0x{0:x}' -f $hwnd) '--output' (Join-Path $logDirectory 'window-4k-result.json') *> (Join-Path $logDirectory 'window-4k.log')
    if ($LASTEXITCODE -ne 0) { throw 'The 4K source-window step failed; see logs/window-4k.log.' }

    $bootstrap = Start-Process -FilePath $consumerExe -ArgumentList @('--duration','0','--head-locked','--log',(Quote-Argument (Join-Path $logDirectory 'bootstrap.log'))) -WorkingDirectory $packageRoot -WindowStyle Hidden -PassThru
    Save-ProvisionalComponent bootstrap $bootstrap $consumerExe "Local\AnvilShadowsVR.Stop.$($bootstrap.Id)"
    $session.bootstrap = New-ComponentRecord bootstrap $bootstrap $consumerExe "Local\AnvilShadowsVR.Stop.$($bootstrap.Id)"
    Write-JsonAtomic $sessionPath $session
    Start-Sleep -Seconds 2

    $cameraArgs = @((Quote-Argument $cameraScript),'--pid',$GamePid,'--duration','0','--hmd-projection','--paired','--camera-control',(Quote-Argument $cameraControl),'--camera-math-helper',(Quote-Argument $cameraMath),'--native-color-helper',(Quote-Argument $producer),'--output',(Quote-Argument (Join-Path $logDirectory 'publisher-result.json')))
    $camera = Start-Process -FilePath $python -ArgumentList $cameraArgs -WorkingDirectory $packageRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $logDirectory 'publisher-stdout.log') -RedirectStandardError (Join-Path $logDirectory 'publisher-stderr.log') -PassThru
    Save-ProvisionalComponent camera $camera $cameraScript "Local\AnvilShadowsVR.Camera.Stop.$($camera.Id)"
    $session.camera = New-ComponentRecord camera $camera $cameraScript "Local\AnvilShadowsVR.Camera.Stop.$($camera.Id)"
    Write-JsonAtomic $sessionPath $session

    $deadline = (Get-Date).AddSeconds(180); $ready = $false
    do {
        if ($camera.HasExited) { throw 'Camera publisher exited; see logs/publisher-stderr.log.' }
        try {
            $map = [IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting('Local\AnvilShadowsVR.NativeColor.v1',[IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
            $view = $map.CreateViewAccessor(0,1072,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
            try { $ready = $view.ReadUInt32(24) -eq $GamePid -and $view.ReadUInt64(72) -gt 0 -and $view.ReadUInt64(80) -gt 0 } finally { $view.Dispose(); $map.Dispose() }
        } catch [IO.FileNotFoundException] { }
        if (-not $ready) { Start-Sleep -Milliseconds 200 }
    } while (-not $ready -and (Get-Date) -lt $deadline)
    if (-not $ready) { throw 'Native eye publisher did not initialize within 180 seconds.' }

    Signal-Event $session.bootstrap.stopEvent
    if (-not $bootstrap.WaitForExit(10000)) { throw 'Temporary bootstrap OpenXR session did not stop gracefully.' }
    $session.bootstrap['stopped'] = (Get-Date).ToString('o')
    Write-JsonAtomic $sessionPath $session

    $consumer = Start-Process -FilePath $consumerExe -ArgumentList @('--native-stereo','--native-color-helper',(Quote-Argument $receiver),'--duration','0','--log',(Quote-Argument (Join-Path $logDirectory 'native-stereo.log'))) -WorkingDirectory $packageRoot -WindowStyle Hidden -PassThru
    Save-ProvisionalComponent consumer $consumer $consumerExe "Local\AnvilShadowsVR.Stop.$($consumer.Id)"
    $session.consumer = New-ComponentRecord consumer $consumer $consumerExe "Local\AnvilShadowsVR.Stop.$($consumer.Id)"
    Write-JsonAtomic $sessionPath $session

    $lod = Start-Process -FilePath $python -ArgumentList @((Quote-Argument $lodScript),'--pid',$GamePid,'--duration','0','--experimental','--control',(Quote-Argument $lodControl),'--output',(Quote-Argument (Join-Path $logDirectory 'model-lod-runtime.json'))) -WorkingDirectory $packageRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $logDirectory 'lod-stdout.log') -RedirectStandardError (Join-Path $logDirectory 'lod-stderr.log') -PassThru
    Save-ProvisionalComponent lod $lod $lodScript "Local\AnvilShadowsVR.ModelLodNative.Stop.$($lod.Id)"
    $session.lod = New-ComponentRecord lod $lod $lodScript "Local\AnvilShadowsVR.ModelLodNative.Stop.$($lod.Id)"
    Write-JsonAtomic $sessionPath $session

    if ($settings.historyResetEnabled) {
        Write-Warning ([string]$settings.historyResetNotice)
        $history = Start-Process -FilePath $python -ArgumentList @((Quote-Argument $historyScript),'--pid',$GamePid,'--duration','0','--reset-history','--output',(Quote-Argument (Join-Path $logDirectory 'history-reset-runtime.json'))) -WorkingDirectory $packageRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $logDirectory 'history-stdout.log') -RedirectStandardError (Join-Path $logDirectory 'history-stderr.log') -PassThru
        Save-ProvisionalComponent history $history $historyScript "Local\AnvilShadowsVR.TemporalReset.Stop.$($history.Id)"
        $session.history = New-ComponentRecord history $history $historyScript "Local\AnvilShadowsVR.TemporalReset.Stop.$($history.Id)"
        $session.history['mode'] = 'diagnostic-every-frame'
        Write-JsonAtomic $sessionPath $session
    }
    Write-JsonAtomic $sessionPath $session
    Start-Sleep -Seconds 2
    foreach ($check in @(
        [pscustomobject]@{ name='consumer'; process=$consumer; log='logs/native-stereo.log' },
        [pscustomobject]@{ name='LOD controller'; process=$lod; log='logs/lod-stderr.log' },
        [pscustomobject]@{ name='history diagnostic'; process=$history; log='logs/history-stderr.log' }
    )) {
        if ($null -ne $check.process -and $check.process.HasExited) {
            throw "$($check.name) exited during startup; see $($check.log)."
        }
    }
    Write-Host "Shadows VR started: game=$GamePid camera=$($camera.Id) consumer=$($consumer.Id) LOD=$($lod.Id)."
} catch {
    $failure = $_.Exception.Message
    $session['error'] = [ordered]@{ message=$failure; time=(Get-Date).ToString('o') }
    $fallbacks = @(
        [pscustomobject]@{ process=$history; marker=$historyScript; event=if($history){"Local\AnvilShadowsVR.TemporalReset.Stop.$($history.Id)"}else{$null} },
        [pscustomobject]@{ process=$lod; marker=$lodScript; event=if($lod){"Local\AnvilShadowsVR.ModelLodNative.Stop.$($lod.Id)"}else{$null} },
        [pscustomobject]@{ process=$camera; marker=$cameraScript; event=if($camera){"Local\AnvilShadowsVR.Camera.Stop.$($camera.Id)"}else{$null} },
        [pscustomobject]@{ process=$consumer; marker=$consumerExe; event=if($consumer){"Local\AnvilShadowsVR.Stop.$($consumer.Id)"}else{$null} },
        [pscustomobject]@{ process=$bootstrap; marker=$consumerExe; event=if($bootstrap){"Local\AnvilShadowsVR.Stop.$($bootstrap.Id)"}else{$null} }
    )
    $cleanupDeadline = (Get-Date).AddSeconds(15)
    do {
        $remaining = @()
        foreach ($name in @('history','lod','camera','consumer','bootstrap')) {
            $record = $session[$name]
            if ($record -and (Get-Process -Id $record.pid -ErrorAction SilentlyContinue)) {
                $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)" -ErrorAction SilentlyContinue
                if ($owned -and [string]$owned.CommandLine -match [regex]::Escape([string]$record.commandMarker)) {
                    if ($record.identityPending) {
                        $record['creationTimeUtc'] = $owned.CreationDate.ToUniversalTime().ToString('o')
                        $record['identityPending'] = $false
                    }
                    Signal-Event $record.stopEvent
                }
                $remaining += "$name=$($record.pid)"
            }
        }
        foreach($fallback in $fallbacks) {
            if($fallback.process -and (Get-Process -Id $fallback.process.Id -ErrorAction SilentlyContinue)) {
                $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$($fallback.process.Id)" -ErrorAction SilentlyContinue
                if ($owned -and [string]$owned.CommandLine -match [regex]::Escape([string]$fallback.marker)) {
                    Signal-Event $fallback.event
                }
                if ($remaining -notcontains "unrecorded=$($fallback.process.Id)") { $remaining += "unrecorded=$($fallback.process.Id)" }
            }
        }
        if ($remaining.Count -and (Get-Date) -lt $cleanupDeadline) { Start-Sleep -Milliseconds 200 }
    } while ($remaining.Count -and (Get-Date) -lt $cleanupDeadline)
    $session['remainingAfterFailedStart'] = @($remaining)
    Write-JsonAtomic $sessionPath $session
    throw $failure
}
