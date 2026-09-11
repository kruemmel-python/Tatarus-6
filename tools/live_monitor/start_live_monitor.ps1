param(
    [switch]$NoBrowser,
    [ValidateRange(1024, 65535)]
    [int]$Port = 8765
)

$ErrorActionPreference = 'Stop'

$monitorDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = (Resolve-Path (Join-Path $monitorDirectory '..\..')).Path
$buildDirectory = Join-Path $projectRoot 'build-live-monitor'

$windowsCMake = 'C:\Program Files\CMake\bin\cmake.exe'
if (Test-Path $windowsCMake) {
    $cmake = $windowsCMake
} else {
    $cmakeCommand = Get-Command cmake.exe -ErrorAction Stop
    $cmake = $cmakeCommand.Source
}

Write-Host 'TATARUS Live-Monitor wird vorbereitet (IMAGINATIO V14 + Hybrid Cortex) ...'
& $cmake -S $projectRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64 `
    -DTATARUS_BUILD_TESTS=ON `
    -DTATARUS_BUILD_EXAMPLES=ON `
    -DTATARUS_BUILD_SHARED=ON `
    -DTATARUS_BUILD_CORTEX=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Keep the first native build reliable on machines with limited compiler heap.
# The Cortex translation units are memory-heavy and an unbounded MSBuild /m can
# otherwise start enough compiler processes to exhaust RAM.
& $cmake --build $buildDirectory --config Release --target tatarus_c --parallel 2
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$library = Join-Path $buildDirectory 'Release\tatarus4_c.dll'
if (-not (Test-Path $library)) {
    throw "Die gebaute SDK-Bibliothek wurde nicht gefunden: $library"
}

$serverArguments = @(
    (Join-Path $monitorDirectory 'server.py'),
    '--library', $library,
    '--web-root', $monitorDirectory,
    '--port', $Port
)
if ($NoBrowser) { $serverArguments += '--no-browser' }

$python = Get-Command py.exe -ErrorAction SilentlyContinue
if ($python) {
    & $python.Source -3 @serverArguments
} else {
    $python = Get-Command python.exe -ErrorAction Stop
    & $python.Source @serverArguments
}
