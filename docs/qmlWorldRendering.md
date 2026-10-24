# Qt Quick 世界地形渲染与数据调度

本文记录当前 Qt Quick 地图的数据所有权和线程边界。实现入口包括 `src/controller/worldsession.cpp`、`src/loader/asynclevelloader.cpp`、`src/render/terraindataprovider.cpp`、`src/qml/terrainquickitem.cpp` 与 `src/qml/mapoverlayquickitem.cpp`。

## 所有权

`SessionController` 管理多个世界会话。每个 `WorldSession` 独立拥有 loader、地图状态、地形 provider 和编辑控制器。可见的 `WorldWorkspace` 把当前会话的 `MapController` 与 `TerrainDataProvider` 交给 `MapCanvas`。`TerrainQuickItem` 维护当前窗口的 GPU 资源；世界切换时会清理旧世界的待上传 tile 和驻留状态。

`MapView`/`MapController` 是相机、维度、图层、选择与地图操作的状态源。`TerrainQuickItem` 从这些状态计算可见 region 与所需 LOD，向 provider 请求数据，不自行读 LevelDB。

## 三个线程域

1. **GUI 线程**：QML 输入、控制器属性和信号、视口需求提交，以及 provider 的结果验证。
2. **工作线程**：LevelDB 读取、区域烘焙、tile 采样和概览图生成。任务归属于对应世界会话，结果回到 GUI 线程后检查代际与视口需求。
3. **Scene Graph 渲染线程**：`TerrainQuickItem` 上传 tile、维护纹理与 page table 并绘制地形；`MapOverlayQuickItem` 更新叠加层节点。该线程不直接访问 LevelDB 或可变世界数据。

## 打开、切换与关闭

打开世界时，`WorldSession` 创建并启动 `AsyncLevelLoader`，随后关联 `MapController`、`TerrainDataProvider` 和编辑控制器。加载进度通过会话属性展示在 `OpenWorldPage.qml`。坐标索引可异步预加载；地形仍以当前视口为优先，按需烘焙 region。

隐藏会话保留自身的世界和缓存，但暂停可见视口驱动的工作。再次激活时，地图 item 用当前相机状态重新提交需求。关闭会话时先断开页面与 provider，等待会话拥有的后台任务完成，再销毁 loader 和数据对象。

## Tile 数据流

`TerrainDataProvider` 以视口 region、维度、图层和采样精度为输入，协调 `RegionRenderScheduler` 与 `AsyncLevelLoader`。区域烘焙形成 `ChunkRegion` 的方块高度、颜色、材质和实体快照；`TerrainTiles` 将其采样成可上传的 tile。provider 交付 tile 时附带坐标、维度、图层和 LOD，供 item 判断是否仍适用。

`TerrainQuickItem` 依据视口和缩放选择 LOD，把已交付 tile 放入纹理数组，并更新 page table。概览图在细节 tile 不足时提供粗尺度内容。区块编辑会使受影响 region 失效，后续按当前需求重新加载和采样。

## 叠加层

`MapOverlayQuickItem` 从地图控制器和 provider 获取选择、网格、实体、HSA、村庄与史莱姆区块等视图快照，在 scene graph 中组织相应节点。它与地形 item 是兄弟绘制层，不拥有世界数据或相机状态。

## 关键约束

- 世界数据、区域缓存和任务代际按会话隔离；仅凭 region 坐标不能跨世界复用 tile。
- 视口变化只请求需要的数据。后台会话不持续轮询或上传不可见 tile。
- GUI 和渲染线程只接收不可变的区域或图像快照；写入与编辑通过 loader/控制器完成。
- LOD 切换期间保留可用的旧内容，直到新需求得到足够的数据，避免整窗同时变成占位块。

具体纹理布局、预算和着色参数以当前 C++ 与 shader 为准。
