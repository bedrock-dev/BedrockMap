$build_dir = "./build"
$exePath = Resolve-Path "./$build_dir/BedrockMap.exe"
Push-Location $build_dir
& $exePath
Pop-Location
