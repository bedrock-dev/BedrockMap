$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $projectRoot "build"
$exePath = Join-Path $buildDir "BedrockMap.exe"
$qtPath = $env:QT_ROOT

if (-not $qtPath) {
    throw "QT_ROOT is not set. Set it to the Qt MinGW installation before running the application."
}
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Executable not found: $exePath. Build the project first."
}

# Use the development Qt installation directly. This avoids copying runtime
# DLLs with windeployqt for every local run.
$mingwBin = Split-Path (Get-Command gcc -ErrorAction Stop).Source -Parent
# Put the compiler's runtime first. The Qt installation also contains MinGW
# runtime DLLs, but they may not match the compiler used for this project.
$env:PATH = "$mingwBin;$qtPath\bin;$env:PATH"
$qtPluginPath = Join-Path $qtPath "plugins"
if (Test-Path -LiteralPath $qtPluginPath) {
    $env:QT_PLUGIN_PATH = $qtPluginPath
}

# Debug builds resolve translations and development assets relative to build/.
Push-Location $buildDir
try {
    & $exePath -style windows11
} finally {
    Pop-Location
}
