param(
    [switch]$Rel
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# Release runs (-Rel) use the build_rls/ output.
$buildDir = Join-Path $projectRoot $(if ($Rel) { "build_rls" } else { "build" })
$exePath = Join-Path $buildDir "BedrockMap.exe"
$qtPath = $env:QT_ROOT
$cmakeCachePath = Join-Path $buildDir "CMakeCache.txt"

# Match the Qt headers and libraries selected by CMake, even if QT_ROOT has
# changed since configuration. Mixing installations also mixes QML plugins.
if (Test-Path -LiteralPath $cmakeCachePath) {
    $qtCacheEntry = Select-String -LiteralPath $cmakeCachePath -Pattern '^Qt6_DIR:PATH=(.+)$' |
        Select-Object -First 1
    if ($qtCacheEntry) {
        $qtCmakeDir = $qtCacheEntry.Matches[0].Groups[1].Value
        $qtPath = [System.IO.Path]::GetFullPath((Join-Path $qtCmakeDir "../../.."))
    }
}

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
$env:QML_IMPORT_PATH = Join-Path $qtPath "qml"

# Debug builds resolve translations and development assets relative to build/.
Push-Location $buildDir
try {
    # The application logs to stderr from its first line. Windows PowerShell
    # turns a native command's stderr into a terminating error while
    # $ErrorActionPreference is Stop, which would abort this script and take the
    # application down with it; merge the streams and print them as plain text
    # instead, so the log stays readable.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $exePath 2>&1 | ForEach-Object { Write-Host "$_" }
    } finally {
        $ErrorActionPreference = $previousPreference
    }
} finally {
    Pop-Location
}
