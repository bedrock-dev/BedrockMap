#include "voxelwidget.h"

#include <qchar.h>
#include <qcolor.h>
#include <qdebug.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qopenglshaderprogram.h>
#include <qtconcurrentrun.h>
#include <qthread.h>
#include <qurl.h>

#include <QDebug>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtConcurrent>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <vector>

#include "voxelwidget.h"

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
    updateVoxelData({});
}

VoxelWidget::~VoxelWidget() {
    makeCurrent();
    GLuint vaos[] = {vao_opaque_, vao_transparent_};
    GLuint vbos[] = {vbo_opaque_, vbo_transparent_};
    GLuint ebos[] = {ebo_opaque_, ebo_transparent_};
    if (vao_opaque_ != 0 || vao_transparent_ != 0) {
        qDebug() << "Delete VAOs";
        glDeleteVertexArrays(2, vaos);
    }
    if (vbo_opaque_ != 0 || vbo_transparent_ != 0) {
        qDebug() << "Delete VBOs";
        glDeleteBuffers(2, vbos);
    }
    if (ebo_opaque_ != 0 || ebo_transparent_ != 0) {
        qDebug() << "Delete EBOs";
        glDeleteBuffers(2, ebos);
    }

    doneCurrent();
    if (gl_shader_) {
        delete gl_shader_;
        gl_shader_ = nullptr;
    }
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

void VoxelWidget::updateVoxelData(const std::vector<std::vector<std::vector<Voxel>>>& newData) {
    voxel_data_ = newData;
    if (!voxel_data_.empty()) {
        start_layer_ = 0;
        ender_layer_ = voxel_data_.size() - 1;
    }
    buildVoxelVertices();
    makeCurrent();
    updateOpenGLBuffers();
    doneCurrent();

    // change scale
    if (!newData.empty() && !newData.begin()->empty()) {
        auto sz1 = newData.size();
        auto sz2 = newData.begin()->size();
        auto sz = ::sqrt(sz1 * sz1 + sz2 * sz2);
        m_scale = 24. / sz;
    }

    update();
}

void VoxelWidget::generateOpenGLBuffers() {
    if (vao_opaque_ == 0) {
        glGenVertexArrays(1, &vao_opaque_);
        glGenBuffers(1, &vbo_opaque_);
        glGenBuffers(1, &ebo_opaque_);
    }

    if (vao_transparent_ == 0) {
        glGenVertexArrays(1, &vao_transparent_);
        glGenBuffers(1, &vbo_transparent_);
        glGenBuffers(1, &ebo_transparent_);
    }

    setupVertexAttributes();
}

void VoxelWidget::setupVertexAttributes() {
    // 设置opaque对象的顶点属性
    glBindVertexArray(vao_opaque_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_opaque_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_opaque_);

    // 顶点位置
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // 法线
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // 颜色
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // 设置transparent对象的顶点属性
    glBindVertexArray(vao_transparent_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_transparent_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_transparent_);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // 解绑VAO
    glBindVertexArray(0);
}

void VoxelWidget::updateOpenGLBuffers() {
    if (vao_opaque_ == 0 && vao_transparent_ == 0) {
        qDebug() << "OpenGL buffers not generated yet!";
        return;
    }
    // 更新opaque对象的缓冲数据
    if (vao_opaque_ != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_opaque_);
        glBufferData(GL_ARRAY_BUFFER, verticles_opaque_.size() * sizeof(float), verticles_opaque_.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_opaque_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_opaque_.size() * sizeof(GLuint), indices_opaque_.data(), GL_DYNAMIC_DRAW);
    }

    // 更新transparent对象的缓冲数据
    if (vao_transparent_ != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_transparent_);
        glBufferData(GL_ARRAY_BUFFER, verticles_transparent_.size() * sizeof(float), verticles_transparent_.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_transparent_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_transparent_.size() * sizeof(GLuint), indices_transparent_.data(), GL_DYNAMIC_DRAW);
    }

    // 解绑缓冲
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void VoxelWidget::initializeGL() {
    initializeOpenGLFunctions();
    // 设置OpenGL状态
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // 编译着色器
    gl_shader_ = new QOpenGLShaderProgram(this);
    gl_shader_->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/res/shaders/voxel.vert");
    gl_shader_->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/res/shaders/voxel.frag");

    if (!gl_shader_->link()) {
        qDebug() << "Can not link OpenGL Shader：" << gl_shader_->log();
        return;
    }

    generateOpenGLBuffers();
    updateOpenGLBuffers();
}

void VoxelWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!gl_shader_ || !gl_shader_->isLinked()) return;
    gl_shader_->bind();
    updateModelMatrix();
    setUniforms();
    renderOpaqueObjects();
    renderTransparentObjects();

    gl_shader_->release();
}

void VoxelWidget::updateModelMatrix() {
    m_model.setToIdentity();
    m_model.scale(m_scale);
    m_model.rotate(rotate_x_, 1.0f, 0.0f, 0.0f);
    m_model.rotate(rotate_y_, 0.0f, 1.0f, 0.0f);
    m_model.translate(m_cameraTranslate);

    // 居中平移
    if (!voxel_data_.empty() && !voxel_data_[0].empty() && !voxel_data_[0][0].empty()) {
        float cx = (voxel_data_[0].size() * voxel_size_) / 2.0f;
        float cy = (voxel_data_.size() * voxel_size_) / 2.0f;
        float cz = (voxel_data_[0][0].size() * voxel_size_) / 2.0f;
        m_model.translate(-cx, -cy, -cz);
    }
}

void VoxelWidget::setUniforms() {
    gl_shader_->setUniformValue("model", m_model);
    gl_shader_->setUniformValue("view", m_view);
    gl_shader_->setUniformValue("projection", m_projection);
    gl_shader_->setUniformValue("lightPos", m_lightPos);
    gl_shader_->setUniformValue("lightColor", m_lightColor);
    gl_shader_->setUniformValue("ambientLight", m_ambientLight);
}

void VoxelWidget::renderOpaqueObjects() {
    if (indices_opaque_.empty()) return;
    glBindVertexArray(vao_opaque_);
    glDepthMask(GL_TRUE);
    glDrawElements(GL_TRIANGLES, indices_opaque_.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void VoxelWidget::renderTransparentObjects() {
    if (indices_transparent_.empty()) return;
    glBindVertexArray(vao_transparent_);
    glDepthMask(GL_FALSE);
    glDrawElements(GL_TRIANGLES, indices_transparent_.size(), GL_UNSIGNED_INT, 0);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

// 添加一个辅助函数用于调试
void VoxelWidget::checkOpenGLError(const char* location) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qDebug() << "OpenGL error at" << location << ":" << err;
    }
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

// Mesh Building
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

// Camera & Controlling
void VoxelWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_lastMousePos = e->pos();
    } else if (e->button() == Qt::RightButton) {
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

void VoxelWidget::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton && !m_isPanDragging) {
        int dx = e->x() - m_lastMousePos.x();
        int dy = e->y() - m_lastMousePos.y();
        rotate_y_ += dx * 0.5f;
        rotate_x_ += dy * 0.5f;
        rotate_x_ = std::clamp(rotate_x_, -90.0f, 90.0f);
        m_lastMousePos = e->pos();
        update();
        return;
    }

    if ((e->buttons() & Qt::RightButton) && m_isPanDragging) {
        int deltaX = e->x() - m_panStartPos.x();
        int deltaY = e->y() - m_panStartPos.y();

        float sensitivity = m_panSensitivity / m_scale;

        if (!m_isShiftPressed) {
            m_cameraTranslate.setX(m_cameraTranslate.x() + deltaX * sensitivity);
            m_cameraTranslate.setY(m_cameraTranslate.y() - deltaY * sensitivity);
        } else {
            m_cameraTranslate.setZ(m_cameraTranslate.z() + deltaX * sensitivity);
            m_cameraTranslate.setY(m_cameraTranslate.y() - deltaY * sensitivity);
        }

        m_panStartPos = e->pos();
        update();
    }
}

void VoxelWidget::wheelEvent(QWheelEvent* e) {
    int delta = e->angleDelta().y();
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
        rotate_x_ = 0.0f;
        rotate_y_ = 0.0f;
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

// helper
std::vector<std::vector<std::vector<Voxel>>> VoxelWidget::createVoxelDataFromChunks(const std::vector<std::vector<bl::chunk*>>& chunks,
                                                                                    const std::function<void(int)>& f) {
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

    int n = 0;
    for (int xIdx = 0; xIdx < width; ++xIdx) {
        if (xIdx >= chunks.size()) continue;
        const auto& row = chunks[xIdx];

        for (int zIdx = 0; zIdx < depth; ++zIdx) {
            if (zIdx >= row.size()) continue;
            auto* ch = row[zIdx];
            if (!ch) continue;

            auto [min_y, max_y] = ch->get_pos().get_y_range(ch->get_version());
            int y_range = max_y - min_y + 1;

            // 预计算全局索引的基础偏移
            int base_global_x = xIdx * CW;
            int base_global_z = zIdx * CW;

            // 预检查y范围是否有效
            if (min_y < 0 || max_y >= total_layer) {
                // 调整有效的y范围
                min_y = std::max(min_y, 0);
                max_y = std::min(max_y, total_layer - 1);
                if (min_y > max_y) continue;
            }

            for (int x = 0; x < CW; ++x) {
                int global_x_idx = base_global_x + x;
                if (global_x_idx < 0 || global_x_idx >= data[0].size()) continue;

                for (int z = 0; z < CW; ++z) {
                    int global_z_idx = base_global_z + z;
                    if (global_z_idx < 0 || global_z_idx >= data[0][0].size()) continue;

                    // 内层遍历y（垂直方向）
                    for (int y = min_y; y <= max_y; ++y) {
                        int global_y_idx = y;
                        if (global_y_idx < 0 || global_y_idx >= total_layer) continue;

                        // 检查边界
                        if (global_y_idx >= data.size() || global_x_idx >= data[global_y_idx].size() ||
                            global_z_idx >= data[global_y_idx][global_x_idx].size()) {
                            continue;
                        }

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

            n++;
            f(n);
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

bool ChunkRenderWidget::showChunks(const bl::chunk_pos& minPos, const bl::chunk_pos& maxPos, AsyncLevelLoader& loader) {
    if (!chunk_render_watcher_.isFinished()) {
        qDebug() << "Current render task is not finished";
        return false;
    }

    if (maxPos.x < minPos.x || maxPos.z < minPos.z) {
        qDebug() << "Invald Chunk Area";
        return false;
    }

    bar_->show();
    bar_->setValue(0);
    bar_->setMaximum((maxPos.x - minPos.x + 1) * (maxPos.z - minPos.z + 1) * 2);

    chunks_.clear();
    voxels_.clear();
    voxelWidget_->updateVoxelData({});
    chunk_render_watcher_.setFuture(QtConcurrent::run([this, minPos, maxPos, &loader]() {
        // load chunk in another thread
        this->chunks_.clear();
        chunks_.resize(maxPos.x - minPos.x + 1);
        for (auto& row : chunks_) {
            row.resize(maxPos.z - minPos.z + 1);
        }
        size_t chunk_loaded = 0;
        auto dim = minPos.dim;
        for (int i = minPos.x; i <= maxPos.x; i++) {
            for (int j = minPos.z; j <= maxPos.z; j++) {
                auto* chunk = loader.getChunkDirect(bl::chunk_pos{i, j, dim});
                this->chunks_[i - minPos.x][j - minPos.z] = chunk;
                chunk_loaded++;
                emit chunkMeshBuilt(chunk_loaded);
            }
        }
        voxels_ = VoxelWidget::createVoxelDataFromChunks(this->chunks_, [&](int cnt) { emit chunkMeshBuilt(cnt + chunk_loaded); });
        return true;
    }));
    show();
    return true;
}