param([switch]$BuildBL)

$qtPath = $env:QT_ROOT

# optionally build bedrock-level first
if ($BuildBL) {
    Push-Location ./bedrock-level
    .\build.ps1
    Pop-Location
}

#update language files
$lupdate = Join-Path  $qtPath "\bin\lupdate.exe"
& $lupdate -no-obsolete -no-ui-lines -recursive ./src -ts translations/zh_CN.ts translations/en.ts
# Strip line numbers from .ts files (UTF-8 no BOM) to avoid spurious diffs on every code change
Get-ChildItem translations/*.ts | ForEach-Object { $content = (Get-Content $_ -Encoding UTF8 -Raw) -replace ' line="\d+"', ''; [System.IO.File]::WriteAllText($_.FullName, $content, [System.Text.UTF8Encoding]::new($false)) }

#cmake (首次运行自动检测并生成 Ninja 构建系统)
$build_dir = "./build"
if (!(Test-Path $build_dir)) {
    New-Item -Path "." -Name $build_dir -ItemType Directory 
}
if (!(Test-Path "$build_dir/build.ninja")) {
    cmake -G "Ninja" -B $build_dir .
}
cmake --build $build_dir --config Debug -j 32 --