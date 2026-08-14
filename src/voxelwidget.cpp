#include "voxelwidget.h"

#include <qchar.h>
#include <qcolor.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qopenglshaderprogram.h>
#include <qtconcurrentrun.h>
#include <qthread.h>
#include <qurl.h>

#include <QMouseEvent>
#include <QWheelEvent>
#include <QtConcurrent>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <vector>

#include "loguru/loguru.hpp"
#include "voxelwidget.h"

// index template for each face (2 triangles, 6 indices)
const std::vector<GLuint> FACE_INDICES = {0, 1, 2, 0, 2, 3};
// initialize the face template outside the class (1D vector, 12 coordinate values per face)
const std::vector<std::vector<float>> VoxelWidget::m_faceTemplates = {
    {-0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f},      // +Z front
    {-0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f},  // -Z back
    {-0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f},  // -X left
    {0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f},      // +X right
    {-0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f},      // +Y top
    {-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f}   // -Y bottom
};

// normal directions of the 6 faces
const std::vector<QVector3D> VoxelWidget::m_faceNormals = {QVector3D(0.0f, 0.0f, 1.0f),  QVector3D(0.0f, 0.0f, -1.0f),
                                                           QVector3D(-1.0f, 0.0f, 0.0f), QVector3D(1.0f, 0.0f, 0.0f),
                                                           QVector3D(0.0f, 1.0f, 0.0f),  QVector3D(0.0f, -1.0f, 0.0f)};

VoxelWidget::VoxelWidget(QWidget* parent) : QOpenGLWidget(parent) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(8);  // anti-aliasing
    setFormat(format);
    // NOTE: do not touch the GL context here. Calling makeCurrent() before the widget
    // is shown forces native window creation; on Windows, when the widget is embedded
    // in an already-visible window this recreates the top-level window (visible as a
    // close/reopen flicker). Vertex data is uploaded lazily from initializeGL().
}

VoxelWidget::~VoxelWidget() {
    if (!context() || !context()->isValid()) {
        // never shown / GL context never created: nothing to clean up
        return;
    }
    makeCurrent();
    GLuint vaos[] = {vao_opaque_, vao_transparent_};
    GLuint vbos[] = {vbo_opaque_, vbo_transparent_};
    GLuint ebos[] = {ebo_opaque_, ebo_transparent_};
    if (vao_opaque_ != 0 || vao_transparent_ != 0) {
        LOG_F(INFO, "Delete VAOs");
        glDeleteVertexArrays(2, vaos);
    }
    if (vbo_opaque_ != 0 || vbo_transparent_ != 0) {
        LOG_F(INFO, "Delete VBOs");
        glDeleteBuffers(2, vbos);
    }
    if (ebo_opaque_ != 0 || ebo_transparent_ != 0) {
        LOG_F(INFO, "Delete EBOs");
        glDeleteBuffers(2, ebos);
    }

    doneCurrent();
    if (gl_shader_) {
        delete gl_shader_;
        gl_shader_ = nullptr;
    }
}

// set render layer range
void VoxelWidget::setLayer(int startLayer, int endLayer) {
    if (voxel_data_.empty()) return;
    // bounds check
    start_layer_ = std::max(0, startLayer);
    ender_layer_ = std::min(static_cast<int>(voxel_data_.size()) - 1, endLayer);
    start_layer_ = std::min(start_layer_, ender_layer_);
    // rebuild vertices and refresh
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
    // Upload only after initializeGL() has created the GL objects; if this is called
    // before the widget is shown, initializeGL() picks up the CPU-side buffers later.
    if (gl_initialized_) {
        makeCurrent();
        updateOpenGLBuffers();
        doneCurrent();
    }

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
    // set vertex attributes for opaque objects
    glBindVertexArray(vao_opaque_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_opaque_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_opaque_);

    // vertex position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // color
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // set vertex attributes for transparent objects
    glBindVertexArray(vao_transparent_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_transparent_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_transparent_);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 10 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // unbind VAO
    glBindVertexArray(0);
}

void VoxelWidget::updateOpenGLBuffers() {
    if (vao_opaque_ == 0 && vao_transparent_ == 0) {
        LOG_F(WARNING, "OpenGL buffers not generated yet!");
        return;
    }
    // update buffer data for opaque objects
    if (vao_opaque_ != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_opaque_);
        glBufferData(GL_ARRAY_BUFFER, verticles_opaque_.size() * sizeof(float), verticles_opaque_.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_opaque_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_opaque_.size() * sizeof(GLuint), indices_opaque_.data(), GL_DYNAMIC_DRAW);
    }

    // update buffer data for transparent objects
    if (vao_transparent_ != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_transparent_);
        glBufferData(GL_ARRAY_BUFFER, verticles_transparent_.size() * sizeof(float), verticles_transparent_.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_transparent_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_transparent_.size() * sizeof(GLuint), indices_transparent_.data(), GL_DYNAMIC_DRAW);
    }

    // unbind buffers
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void VoxelWidget::initializeGL() {
    initializeOpenGLFunctions();
    // set OpenGL state
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // compile shaders
    gl_shader_ = new QOpenGLShaderProgram(this);
    gl_shader_->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/res/shaders/voxel.vert");
    gl_shader_->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/res/shaders/voxel.frag");

    if (!gl_shader_->link()) {
        LOG_F(ERROR, "Can not link OpenGL Shader: %s", gl_shader_->log().toStdString().c_str());
        return;
    }

    generateOpenGLBuffers();
    gl_initialized_ = true;
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

    // center translate
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

// add a helper function for debugging
void VoxelWidget::checkOpenGLError(const char* location) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_F(WARNING, "OpenGL error at %s: %d", location, err);
    }
}

void VoxelWidget::resizeGL(int w, int h) {
    // set viewport
    glViewport(0, 0, w, h);
    // projection matrix (perspective projection)
    m_projection.setToIdentity();
    m_projection.perspective(45.0f, (float)w / h, 0.1f, 1000.0f);

    // view matrix (camera position)
    m_view.setToIdentity();
    m_view.lookAt(QVector3D(0.0f, 0.0f, 50.0f),  // camera position
                  QVector3D(0.0f, 0.0f, 0.0f),   // look target
                  QVector3D(0.0f, 1.0f, 0.0f));  // up direction
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

    bool isOpaque = !voxel.transparent;  // transparent=false -> opaque layer
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

    for (int layer = start_layer_; layer <= ender_layer_; ++layer) {  // Y axis (layer)
        const auto& layerData = voxel_data_[layer];
        if (layerData.empty()) continue;

        for (int x = 0; x < layerData.size(); ++x) {  // X axis
            const auto& rowData = layerData[x];
            if (rowData.empty()) continue;

            for (int z = 0; z < rowData.size(); ++z) {  // Z axis
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
        int dx = e->pos().x() - m_lastMousePos.x();
        int dy = e->pos().y() - m_lastMousePos.y();
        rotate_y_ += dx * 0.5f;
        rotate_x_ += dy * 0.5f;
        rotate_x_ = std::clamp(rotate_x_, -90.0f, 90.0f);
        m_lastMousePos = e->pos();
        update();
        return;
    }

    if ((e->buttons() & Qt::RightButton) && m_isPanDragging) {
        int deltaX = e->pos().x() - m_panStartPos.x();
        int deltaY = e->pos().y() - m_panStartPos.y();

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
    update();  // refresh render
}

void VoxelWidget::showEvent(QShowEvent* e) { QOpenGLWidget::showEvent(e); }

void VoxelWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Shift) {
        m_isShiftPressed = true;
    } else if (e->key() == Qt::Key_R) {
        rotate_x_ = 0.0f;
        rotate_y_ = 0.0f;
        m_scale = 1.0f;
        m_cameraTranslate = QVector3D(0.0f, 0.0f, 0.0f);  // reset pan
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
    const int width = chunks.size();     // number of chunks along X
    const int depth = chunks[0].size();  // number of chunks along Z
    const int CW = 16;                   // block size of each chunk (16x16xN)

    // 2. compute the Y range of all chunks (fixes the MIN/MAX init logic)
    int min_y = INT_MAX, max_y = INT_MIN;
    for (const auto& chunk_row : chunks) {
        for (const auto& ch : chunk_row) {
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
    LOG_F(INFO, "Voxel Size: %zu (Y) * %zu (X) * %zu (Z)", data.size(), data[0].size(), data[0][0].size());

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

            // precompute the base offset of global indices
            int base_global_x = xIdx * CW;
            int base_global_z = zIdx * CW;

            // pre-check whether the y range is valid
            if (min_y < 0 || max_y >= total_layer) {
                // adjust the valid y range
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

                    // inner loop over y (vertical direction)
                    for (int y = min_y; y <= max_y; ++y) {
                        int global_y_idx = y;
                        if (global_y_idx < 0 || global_y_idx >= total_layer) continue;

                        // bounds check
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
    LOG_F(INFO, "Real Voxel Size: %zu (Y) * %zu (X) * %zu (Z)", ret.size(), ret[0].size(), ret[0][0].size());
    return ret;
}

bool ChunkRenderWidget::showChunks(const bl::chunk_pos& minPos, const bl::chunk_pos& maxPos, AsyncLevelLoader& loader) {
    setWindowTitle(QString("%1 ~ %2").arg(minPos.to_string().c_str()).arg(maxPos.to_string().c_str()));
    if (!chunk_render_watcher_.isFinished()) {
        LOG_F(WARNING, "Current render task is not finished");
        return false;
    }

    if (maxPos.x < minPos.x || maxPos.z < minPos.z) {
        LOG_F(WARNING, "Invald Chunk Area");
        return false;
    }

    bar_->show();
    bar_->setValue(0);
    bar_->setMaximum((maxPos.x - minPos.x + 1) * (maxPos.z - minPos.z + 1) * 2);

    for (auto& row : chunks_)
        for (auto* c : row) delete c;
    chunks_.clear();
    voxels_.clear();
    voxelWidget_->updateVoxelData({});
    chunk_render_watcher_.setFuture(QtConcurrent::run([this, minPos, maxPos, &loader]() {
        // load chunk in another thread
        for (auto& row : this->chunks_)
            for (auto* c : row) delete c;
        this->chunks_.clear();
        chunks_.resize(maxPos.x - minPos.x + 1);
        for (auto& row : chunks_) {
            row.resize(maxPos.z - minPos.z + 1);
        }
        size_t chunk_loaded = 0;
        auto dim = minPos.dim;
        for (int i = minPos.x; i <= maxPos.x; i++) {
            for (int j = minPos.z; j <= maxPos.z; j++) {
                this->chunks_[i - minPos.x][j - minPos.z] = loader.getChunk(bl::chunk_pos{i, j, dim});
                chunk_loaded++;
                emit chunkMeshBuilt(chunk_loaded);
            }
        }
        voxels_ = VoxelWidget::createVoxelDataFromChunks(this->chunks_, [&](int cnt) { emit chunkMeshBuilt(cnt + chunk_loaded); });
        for (auto& row : this->chunks_)
            for (auto* c : row) delete c;
        this->chunks_.clear();
        return true;
    }));
    show();
    return true;
}
