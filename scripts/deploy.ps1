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

Copy-Item -Path config.ini -Destination $release_dir
Copy-Item -Path .\bedrock-level\data\colors\block_color.json -Destination $release_dir
Copy-Item -Path .\bedrock-level\data\colors\biome_color.json -Destination $release_dir

Compress-Archive -Path $release_dir -DestinationPath BedrockMap.zip -Force
Remove-Item -Path $release_dir -Recurse -Force