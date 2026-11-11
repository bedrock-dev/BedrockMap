#Requires -Version 5.1
param(
    [string]$QtRoot = 'C:\noinstall\Qt\6.11.1',
    [string]$EmsdkRoot = $(if ($env:EMSDK) { $env:EMSDK } else { Join-Path $env:USERPROFILE 'dev/emsdk-4.0.7' }),
    [string]$BuildDir = 'build_wasm',
    [ValidateRange(1, 256)]
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$sourceDir = Split-Path -Parent $PSScriptRoot
$qtWasm = Join-Path $QtRoot 'wasm_multithread'
$qtHost = Join-Path $QtRoot 'mingw_64'
$qtCmake = Join-Path $qtWasm 'bin/qt-cmake.bat'
$sdkConfig = Join-Path $EmsdkRoot '.emscripten'
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $sourceDir $BuildDir }
foreach ($required in @($qtCmake, $sdkConfig)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Build dependency not found: $required" }
}

$buildEnvironment = @{ EMSDK = $EmsdkRoot; EM_CONFIG = $sdkConfig; QT_ROOT = $qtWasm }
$previousEnvironment = @{}
try {
    foreach ($entry in $buildEnvironment.GetEnumerator()) {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    & $qtCmake -S $sourceDir -B $BuildDir -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DQT_HOST_PATH=$qtHost" '-UBEDROCKMAP_VERSION_SUFFIX'
    if ($LASTEXITCODE -ne 0) { throw "Wasm configure failed ($LASTEXITCODE)." }
    cmake --build $BuildDir --target BedrockMap --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Wasm build failed ($LASTEXITCODE)." }
    # Name the distribution directory after the configured application version.
    $cache = Get-Content -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Raw
    $version = [regex]::Match($cache, '(?m)^CMAKE_PROJECT_VERSION:STATIC=([^\r\n]+)').Groups[1].Value
    $suffix = [regex]::Match($cache, '(?m)^BEDROCKMAP_VERSION_SUFFIX:STRING=([^\r\n]*)').Groups[1].Value
    $versionName = "$version$suffix"
    if (-not $version -or $versionName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') { throw 'The configured application version is invalid.' }
    $distDir = Join-Path $sourceDir "dist/BedrockMap-$versionName-wasm"
    $runtimeFiles = @('index.html', 'BedrockMap.js', 'BedrockMap.wasm', 'qtloader.js', 'coi-serviceworker.js')
    foreach ($name in $runtimeFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $BuildDir $name) -PathType Leaf)) { throw "Missing wasm runtime file: $name" }
    }
    [void][IO.Directory]::CreateDirectory($distDir)
    foreach ($name in $runtimeFiles) {
        Copy-Item -LiteralPath (Join-Path $BuildDir $name) -Destination $distDir -Force
    }
    Copy-Item -LiteralPath (Join-Path $sourceDir 'web/serve.py') -Destination $distDir -Force
    # Static hosts that support precompressed assets serve these with Content-Encoding: gzip.
    Add-Type -AssemblyName System.IO.Compression
    $levels = [Enum]::GetNames([IO.Compression.CompressionLevel])
    $level = if ($levels -contains 'SmallestSize') { 'SmallestSize' } else { 'Optimal' }
    foreach ($name in @('BedrockMap.wasm', 'BedrockMap.js', 'qtloader.js')) {
        $source = Join-Path $distDir $name
        $sourceStream = [IO.File]::OpenRead($source)
        try {
            $output = [IO.File]::Create("$source.gz")
            try {
                $gzip = New-Object IO.Compression.GZipStream($output, [IO.Compression.CompressionLevel]$level)
                try { $sourceStream.CopyTo($gzip) } finally { $gzip.Dispose() }
            } finally { $output.Dispose() }
        } finally { $sourceStream.Dispose() }
    }
    Write-Host "Distribution directory: $distDir"
    Write-Host "Serve with: python `"$(Join-Path $distDir 'serve.py')`""
} finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
}
