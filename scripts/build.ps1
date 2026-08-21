param(
    [switch]$BuildBL,
    [ValidateRange(1, 256)]
    [int]$Jobs = 0
)

$qtPath = $env:QT_ROOT

# Optionally build bedrock-level first.
if ($BuildBL) {
    Push-Location ./bedrock-level
    .\build.ps1
    Pop-Location
}

# Update language files.
$lupdate = Join-Path  $qtPath "\bin\lupdate.exe"
& $lupdate -no-obsolete -no-ui-lines -recursive ./src -ts translations/zh_CN.ts translations/en.ts
# Strip line numbers from .ts files (UTF-8 without BOM) to avoid spurious diffs.
Get-ChildItem translations/*.ts | ForEach-Object {
    $content = (Get-Content $_ -Encoding UTF8 -Raw) -replace ' line="\d+"', ''
    [System.IO.File]::WriteAllText($_.FullName, $content, [System.Text.UTF8Encoding]::new($false))
}

# Configure only when this build directory has no CMake cache. The generator
# is selected by the initial configure and must not be inferred from stale files.
$build_dir = Join-Path (Get-Location) "build"
if (!(Test-Path $build_dir)) {
    New-Item -Path $build_dir -ItemType Directory | Out-Null
}
if (!(Test-Path (Join-Path $build_dir "CMakeCache.txt"))) {
    cmake -G "Ninja" -B $build_dir .
}

if ($Jobs -eq 0) {
    $Jobs = [Environment]::ProcessorCount
}
cmake --build $build_dir --config Debug --parallel $Jobs
