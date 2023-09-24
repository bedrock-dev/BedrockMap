$build_dir = "./build"
cmake --build $build_dir --config Debug -j 18 -- 
# 运行
Start-Process -FilePath  "./$build_dir/BedrockMap.exe"
