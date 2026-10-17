param(
    [Parameter(Mandatory = $true)][string]$Azimuth
)
$ErrorActionPreference = "Stop"
$manifest = Join-Path $Azimuth "fh2wheel-install.json"
$backup = Join-Path $Azimuth "fh2wheel-backup"

if (-not (Test-Path $manifest)) { Write-Output "nothing to uninstall"; return }
foreach ($name in (Get-Content $manifest -Raw | ConvertFrom-Json).files) {
    $target = Join-Path $Azimuth $name
    if (Test-Path $target) { Remove-Item $target; Write-Output "removed: $target" }
    $saved = Join-Path $backup $name
    if (Test-Path $saved) { Move-Item $saved $target; Write-Output "restored: $name" }
}
Remove-Item $manifest
if ((Test-Path $backup) -and -not (Get-ChildItem $backup)) { Remove-Item $backup }
