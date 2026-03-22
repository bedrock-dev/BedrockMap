#include "voxelwidget.h"

#include <qchar.h>
#include <qcolor.h>
#include <qdebug.h>

#include <QDebug>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <string>
#include <vector>

#include "include/voxelwidget.h"
#include "utils.h"

// 每个面的索引模板（2个三角形，6个索引）
const std::vector<GLuint> FACE_INDICES = {0, 1, 2, 0, 2, 3};
// 类外初始化面模板（一维vector，每个面12个坐标值）
const std::vector<std::vector<float>> VoxelWidget::m_faceTemplates = {
    {-0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f},      // +Z 前面
    {-0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f},  // -Z 后面
    {-0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f},  // -X 左面
    {0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f},      // +X 右面
    {-0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f},      // +Y 上面
    {-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f}   // -Y 下面
};

// 6个面的法线方向
const std::vector<QVector3D> VoxelWidget::m_faceNormals = {QVector3D(0.0f, 0.0f, 1.0f),  QVector3D(0.0f, 0.0f, -1.0f),
                                                           QVector3D(-1.0f, 0.0f, 0.0f), QVector3D(1.0f, 0.0f, 0.0f),
                                                           QVector3D(0.0f, 1.0f, 0.0f),  QVector3D(0.0f, -1.0f, 0.0f)};
VoxelWidget::VoxelWidget(QWidget* parent) : QOpenGLWidget(parent) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);  // 抗锯齿
    setFormat(format);
    this->setAttribute(Qt::WA_NativeWindow, true);

    // 初始化层范围
    if (!m_voxelData.empty()) {
        m_startLayer = 0;
        m_endLayer = m_voxelData.size() - 1;
    }
    m_voxelSize = 1.0f;

    // 提前构建顶点数据，避免初始化时为空
    buildVoxelVertices();
    setAutoFillBackground(false);
}

VoxelWidget::~VoxelWidget() {
    makeCurrent();
    // 释放OpenGL资源
    glDeleteVertexArrays(1, &m_vaoOpaque);
    glDeleteVertexArrays(1, &m_vaoTransparent);
    glDeleteBuffers(1, &m_vboOpaque);
    glDeleteBuffers(1, &m_vboTransparent);
    glDeleteBuffers(1, &m_eboOpaque);
    glDeleteBuffers(1, &m_eboTransparent);
    delete m_shaderProgram;
    doneCurrent();
}

// 设置渲染层范围
void VoxelWidget::setLayer(int startLayer, int endLayer) {
    if (m_voxelData.empty()) return;
    // 边界校验
    m_startLayer = std::max(0, startLayer);
    m_endLayer = std::min((int)m_voxelData.size() - 1, endLayer);
    m_startLayer = std::min(m_startLayer, m_endLayer);
    // 重建顶点并刷新
    buildVoxelVertices();
    update();
}

// 更新体素数据
void VoxelWidget::updateVoxelData(const std::vector<std::vector<std::vector<Voxel>>>& newData) {
    m_voxelData = newData;
    // 重置层范围
    if (!m_voxelData.empty()) {
        m_startLayer = 0;
        m_endLayer = m_voxelData.size() - 1;
    }
    buildVoxelVertices();
    update();
}

void VoxelWidget::forceInitAndRender() {
    // 强制激活OpenGL上下文
    makeCurrent();

    // 重新初始化缓冲（解决上下文丢失）
    if (m_vaoOpaque == 0 || m_vaoTransparent == 0) {
        initOpenGLBuffers();
    }

    // 强制触发resize（解决视口/投影矩阵问题）
    resizeGL(width(), height());

    // 强制刷新渲染
    update();

    doneCurrent();
}

// 核心：邻居判定函数（严格按自定义剔除规则）
bool VoxelWidget::hasNeighbor(int layer, int x, int z, int dLayer, int dX, int dZ) {
    // 1. 计算相邻体素三维坐标（Y/X/Z）
    int neighborY = layer + dLayer;  // Y轴（层）偏移
    int neighborX = x + dX;          // X轴偏移
    int neighborZ = z + dZ;          // Z轴偏移

    // 2. 定义「空气」：超出边界 或 alpha=0
    bool isNeighborAir = true;
    const Voxel* neighborVoxel = nullptr;

    // 边界校验：未超出边界时获取邻居体素
    if (neighborY >= 0 && neighborY < static_cast<int>(m_voxelData.size()) && neighborX >= 0 &&
        neighborX < static_cast<int>(m_voxelData[neighborY].size()) && neighborZ >= 0 &&
        neighborZ < static_cast<int>(m_voxelData[neighborY][neighborX].size())) {
        neighborVoxel = &m_voxelData[neighborY][neighborX][neighborZ];
        isNeighborAir = (neighborVoxel->color.alpha() == 0);  // alpha=0=空气
    }

    // 3. 获取当前体素的透明状态
    const Voxel& currentVoxel = m_voxelData[layer][x][z];
    bool isCurrentOpaque = !currentVoxel.transparent;      // 不透明：transparent=false
    bool isCurrentTransparent = currentVoxel.transparent;  // 半透明：transparent=true

    // 4. 严格执行自定义剔除规则
    if (isCurrentOpaque) {
        // 规则1：不透明方块 → 仅遇到不透明方块才剔除（返回true），其他均保留（返回false）
        if (neighborVoxel != nullptr && !neighborVoxel->transparent && neighborVoxel->color.alpha() > 0) {
            return true;  // 遇到不透明 → 剔除面
        } else {
            return false;  // 遇到半透明/空气 → 保留面
        }
    } else if (isCurrentTransparent) {
        // 规则2：半透明方块 → 仅遇到空气才不剔除（返回false），其他均剔除（返回true）
        if (isNeighborAir) {
            return false;  // 遇到空气 → 保留面
        } else {
            return true;  // 遇到不透明/半透明 → 剔除面
        }
    }

    // 兜底：默认保留面
    return false;
}

// 添加单个面的顶点数据（仅用transparent区分不透明/半透明）
void VoxelWidget::addFaceVertices(int layer, int x, int z, const Voxel& voxel, const std::vector<float>& faceVertices,
                                  const QVector3D& normal) {
    // 体素世界坐标（Y/X/Z）
    float worldX = x * m_voxelSize;
    float worldY = layer * m_voxelSize;
    float worldZ = z * m_voxelSize;

    // 严格基于transparent区分渲染层（核心）
    bool isOpaque = !voxel.transparent;  // transparent=false → 不透明层
    auto& vertices = isOpaque ? m_verticesOpaque : m_verticesTransparent;
    auto& indices = isOpaque ? m_indicesOpaque : m_indicesTransparent;

    // 当前面的顶点偏移（3位置+3法线+4颜色=10个分量）
    int vertexOffset = vertices.size() / 10;

    // 添加4个顶点数据
    for (int i = 0; i < 4; ++i) {
        // 1. 位置（缩放+平移）
        float posX = faceVertices[i * 3 + 0] * m_voxelSize + worldX;
        float posY = faceVertices[i * 3 + 1] * m_voxelSize + worldY;
        float posZ = faceVertices[i * 3 + 2] * m_voxelSize + worldZ;
        vertices.push_back(posX);
        vertices.push_back(posY);
        vertices.push_back(posZ);

        // 2. 法线（统一方向，无需缩放）
        vertices.push_back(normal.x());
        vertices.push_back(normal.y());
        vertices.push_back(normal.z());

        // 3. 颜色（RGBA归一化到0~1）
        vertices.push_back(voxel.color.redF());
        vertices.push_back(voxel.color.greenF());
        vertices.push_back(voxel.color.blueF());
        vertices.push_back(voxel.color.alphaF());
    }

    // 添加6个索引（2个三角形）
    for (GLuint idx : FACE_INDICES) {
        indices.push_back(idx + vertexOffset);
    }
}
// 构建所有体素的顶点数据（仅渲染外表面）
void VoxelWidget::buildVoxelVertices() {
    // 清空旧数据
    m_verticesOpaque.clear();
    m_verticesTransparent.clear();
    m_indicesOpaque.clear();
    m_indicesTransparent.clear();

    if (m_voxelData.empty() || m_startLayer > m_endLayer) return;

    // 遍历三维体素（Y/X/Z）
    for (int layer = m_startLayer; layer <= m_endLayer; ++layer) {  // Y轴（层）
        const auto& layerData = m_voxelData[layer];
        if (layerData.empty()) continue;

        for (int x = 0; x < layerData.size(); ++x) {  // X轴
            const auto& rowData = layerData[x];
            if (rowData.empty()) continue;

            for (int z = 0; z < rowData.size(); ++z) {  // Z轴
                const Voxel& voxel = rowData[z];

                // 过滤完全透明体素（空气不渲染）
                if (voxel.color.alpha() == 0) {
                    continue;
                }

                // 检查6个方向的邻居，仅渲染需要保留的面
                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                    int dLayer = 0, dX = 0, dZ = 0;
                    // 每个面的偏移方向
                    switch (faceIdx) {
                        case 0:
                            dZ = 1;
                            break;  // +Z 前面
                        case 1:
                            dZ = -1;
                            break;  // -Z 后面
                        case 2:
                            dX = -1;
                            break;  // -X 左面
                        case 3:
                            dX = 1;
                            break;  // +X 右面
                        case 4:
                            dLayer = 1;
                            break;  // +Y 上面
                        case 5:
                            dLayer = -1;
                            break;  // -Y 下面
                    }

                    // 核心：!hasNeighbor → 面需要保留 → 渲染该面
                    if (!hasNeighbor(layer, x, z, dLayer, dX, dZ)) {
                        addFaceVertices(layer, x, z, voxel, m_faceTemplates[faceIdx], m_faceNormals[faceIdx]);
                    }
                }
            }
        }
    }
}

// 初始化OpenGL缓冲（VAO/VBO/EBO）
void VoxelWidget::initOpenGLBuffers() {
    // ========== 初始化不透明体素缓冲 ==========
    if (m_vaoOpaque == 0) glGenVertexArrays(1, &m_vaoOpaque);
    if (m_vboOpaque == 0) glGenBuffers(1, &m_vboOpaque);
    if (m_eboOpaque == 0) glGenBuffers(1, &m_eboOpaque);

    glBindVertexArray(m_vaoOpaque);
    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, m_vboOpaque);
    glBufferData(GL_ARRAY_BUFFER, m_verticesOpaque.size() * sizeof(float), m_verticesOpaque.data(), GL_DYNAMIC_DRAW);
    // EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_eboOpaque);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indicesOpaque.size() * sizeof(GLuint), m_indicesOpaque.data(), GL_DYNAMIC_DRAW);

    // 顶点属性配置
    // 位置：3个float，偏移0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // 法线：3个float，偏移3*sizeof(float)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // 颜色：4个float，偏移6*sizeof(float)
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // ========== 初始化半透明体素缓冲 ==========
    if (m_vaoTransparent == 0) glGenVertexArrays(1, &m_vaoTransparent);
    if (m_vboTransparent == 0) glGenBuffers(1, &m_vboTransparent);
    if (m_eboTransparent == 0) glGenBuffers(1, &m_eboTransparent);

    glBindVertexArray(m_vaoTransparent);
    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, m_vboTransparent);
    glBufferData(GL_ARRAY_BUFFER, m_verticesTransparent.size() * sizeof(float), m_verticesTransparent.data(), GL_DYNAMIC_DRAW);
    // EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_eboTransparent);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indicesTransparent.size() * sizeof(GLuint), m_indicesTransparent.data(), GL_DYNAMIC_DRAW);

    // 顶点属性配置（和不透明一致）
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // 解绑
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void VoxelWidget::initializeGL() {
    // 初始化OpenGL核心函数
    initializeOpenGLFunctions();

    // ========== 渲染状态配置（关键：修复半透明） ==========
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);               // 背景色
    glEnable(GL_DEPTH_TEST);                            // 深度测试（解决分层）
    glDepthFunc(GL_LESS);                               // 严格深度判定
    glEnable(GL_BLEND);                                 // 混合（半透明）
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // 标准混合模式
    glEnable(GL_MULTISAMPLE);                           // 抗锯齿（解决边缘锯齿）
    glEnable(GL_CULL_FACE);                             // 背面剔除（优化性能）
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);  // 明确正面朝向

    // ========== 编译着色器（含基础光照） ==========
    m_shaderProgram = new QOpenGLShaderProgram(this);

    // 顶点着色器（法线+光照）
    m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec4 aColor;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        out vec4 vColor;
        out vec3 vNormal;
        out vec3 vFragPos;

        void main()
        {
            vFragPos = vec3(model * vec4(aPos, 1.0));
            // 法线变换（适配缩放/旋转）
            vNormal = mat3(transpose(inverse(model))) * aNormal;
            vColor = aColor;
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )");

    // 片段着色器（漫反射光照+半透明）
    m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        in vec4 vColor;
        in vec3 vNormal;
        in vec3 vFragPos;

        uniform vec3 lightPos;
        uniform vec3 lightColor;
        uniform vec3 ambientLight;

        out vec4 FragColor;

        void main()
        {
            // 1. 环境光
            vec3 ambient = ambientLight * vColor.rgb;

            // 2. 漫反射光照
            vec3 norm = normalize(vNormal);
            vec3 lightDir = normalize(lightPos - vFragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * lightColor * vColor.rgb;

            // 3. 最终颜色（保留Alpha通道）
            vec3 result = ambient + diffuse;
            FragColor = vec4(result, vColor.a);

            // 剔除完全透明片段
            if (FragColor.a <= 0.0) discard;
        }
    )");

    // 链接着色器
    if (!m_shaderProgram->link()) {
        qDebug() << "着色器链接失败：" << m_shaderProgram->log();
        return;
    }

    // 初始化OpenGL缓冲
    initOpenGLBuffers();
}

void VoxelWidget::resizeGL(int w, int h) {
    // 设置视口
    glViewport(0, 0, w, h);

    // 投影矩阵（透视投影）
    m_projection.setToIdentity();
    m_projection.perspective(45.0f, (float)w / h, 0.1f, 1000.0f);

    // 视图矩阵（相机位置）
    m_view.setToIdentity();
    m_view.lookAt(QVector3D(0.0f, 0.0f, 50.0f),  // 相机位置
                  QVector3D(0.0f, 0.0f, 0.0f),   // 观察目标
                  QVector3D(0.0f, 1.0f, 0.0f));  // 上方向
}

void VoxelWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_shaderProgram || !m_shaderProgram->isLinked()) return;

    m_shaderProgram->bind();

    // 更新模型矩阵（新增平移逻辑）
    m_model.setToIdentity();
    m_model.scale(m_scale);                       // 缩放
    m_model.rotate(m_rotateX, 1.0f, 0.0f, 0.0f);  // X轴旋转
    m_model.rotate(m_rotateY, 0.0f, 1.0f, 0.0f);  // Y轴旋转
    m_model.translate(m_cameraTranslate);         // 新增：应用相机平移
    // 居中平移（原有逻辑，平移叠加在居中之后）
    if (!m_voxelData.empty() && !m_voxelData[0].empty() && !m_voxelData[0][0].empty()) {
        float cx = (m_voxelData[0].size() * m_voxelSize) / 2.0f;
        float cy = (m_voxelData.size() * m_voxelSize) / 2.0f;
        float cz = (m_voxelData[0][0].size() * m_voxelSize) / 2.0f;
        m_model.translate(-cx, -cy, -cz);
    }

    // ========== 设置Uniform变量 ==========
    m_shaderProgram->setUniformValue("model", m_model);
    m_shaderProgram->setUniformValue("view", m_view);
    m_shaderProgram->setUniformValue("projection", m_projection);
    m_shaderProgram->setUniformValue("lightPos", m_lightPos);
    m_shaderProgram->setUniformValue("lightColor", m_lightColor);
    m_shaderProgram->setUniformValue("ambientLight", m_ambientLight);

    // ========== 1. 渲染不透明体素（写深度缓冲） ==========
    if (!m_indicesOpaque.empty()) {
        glDepthMask(GL_TRUE);  // 开启深度写入
        glBindVertexArray(m_vaoOpaque);
        // 更新VBO数据
        glBindBuffer(GL_ARRAY_BUFFER, m_vboOpaque);
        glBufferSubData(GL_ARRAY_BUFFER, 0, m_verticesOpaque.size() * sizeof(float), m_verticesOpaque.data());
        // 更新EBO数据
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_eboOpaque);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, m_indicesOpaque.size() * sizeof(GLuint), m_indicesOpaque.data());
        // 绘制
        glDrawElements(GL_TRIANGLES, m_indicesOpaque.size(), GL_UNSIGNED_INT, 0);
    }

    // ========== 2. 渲染半透明体素（只读深度缓冲） ==========
    if (!m_indicesTransparent.empty()) {
        glDepthMask(GL_FALSE);  // 关闭深度写入（关键：半透明不遮挡后续像素）
        glBindVertexArray(m_vaoTransparent);
        // 更新VBO数据
        glBindBuffer(GL_ARRAY_BUFFER, m_vboTransparent);
        glBufferSubData(GL_ARRAY_BUFFER, 0, m_verticesTransparent.size() * sizeof(float), m_verticesTransparent.data());
        // 更新EBO数据
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_eboTransparent);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, m_indicesTransparent.size() * sizeof(GLuint), m_indicesTransparent.data());
        // 绘制
        glDrawElements(GL_TRIANGLES, m_indicesTransparent.size(), GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);  // 恢复深度写入
    }

    // 解绑
    glBindVertexArray(0);
    m_shaderProgram->release();
}

// 鼠标按下：记录初始位置
void VoxelWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        // 左键：旋转
        m_lastMousePos = e->pos();
    } else if (e->button() == Qt::RightButton) {
        // 右键：平移（记录起始位置）
        m_panStartPos = e->pos();
        m_isPanDragging = true;
    }
    e->accept();
}

void VoxelWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        m_isPanDragging = false;
    }
    e->accept();
}

// 鼠标拖拽：旋转模型
void VoxelWidget::mouseMoveEvent(QMouseEvent* e) {
    // 1. 左键旋转（原有逻辑保留）
    if (e->buttons() & Qt::LeftButton && !m_isPanDragging) {
        int dx = e->x() - m_lastMousePos.x();
        int dy = e->y() - m_lastMousePos.y();
        m_rotateY += dx * 0.5f;
        m_rotateX += dy * 0.5f;
        m_rotateX = std::clamp(m_rotateX, -90.0f, 90.0f);
        m_lastMousePos = e->pos();
        update();
        return;
    }

    // 2. 右键平移（核心优化：全维度）
    if ((e->buttons() & Qt::RightButton) && m_isPanDragging) {
        // 计算鼠标偏移（相对于起始位置）
        int deltaX = e->x() - m_panStartPos.x();
        int deltaY = e->y() - m_panStartPos.y();

        // 灵敏度适配：缩放越大，平移越精细
        float sensitivity = m_panSensitivity / m_scale;

        // 平移规则（符合直觉）：
        // - 无Shift：水平→左右（X轴），垂直→上下（Y轴）
        // - 按Shift：水平→前后（Z轴），垂直→上下（Y轴）
        if (!m_isShiftPressed) {
            // 左右平移（X轴）：鼠标右移→模型右移，左移→模型左移
            m_cameraTranslate.setX(m_cameraTranslate.x() + deltaX * sensitivity);
            // 上下平移（Y轴）：鼠标上移→模型上移，下移→模型下移
            m_cameraTranslate.setY(m_cameraTranslate.y() - deltaY * sensitivity);
        } else {
            // 前后平移（Z轴）：鼠标右移→模型前移，左移→模型后移
            m_cameraTranslate.setZ(m_cameraTranslate.z() + deltaX * sensitivity);
            // 上下平移（Y轴）：保留
            m_cameraTranslate.setY(m_cameraTranslate.y() - deltaY * sensitivity);
        }

        // 更新起始位置（持续拖拽）
        m_panStartPos = e->pos();
        update();  // 刷新渲染
    }
}

// 滚轮缩放
void VoxelWidget::wheelEvent(QWheelEvent* e) {
    int delta = e->angleDelta().y();
    // 缩放增量（限制范围0.1~10.0）
    if (delta > 0) {
        m_scale = std::min(m_scale + 0.1f, 10.0f);
    } else {
        m_scale = std::max(m_scale - 0.1f, 0.1f);
    }
    update();  // 刷新渲染
}

void VoxelWidget::showEvent(QShowEvent* e) {
    QOpenGLWidget::showEvent(e);
    forceInitAndRender();
}
void VoxelWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Shift) {
        m_isShiftPressed = true;
    } else if (e->key() == Qt::Key_R) {
        m_rotateX = 0.0f;
        m_rotateY = 0.0f;
        m_scale = 1.0f;
        m_cameraTranslate = QVector3D(0.0f, 0.0f, 0.0f);  // 重置平移
        update();
    }
    QOpenGLWidget::keyPressEvent(e);
}
void VoxelWidget::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Shift) {
        m_isShiftPressed = false;
    }
    QOpenGLWidget::keyReleaseEvent(e);
}

std::vector<std::vector<std::vector<Voxel>>> VoxelWidget::createVoxelDataFromChunks(const std::vector<std::vector<bl::chunk*>>& chunks) {
    // 1. 空值快速返回（增强校验）
    if (chunks.empty() || chunks[0].empty()) {
        qDebug() << "Error: Chunks is empty!";
        return {};
    }
    const int chunk_width = chunks.size();     // Chunk的X方向数量
    const int chunk_depth = chunks[0].size();  // Chunk的Z方向数量
    const int chunk_block_size = 16;           // 每个Chunk的块尺寸（16x16xN）

    // 2. 计算所有Chunk的Y轴范围（修复MIN/MAX初始化逻辑）
    int min_y = INT_MAX, max_y = INT_MIN;
    for (const auto& chunk_row : chunks) {
        for (const bl::chunk* ch : chunk_row) {
            if (!ch) continue;
            auto [chunk_min_y, chunk_max_y] = ch->get_pos().get_y_range(ch->get_version());
            min_y = std::min(min_y, chunk_min_y);
            max_y = std::max(max_y, chunk_max_y);
        }
    }

    // 3. Y轴范围无效校验（修复MAX<=MIN的逻辑）
    if (max_y < min_y) {
        qDebug() << "Error: Invalid Y range! min_y=" << min_y << ", max_y=" << max_y;
        return {};
    }
    const int total_y_layers = max_y - min_y + 1;

    // 4. 初始化三维Voxel数组（修复维度映射：Y → X → Z）
    std::vector<std::vector<std::vector<Voxel>>> data;
    data.resize(total_y_layers);  // Y轴：总层数
    for (auto& y_layer : data) {
        y_layer.resize(chunk_width * chunk_block_size);  // X轴：Chunk宽度×16
        for (auto& x_row : y_layer) {
            x_row.resize(chunk_depth * chunk_block_size);  // Z轴：Chunk深度×16
            // 初始化默认值：空气（透明+全黑+alpha=0）
            for (auto& voxel : x_row) {
                voxel.transparent = true;
                voxel.color = QColor(255, 255, 255, 0);
            }
        }
    }
    qDebug() << "Voxel Size: " << QString("%1 (Y) * %2 (X) * %3 (Z)").arg(data.size()).arg(data[0].size()).arg(data[0][0].size());

    // 5. 填充Chunk数据到Voxel数组（核心修复：坐标映射+越界校验）
    for (int chunk_x_idx = 0; chunk_x_idx < chunk_width; ++chunk_x_idx) {
        // 校验Chunk行是否越界
        if (chunk_x_idx >= chunks.size()) continue;
        const auto& chunk_row = chunks[chunk_x_idx];

        for (int chunk_z_idx = 0; chunk_z_idx < chunk_depth; ++chunk_z_idx) {
            // 校验Chunk列是否越界
            if (chunk_z_idx >= chunk_row.size()) continue;
            bl::chunk* ch = chunk_row[chunk_z_idx];

            // 空Chunk直接跳过（增强校验）
            if (!ch) {
                qDebug() << "Warning: Empty chunk at (" << chunk_x_idx << ", " << chunk_z_idx << ")";
                continue;
            }

            // 获取当前Chunk的Y轴范围
            auto [chunk_min_y, chunk_max_y] = ch->get_pos().get_y_range(ch->get_version());
            // 遍历当前Chunk的所有块
            for (int local_y = chunk_min_y; local_y <= chunk_max_y; ++local_y) {
                // 校验Y轴索引是否在Voxel数组范围内（核心越界防护）
                const int global_y_idx = local_y - min_y;
                if (global_y_idx < 0 || global_y_idx >= total_y_layers) {
                    qDebug() << "Warning: Y out of range! local_y=" << local_y << ", global_y_idx=" << global_y_idx;
                    continue;
                }

                for (int local_x = 0; local_x < chunk_block_size; ++local_x) {
                    // 计算全局X索引
                    const int global_x_idx = chunk_x_idx * chunk_block_size + local_x;
                    if (global_x_idx < 0 || global_x_idx >= data[global_y_idx].size()) {
                        qDebug() << "Warning: X out of range! local_x=" << local_x << ", global_x_idx=" << global_x_idx;
                        continue;
                    }

                    for (int local_z = 0; local_z < chunk_block_size; ++local_z) {
                        // 计算全局Z索引（修复核心坐标映射）
                        const int global_z_idx = chunk_z_idx * chunk_block_size + local_z;
                        if (global_z_idx < 0 || global_z_idx >= data[global_y_idx][global_x_idx].size()) {
                            qDebug() << "Warning: Z out of range! local_z=" << local_z << ", global_z_idx=" << global_z_idx;
                            continue;
                        }

                        // 填充Voxel数据
                        Voxel& voxel = data[global_y_idx][global_x_idx][global_z_idx];
                        auto block_info = ch->get_block(local_x, local_y, local_z);
                        auto biome = ch->get_biome(local_x, local_y, local_z);
                        if (block_info.name == "minecraft:unknown" || block_info.name == "minecraft:air") continue;

                        // 计算生物群系混合色（移除硬编码覆盖）
                        auto blend_color = bl::blend_color_with_biome(block_info.name, block_info.color, biome);
                        voxel.color = QColor(blend_color.r, blend_color.g, blend_color.b, blend_color.a);
                        // 仅将水标记为透明（保留原始逻辑）
                        voxel.transparent = (block_info.name.find("minecraft:water") != std::string::npos);
                    }
                }
            }
        }
    }
    return data;
}