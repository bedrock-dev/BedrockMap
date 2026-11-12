#ifndef BEDROCKMAP_VOXELQUICKITEM_H
#define BEDROCKMAP_VOXELQUICKITEM_H

#include <QtQml/qqmlregistration.h>

#include <QPointer>
#include <QQuickRhiItem>
#include <QVector3D>
#include <memory>

#include "controller/voxelscenecontroller.h"
#include "render/voxel_mesh.h"
#include "render/voxel_scene.h"

struct VoxelRhiCameraState {
    float yaw{45.0f};
    float pitch{45.0f};
    float zoom{1.0f};
    QVector3D pan;
    bool orthoMode{false};
};

struct VoxelRhiSelectionState {
    bl::block_box bounds;
    QColor color{QStringLiteral("#e641b9dc")};
    VoxelSelectionHandle active{VoxelSelectionHandle::None};
    bool enabled{false};
};

class VoxelQuickItem : public QQuickRhiItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(VoxelSceneController* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(bool orthoMode READ orthoMode WRITE setOrthoMode NOTIFY viewOptionsChanged)
    Q_PROPERTY(bool rotationLocked READ rotationLocked WRITE setRotationLocked NOTIFY viewOptionsChanged)
    Q_PROPERTY(bool showAxes READ showAxes WRITE setShowAxes NOTIFY axesChanged)
    Q_PROPERTY(QVector3D modelSize READ modelSize NOTIFY sceneChanged)
    Q_PROPERTY(QVector3D modelOrigin READ modelOrigin NOTIFY sceneChanged)
    Q_PROPERTY(bool selectionEnabled READ selectionEnabled WRITE setSelectionEnabled NOTIFY selectionOptionsChanged)
    Q_PROPERTY(bool selectionMoveMode READ selectionMoveMode WRITE setSelectionMoveMode NOTIFY selectionOptionsChanged)
    Q_PROPERTY(bool selectionLocked READ selectionLocked NOTIFY selectionOptionsChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY selectionOptionsChanged)
    Q_PROPERTY(QVector3D selectionMinimum READ selectionMinimum NOTIFY selectionChanged)
    Q_PROPERTY(QVector3D selectionMaximum READ selectionMaximum NOTIFY selectionChanged)
    Q_PROPERTY(bool selectionDragging READ selectionDragging NOTIFY selectionInteractionChanged)
    Q_PROPERTY(float saturation READ saturation WRITE setSaturation NOTIFY toneChanged)
    Q_PROPERTY(float brightness READ brightness WRITE setBrightness NOTIFY toneChanged)

   public:
    explicit VoxelQuickItem(QQuickItem* parent = nullptr);

    [[nodiscard]] VoxelSceneController* controller() const { return controller_; }
    void setController(VoxelSceneController* controller);
    [[nodiscard]] std::shared_ptr<const VoxelSceneData> currentScene() const;
    [[nodiscard]] std::shared_ptr<const VoxelSceneData> currentOverlayScene() const;
    [[nodiscard]] VoxelRhiCameraState cameraState() const;
    [[nodiscard]] bool orthoMode() const;
    void setOrthoMode(bool ortho);
    [[nodiscard]] bool rotationLocked() const;
    void setRotationLocked(bool locked);
    [[nodiscard]] bool showAxes() const;
    void setShowAxes(bool enabled);
    [[nodiscard]] QVector3D modelSize() const;
    [[nodiscard]] QVector3D modelOrigin() const;
    [[nodiscard]] VoxelRhiSelectionState selectionState() const;
    [[nodiscard]] bool selectionEnabled() const;
    void setSelectionEnabled(bool enabled);
    [[nodiscard]] bool selectionMoveMode() const;
    void setSelectionMoveMode(bool enabled);
    [[nodiscard]] bool selectionLocked() const;
    [[nodiscard]] QColor selectionColor() const;
    void setSelectionColor(const QColor& color);
    [[nodiscard]] QVector3D selectionMinimum() const;
    [[nodiscard]] QVector3D selectionMaximum() const;
    [[nodiscard]] bool selectionDragging() const;
    [[nodiscard]] float saturation() const;
    void setSaturation(float saturation);
    [[nodiscard]] float brightness() const;
    void setBrightness(float brightness);
    Q_INVOKABLE void setSelectionBoundary(int axis, bool maximum, int value);
    Q_INVOKABLE void activateImportSelection(const QVector3D& size);
    Q_INVOKABLE void deactivateImportSelection();
    Q_INVOKABLE bool beginSelectionDrag(const QPointF& position, qreal pickRadius = 10.8);
    Q_INVOKABLE void dragSelection(const QPointF& position);
    Q_INVOKABLE void endSelectionDrag();
    Q_INVOKABLE void rotateView(float yawDegrees, float pitchDegrees);
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void focusFrontFace();
    Q_INVOKABLE void beginRotate(const QPointF& position);
    Q_INVOKABLE void beginPan(const QPointF& position);
    Q_INVOKABLE void dragView(const QPointF& position);
    Q_INVOKABLE void endView();
    Q_INVOKABLE void zoomBy(qreal factor);

   signals:
    void controllerChanged();
    void sceneChanged();
    void viewOptionsChanged();
    void axesChanged();
    void selectionOptionsChanged();
    void selectionChanged();
    void selectionInteractionChanged();
    void toneChanged();

   protected:
    QQuickRhiItemRenderer* createRenderer() override;

   private:
    void syncScene();
    void syncOverlay();

    QPointer<VoxelSceneController> controller_;
    QMetaObject::Connection scene_connection_;
    QMetaObject::Connection overlay_connection_;
    std::shared_ptr<const VoxelSceneData> scene_;
    std::shared_ptr<const VoxelSceneData> overlay_scene_;
    float yaw_{45.0f};
    float pitch_{45.0f};
    float zoom_{1.0f};
    QVector3D pan_;
    QPointF last_mouse_position_;
    bool rotating_{false};
    bool panning_{false};
    bool ortho_mode_{false};
    bool rotation_locked_{false};
    bool show_axes_{false};
    float saturation_{1.0f};
    float brightness_{1.0f};
    VoxelRhiSelectionState selection_;
    VoxelRhiSelectionState selection_before_import_;
    VoxelSelectionDrag selection_drag_;
    bool selection_move_mode_{false};
    bool selection_move_mode_before_import_{false};
    bool selection_locked_{false};
};

#endif  // BEDROCKMAP_VOXELQUICKITEM_H
