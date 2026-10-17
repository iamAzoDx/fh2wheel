$ErrorActionPreference = "Stop"
$build = Join-Path $PSScriptRoot "..\build"
$failed = 0
foreach ($test in @("test_config.exe")) {
    $path = Join-Path $build $test
    if (-not (Test-Path $path)) { Write-Output "missing: $test (run make build)"; $failed++; continue }
    & $path
    if ($LASTEXITCODE -ne 0) { $failed++ }
}
exit $failed
