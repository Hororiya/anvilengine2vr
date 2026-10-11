[CmdletBinding()]
param(
    [ValidateRange(-2.0, 3.0)]
    [Nullable[double]]$CameraForward,

    [ValidateRange(-0.5, 0.0)]
    [Nullable[double]]$LodBias
)

$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$stateDirectory = Join-Path $packageRoot 'state'
$cameraControl = Join-Path $stateDirectory 'camera-control.json'
$lodControl = Join-Path $stateDirectory 'lod-control.json'

function Write-JsonAtomic {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$Value)
    $temporary = "$Path.tmp.$PID"
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

New-Item -ItemType Directory -Force -Path $stateDirectory | Out-Null

if ($null -ne $CameraForward -and ([double]::IsNaN([double]$CameraForward) -or [double]::IsInfinity([double]$CameraForward))) {
    throw 'CameraForward must be finite.'
}
if ($null -ne $LodBias -and ([double]::IsNaN([double]$LodBias) -or [double]::IsInfinity([double]$LodBias))) {
    throw 'LodBias must be finite.'
}

if ($null -ne $CameraForward) {
    Write-JsonAtomic -Path $cameraControl -Value ([ordered]@{
        forward_game_units = [double]$CameraForward
        updated = (Get-Date).ToString('o')
    })
}

if ($null -ne $LodBias) {
    Write-JsonAtomic -Path $lodControl -Value ([ordered]@{
        geometry_lod_bias = [double]$LodBias
        updated = (Get-Date).ToString('o')
    })
}

$camera = if (Test-Path -LiteralPath $cameraControl) {
    Get-Content -LiteralPath $cameraControl -Raw | ConvertFrom-Json
} else { [pscustomobject]@{ forward_game_units = $null } }
$lod = if (Test-Path -LiteralPath $lodControl) {
    Get-Content -LiteralPath $lodControl -Raw | ConvertFrom-Json
} else { [pscustomobject]@{ geometry_lod_bias = $null } }

[pscustomobject]@{
    cameraForwardGameUnits = $camera.forward_game_units
    geometryLodBias = $lod.geometry_lod_bias
    cameraControl = $cameraControl
    lodControl = $lodControl
} | Format-List
