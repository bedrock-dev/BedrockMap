## 编译指南

### 环境配置
本项目默认使用的编译器是mingw64 + Qt5,其他编译器如msvc理论上可以编译，但是没有经过充分测试，因此推荐使用mingw64。
- 前往[https://www.mingw-w64.org/](https://www.mingw-w64.org/)下载mingw64,并配置环境变量，具体请自行寻找网络的教程。
- 请自行下载Qt5，并配置环境变量，具体请自行寻找网络教程。



### 编译bedrock-level

本项目依赖于[https://github.com/bedrock-dev/bedrock-level](https://github.com/bedrock-dev/bedrock-level)。
- 前往上述仓库链接下载源码，并使用Cmake编译即可，工具链请同样选择上述mingw64:编译成功后build目录应该会生成`libbedrock-level.a`库文件

## 编译bedrock-map
**欢迎PR完善自动化编译流程**
- 修改cmake，由于暂时没有设置变量，因此需要手动修改如下几处：

    1. `BEDROCK_LEVEL_ROOT`修改为上一步下载的bedrock-level的源码的根目录，并把`/.release-mingw/libbedrock-level.a`修改为你上一步编译出来的`libbedrock-level.a`的路径
    ```
    set(BEDROCK_LEVEL_ROOT "C:/Users/xhy/dev/bedrock-level")
    set(BEDROCK_LIBS
            ${BEDROCK_LEVEL_ROOT}/.release-mingw/libbedrock-level.a
            ${BEDROCK_LEVEL_ROOT}/libs/libleveldb-mingw64.a
            ${BEDROCK_LEVEL_ROOT}/libs/libz-mingw64.a
    )
    ```

    2. 修改下面变量为你安装的Qt对应的路径
    ```
    set(CMAKE_PREFIX_PATH "C:\\Qt\\Qt5.12.12\\5.12.12\\mingw73_64")
    ```
- 编译，直接使用任意IDE打开项目(推荐Qt官方的Qt Creator / Clion / vscode+clangd)，配置编译工具链为mingw64,直接编译为release即可.


## 打包和发布

请自行查询`windeployqt`的使用方法。

