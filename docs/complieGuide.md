# 编译指南

BedrockMap 使用 Qt 6、C++20、CMake 和 Ninja。Windows 开发环境需要 MinGW；将 `QT_ROOT` 设置为匹配该编译器的 Qt 6 安装目录，并确保 `cmake`、`ninja` 和 `gcc` 在 `PATH` 中。

```powershell
git clone --recursive https://github.com/bedrock-dev/BedrockMap.git
cd BedrockMap
.\scripts\build.ps1
.\scripts\run.ps1
```

`build.ps1 -Rel` 使用 `build_rls/` 构建 Release；默认 Debug 构建在 `build/`。`build_run.ps1` 会连续构建和运行。CMake 会通过子目录自动构建 `bedrock-level`，无需单独编译。

桌面应用的 CMake 目标为 `BedrockMapQml`，可用 `cmake --build build --target BedrockMapQml --parallel 8` 增量构建。Android 构建使用 `scripts/build_android.ps1`。
