$qtPath = $env:QT_ROOT
if (-not $qtPath) {
    Write-Error "Env 'Qt_ROOT' is not set"
    exit 1
}
$windeployqt = Join-Path  $qtPath "\bin\windeployqt.exe"

#build
$build_dir = "./build_rls"
if (Test-Path $build_dir) {
    Remove-Item $build_dir -Recurse -Force
}
New-Item -Path "." -Name $build_dir -ItemType Directory 

$lupdate = Join-Path  $qtPath "\bin\lupdate.exe"
& $lupdate -no-obsolete -no-ui-lines -recursive ./src -ts translations/zh_CN.ts translations/en.ts
# Strip line numbers from .ts files (UTF-8 no BOM) to avoid spurious diffs on every code change
Get-ChildItem translations/*.ts | ForEach-Object { $content = (Get-Content $_ -Encoding UTF8 -Raw) -replace ' line="\d+"', ''; [System.IO.File]::WriteAllText($_.FullName, $content, [System.Text.UTF8Encoding]::new($false)) }

# 编译
cmake -G "MinGW Makefiles" -B  $build_dir -DCMAKE_BUILD_TYPE=Release .
cmake --build $build_dir -j 18 --

#deploy
$release_dir = "BedrockMap"
if (Test-Path $release_dir) {
    Remove-Item $release_dir -Recurse -Force
}
New-Item -Path "." -Name $release_dir -ItemType Directory 

Copy-Item -Path "$build_dir\BedrockMap.exe" -Destination $release_dir

#& windeployqt
Push-Location $release_dir
& $windeployqt --release BedrockMap.exe
Pop-Location

# windeployqt 复制的 libwinpthread-1.dll/libstdc++-6.dll/libgcc_s_seh-1.dll 是
# Qt 自带的 MSVCRT 版本，与 MinGW UCRT 编译的 exe 不兼容（堆损坏 0xC0000374）
# 用 MinGW 编译器自带的版本统一替换
$mingwBin = Split-Path (Get-Command gcc -ErrorAction Stop).Source -Parent
foreach ($dll in @("libwinpthread-1.dll", "libstdc++-6.dll", "libgcc_s_seh-1.dll")) {
    $mingwDll = Join-Path $mingwBin $dll
    if (Test-Path $mingwDll) {
        Copy-Item -Path $mingwDll -Destination "$release_dir\$dll" -Force
        Write-Host "Replaced $dll with MinGW version"
    } else {
        Write-Warning "MinGW $dll not found at $mingwDll"
    }
}

Copy-Item -Path config.ini -Destination $release_dir
Copy-Item -Path .\bedrock-level\data\colors\block_color.json -Destination $release_dir
Copy-Item -Path .\bedrock-level\data\colors\biome_color.json -Destination $release_dir
# 复制项目翻译文件
Copy-Item -Path "$build_dir\en.qm" -Destination "$release_dir\translations\" -ErrorAction SilentlyContinue
Copy-Item -Path "$build_dir\zh_CN.qm" -Destination "$release_dir\translations\" -ErrorAction SilentlyContinue

Compress-Archive -Path $release_dir -DestinationPath BedrockMap.zip -Force
Remove-Item -Path $release_dir -Recurse -Force