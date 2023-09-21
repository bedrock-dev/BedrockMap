$build_dir="./build"
# 编译
cmake -B  $build_dir .
cmake --build $build_dir --config Debug -j 18 -- 
# 运行
Start-Process -FilePath  "./$build_dir/BedrockMap.exe"
