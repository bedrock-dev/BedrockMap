#include "model/voxelviewstate.h"

#include <QQuaternion>
#include <algorithm>
#include <cmath>

namespace {
    // The camera sits VIEW_DISTANCE units from the model with a 45-degree vertical FOV,
    // so the model's centre plane spans 2 * VIEW_HALF_HEIGHT units of the viewport height.
    constexpr float VIEW_DISTANCE = 64.0f;
    constexpr float VIEW_HALF_HEIGHT = 26.509668f;  // VIEW_DISTANCE * tan(22.5 degrees)
}  // namespace

QMatrix4x4 VoxelCameraState::matrix(const QVector3D& modelSize, const QSizeF& viewport) const {
    QMatrix4x4 projection;
    const float aspect = viewport.height() > 0 ? static_cast<float>(viewport.width() / viewport.height()) : 1.0f;
    if (orthoMode) {
        // Match the perspective view at the model's center plane.
        projection.ortho(-VIEW_HALF_HEIGHT * aspect, VIEW_HALF_HEIGHT * aspect, -VIEW_HALF_HEIGHT, VIEW_HALF_HEIGHT, 0.1f, 1000.0f);
    } else {
        projection.perspective(45.0f, aspect, 0.1f, 1000.0f);
    }
    QMatrix4x4 view;
    view.lookAt(QVector3D(0, 0, VIEW_DISTANCE), QVector3D(), QVector3D(0, 1, 0));
    const float fit = 24.0f / std::max({1.0f, modelSize.x(), modelSize.y(), modelSize.z()});
    QMatrix4x4 model;
    model.translate(pan);
    model.rotate(pitch, 1, 0, 0);
    model.rotate(yaw, 0, 1, 0);
    model.scale(std::max(0.05f, fit * zoom));
    model.translate(-modelSize * 0.5f);
    return projection * view * model;
}

VoxelSelectionView VoxelViewState::selectionView(const QVector3D& modelSize, const QSizeF& viewport) const {
    return {camera_.matrix(modelSize, viewport), viewport};
}

bool VoxelViewState::setOrthoMode(bool ortho) {
    if (camera_.orthoMode == ortho) return false;
    camera_.orthoMode = ortho;
    return true;
}

bool VoxelViewState::setRotationLocked(bool locked) {
    if (rotation_locked_ == locked) return false;
    rotation_locked_ = locked;
    if (locked) rotating_ = false;
    return true;
}

bool VoxelViewState::setSelectionEnabled(bool enabled) {
    if (selection_locked_ && !enabled) return false;
    if (selection_.enabled == enabled) return false;
    selection_.enabled = enabled;
    if (!enabled) selection_allows_outside_ = false;
    selection_.active = VoxelSelectionHandle::None;
    selection_drag_ = {};
    return true;
}

bool VoxelViewState::setSelectionMoveMode(bool enabled) {
    if (selection_locked_ && !enabled) return false;
    if (selection_move_mode_ == enabled) return false;
    selection_move_mode_ = enabled;
    selection_.active = VoxelSelectionHandle::None;
    selection_drag_ = {};
    return true;
}

bool VoxelViewState::setSelectionColor(const QColor& color) {
    if (!color.isValid() || selection_.color == color) return false;
    selection_.color = color;
    return true;
}

bool VoxelViewState::setSelectionBoundary(int axis, bool maximum, int value, const QVector3D& modelSize) {
    if (axis < 0 || axis > 2 || selection_locked_ || !selection_.enabled || !selection_.bounds.is_valid()) return false;
    const auto bounds = voxel_selection::setFace(selection_.bounds, axis, maximum, value, modelSize, selectionAllowsOutside());
    if (bounds.min_pos == selection_.bounds.min_pos && bounds.max_pos == selection_.bounds.max_pos) return false;
    selection_.bounds = bounds;
    selection_.active = VoxelSelectionHandle::None;
    selection_drag_ = {};
    return true;
}

bool VoxelViewState::setSelectionBounds(const QVector3D& minimum, const QVector3D& maximum, bool allowOutside, const QVector3D& modelSize) {
    if (selection_locked_ || !selection_.enabled) return false;
    auto bounds = bl::block_box{
        {qRound(minimum.x()), qRound(minimum.y()), qRound(minimum.z())},
        {qRound(maximum.x()), qRound(maximum.y()), qRound(maximum.z())},
    };
    if (!allowOutside) bounds = voxel_selection::clamp(bounds, modelSize);
    if (!bounds.is_valid()) return false;
    selection_allows_outside_ = allowOutside;
    selection_.bounds = bounds;
    selection_.active = VoxelSelectionHandle::None;
    selection_drag_ = {};
    return true;
}

bool VoxelViewState::activateImportSelection(const QVector3D& size) {
    if (size.x() <= 0.0f || size.y() <= 0.0f || size.z() <= 0.0f) return false;
    if (!selection_locked_) {
        selection_before_import_ = selection_;
        selection_move_mode_before_import_ = selection_move_mode_;
    }
    selection_locked_ = true;
    selection_move_mode_ = true;
    selection_.enabled = true;
    selection_.active = VoxelSelectionHandle::None;
    const auto origin = selection_.bounds.is_valid() ? selection_.bounds.min_pos : bl::block_pos{};
    selection_.bounds = bl::block_box::from_min_and_size(origin, qRound(size.x()), qRound(size.y()), qRound(size.z()));
    selection_drag_ = {};
    return true;
}

bool VoxelViewState::deactivateImportSelection() {
    if (!selection_locked_) return false;
    selection_ = selection_before_import_;
    selection_move_mode_ = selection_move_mode_before_import_;
    selection_locked_ = false;
    selection_drag_ = {};
    return true;
}

bool VoxelViewState::beginSelectionDrag(const QPointF& position, qreal pickRadius, const QVector3D& modelSize, const QSizeF& viewport) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()) || !std::isfinite(pickRadius) || pickRadius <= 0 ||
        viewport.isEmpty() || !selection_.enabled)
        return false;
    const auto drag = VoxelSelectionDrag::begin(selection_.bounds, modelSize, selectionView(modelSize, viewport), position,
                                                selection_move_mode_ || selection_locked_, static_cast<float>(pickRadius));
    if (drag.handle == VoxelSelectionHandle::None) return false;
    selection_drag_ = drag;
    selection_.active = drag.handle;
    rotating_ = false;
    panning_ = false;
    return true;
}

bool VoxelViewState::dragSelection(const QPointF& position, const QVector3D& modelSize) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y()) || !selection_.enabled ||
        selection_.active == VoxelSelectionHandle::None)
        return false;
    const auto bounds = selection_drag_.update(modelSize, position, selectionAllowsOutside());
    if (!bounds.is_valid() || (bounds.min_pos == selection_.bounds.min_pos && bounds.max_pos == selection_.bounds.max_pos)) return false;
    selection_.bounds = bounds;
    return true;
}

bool VoxelViewState::endSelectionDrag() {
    if (selection_.active == VoxelSelectionHandle::None) return false;
    selection_.active = VoxelSelectionHandle::None;
    selection_drag_ = {};
    return true;
}

void VoxelViewState::resetForScene(const bl::block_box& bounds) {
    selection_.active = VoxelSelectionHandle::None;
    selection_drag_ = {};
    selection_.bounds = bounds;
    resetCamera();
    rotating_ = false;
    panning_ = false;
}

void VoxelViewState::cancelSelectionDrag() {
    selection_.active = VoxelSelectionHandle::None;
    selection_drag_ = {};
}

void VoxelViewState::rotate(float yawDegrees, float pitchDegrees) {
    if (!std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees)) return;
    // Explicit buttons remain available while drag rotation is locked,
    // matching the native preview's controls.
    camera_.yaw = std::remainder(camera_.yaw + yawDegrees, 360.0f);
    camera_.pitch = std::remainder(camera_.pitch + pitchDegrees, 360.0f);
}

void VoxelViewState::resetCamera() {
    // Orthographic mode is a user preference, so it survives a reset.
    camera_ = VoxelCameraState{.orthoMode = camera_.orthoMode};
}

void VoxelViewState::focusFrontFace() {
    const QQuaternion current = QQuaternion::fromAxisAndAngle(1, 0, 0, camera_.pitch) * QQuaternion::fromAxisAndAngle(0, 1, 0, camera_.yaw);
    const QVector3D axes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    QVector3D front;
    float facing = -2.0f;
    for (const auto& axis : axes) {
        const float z = current.rotatedVector(axis).z();
        if (z > facing) {
            facing = z;
            front = axis;
        }
    }
    // Pick the nearest axis-aligned yaw/pitch pose that faces the camera.
    // This preserves the roll-free orbit used by dragging and arrow buttons.
    float bestDot = -1.0f;
    for (const float pitch : {-180.0f, -90.0f, 0.0f, 90.0f}) {
        for (const float yaw : {-180.0f, -90.0f, 0.0f, 90.0f}) {
            const QQuaternion candidate = QQuaternion::fromAxisAndAngle(1, 0, 0, pitch) * QQuaternion::fromAxisAndAngle(0, 1, 0, yaw);
            if (candidate.rotatedVector(front).z() < 0.999f) continue;
            const float dot = std::abs(QQuaternion::dotProduct(current, candidate));
            if (dot > bestDot) {
                bestDot = dot;
                camera_.yaw = yaw;
                camera_.pitch = pitch;
            }
        }
    }
}

void VoxelViewState::beginRotate(const QPointF& position) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y())) return;
    last_mouse_position_ = position;
    rotating_ = !rotation_locked_;
    panning_ = false;
}

void VoxelViewState::beginPan(const QPointF& position) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y())) return;
    last_mouse_position_ = position;
    panning_ = true;
    rotating_ = false;
}

bool VoxelViewState::dragView(const QPointF& position, qreal viewportHeight) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y())) return false;
    const QPointF delta = position - last_mouse_position_;
    last_mouse_position_ = position;
    if (rotating_ && !rotation_locked_) {
        camera_.yaw = std::remainder(camera_.yaw + static_cast<float>(delta.x()) * 0.5f, 360.0f);
        camera_.pitch = std::remainder(camera_.pitch + static_cast<float>(delta.y()) * 0.5f, 360.0f);
        return true;
    }
    if (!panning_) return false;
    const float worldPerPixel = 2.0f * VIEW_HALF_HEIGHT / static_cast<float>(std::max(1.0, viewportHeight));
    camera_.pan.setX(camera_.pan.x() + static_cast<float>(delta.x()) * worldPerPixel);
    camera_.pan.setY(camera_.pan.y() - static_cast<float>(delta.y()) * worldPerPixel);
    return true;
}

bool VoxelViewState::endView() {
    if (!rotating_ && !panning_) return false;
    rotating_ = false;
    panning_ = false;
    return true;
}

bool VoxelViewState::zoomBy(qreal factor) {
    if (!std::isfinite(factor) || factor <= 0.0) return false;
    camera_.zoom = std::clamp(camera_.zoom * static_cast<float>(factor), 0.1f, 10.0f);
    return true;
}
