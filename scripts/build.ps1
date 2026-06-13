$build_dir = "./build"
if (!(Test-Path $build_dir)) {
    New-Item -Path "." -Name $build_dir -ItemType Directory 
}
# 编译 (首次运行需要 cmake -G "Ninja" -B build .  来生成构建系统)
if (!(Test-Path "$build_dir/build.ninja")) {
    cmake -G "Ninja" -B $build_dir .
}
cmake --build $build_dir --config Debug -j 18 -- 