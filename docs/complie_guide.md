## 编译指南

### 1. 准备

本项目默认使用的编译器是mingw64 + Qt5.12,其他编译器如msvc理论上可以编译，但是没有经过充分测试，因此推荐使用mingw64。
- 前往[https://www.mingw-w64.org/](https://www.mingw-w64.org/)下载mingw64,并配置环境变量，具体请自行寻找网络的教程。
- 下载Qt5.12，并配置环境变量，具体请自行寻找网络教程，最好做到小版本也匹配，否则可能无法兼容。

### 2.编译

1. 下载源码:

    ```shell
    git clone --recursive https://github.com/bedrock-dev/BedrockMap.git
    ```
2. 进入bedrock目录后编译bedrock-level
    ```shell
    cd .\bedrock-level\
    .\build.ps1
    ```
3. 编译并运行BedrockMap
    ```shell
    .\scripts\build_run.ps1
    ```
注意默认编译的是开发版本，如果有打包需求请自行修改`build_run.ps1`脚本


