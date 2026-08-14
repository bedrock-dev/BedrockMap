#ifndef VOXELWIDGET_H
#define VOXELWIDGET_H

#include <qboxlayout.h>
#include <qmainwindow.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qprogressbar.h>
#include <qwidget.h>

#include <QColor>
#include <QFuture>
#include <QFutureWatcher>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QVector3D>
#include <QWidget>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <functional>
#include <vector>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunk.h"

struct Voxel {
    QColor color;              // color (alpha channel assists transparency check)
    bool transparent = false;  // primary transparency flag (true=transparent, false=opaque)
    Voxel() : color(Qt::white) {}
    Voxel(const QColor& c, bool trans = false) : color(c), transparent(trans) {}
};

class VoxelWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

   public:
    explicit VoxelWidget(QWidget* parent = nullptr);
    ~VoxelWidget() override;

    void setLayer(int startLayer, int endLayer);
    void updateVoxelData(const std::vector<std::vector<std::vector<Voxel>>>& newData);

    static std::vector<std::vector<std::vector<Voxel>>> createVoxelDataFromChunks(const std::vector<std::vector<bl::chunk*>>& chunks,
                                                                                  const std::function<void(int)>& f);

   protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

   private:
    // opengl
    void generateOpenGLBuffers();
    void setupVertexAttributes();                 // set vertex attributes (called once only)
    void updateOpenGLBuffers();                   // update OpenGL buffer data
    void updateModelMatrix();                     // update model matrix
    void setUniforms();                           // set shader uniforms
    void renderOpaqueObjects();                   // render opaque objects
    void renderTransparentObjects();              // render transparent objects
    void checkOpenGLError(const char* location);  // debug error checking

    // mesh building
    bool hasNeighbor(int layer, int x, int z, int dLayer, int dX, int dZ);
    void buildVoxelVertices();
    void addFaceVertices(int layer, int x, int z, const Voxel& voxel, const std::vector<float>& faceVertices, const QVector3D& normal);
    // OpenGL obj (opaque, transparent)
    QOpenGLShaderProgram* gl_shader_ = nullptr;
    GLuint vao_opaque_ = 0, vao_transparent_ = 0;
    GLuint vbo_opaque_ = 0, vbo_transparent_ = 0;
    GLuint ebo_opaque_ = 0, ebo_transparent_ = 0;
    bool gl_initialized_{false};  // true once initializeGL() has run and GL objects exist

    // vertices(opaque)
    std::vector<float> verticles_opaque_;
    std::vector<float> verticles_transparent_;

    // vertices(transparent)
    std::vector<GLuint> indices_opaque_;
    std::vector<GLuint> indices_transparent_;

    // mesh
    std::vector<std::vector<std::vector<Voxel>>> voxel_data_;
    int start_layer_ = 0;
    int ender_layer_ = 0;

    // camera
    QPoint m_lastMousePos;
    float rotate_x_ = 45.f;
    float rotate_y_ = 45.f;
    float m_scale = 1.0f;
    float voxel_size_ = 1.0f;

    QVector3D m_cameraTranslate;   // camera pan offset (X/Y/Z axis)
    bool m_isPanDragging{false};   // whether panning drag is active
    QPoint m_panStartPos;          // mouse start position for pan
    float m_panSensitivity{0.3};   // pan sensitivity
    bool m_isShiftPressed{false};  // whether Shift is pressed (distinguishes forward/back vs left/right pan)

    // matrix
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QMatrix4x4 m_model;

    // shadering
    QVector3D m_lightPos = QVector3D(8.0f, 384.0f, 8.0f);
    QVector3D m_lightColor = QVector3D(1.0f, 1.0f, 1.0f);
    QVector3D m_ambientLight = QVector3D(0.8f, 0.8f, 0.8f);

    // static data for mesh building
    static const std::vector<std::vector<float>> m_faceTemplates;
    static const std::vector<QVector3D> m_faceNormals;
};

class ChunkRenderWidget : public QWidget {
    Q_OBJECT
   public:
    ChunkRenderWidget() : QWidget() {
        voxelWidget_ = new VoxelWidget(this);
        bar_ = new QProgressBar(this);
        auto* layout = new QVBoxLayout();
        layout->addWidget(voxelWidget_);
        layout->addWidget(bar_);
        setLayout(layout);
        setGeometry({0, 0, 1200, 900});
        connect(&this->chunk_render_watcher_, &QFutureWatcher<bool>::finished, this, [this]() {
            bar_->hide();
            voxelWidget_->updateVoxelData(voxels_);
        });
        connect(this, &ChunkRenderWidget::chunkMeshBuilt, this, [this](int n) { bar_->setValue(n); });
    }

    bool showChunks(const bl::chunk_pos& min, const bl::chunk_pos& max, AsyncLevelLoader& loader);

   signals:
    void chunkMeshBuilt(int n);

   private:
    QProgressBar* bar_;
    VoxelWidget* voxelWidget_;
    // data
    QFutureWatcher<bool> chunk_render_watcher_;
    std::vector<std::vector<std::vector<Voxel>>> voxels_;
    std::vector<std::vector<bl::chunk*>> chunks_;
};

#endif  // VOXELWIDGET_H
