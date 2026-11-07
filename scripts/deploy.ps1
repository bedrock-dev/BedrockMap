$qtPath = $env:QT_ROOT
if (-not $qtPath) {
    Write-Error "Env 'QT_ROOT' is not set"
    exit 1
}
$windeployqt = Join-Path $qtPath "bin\windeployqt.exe"
$jobs = [Environment]::ProcessorCount

# Build a clean release tree with Ninja.
$build_dir = "./build_rls"
if (Test-Path $build_dir) {
    Remove-Item $build_dir -Recurse -Force
}
New-Item -Path $build_dir -ItemType Directory | Out-Null

$lupdate = Join-Path  $qtPath "\bin\lupdate.exe"
& $lupdate -no-obsolete -no-ui-lines -recursive ./src -ts translations/zh_CN.ts translations/en.ts
# Strip line numbers from .ts files (UTF-8 no BOM) to avoid spurious diffs on every code change
Get-ChildItem translations/*.ts | ForEach-Object { $content = (Get-Content $_ -Encoding UTF8 -Raw) -replace ' line="\d+"', ''; [System.IO.File]::WriteAllText($_.FullName, $content, [System.Text.UTF8Encoding]::new($false)) }

# Configure and compile with Ninja.
cmake -S . -B $build_dir -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build $build_dir --config Release --parallel $jobs

# Package the standalone release directory.
$release_dir = "BedrockMap"
if (Test-Path $release_dir) {
    Remove-Item $release_dir -Recurse -Force
}
New-Item -Path "." -Name $release_dir -ItemType Directory 

Copy-Item -Path "$build_dir\BedrockMap.exe" -Destination $release_dir

# Deploy Qt runtime files for the standalone package.
Push-Location $release_dir
& $windeployqt --release --no-translations --no-compiler-runtime --qmldir "$PSScriptRoot\..\src\qml" BedrockMap.exe
Pop-Location

# The application always selects the Basic Controls style. Remove the other
# deployed styles and their QML trees to avoid shipping unused themes.
$releaseRoot = [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $release_dir))
$unusedStylePaths = @(
    "qml\QtQuick\Controls\FluentWinUI3",
    "qml\QtQuick\Controls\Fusion",
    "qml\QtQuick\Controls\Imagine",
    "qml\QtQuick\Controls\Material",
    "qml\QtQuick\Controls\Universal",
    "qml\QtQuick\Controls\Windows",
    "qml\QtQuick\NativeStyle"
)
foreach ($relativePath in $unusedStylePaths) {
    $targetPath = [IO.Path]::GetFullPath((Join-Path $releaseRoot $relativePath))
    if (-not $targetPath.StartsWith($releaseRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside release directory: $targetPath"
    }
    if (Test-Path -LiteralPath $targetPath) {
        Remove-Item -LiteralPath $targetPath -Recurse -Force
    }
}
$unusedStyleLibraries = Get-ChildItem -LiteralPath $releaseRoot -Filter "Qt6QuickControls2*.dll" -File |
    Where-Object Name -Match "Qt6QuickControls2(?:FluentWinUI3|Fusion|Imagine|Material|Universal|Windows).*\.dll$"
foreach ($library in $unusedStyleLibraries) {
    Remove-Item -LiteralPath $library.FullName -Force
}

# Keep debug information in the build tree, but not in the user-facing package.
$strip = (Get-Command strip -ErrorAction Stop).Source
& $strip --strip-unneeded (Join-Path $releaseRoot "BedrockMap.exe")
if ($LASTEXITCODE -ne 0) {
    throw "Failed to strip packaged executable ($LASTEXITCODE)."
}

# windeployqt may copy runtime DLLs from the Qt installation. Replace them
# with the versions from the compiler used to build this project.
$mingwBin = Split-Path (Get-Command gcc -ErrorAction Stop).Source -Parent
foreach ($dll in @("libwinpthread-1.dll", "libstdc++-6.dll", "libgcc_s_seh-1.dll","libbacktrace-0.dll")) {
    $mingwDll = Join-Path $mingwBin $dll
    if (Test-Path $mingwDll) {
        Copy-Item -Path $mingwDll -Destination "$release_dir\$dll" -Force
        Write-Host "Replaced $dll with MinGW version"
    } else {
        Write-Warning "MinGW $dll not found at $mingwDll"
    }
}

# Name the archive after the most recent version tag, e.g. BedrockMap-v1.0.0-beta10.zip
$versionTag = git describe --tags --abbrev=0 2>$null
if (-not $versionTag) {
    $versionTag = "unknown"
    Write-Warning "No git tag found; archive will be named with 'unknown'"
}
$archiveName = Join-Path (Split-Path -Parent $PSScriptRoot) "dist\BedrockMap-$versionTag.zip"

Compress-Archive -Path $release_dir -DestinationPath $archiveName -Force
Write-Host "Archive: $archiveName"
Remove-Item -Path $release_dir -Recurse -Force
