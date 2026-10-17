param(
    [Parameter(Mandatory = $true)][string]$Azimuth,
    [string]$Build = (Join-Path $PSScriptRoot "..\build")
)
$ErrorActionPreference = "Stop"
$files = @("D3DCOMPILER_47.dll", "fh2wheel.dll", "fh2wheel-config.exe")
$manifest = Join-Path $Azimuth "fh2wheel-install.json"
$backup = Join-Path $Azimuth "fh2wheel-backup"

foreach ($name in $files) {
    if (-not (Test-Path (Join-Path $Build $name))) { throw "missing from the build: $name (run make build)" }
}
$installed = @()
foreach ($name in $files) {
    $target = Join-Path $Azimuth $name
    $previous = Test-Path $manifest
    if ((Test-Path $target) -and -not $previous) {
        New-Item -ItemType Directory -Force $backup | Out-Null
        Copy-Item $target (Join-Path $backup $name)
        Write-Output "backed up: $name"
    }
    Copy-Item (Join-Path $Build $name) $target -Force
    $installed += $name
    Write-Output "installed: $target"
}
@{ files = $installed; date = (Get-Date).ToString("s") } | ConvertTo-Json | Out-File -Encoding utf8 $manifest
