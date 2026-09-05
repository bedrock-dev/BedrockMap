param(
    [switch]$Rel,
    [ValidateRange(1, 256)]
    [int]$Jobs = 0
)

$qtPath = $env:QT_ROOT

# bedrock-level is built from source via add_subdirectory in CMakeLists.txt,
# so no separate pre-build step is needed (the old -BuildBL flag is gone).

# Update language files.
$lupdate = Join-Path  $qtPath "\bin\lupdate.exe"
& $lupdate -no-obsolete -no-ui-lines -recursive ./src -ts translations/zh_CN.ts translations/en.ts
# Strip line numbers from .ts files (UTF-8 without BOM) to avoid spurious diffs.
Get-ChildItem translations/*.ts | ForEach-Object {
    $content = (Get-Content $_ -Encoding UTF8 -Raw) -replace ' line="\d+"', ''
    [System.IO.File]::WriteAllText($_.FullName, $content, [System.Text.UTF8Encoding]::new($false))
}

# Release (-Rel) builds go to build_rls/, debug builds to build/.
$build_dir = Join-Path (Get-Location) $(if ($Rel) { "build_rls" } else { "build" })
$config_type = if ($Rel) { "Release" } else { "Debug" }

# Configure only when this build directory has no CMake cache. The generator
# is selected by the initial configure and must not be inferred from stale files.
if (!(Test-Path $build_dir)) {
    New-Item -Path $build_dir -ItemType Directory | Out-Null
}
if (!(Test-Path (Join-Path $build_dir "CMakeCache.txt"))) {
    # Quote the -D arg: PS 5.1 passes -DNAME=$var through unexpanded otherwise.
    cmake -G "Ninja" -B $build_dir "-DCMAKE_BUILD_TYPE=$config_type" .
}

if ($Jobs -eq 0) {
    $Jobs = [Environment]::ProcessorCount
}
cmake --build $build_dir --config $config_type --parallel $Jobs
