param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$BenchmarkArgs
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exePath = Join-Path $projectRoot "build\benchmark\BedrockMapBenchmark.exe"
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Benchmark executable not found. Build it with: cmake --build build --target BedrockMapBenchmark --config Debug"
}

$qtPath = $env:QT_ROOT
if (-not $qtPath) {
    throw "QT_ROOT is not set. Set it to the Qt MinGW installation before running a benchmark."
}

$mingwBin = Split-Path (Get-Command gcc -ErrorAction Stop).Source -Parent
$env:PATH = "$mingwBin;$qtPath\bin;$env:PATH"

Push-Location (Join-Path $projectRoot "build")
try {
    & $exePath @BenchmarkArgs
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
