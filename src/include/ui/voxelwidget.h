#ifndef BEDROCKMAP_VOXELWIDGET_H
#define BEDROCKMAP_VOXELWIDGET_H

#include <qboxlayout.h>
#include <qmainwindow.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qprogressbar.h>
#include <qwidget.h>

#include <QCheckBox>
#include <QColor>
#include <QFuture>
#include <QGroupBox>
#include <QLabel>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QPointF>
#include <QPushButton>
#include <QQuaternion>
#include <QSizePolicy>
#include <QSpinBox>
#include <QToolButton>
#include <QVector3D>
#include <QWidget>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "chunk.h"
#include "guitaskrunner.h"

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

using VoxelGrid = std::vector<std::vector<std::vector<Voxel>>>;

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
    void setSelection(const VoxelSelection& selection);
    void setSelectionMoveMode(bool enabled);
    [[nodiscard]] bool isSelectionMoveMode() const { return selection_move_mode_; }
    /// Import placement lock: the selection keeps its size and can only be moved.
    void setSelectionLocked(bool locked);
    [[nodiscard]] bool isSelectionLocked() const { return selection_locked_; }
    /// Ghost mesh of a structure being placed. It is a separate grid, so it is not
    /// occluded by the model and is drawn with reduced opacity.
    void setPreviewVoxelData(VoxelGrid data);
    void setPreviewOffset(const QVector3D& offsetVoxels);
    void clearPreviewVoxelData();
    void setAxesVisible(bool visible);
    [[nodiscard]] bool isAxesVisible() const { return axes_visible_; }
    void setOrthoMode(bool ortho);
    [[nodiscard]] bool isOrthoMode() const { return ortho_mode_; }
    void setRotationLocked(bool locked);
    [[nodiscard]] bool isRotationLocked() const { return rotation_locked_; }
    void rotateView(float yawDegrees, float pitchDegrees);
    void focusFrontFace();                      // snap the face closest to the screen flat (the F key)
    [[nodiscard]] QVector3D modelSize() const;  // voxel dimensions of the loaded model
    [[nodiscard]] bool exportGlb(const QString& filePath, QString* errorMessage = nullptr) const;

    static std::vector<std::vector<std::vector<Voxel>>> createVoxelDataFromChunks(const std::vector<std::vector<bl::chunk*>>& chunks,
                                                                                  const std::function<void(int)>& f,
                                                                                  int* firstWorldY = nullptr);

   signals:
    void selectionChanged(VoxelSelection selection);
    void selectionEnabledChanged(bool enabled);
    void viewOptionsChanged();  // axes visibility, projection mode, or selection move mode

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
    void setupVertexAttributes();              // set vertex attributes (called once only)
    void updateOpenGLBuffers();                // update OpenGL buffer data
    void updateModelMatrix();                  // update model matrix
    void updateProjection();                   // rebuild projection from the current mode / size
    [[nodiscard]] float maxZoomScale() const;  // zoom bound that keeps the camera outside the model
    void buildAxisVertices();                  // rebuild the coordinate axis line vertices
    void buildSelectionVertices();             // rebuild the selection fill, outline, and handles
    void setupShortcutHelpButton();
    void updateShortcutHelpButtonGeometry();
    void showShortcutHelp();
    void updateSelectionOpenGLBuffer();
    void updatePreviewOpenGLBuffer();
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
    [[nodiscard]] bool hasNeighborInBounds(const VoxelGrid& grid, int layer, int x, int z, int dLayer, int dX, int dZ,
                                           const bl::block_box& bounds, MeshOcclusionMode mode = MeshOcclusionMode::RenderView) const;
    [[nodiscard]] std::optional<bl::block_box> fullVoxelBounds() const;
    [[nodiscard]] std::optional<bl::block_box> currentExportBounds() const;
    void addFaceVerticesToBuffers(int layer, int x, int z, const Voxel& voxel, const std::vector<float>& faceVertices,
                                  const QVector3D& normal, std::vector<float>& vertices, std::vector<GLuint>& indices,
                                  float alphaScale = 1.0f) const;
    void appendVisibleVoxelMesh(const VoxelGrid& grid, const bl::block_box& bounds, std::vector<float>& vertices,
                                std::vector<GLuint>& indices, std::vector<float>* transparentVertices = nullptr,
                                std::vector<GLuint>* transparentIndices = nullptr, MeshOcclusionMode mode = MeshOcclusionMode::RenderView,
                                float alphaScale = 1.0f) const;
    void buildVoxelVertices();
    void buildPreviewVertices();
    // OpenGL obj (opaque, transparent)
    QOpenGLShaderProgram* gl_shader_ = nullptr;
    QOpenGLShaderProgram* axis_shader_ = nullptr;
    GLuint vao_opaque_ = 0, vao_transparent_ = 0;
    GLuint vbo_opaque_ = 0, vbo_transparent_ = 0;
    GLuint ebo_opaque_ = 0, ebo_transparent_ = 0;
    GLuint vao_axes_ = 0, vbo_axes_ = 0;
    GLuint vao_selection_ = 0, vbo_selection_ = 0;
    GLuint vao_preview_ = 0, vbo_preview_ = 0, ebo_preview_ = 0;
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
    VoxelGrid preview_data_;                 // structure being placed, empty when not previewing
    std::vector<float> preview_vertices_;    // 10 floats per vertex: pos3 + normal3 + rgba4
    std::vector<GLuint> preview_indices_;
    QVector3D preview_offset_;  // placement offset in voxel units
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
    float fit_scale_ = 1.0f;  // zoom that frames a newly loaded model (the R key target)
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
    QMatrix4x4 m_preview_model;  // m_model with the placement offset applied
    bool ortho_mode_{false};     // false = perspective, true = orthographic
    bool axes_visible_{false};   // coordinate axes overlay (A key)
    bool rotation_locked_{false};
    QToolButton* shortcut_help_button_{nullptr};
    QWidget* shortcut_help_popup_{nullptr};

    // voxel selection
    bool selection_enabled_{false};
    bool selection_move_mode_{false};  // true: dragging a handle moves the selection instead of resizing it
    bool selection_locked_{false};     // true: the selection size is fixed and it cannot be disabled
    VoxelSelection selection_;
    SelectionHandle active_selection_handle_{SelectionHandle::None};
    QPointF selection_drag_start_;
    QPointF selection_drag_axis_screen_;
    int selection_drag_start_value_{0};
    QVector3D selection_drag_start_minimum_;
    QVector3D selection_drag_start_maximum_;
    std::array<QPointF, 3> selection_drag_screen_axes_{};  // screen-space step per world axis, for move mode

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

    explicit VoxelPreviewWidget(QWidget* parent = nullptr);

    bool loadChunksAsync(const bl::chunk_pos& min, const bl::chunk_pos& max, AsyncLevelLoader& loader);
    void loadMcstructureAsync(std::shared_ptr<const bl::mcstructure> structure);
    /// Re-reads the chunk range the preview was loaded from, picking up level edits.
    /// Returns false when the preview is not backed by chunks.
    bool reloadChunks();
    [[nodiscard]] bl::block_pos voxelOrigin() const { return voxel_origin_; }
    /// Import placement mode: the selection is locked to the size of the model to
    /// be imported and can only be moved. Export controls stay disabled until the
    /// mode ends, which happens on confirm or cancel.
    void beginImportMode(const bl::block_pos& importedSize);

   signals:
    void exportMcstructureRequested(VoxelSelection selection, bool hasSelection, bool compress, bool exportEntities, bool useNewFormat);
    /// World-space placement of the imported model: minimum corner inclusive, maximum exclusive.
    void importConfirmed(VoxelSelection placement, std::shared_ptr<const bl::mcstructure> structure);

   private:
    void setVoxelData(VoxelGrid&& data, const bl::block_pos& origin);
    void exportGlbModel();
    void chooseImportFile();
    void endImportMode();
    // side panel
    [[nodiscard]] QWidget* buildModelPanel();
    [[nodiscard]] QWidget* buildSelectionPanel();
    [[nodiscard]] QWidget* buildViewPanel();
    [[nodiscard]] QWidget* buildMcstructurePanel();
    [[nodiscard]] QWidget* buildGlbPanel();
    [[nodiscard]] QWidget* buildImportBar();
    [[nodiscard]] QVector3D worldOrigin() const;
    void refreshModelInfo();
    void refreshSelectionFields();
    void refreshViewOptions();
    void applySelectionFields();

    QProgressBar* bar_;
    VoxelWidget* voxelWidget_;
    QLabel* model_info_label_{nullptr};
    QGroupBox* selection_group_{nullptr};
    std::array<QSpinBox*, 3> selection_min_boxes_{};
    std::array<QSpinBox*, 3> selection_max_boxes_{};
    QCheckBox* selection_move_box_{nullptr};
    QCheckBox* axes_box_{nullptr};
    QCheckBox* ortho_box_{nullptr};
    QCheckBox* rotation_lock_box_{nullptr};
    QCheckBox* mcstructureCompressBox_{nullptr};
    QCheckBox* mcstructureEntitiesBox_{nullptr};
    QCheckBox* mcstructureNewFormatBox_{nullptr};
    QPushButton* mcstructure_import_button_{nullptr};
    QPushButton* mcstructure_export_button_{nullptr};
    QPushButton* glb_export_button_{nullptr};
    QWidget* import_bar_{nullptr};
    std::shared_ptr<const bl::mcstructure> import_structure_;
    bool import_mode_{false};
    bool syncing_selection_fields_{false};  // true while the panel writes into the spin boxes
    // chunk source kept so the preview can be reloaded after an edit
    AsyncLevelLoader* loaded_loader_{nullptr};
    bl::chunk_pos loaded_min_chunk_;
    bl::chunk_pos loaded_max_chunk_;
    bool has_chunk_source_{false};
    // data
    bl::block_pos voxel_origin_;
    GuiTaskRunner chunk_task_;
    VoxelLoadResult pending_chunk_result_;
    GuiTaskRunner mcstructure_task_;
    VoxelLoadResult pending_mcstructure_result_;
};

#endif  // BEDROCKMAP_VOXELWIDGET_H
