#include "voxelwidget.h"

#include <qchar.h>
#include <qcolor.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qopenglshaderprogram.h>
#include <qtconcurrentrun.h>
#include <qthread.h>
#include <qurl.h>

#include <QtConcurrent>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "loguru/loguru.hpp"
#include "voxelwidget.h"

// index template for each face (2 triangles, 6 indices)
const std::vector<GLuint> FACE_INDICES = {0, 1, 2, 0, 2, 3};
// half height of the visible view at z=0: tan(fov/2) * camera distance (50)
constexpr float kViewHalfHeight = 20.710678f;

void appendColoredVertex(std::vector<float>& vertices, const QVector3D& position, const QColor& color) {
    vertices.push_back(position.x());
    vertices.push_back(position.y());
    vertices.push_back(position.z());
    vertices.push_back(color.redF());
    vertices.push_back(color.greenF());
    vertices.push_back(color.blueF());
    vertices.push_back(color.alphaF());
}

void appendColoredQuad(std::vector<float>& vertices, const QVector3D& p0, const QVector3D& p1, const QVector3D& p2, const QVector3D& p3,
                       const QColor& color) {
    appendColoredVertex(vertices, p0, color);
    appendColoredVertex(vertices, p1, color);
    appendColoredVertex(vertices, p2, color);
    appendColoredVertex(vertices, p0, color);
    appendColoredVertex(vertices, p2, color);
    appendColoredVertex(vertices, p3, color);
}

void appendColoredBox(std::vector<float>& vertices, const QVector3D& center, float halfSize, const QColor& color) {
    const QVector3D h(halfSize, halfSize, halfSize);
    const QVector3D p[8] = {
        center + QVector3D(-h.x(), -h.y(), -h.z()), center + QVector3D(h.x(), -h.y(), -h.z()), center + QVector3D(h.x(), h.y(), -h.z()),
        center + QVector3D(-h.x(), h.y(), -h.z()),  center + QVector3D(-h.x(), -h.y(), h.z()), center + QVector3D(h.x(), -h.y(), h.z()),
        center + QVector3D(h.x(), h.y(), h.z()),    center + QVector3D(-h.x(), h.y(), h.z()),
    };
    const int faces[6][4] = {
        {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {3, 7, 6, 2}, {0, 1, 5, 4},
    };
    for (const auto& face : faces) {
        appendColoredQuad(vertices, p[face[0]], p[face[1]], p[face[2]], p[face[3]], color);
    }
}

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
    setFocusPolicy(Qt::StrongFocus);  // needed so R / O / arrow keys reach this widget
    // NOTE: do not touch the GL context here. Calling makeCurrent() before the widget
    // is shown forces native window creation; on Windows, when the widget is embedded
    // in an already-visible window this recreates the top-level window (visible as a
    // close/reopen flicker). Vertex data is uploaded lazily from initializeGL().
    setupShortcutHelpButton();
}

VoxelWidget::~VoxelWidget() {
    if (!context() || !context()->isValid()) {
        // never shown / GL context never created: nothing to clean up
        return;
    }
    makeCurrent();
    GLuint vaos[] = {vao_opaque_, vao_transparent_, vao_axes_, vao_selection_};
    GLuint vbos[] = {vbo_opaque_, vbo_transparent_, vbo_axes_, vbo_selection_};
    GLuint ebos[] = {ebo_opaque_, ebo_transparent_};
    if (vao_opaque_ != 0 || vao_transparent_ != 0 || vao_axes_ != 0 || vao_selection_ != 0) {
        LOG_F(INFO, "Delete VAOs");
        glDeleteVertexArrays(4, vaos);
    }
    if (vbo_opaque_ != 0 || vbo_transparent_ != 0 || vbo_axes_ != 0 || vbo_selection_ != 0) {
        LOG_F(INFO, "Delete VBOs");
        glDeleteBuffers(4, vbos);
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
    if (axis_shader_) {
        delete axis_shader_;
        axis_shader_ = nullptr;
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
    buildAxisVertices();
    resetSelectionToModelBounds();
    if (!newData.empty() && !newData.begin()->empty()) {
        auto sz1 = newData.size();
        auto sz2 = newData.begin()->size();
        auto sz = ::sqrt(sz1 * sz1 + sz2 * sz2);
        m_scale = 24. / sz;
    }

    buildSelectionVertices();
    // Upload only after initializeGL() has created the GL objects; if this is called
    // before the widget is shown, initializeGL() picks up the CPU-side buffers later.
    if (gl_initialized_) {
        makeCurrent();
        updateOpenGLBuffers();
        doneCurrent();
    }

    update();
}

void VoxelWidget::updateVoxelData(std::vector<std::vector<std::vector<Voxel>>>&& newData) {
    voxel_data_ = std::move(newData);
    if (!voxel_data_.empty()) {
        start_layer_ = 0;
        ender_layer_ = voxel_data_.size() - 1;
    }
    buildVoxelVertices();
    buildAxisVertices();
    resetSelectionToModelBounds();
    if (!voxel_data_.empty() && !voxel_data_.begin()->empty()) {
        auto sz1 = voxel_data_.size();
        auto sz2 = voxel_data_.begin()->size();
        auto sz = ::sqrt(sz1 * sz1 + sz2 * sz2);
        m_scale = 24. / sz;
    }

    buildSelectionVertices();
    if (gl_initialized_) {
        makeCurrent();
        updateOpenGLBuffers();
        doneCurrent();
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

    if (vao_axes_ == 0) {
        glGenVertexArrays(1, &vao_axes_);
        glGenBuffers(1, &vbo_axes_);
    }

    if (vao_selection_ == 0) {
        glGenVertexArrays(1, &vao_selection_);
        glGenBuffers(1, &vbo_selection_);
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

    // axis lines: pos (location 0) + color (location 1), stride = 7 floats
    glBindVertexArray(vao_axes_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_axes_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // selection overlay: pos (location 0) + color (location 1), stride = 7 floats
    glBindVertexArray(vao_selection_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_selection_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

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

    // axis lines
    if (vao_axes_ != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_axes_);
        glBufferData(GL_ARRAY_BUFFER, axes_vertices_.size() * sizeof(float), axes_vertices_.data(), GL_DYNAMIC_DRAW);
    }

    updateSelectionOpenGLBuffer();

    // unbind buffers
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void VoxelWidget::updateSelectionOpenGLBuffer() {
    if (vbo_selection_ == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_selection_);
    glBufferData(GL_ARRAY_BUFFER, selection_vertices_.size() * sizeof(float), selection_vertices_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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

    axis_shader_ = new QOpenGLShaderProgram(this);
    axis_shader_->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/res/shaders/axis.vert");
    axis_shader_->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/res/shaders/axis.frag");
    if (!axis_shader_->link()) {
        LOG_F(ERROR, "Can not link axis OpenGL Shader: %s", axis_shader_->log().toStdString().c_str());
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
    buildSelectionVertices();
    updateSelectionOpenGLBuffer();
    setUniforms();
    renderOpaqueObjects();
    renderTransparentObjects();

    if (selection_enabled_ && selection_.isValid() && axis_shader_ && axis_shader_->isLinked() && !selection_vertices_.empty()) {
        axis_shader_->bind();
        axis_shader_->setUniformValue("model", m_model);
        axis_shader_->setUniformValue("view", m_view);
        axis_shader_->setUniformValue("projection", m_projection);

        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glBindVertexArray(vao_selection_);
        glDrawArrays(GL_TRIANGLES, 0, selection_fill_vertex_count_);
        glDepthFunc(GL_LESS);
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.5f);
        glDrawArrays(GL_LINES, selection_fill_vertex_count_, selection_line_vertex_count_);
        glLineWidth(1.0f);
        const GLsizei handleOffset = selection_fill_vertex_count_ + selection_line_vertex_count_;
        const GLsizei totalVertexCount = static_cast<GLsizei>(selection_vertices_.size() / 7);
        glDrawArrays(GL_TRIANGLES, handleOffset, totalVertexCount - handleOffset);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        axis_shader_->release();
    }

    // coordinate axes overlay (A key)
    if (axes_visible_ && axis_shader_ && axis_shader_->isLinked() && !axes_vertices_.empty()) {
        axis_shader_->bind();
        axis_shader_->setUniformValue("model", m_model);
        axis_shader_->setUniformValue("view", m_view);
        axis_shader_->setUniformValue("projection", m_projection);
        // gizmo style: always visible, drawn on top of the model
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glBindVertexArray(vao_axes_);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(axes_vertices_.size() / 7));
        glBindVertexArray(0);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        axis_shader_->release();
    }

    gl_shader_->release();
}

void VoxelWidget::buildAxisVertices() {
    axes_vertices_.clear();
    if (voxel_data_.empty() || voxel_data_[0].empty() || voxel_data_[0][0].empty()) return;

    // the voxel mesh now spans [0, size] in each axis: start the axes at the
    // origin and extend to the model's maximum corner
    const float vs = voxel_size_;
    const float maxX = static_cast<float>(voxel_data_[0].size()) * vs;
    const float maxY = static_cast<float>(voxel_data_.size()) * vs;
    const float maxZ = static_cast<float>(voxel_data_[0][0].size()) * vs;
    const float width = std::max(0.08f * vs, 0.05f);  // axis thickness in world units

    auto pushBox = [this](const QVector3D& a, const QVector3D& b, float w, const QColor& c) {
        const QVector3D dir = b - a;
        QVector3D t1 = QVector3D::crossProduct(dir, QVector3D(0.0f, 1.0f, 0.0f));
        if (t1.lengthSquared() < 1e-8f) t1 = QVector3D::crossProduct(dir, QVector3D(0.0f, 0.0f, 1.0f));
        t1.normalize();
        const QVector3D t2 = QVector3D::crossProduct(dir, t1).normalized();
        const QVector3D h1 = t1 * (w * 0.5f);
        const QVector3D h2 = t2 * (w * 0.5f);

        const QVector3D p[8] = {
            a - h1 - h2, a + h1 - h2, a + h1 + h2, a - h1 + h2, b - h1 - h2, b + h1 - h2, b + h1 + h2, b - h1 + h2,
        };
        const int faces[6][4] = {
            {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0},
        };
        auto vertex = [this, &c](const QVector3D& v) {
            axes_vertices_.push_back(v.x());
            axes_vertices_.push_back(v.y());
            axes_vertices_.push_back(v.z());
            axes_vertices_.push_back(c.redF());
            axes_vertices_.push_back(c.greenF());
            axes_vertices_.push_back(c.blueF());
            axes_vertices_.push_back(c.alphaF());
        };
        for (const auto& f : faces) {
            vertex(p[f[0]]);
            vertex(p[f[1]]);
            vertex(p[f[2]]);
            vertex(p[f[0]]);
            vertex(p[f[2]]);
            vertex(p[f[3]]);
        }
    };

    const QVector3D origin(0.0f, 0.0f, 0.0f);
    pushBox(origin, QVector3D(maxX, 0.0f, 0.0f), width, QColor(255, 60, 60));  // X: red
    pushBox(origin, QVector3D(0.0f, maxY, 0.0f), width, QColor(60, 255, 60));  // Y: green
    pushBox(origin, QVector3D(0.0f, 0.0f, maxZ), width, QColor(60, 60, 255));  // Z: blue
}

void VoxelWidget::resetSelectionToModelBounds() {
    active_selection_handle_ = SelectionHandle::None;
    selection_ = {};
    if (voxel_data_.empty() || voxel_data_[0].empty() || voxel_data_[0][0].empty()) return;

    selection_.minimum = QVector3D(0.0f, 0.0f, 0.0f);
    selection_.maximum = QVector3D(static_cast<float>(voxel_data_[0].size()), static_cast<float>(voxel_data_.size()),
                                   static_cast<float>(voxel_data_[0][0].size()));
}

void VoxelWidget::buildSelectionVertices() {
    selection_vertices_.clear();
    selection_fill_vertex_count_ = 0;
    selection_line_vertex_count_ = 0;
    if (!selection_enabled_ || !selection_.isValid()) return;

    const QVector3D minimum = selection_.minimum * voxel_size_;
    const QVector3D maximum = selection_.maximum * voxel_size_;
    const QVector3D p[8] = {
        {minimum.x(), minimum.y(), minimum.z()}, {maximum.x(), minimum.y(), minimum.z()}, {maximum.x(), maximum.y(), minimum.z()},
        {minimum.x(), maximum.y(), minimum.z()}, {minimum.x(), minimum.y(), maximum.z()}, {maximum.x(), minimum.y(), maximum.z()},
        {maximum.x(), maximum.y(), maximum.z()}, {minimum.x(), maximum.y(), maximum.z()},
    };
    const QVector3D center = (minimum + maximum) * 0.5f;
    const float fillOutset = std::max(0.015f * voxel_size_, 0.01f);
    QVector3D fillPoints[8];
    for (int i = 0; i < 8; ++i) {
        QVector3D direction = p[i] - center;
        if (direction.lengthSquared() > 1e-8f) {
            direction.normalize();
            fillPoints[i] = p[i] + direction * fillOutset;
        } else {
            fillPoints[i] = p[i];
        }
    }

    const QColor fillColor(18, 105, 140, 82);
    const int faces[6][4] = {
        {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {3, 7, 6, 2}, {0, 1, 5, 4},
    };
    for (const auto& face : faces) {
        appendColoredQuad(selection_vertices_, fillPoints[face[0]], fillPoints[face[1]], fillPoints[face[2]], fillPoints[face[3]],
                          fillColor);
    }
    selection_fill_vertex_count_ = static_cast<GLsizei>(selection_vertices_.size() / 7);

    const QColor outlineColor(65, 185, 220, 230);
    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    for (const auto& edge : edges) {
        appendColoredVertex(selection_vertices_, p[edge[0]], outlineColor);
        appendColoredVertex(selection_vertices_, p[edge[1]], outlineColor);
    }
    selection_line_vertex_count_ = static_cast<GLsizei>(selection_vertices_.size() / 7) - selection_fill_vertex_count_;

    constexpr std::array<SelectionHandle, 6> handles = {
        SelectionHandle::MinX, SelectionHandle::MaxX, SelectionHandle::MinY,
        SelectionHandle::MaxY, SelectionHandle::MinZ, SelectionHandle::MaxZ,
    };
    for (const SelectionHandle handle : handles) {
        const QColor color = handle == active_selection_handle_ ? QColor(255, 245, 195, 250) : QColor(230, 170, 55, 235);
        const QVector3D handlePosition = selectionHandlePosition(handle);
        appendColoredBox(selection_vertices_, handlePosition, selectionHandleHalfSizeAt(handlePosition), color);
    }
}

QVector3D VoxelWidget::selectionHandlePosition(SelectionHandle handle) const {
    const QVector3D minimum = selection_.minimum * voxel_size_;
    const QVector3D maximum = selection_.maximum * voxel_size_;
    const QVector3D center = (minimum + maximum) * 0.5f;
    switch (handle) {
        case SelectionHandle::MinX:
            return {minimum.x(), center.y(), center.z()};
        case SelectionHandle::MaxX:
            return {maximum.x(), center.y(), center.z()};
        case SelectionHandle::MinY:
            return {center.x(), minimum.y(), center.z()};
        case SelectionHandle::MaxY:
            return {center.x(), maximum.y(), center.z()};
        case SelectionHandle::MinZ:
            return {center.x(), center.y(), minimum.z()};
        case SelectionHandle::MaxZ:
            return {center.x(), center.y(), maximum.z()};
        case SelectionHandle::None:
            return center;
    }
    return center;
}

void VoxelWidget::setSelectionEnabled(bool enabled) {
    selection_enabled_ = enabled;
    active_selection_handle_ = SelectionHandle::None;
    unsetCursor();
    if (selection_enabled_ && !selection_.isValid()) {
        resetSelectionToModelBounds();
    }
    buildSelectionVertices();
    if (gl_initialized_) {
        makeCurrent();
        updateSelectionOpenGLBuffer();
        doneCurrent();
    }
    update();
}

void VoxelWidget::updateModelMatrix() {
    m_model.setToIdentity();
    // Pan is applied after the rotation, in the fixed view frame, so dragging
    // always moves the model along the screen axes regardless of its rotation.
    m_model.translate(m_cameraTranslate);
    m_model.rotate(m_rotation);
    m_model.scale(m_scale);

    // center translate
    if (!voxel_data_.empty() && !voxel_data_[0].empty() && !voxel_data_[0][0].empty()) {
        // the mesh spans [0, size] in each axis, center on its bounds
        float cx = static_cast<float>(voxel_data_[0].size()) * voxel_size_ * 0.5f;
        float cy = static_cast<float>(voxel_data_.size()) * voxel_size_ * 0.5f;
        float cz = static_cast<float>(voxel_data_[0][0].size()) * voxel_size_ * 0.5f;
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
    updateProjection();
}

void VoxelWidget::updateProjection() {
    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    m_projection.setToIdentity();
    if (ortho_mode_) {
        // same view size as the perspective mode, so toggling O never changes
        // the apparent model size (zoom is handled by m_scale in both modes)
        const float halfW = kViewHalfHeight * aspect;
        m_projection.ortho(-halfW, halfW, -kViewHalfHeight, kViewHalfHeight, 0.1f, 1000.0f);
    } else {
        // perspective projection
        m_projection.perspective(45.0f, aspect, 0.1f, 1000.0f);
    }

    // view matrix (camera position)
    m_view.setToIdentity();
    m_view.lookAt(QVector3D(0.0f, 0.0f, 50.0f),  // camera position
                  QVector3D(0.0f, 0.0f, 0.0f),   // look target
                  QVector3D(0.0f, 1.0f, 0.0f));  // up direction
}

bool VoxelWidget::hasNeighborInBounds(int layer, int x, int z, int dy, int dx, int dz, const VoxelBounds& bounds,
                                      MeshOcclusionMode mode) const {
    const auto& current = voxel_data_[layer][x][z];

    int ny = layer + dy;  // y-offset
    int nx = x + dx;      // x-offset
    int nz = z + dz;      // z-offset

    if (ny < bounds.minimumY || ny >= bounds.maximumY ||  //
        nx < bounds.minimumX || nx >= bounds.maximumX ||  //
        nz < bounds.minimumZ || nz >= bounds.maximumZ ||  //
        ny < 0 || ny >= static_cast<int>(voxel_data_.size()) || nx < 0 || nx >= static_cast<int>(voxel_data_[ny].size()) || nz < 0 ||
        nz >= static_cast<int>(voxel_data_[ny][nx].size())) {
        return false;
    }

    const auto& neighbor = voxel_data_[ny][nx][nz];
    if (neighbor.color.alpha() == 0) {
        return false;
    }

    if (mode == MeshOcclusionMode::OccupiedVoxelShell) {
        // GLB export should describe the voxel shell. Any non-air neighbor hides
        // the shared face, otherwise transparent blocks create visible internal slices.
        return true;
    }

    if (!current.transparent) {
        // solid
        return !neighbor.transparent;
    }

    // transparent
    return neighbor.color.alpha() != 0;
}

std::optional<VoxelWidget::VoxelBounds> VoxelWidget::fullVoxelBounds() const {
    if (voxel_data_.empty() || voxel_data_[0].empty() || voxel_data_[0][0].empty()) {
        return std::nullopt;
    }

    return VoxelBounds{
        0, 0, 0, static_cast<int>(voxel_data_[0].size()), static_cast<int>(voxel_data_.size()), static_cast<int>(voxel_data_[0][0].size())};
}

std::optional<VoxelWidget::VoxelBounds> VoxelWidget::currentExportBounds() const {
    const auto fullBounds = fullVoxelBounds();
    if (!fullBounds) return std::nullopt;
    if (!selection_enabled_ || !selection_.isValid()) return fullBounds;

    VoxelBounds bounds;
    bounds.minimumX = std::clamp(static_cast<int>(std::floor(selection_.minimum.x())), fullBounds->minimumX, fullBounds->maximumX);
    bounds.minimumY = std::clamp(static_cast<int>(std::floor(selection_.minimum.y())), fullBounds->minimumY, fullBounds->maximumY);
    bounds.minimumZ = std::clamp(static_cast<int>(std::floor(selection_.minimum.z())), fullBounds->minimumZ, fullBounds->maximumZ);
    bounds.maximumX = std::clamp(static_cast<int>(std::ceil(selection_.maximum.x())), fullBounds->minimumX, fullBounds->maximumX);
    bounds.maximumY = std::clamp(static_cast<int>(std::ceil(selection_.maximum.y())), fullBounds->minimumY, fullBounds->maximumY);
    bounds.maximumZ = std::clamp(static_cast<int>(std::ceil(selection_.maximum.z())), fullBounds->minimumZ, fullBounds->maximumZ);
    if (!bounds.isValid()) return std::nullopt;
    return bounds;
}

void VoxelWidget::addFaceVerticesToBuffers(int layer, int x, int z, const Voxel& voxel, const std::vector<float>& faceVertices,
                                           const QVector3D& normal, std::vector<float>& vertices, std::vector<GLuint>& indices) const {
    // block (x, layer, z) occupies [x, x+1] x [y, y+1] x [z, z+1], so the model
    // grid aligns with the world coordinate grid (min corner at 0,0,0)
    float worldX = x * voxel_size_ + 0.5f * voxel_size_;
    float worldY = layer * voxel_size_ + 0.5f * voxel_size_;
    float worldZ = z * voxel_size_ + 0.5f * voxel_size_;

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

void VoxelWidget::appendVisibleVoxelMesh(const VoxelBounds& bounds, std::vector<float>& vertices, std::vector<GLuint>& indices,
                                         std::vector<float>* transparentVertices, std::vector<GLuint>* transparentIndices,
                                         MeshOcclusionMode mode) const {
    if (!bounds.isValid()) return;

    static int dxArr[] = {0, 0, -1, 1, 0, 0};
    static int dyArr[] = {0, 0, 0, 0, 1, -1};
    static int dzArr[] = {1, -1, 0, 0, 0, 0};

    const int maxLayer = std::min(bounds.maximumY, static_cast<int>(voxel_data_.size()));
    for (int layer = std::max(0, bounds.minimumY); layer < maxLayer; ++layer) {  // Y axis (layer)
        const auto& layerData = voxel_data_[layer];
        if (layerData.empty()) continue;

        const int maxX = std::min(bounds.maximumX, static_cast<int>(layerData.size()));
        for (int x = std::max(0, bounds.minimumX); x < maxX; ++x) {  // X axis
            const auto& rowData = layerData[x];
            if (rowData.empty()) continue;

            const int maxZ = std::min(bounds.maximumZ, static_cast<int>(rowData.size()));
            for (int z = std::max(0, bounds.minimumZ); z < maxZ; ++z) {  // Z axis
                const Voxel& voxel = rowData[z];

                if (voxel.color.alpha() == 0) {
                    continue;
                }

                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                    int dLayer = dyArr[faceIdx], dX = dxArr[faceIdx], dZ = dzArr[faceIdx];
                    if (!hasNeighborInBounds(layer, x, z, dLayer, dX, dZ, bounds, mode)) {
                        auto& targetVertices = (voxel.transparent && transparentVertices) ? *transparentVertices : vertices;
                        auto& targetIndices = (voxel.transparent && transparentIndices) ? *transparentIndices : indices;
                        addFaceVerticesToBuffers(layer, x, z, voxel, m_faceTemplates[faceIdx], m_faceNormals[faceIdx], targetVertices,
                                                 targetIndices);
                    }
                }
            }
        }
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

    if (const auto fullBounds = fullVoxelBounds()) {
        VoxelBounds layerBounds = *fullBounds;
        layerBounds.minimumY = std::clamp(start_layer_, fullBounds->minimumY, fullBounds->maximumY);
        layerBounds.maximumY = std::clamp(ender_layer_ + 1, fullBounds->minimumY, fullBounds->maximumY);
        appendVisibleVoxelMesh(layerBounds, verticles_opaque_, indices_opaque_, &verticles_transparent_, &indices_transparent_);
    }
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

    // Compute the world-space Y range covered by all chunks.
    int world_min_y = INT_MAX, world_max_y = INT_MIN;
    for (const auto& chunk_row : chunks) {
        for (const auto& ch : chunk_row) {
            if (!ch) continue;
            auto [chunk_min_y, chunk_max_y] = ch->get_pos().get_y_range(ch->get_version());
            world_min_y = std::min(world_min_y, chunk_min_y);
            world_max_y = std::max(world_max_y, chunk_max_y);
        }
    }

    if (world_max_y < world_min_y) return {};

    const int total_layer = world_max_y - world_min_y + 1;

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

            auto [chunk_min_y, chunk_max_y] = ch->get_pos().get_y_range(ch->get_version());

            // Precompute the base offset of global indices.
            const int base_global_x = xIdx * CW;
            const int base_global_z = zIdx * CW;

            for (int x = 0; x < CW; ++x) {
                int global_x_idx = base_global_x + x;
                if (global_x_idx < 0 || global_x_idx >= data[0].size()) continue;

                for (int z = 0; z < CW; ++z) {
                    int global_z_idx = base_global_z + z;
                    if (global_z_idx < 0 || global_z_idx >= data[0][0].size()) continue;

                    // Iterate in world-space Y, then map to the local voxel array index.
                    for (int world_y = chunk_min_y; world_y <= chunk_max_y; ++world_y) {
                        const int global_y_idx = world_y - world_min_y;
                        if (global_y_idx < 0 || global_y_idx >= total_layer) continue;

                        // Bounds check.
                        if (global_y_idx >= data.size() || global_x_idx >= data[global_y_idx].size() ||
                            global_z_idx >= data[global_y_idx][global_x_idx].size()) {
                            continue;
                        }

                        Voxel& voxel = data[global_y_idx][global_x_idx][global_z_idx];
                        auto block_info = ch->get_block(x, world_y, z);
                        auto biome = ch->get_biome(x, world_y, z);

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

    std::vector<std::vector<std::vector<Voxel>>> ret(firstIt, lastIt);
    LOG_F(INFO, "Real Voxel Size: %zu (Y) * %zu (X) * %zu (Z)", ret.size(), ret[0].size(), ret[0][0].size());
    return ret;
}
