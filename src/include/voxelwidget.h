#ifndef VOXELWIDGET_H
#define VOXELWIDGET_H

#include <qboxlayout.h>
#include <qmainwindow.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qprogressbar.h>
#include <qwidget.h>

#include <QCheckBox>
#include <QColor>
#include <QFuture>
#include <QFutureWatcher>
#include <QLabel>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QPointF>
#include <QQuaternion>
#include <QSizePolicy>
#include <QToolButton>
#include <QVector3D>
#include <QWidget>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunk.h"

class QResizeEvent;
namespace bl {
    class chunk;
    struct mcstructure;
}  // namespace bl
class QLabel;

struct Voxel {
    QColor color;              // color (alpha channel assists transparency check)
    bool transparent = false;  // primary transparency flag (true=transparent, false=opaque)
    Voxel() : color(Qt::white) {}
    Voxel(const QColor& c, bool trans = false) : color(c), transparent(trans) {}
};

/// An axis-aligned selection in model-local voxel coordinates.
/// The minimum boundary is inclusive and the maximum boundary is exclusive.
struct VoxelSelection {
    QVector3D minimum;
    QVector3D maximum;

    [[nodiscard]] bool isValid() const { return minimum.x() < maximum.x() && minimum.y() < maximum.y() && minimum.z() < maximum.z(); }
};

Q_DECLARE_METATYPE(VoxelSelection)

class VoxelWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

   public:
    explicit VoxelWidget(QWidget* parent = nullptr);
    ~VoxelWidget() override;

    void setLayer(int startLayer, int endLayer);
    void updateVoxelData(const std::vector<std::vector<std::vector<Voxel>>>& newData);
    void updateVoxelData(std::vector<std::vector<std::vector<Voxel>>>&& newData);
    void setSelectionEnabled(bool enabled);
    [[nodiscard]] bool isSelectionEnabled() const { return selection_enabled_; }
    [[nodiscard]] VoxelSelection getSelection() const { return selection_; }
    [[nodiscard]] bool exportGlb(const QString& filePath, QString* errorMessage = nullptr) const;

    static std::vector<std::vector<std::vector<Voxel>>> createVoxelDataFromChunks(const std::vector<std::vector<bl::chunk*>>& chunks,
                                                                                  const std::function<void(int)>& f,
                                                                                  int* firstWorldY = nullptr);

   protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;

   private:
    enum class SelectionHandle { None, MinX, MaxX, MinY, MaxY, MinZ, MaxZ };

    enum class MeshOcclusionMode { RenderView, OccupiedVoxelShell };

    // opengl
    void generateOpenGLBuffers();
    void setupVertexAttributes();   // set vertex attributes (called once only)
    void updateOpenGLBuffers();     // update OpenGL buffer data
    void updateModelMatrix();       // update model matrix
    void updateProjection();        // rebuild projection from the current mode / size
    void buildAxisVertices();       // rebuild the coordinate axis line vertices
    void buildSelectionVertices();  // rebuild the selection fill, outline, and handles
    void setupShortcutHelpButton();
    void updateShortcutHelpButtonGeometry();
    void showShortcutHelp();
    void updateSelectionOpenGLBuffer();
    void resetSelectionToModelBounds();
    [[nodiscard]] float pixelsPerWorldUnitAt(const QVector3D& point) const;
    [[nodiscard]] float selectionHandleHalfSizeAt(const QVector3D& point) const;
    [[nodiscard]] SelectionHandle pickSelectionHandle(const QPointF& position) const;
    [[nodiscard]] QVector3D selectionHandlePosition(SelectionHandle handle) const;
    [[nodiscard]] QVector3D selectionHandleAxis(SelectionHandle handle) const;
    [[nodiscard]] QPointF projectToWidget(const QVector3D& point, bool* visible = nullptr) const;
    void updateSelectionFromDrag(const QPointF& position);
    QVector3D localFaceClosestTo(const QVector3D& dir) const;
    void setOrbitAnglesFromRotation();
    void updateRotationFromOrbitAngles();
    void orbitRotate(float yawDegrees, float pitchDegrees);
    void setUniforms();                           // set shader uniforms
    void renderOpaqueObjects();                   // render opaque objects
    void renderTransparentObjects();              // render transparent objects
    void checkOpenGLError(const char* location);  // debug error checking

    // mesh building
    [[nodiscard]] bool hasNeighborInBounds(int layer, int x, int z, int dLayer, int dX, int dZ, const bl::block_box& bounds,
                                           MeshOcclusionMode mode = MeshOcclusionMode::RenderView) const;
    [[nodiscard]] std::optional<bl::block_box> fullVoxelBounds() const;
    [[nodiscard]] std::optional<bl::block_box> currentExportBounds() const;
    void addFaceVerticesToBuffers(int layer, int x, int z, const Voxel& voxel, const std::vector<float>& faceVertices,
                                  const QVector3D& normal, std::vector<float>& vertices, std::vector<GLuint>& indices) const;
    void appendVisibleVoxelMesh(const bl::block_box& bounds, std::vector<float>& vertices, std::vector<GLuint>& indices,
                                std::vector<float>* transparentVertices = nullptr, std::vector<GLuint>* transparentIndices = nullptr,
                                MeshOcclusionMode mode = MeshOcclusionMode::RenderView) const;
    void buildVoxelVertices();
    // OpenGL obj (opaque, transparent)
    QOpenGLShaderProgram* gl_shader_ = nullptr;
    QOpenGLShaderProgram* axis_shader_ = nullptr;
    GLuint vao_opaque_ = 0, vao_transparent_ = 0;
    GLuint vbo_opaque_ = 0, vbo_transparent_ = 0;
    GLuint ebo_opaque_ = 0, ebo_transparent_ = 0;
    GLuint vao_axes_ = 0, vbo_axes_ = 0;
    GLuint vao_selection_ = 0, vbo_selection_ = 0;
    bool gl_initialized_{false};  // true once initializeGL() has run and GL objects exist

    // vertices(opaque)
    std::vector<float> verticles_opaque_;
    std::vector<float> verticles_transparent_;

    // vertices(transparent)
    std::vector<GLuint> indices_opaque_;
    std::vector<GLuint> indices_transparent_;

    // mesh
    std::vector<std::vector<std::vector<Voxel>>> voxel_data_;
    std::vector<float> axes_vertices_;       // 7 floats per vertex: pos3 + rgba4
    std::vector<float> selection_vertices_;  // 7 floats per vertex: pos3 + rgba4
    GLsizei selection_fill_vertex_count_{0};
    GLsizei selection_line_vertex_count_{0};
    int start_layer_ = 0;
    int ender_layer_ = 0;

    // camera
    QPoint m_lastMousePos;
    QQuaternion m_rotation{QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, 45.0f) * QQuaternion::fromAxisAndAngle(0.0f, 1.0f, 0.0f, 45.0f)};
    float orbit_yaw_degrees_{45.0f};
    float orbit_pitch_degrees_{45.0f};
    float m_scale = 1.0f;
    float voxel_size_ = 1.0f;

    QVector3D m_cameraTranslate;   // camera pan offset (X/Y/Z axis)
    bool m_isPanDragging{false};   // whether panning drag is active
    QPoint m_panStartPos;          // mouse start position for pan
    float m_panSensitivity{1.0};   // pan sensitivity multiplier (1.0 = model follows the cursor 1:1)
    bool m_isShiftPressed{false};  // whether Shift is pressed (distinguishes forward/back vs left/right pan)

    // matrix
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QMatrix4x4 m_model;
    bool ortho_mode_{false};    // false = perspective, true = orthographic
    bool axes_visible_{false};  // coordinate axes overlay (A key)
    bool rotation_locked_{false};
    QToolButton* shortcut_help_button_{nullptr};
    QWidget* shortcut_help_popup_{nullptr};

    // voxel selection
    bool selection_enabled_{false};
    VoxelSelection selection_;
    SelectionHandle active_selection_handle_{SelectionHandle::None};
    QPointF selection_drag_start_;
    QPointF selection_drag_axis_screen_;
    int selection_drag_start_value_{0};

    // shadering
    QVector3D m_lightPos = QVector3D(8.0f, 384.0f, 8.0f);
    QVector3D m_lightColor = QVector3D(1.0f, 1.0f, 1.0f);
    QVector3D m_ambientLight = QVector3D(0.8f, 0.8f, 0.8f);

    // static data for mesh building
    static const std::vector<std::vector<float>> m_faceTemplates;
    static const std::vector<QVector3D> m_faceNormals;
};

class VoxelPreviewWidget : public QWidget {
    Q_OBJECT
   public:
    using VoxelGrid = std::vector<std::vector<std::vector<Voxel>>>;
    struct VoxelLoadResult {
        VoxelGrid data;
        bl::block_pos origin;
    };

    explicit VoxelPreviewWidget(QWidget* parent = nullptr) : QWidget(parent) {
        voxelWidget_ = new VoxelWidget(this);
        bar_ = new QProgressBar(this);
        auto* toolbar = new QWidget(this);
        toolbar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* toolbarLayout = new QHBoxLayout(toolbar);
        toolbarLayout->setContentsMargins(0, 0, 0, 0);
        toolbarLayout->setSpacing(6);

        auto* exportMcstructureButton = new QToolButton(toolbar);
        exportMcstructureButton->setText(tr("Export .mcstructure"));
        connect(exportMcstructureButton, &QToolButton::clicked, this, [this]() {
            emit exportMcstructureRequested(voxelWidget_->getSelection(), voxelWidget_->isSelectionEnabled(),
                                            mcstructureCompressBox_->isChecked(), mcstructureEntitiesBox_->isChecked(),
                                            mcstructureNewFormatBox_->isChecked());
        });

        mcstructureCompressBox_ = new QCheckBox(tr("Compress"), toolbar);
        mcstructureEntitiesBox_ = new QCheckBox(tr("Export entities"), toolbar);
        mcstructureNewFormatBox_ = new QCheckBox(tr("Use new format"), toolbar);
        mcstructureCompressBox_->setChecked(false);
        mcstructureEntitiesBox_->setChecked(false);
        mcstructureNewFormatBox_->setChecked(false);

        auto* importMcstructureButton = new QToolButton(toolbar);
        importMcstructureButton->setText(tr("Import .mcstructure"));
        connect(importMcstructureButton, &QToolButton::clicked, this, []() {});

        auto* exportModelButton = new QToolButton(toolbar);
        exportModelButton->setText(tr("Export GLB"));
        exportModelButton->setToolTip(tr("Export as glTF Binary (.glb)"));
        connect(exportModelButton, &QToolButton::clicked, this, [this]() { exportGlbModel(); });

        toolbarLayout->addWidget(exportMcstructureButton);
        toolbarLayout->addWidget(mcstructureEntitiesBox_);
        toolbarLayout->addWidget(mcstructureNewFormatBox_);
        toolbarLayout->addWidget(mcstructureCompressBox_);
        toolbarLayout->addWidget(importMcstructureButton);
        toolbarLayout->addWidget(exportModelButton);
        toolbarLayout->addStretch();
        toolbar->setFixedHeight(toolbar->sizeHint().height());
        voxelWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        auto* layout = new QVBoxLayout();
        layout->addWidget(toolbar, 0);
        layout->addWidget(voxelWidget_, 1);
        layout->addWidget(bar_, 0);
        setLayout(layout);
        setGeometry({0, 0, 1200, 900});
        connect(&this->chunk_render_watcher_, &QFutureWatcher<VoxelLoadResult>::finished, this, [this]() {
            bar_->hide();
            auto result = chunk_render_watcher_.future().result();
            setVoxelData(std::move(result.data), result.origin);
        });
        connect(&this->mcstructure_render_watcher_, &QFutureWatcher<VoxelLoadResult>::finished, this, [this]() {
            bar_->hide();
            auto result = mcstructure_render_watcher_.future().result();
            setVoxelData(std::move(result.data), result.origin);
        });
        connect(this, &VoxelPreviewWidget::chunkMeshBuilt, this, [this](int n) { bar_->setValue(n); });
    }

    bool loadChunksAsync(const bl::chunk_pos& min, const bl::chunk_pos& max, AsyncLevelLoader& loader);
    void loadMcstructureAsync(std::shared_ptr<const bl::mcstructure> structure);
    [[nodiscard]] bl::block_pos voxelOrigin() const { return voxel_origin_; }

   signals:
    void chunkMeshBuilt(int n);
    void exportMcstructureRequested(VoxelSelection selection, bool hasSelection, bool compress, bool exportEntities, bool useNewFormat);

   private:
    void setVoxelData(VoxelGrid&& data, const bl::block_pos& origin);
    void exportGlbModel();

    QProgressBar* bar_;
    VoxelWidget* voxelWidget_;
    QCheckBox* mcstructureCompressBox_{nullptr};
    QCheckBox* mcstructureEntitiesBox_{nullptr};
    QCheckBox* mcstructureNewFormatBox_{nullptr};
    // data
    bl::block_pos voxel_origin_;
    QFutureWatcher<VoxelLoadResult> chunk_render_watcher_;
    QFutureWatcher<VoxelLoadResult> mcstructure_render_watcher_;
};

#endif  // VOXELWIDGET_H
