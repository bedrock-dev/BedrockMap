$build_dir = "./build"
$exePath = Resolve-Path "./$build_dir/BedrockMap.exe"
Push-Location $build_dir
& $exePath -style windows11
# & $exePath -style Fusion
# & $exePath -style windowsvista

Pop-Location
