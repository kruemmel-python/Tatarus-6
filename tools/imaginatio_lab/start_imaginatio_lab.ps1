param(
    [switch]$NoBrowser,
    [ValidateRange(1024, 65535)]
    [int]$Port = 8766
)

$ErrorActionPreference = 'Stop'

$labDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = (Resolve-Path (Join-Path $labDirectory '..\..')).Path
$server = Join-Path $projectRoot 'tools\live_monitor\server.py'
$buildDirectory = Join-Path $projectRoot 'build-imaginatio-lab'
$windowsCMake = 'C:\Program Files\CMake\bin\cmake.exe'

if (Test-Path $windowsCMake) {
    $cmake = $windowsCMake
} else {
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
}

Write-Host 'TATARUS IMAGINATIO · KI-Farblabor wird vorbereitet ...'
& $cmake -S $projectRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64 `
    -DTATARUS_BUILD_TESTS=ON `
    -DTATARUS_BUILD_EXAMPLES=ON `
    -DTATARUS_BUILD_SHARED=ON `
    -DTATARUS_BUILD_CORTEX=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Bound native build concurrency: the Cortex sources can exhaust the Windows
# compiler heap when CMake inherits an unrestricted MSBuild parallelism level.
& $cmake --build $buildDirectory --config Release --target tatarus_c --parallel 2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$library = Join-Path $buildDirectory 'Release\tatarus4_c.dll'
if (-not (Test-Path $library)) {
    throw "Die gebaute IMAGINATIO-Bibliothek wurde nicht gefunden: $library"
}

$arguments = @(
    $server,
    '--library', $library,
    '--web-root', $labDirectory,
    '--port', $Port,
    '--imaginatio-only'
)
if ($NoBrowser) { $arguments += '--no-browser' }

$python = Get-Command py.exe -ErrorAction SilentlyContinue
if ($python) {
    & $python.Source -3 @arguments
} else {
    & (Get-Command python.exe -ErrorAction Stop).Source @arguments
}
