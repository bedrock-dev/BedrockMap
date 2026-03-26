#include "voxelwidget.h"

#include <qchar.h>
#include <qcolor.h>
#include <qdebug.h>
#include <qnamespace.h>
#include <qopenglshaderprogram.h>
#include <qurl.h>

#include <QDebug>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <string>
#include <vector>

#include "config.h"
#include "include/voxelwidget.h"

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
    format.setSamples(8);  // 抗锯齿
    setFormat(format);
}

VoxelWidget::~VoxelWidget() {
    makeCurrent();
    // 释放OpenGL资源
    glDeleteVertexArrays(1, &vao_opaque_);
    glDeleteVertexArrays(1, &vao_transparent_);
    glDeleteBuffers(1, &vbo_opaque_);
    glDeleteBuffers(1, &vbo_transparent_);
    glDeleteBuffers(1, &ebo_opaque_);
    glDeleteBuffers(1, &ebo_transparent_);
    delete gl_shader_;
    doneCurrent();
}

// 设置渲染层范围
void VoxelWidget::setLayer(int startLayer, int endLayer) {
    if (voxel_data_.empty()) return;
    // 边界校验
    start_layer_ = std::max(0, startLayer);
    ender_layer_ = std::min(static_cast<int>(voxel_data_.size()) - 1, endLayer);
    start_layer_ = std::min(start_layer_, ender_layer_);
    // 重建顶点并刷新
    buildVoxelVertices();
    update();
}

// 更新体素数据
void VoxelWidget::updateVoxelData(const std::vector<std::vector<std::vector<Voxel>>>& newData) {
    voxel_data_ = newData;
    if (!voxel_data_.empty()) {
        start_layer_ = 0;
        ender_layer_ = voxel_data_.size() - 1;
    }
    buildVoxelVertices();
    makeCurrent();
    generateOpenGLBuffers();
    rebufferOpenGLData();
    doneCurrent();
    update();
}

bool VoxelWidget::hasNeighbor(int layer, int x, int z, int dy, int dx, int dz) {
    const auto& current = voxel_data_[layer][x][z];

    int ny = layer + dy;  // y-offset
    int nx = x + dx;      // x-offset
    int nz = z + dz;      // z-offset

    if (ny < 0 || ny >= voxel_data_.size() ||       // x
        nx < 0 || nx >= voxel_data_[ny].size() ||   // y
        nz < 0 || nz >= voxel_data_[ny][nx].size()  // z
    ) {
        return false;
    } else {
        auto& neighbor = voxel_data_[ny][nx][nz];
        if (!current.transparent) {
            // solid
            return !neighbor.transparent;
        } else {
            // transparent
            return neighbor.color.alpha() != 0;
        }
    }
}

void VoxelWidget::addFaceVertices(int layer, int x, int z, const Voxel& voxel, const std::vector<float>& faceVertices,
                                  const QVector3D& normal) {
    float worldX = x * voxel_size_;
    float worldY = layer * voxel_size_;
    float worldZ = z * voxel_size_;

    bool isOpaque = !voxel.transparent;  // transparent=false → 不透明层
    auto& vertices = isOpaque ? verticles_opaque_ : verticles_transparent_;
    auto& indices = isOpaque ? indices_opaque_ : indices_transparent_;

    int vertexOffset = vertices.size() / 10;

    for (int i = 0; i < 4; ++i) {
        // pos
        float posX = faceVertices[i * 3 + 0] * voxel_size_ + worldX;
        float posY = faceVertices[i * 3 + 1] * voxel_size_ + worldY;
        float posZ = faceVertices[i * 3 + 2] * voxel_size_ + worldZ;
        vertices.push_back(posX);
        vertices.push_back(posY);
        vertices.push_back(posZ);

        // normal
        vertices.push_back(normal.x());
        vertices.push_back(normal.y());
        vertices.push_back(normal.z());

        // color
        vertices.push_back(voxel.color.redF());
        vertices.push_back(voxel.color.greenF());
        vertices.push_back(voxel.color.blueF());
        vertices.push_back(voxel.color.alphaF());
    }

    // indices
    for (GLuint idx : FACE_INDICES) {
        indices.push_back(idx + vertexOffset);
    }
}

void VoxelWidget::buildVoxelVertices() {
    // clear
    verticles_opaque_.clear();
    verticles_transparent_.clear();
    indices_opaque_.clear();
    indices_transparent_.clear();

    if (voxel_data_.empty() || start_layer_ > ender_layer_) return;

    static int dxArr[] = {0, 0, -1, 1, 0, 0};
    static int dyArr[] = {0, 0, 0, 0, 1, -1};
    static int dzArr[] = {1, -1, 0, 0, 0, 0};

    for (int layer = start_layer_; layer <= ender_layer_; ++layer) {  // Y轴（层）
        const auto& layerData = voxel_data_[layer];
        if (layerData.empty()) continue;

        for (int x = 0; x < layerData.size(); ++x) {  // X轴
            const auto& rowData = layerData[x];
            if (rowData.empty()) continue;

            for (int z = 0; z < rowData.size(); ++z) {  // Z轴
                const Voxel& voxel = rowData[z];

                if (voxel.color.alpha() == 0) {
                    continue;
                }

                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                    int dLayer = dyArr[faceIdx], dX = dxArr[faceIdx], dZ = dzArr[faceIdx];
                    if (!hasNeighbor(layer, x, z, dLayer, dX, dZ)) {
                        addFaceVertices(layer, x, z, voxel, m_faceTemplates[faceIdx], m_faceNormals[faceIdx]);
                    }
                }
            }
        }
    }
}

void VoxelWidget::generateOpenGLBuffers() {
    // for opaque
    if (vao_opaque_ == 0) glGenVertexArrays(1, &vao_opaque_);
    if (vbo_opaque_ == 0) glGenBuffers(1, &vbo_opaque_);
    if (ebo_opaque_ == 0) glGenBuffers(1, &ebo_opaque_);

    // fortransparent
    if (vao_transparent_ == 0) glGenVertexArrays(1, &vao_transparent_);
    if (vbo_transparent_ == 0) glGenBuffers(1, &vbo_transparent_);
    if (ebo_transparent_ == 0) glGenBuffers(1, &ebo_transparent_);
}

// 初始化OpenGL缓冲（VAO/VBO/EBO）
void VoxelWidget::rebufferOpenGLData() {
    // Opaque
    glBindVertexArray(vao_opaque_);
    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, vbo_opaque_);
    glBufferData(GL_ARRAY_BUFFER, verticles_opaque_.size() * sizeof(float), verticles_opaque_.data(), GL_DYNAMIC_DRAW);
    // EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_opaque_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_opaque_.size() * sizeof(GLuint), indices_opaque_.data(), GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // Transparent
    glBindVertexArray(vao_transparent_);
    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, vbo_transparent_);
    glBufferData(GL_ARRAY_BUFFER, verticles_transparent_.size() * sizeof(float), verticles_transparent_.data(), GL_DYNAMIC_DRAW);
    // EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_transparent_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_transparent_.size() * sizeof(GLuint), indices_transparent_.data(), GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // unbind
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void VoxelWidget::initializeGL() {
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

    // shader
    gl_shader_ = new QOpenGLShaderProgram(this);
    gl_shader_->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/res/shaders/voxel.vert");
    gl_shader_->addShaderFromSourceFile(QOpenGLShader::Fragment, ":res/shaders/voxel.frag");

    if (!gl_shader_->link()) {
        qDebug() << "着色器链接失败：" << gl_shader_->log();
        return;
    }
    // 初始化OpenGL缓冲
    rebufferOpenGLData();
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
#define CHECK_GL_ERROR()                                             \
    do {                                                             \
        GLenum err = glGetError();                                   \
        if (err != GL_NO_ERROR) {                                    \
            qDebug() << "OpenGL error at" << __LINE__ << ":" << err; \
        }                                                            \
    } while (0)
    // 渲染代码

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!gl_shader_ || !gl_shader_->isLinked()) return;

    gl_shader_->bind();
    CHECK_GL_ERROR();

    // 更新模型矩阵（新增平移逻辑）
    m_model.setToIdentity();
    m_model.scale(m_scale);                       // 缩放
    m_model.rotate(m_rotateX, 1.0f, 0.0f, 0.0f);  // X轴旋转
    m_model.rotate(m_rotateY, 0.0f, 1.0f, 0.0f);  // Y轴旋转
    m_model.translate(m_cameraTranslate);         // 新增：应用相机平移
    // 居中平移（原有逻辑，平移叠加在居中之后）
    if (!voxel_data_.empty() && !voxel_data_[0].empty() && !voxel_data_[0][0].empty()) {
        float cx = (voxel_data_[0].size() * voxel_size_) / 2.0f;
        float cy = (voxel_data_.size() * voxel_size_) / 2.0f;
        float cz = (voxel_data_[0][0].size() * voxel_size_) / 2.0f;
        m_model.translate(-cx, -cy, -cz);
    }

    // ========== 设置Uniform变量 ==========
    gl_shader_->setUniformValue("model", m_model);
    gl_shader_->setUniformValue("view", m_view);
    gl_shader_->setUniformValue("projection", m_projection);
    gl_shader_->setUniformValue("lightPos", m_lightPos);
    gl_shader_->setUniformValue("lightColor", m_lightColor);
    gl_shader_->setUniformValue("ambientLight", m_ambientLight);
    CHECK_GL_ERROR();

    // draw (opaque)
    if (!indices_opaque_.empty()) {
        if (vbo_opaque_ == 0) {
            qDebug() << "ERROR: VBO is 0, not created!";
            return;
        }

        // 检查 VBO 是否是有效的缓冲区对象
        if (!glIsBuffer(vbo_opaque_)) {
            qDebug() << "ERROR: VBO" << vbo_opaque_ << "is not a valid buffer object!";
            return;
        }

        glDepthMask(GL_TRUE);  // 开启深度写入
        CHECK_GL_ERROR();
        glBindVertexArray(vao_opaque_);
        CHECK_GL_ERROR();
        glBindBuffer(GL_ARRAY_BUFFER, vbo_opaque_);
        CHECK_GL_ERROR();
        glBufferSubData(GL_ARRAY_BUFFER, 0, verticles_opaque_.size() * sizeof(float), verticles_opaque_.data());
        CHECK_GL_ERROR();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_opaque_);
        CHECK_GL_ERROR();
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indices_opaque_.size() * sizeof(GLuint), indices_opaque_.data());
        CHECK_GL_ERROR();
        glDrawElements(GL_TRIANGLES, indices_opaque_.size(), GL_UNSIGNED_INT, 0);
        CHECK_GL_ERROR();
    }

    // draw (transparent)
    if (!indices_transparent_.empty()) {
        glDepthMask(GL_FALSE);
        glBindVertexArray(vao_transparent_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_transparent_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, verticles_transparent_.size() * sizeof(float), verticles_transparent_.data());
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_transparent_);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, indices_transparent_.size() * sizeof(GLuint), indices_transparent_.data());
        glDrawElements(GL_TRIANGLES, indices_transparent_.size(), GL_UNSIGNED_INT, 0);
        glDepthMask(GL_TRUE);
        CHECK_GL_ERROR();
    }

    // 解绑
    glBindVertexArray(0);
    gl_shader_->release();
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

void VoxelWidget::showEvent(QShowEvent* e) { QOpenGLWidget::showEvent(e); }

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
    if (chunks.empty() || chunks[0].empty()) {
        return {};
    }
    const int width = chunks.size();     // Chunk的X方向数量
    const int depth = chunks[0].size();  // Chunk的Z方向数量
    const int CW = 16;                   // 每个Chunk的块尺寸（16x16xN）

    // 2. 计算所有Chunk的Y轴范围（修复MIN/MAX初始化逻辑）
    int min_y = INT_MAX, max_y = INT_MIN;
    for (const auto& chunk_row : chunks) {
        for (const bl::chunk* ch : chunk_row) {
            if (!ch) continue;
            auto [miny, maxy] = ch->get_pos().get_y_range(ch->get_version());
            min_y = std::min(min_y, miny);
            max_y = std::max(max_y, maxy);
        }
    }

    if (max_y < min_y) return {};

    const int total_layer = max_y - min_y + 1;

    std::vector<std::vector<std::vector<Voxel>>> data;
    data.resize(total_layer);
    for (auto& y_layer : data) {
        y_layer.resize(width * CW);
        for (auto& x_row : y_layer) {
            x_row.resize(depth * CW);
            for (auto& voxel : x_row) {
                voxel.transparent = true;
                voxel.color = QColor(255, 255, 255, 0);
            }
        }
    }
    qDebug() << "Voxel Size: " << QString("%1 (Y) * %2 (X) * %3 (Z)").arg(data.size()).arg(data[0].size()).arg(data[0][0].size());

    for (int xIdx = 0; xIdx < width; ++xIdx) {
        if (xIdx >= chunks.size()) continue;
        const auto& row = chunks[xIdx];
        for (int zIdx = 0; zIdx < depth; ++zIdx) {
            if (zIdx >= row.size()) continue;
            auto* ch = row[zIdx];
            if (!ch) continue;
            auto [min_y, max_y] = ch->get_pos().get_y_range(ch->get_version());
            for (int y = min_y; y <= max_y; ++y) {
                const int global_y_idx = y - min_y;
                if (global_y_idx < 0 || global_y_idx >= total_layer) continue;

                for (int x = 0; x < CW; ++x) {
                    const int global_x_idx = xIdx * CW + x;
                    if (global_x_idx < 0 || global_x_idx >= data[global_y_idx].size()) continue;

                    for (int z = 0; z < CW; ++z) {
                        const int global_z_idx = zIdx * CW + z;
                        if (global_z_idx < 0 || global_z_idx >= data[global_y_idx][global_x_idx].size()) continue;

                        Voxel& voxel = data[global_y_idx][global_x_idx][global_z_idx];
                        auto block_info = ch->get_block(x, y, z);
                        auto biome = ch->get_biome(x, y, z);
                        if (block_info.name == "minecraft:unknown" || block_info.name == "minecraft:air") continue;
                        auto blend_color = bl::blend_color_with_biome(block_info.name, block_info.color, biome);
                        voxel.color = QColor(blend_color.r, blend_color.g, blend_color.b, blend_color.a);
                        voxel.transparent = voxel.color.alpha() < 255;
                    }
                }
            }
        }
    }
    // remove all air layers
    auto layerCheck = [](const auto& layer) {
        return std::any_of(layer.cbegin(), layer.cend(), [](const auto& row) {
            return std::any_of(row.cbegin(), row.cend(), [](const auto& voxel) { return voxel.color.alpha() != 0; });
        });
    };

    auto firstIt = std::find_if(data.cbegin(), data.cend(), layerCheck);

    auto lastIt = std::find_if(data.crbegin(), data.crend(), layerCheck).base();

    if (firstIt == data.cend()) {
        return {};
    }

    std::vector<std::vector<std::vector<Voxel>>> ret(firstIt, lastIt - 1);
    qDebug() << "Real Voxel Size: " << QString("%1 (Y) * %2 (X) * %3 (Z)").arg(ret.size()).arg(ret[0].size()).arg(ret[0][0].size());
    return ret;
}