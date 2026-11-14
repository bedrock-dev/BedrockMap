#ifndef BEDROCKMAP_VOXELVIEWSTATE_H
#define BEDROCKMAP_VOXELVIEWSTATE_H

#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>
#include <QVector3D>

#include "model/voxelselection.h"

struct VoxelCameraState {
    float yaw{45.0f};
    float pitch{45.0f};
    float zoom{1.0f};
    QVector3D pan;
    bool orthoMode{false};

    [[nodiscard]] QMatrix4x4 matrix(const QVector3D& modelSize, const QSizeF& viewport) const;
};

/// Camera and selection interaction state for one voxel view.
class VoxelViewState {
   public:
    [[nodiscard]] const VoxelCameraState& camera() const { return camera_; }
    [[nodiscard]] const VoxelSelectionState& selection() const { return selection_; }

    [[nodiscard]] bool orthoMode() const { return camera_.orthoMode; }
    bool setOrthoMode(bool ortho);
    [[nodiscard]] bool rotationLocked() const { return rotation_locked_; }
    bool setRotationLocked(bool locked);

    bool setSelectionEnabled(bool enabled);
    [[nodiscard]] bool selectionMoveMode() const { return selection_move_mode_; }
    bool setSelectionMoveMode(bool enabled);
    bool setSelectionColor(const QColor& color);
    bool setSelectionBoundary(int axis, bool maximum, int value, const QVector3D& modelSize);
    bool setSelectionBounds(const QVector3D& minimum, const QVector3D& maximum, bool allowOutside, const QVector3D& modelSize);
    bool activateImportSelection(const QVector3D& size);
    bool deactivateImportSelection();

    bool beginSelectionDrag(const QPointF& position, qreal pickRadius, const QVector3D& modelSize, const QSizeF& viewport);
    bool dragSelection(const QPointF& position, const QVector3D& modelSize);
    bool endSelectionDrag();

    void resetForScene(const bl::block_box& bounds);
    void cancelSelectionDrag();

    void rotate(float yawDegrees, float pitchDegrees);
    void resetCamera();
    void focusFrontFace();
    void beginRotate(const QPointF& position);
    void beginPan(const QPointF& position);
    bool dragView(const QPointF& position, qreal viewportHeight);
    bool endView();
    bool zoomBy(qreal factor);

   private:
    [[nodiscard]] VoxelSelectionView selectionView(const QVector3D& modelSize, const QSizeF& viewport) const;
    [[nodiscard]] bool selectionAllowsOutside() const { return selection_locked_ || selection_allows_outside_; }

    VoxelCameraState camera_;
    VoxelSelectionState selection_;
    VoxelSelectionState selection_before_import_;
    VoxelSelectionDrag selection_drag_;
    QPointF last_mouse_position_;
    bool rotating_{false};
    bool panning_{false};
    bool rotation_locked_{false};
    bool selection_move_mode_{false};
    bool selection_move_mode_before_import_{false};
    bool selection_locked_{false};
    bool selection_allows_outside_{false};
};

#endif  // BEDROCKMAP_VOXELVIEWSTATE_H
