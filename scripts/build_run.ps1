$qtPath = $env:QT_ROOT

#update language files
$lupdate = Join-Path  $qtPath "\bin\lupdate.exe"
& $lupdate -no-obsolete -recursive ./src -ts translations/zh_CN.ts translations/en.ts

#cmake (首次运行自动检测并生成 Ninja 构建系统)
$build_dir = "./build"
if (!(Test-Path "$build_dir/build.ninja")) {
    cmake -G "Ninja" -B $build_dir .
}
cmake --build $build_dir --config Debug -j 32 --
# 运行 (使用 & 而非 Start-Process，以便在同一个终端看到 qDebug 输出)
$exePath = Resolve-Path "./$build_dir/BedrockMap.exe"
Push-Location $build_dir
& $exePath
Pop-Location