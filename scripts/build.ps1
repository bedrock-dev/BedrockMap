$build_dir = "./build"
# 编译
cmake -G "MinGW Makefiles" -B  $build_dir .
cmake --build $build_dir --config Debug -j 18 -- 