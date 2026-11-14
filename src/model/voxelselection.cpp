#include "model/voxelselection.h"

#include <QVector4D>
#include <algorithm>
#include <cmath>

namespace {
    constexpr float SELECTION_HANDLE_SCREEN_SIZE_PX = 8.0f;

    [[nodiscard]] int modelExtent(const QVector3D& modelSize, int axis) { return static_cast<int>(modelSize[axis]); }

    [[nodiscard]] float pixelsPerWorldUnitAt(const VoxelSelectionView& view, const QVector3D& point) {
        bool originVisible = false;
        const QPointF origin = view.project(point, &originVisible);
        if (!originVisible) return 0.0f;

        float totalPixelsPerWorld = 0.0f;
        int sampleCount = 0;
        for (int axis = 0; axis < 3; ++axis) {
            QVector3D step;
            step[axis] = 1.0f;
            bool axisVisible = false;
            const QPointF projected = view.project(point + step, &axisVisible);
            if (!axisVisible) continue;
            const float dx = static_cast<float>(projected.x() - origin.x());
            const float dy = static_cast<float>(projected.y() - origin.y());
            const float pixels = std::sqrt(dx * dx + dy * dy);
            if (pixels <= 1e-3f) continue;
            totalPixelsPerWorld += pixels;
            ++sampleCount;
        }
        return sampleCount == 0 ? 0.0f : totalPixelsPerWorld / static_cast<float>(sampleCount);
    }
}  // namespace

QPointF VoxelSelectionView::project(const QVector3D& point, bool* visible) const {
    const QVector4D clip = matrix * QVector4D(point, 1.0f);
    if (std::abs(clip.w()) < 1e-6f) {
        if (visible) *visible = false;
        return {};
    }
    const QVector3D ndc = clip.toVector3D() / clip.w();
    if (visible) *visible = clip.w() > 0.0f && ndc.z() >= -1.0f && ndc.z() <= 1.0f;
    return {(ndc.x() + 1.0f) * 0.5f * viewport.width(), (1.0f - ndc.y()) * 0.5f * viewport.height()};
}

float VoxelSelectionView::handleHalfSize(const QVector3D& point) const {
    const float pixelsPerWorld = pixelsPerWorldUnitAt(*this, point);
    return pixelsPerWorld <= 1e-3f ? 0.20f : (SELECTION_HANDLE_SCREEN_SIZE_PX * 0.5f) / pixelsPerWorld;
}

namespace voxel_selection {
    int handleAxisIndex(VoxelSelectionHandle handle) {
        return handle == VoxelSelectionHandle::None ? -1 : (static_cast<int>(handle) - 1) / 2;
    }

    QVector3D handlePosition(const bl::block_box& bounds, VoxelSelectionHandle handle) {
        const QVector3D minimum(bounds.min_pos.x, bounds.min_pos.y, bounds.min_pos.z);
        const QVector3D maximum(bounds.max_pos.x, bounds.max_pos.y, bounds.max_pos.z);
        QVector3D position = (minimum + maximum) * 0.5f;
        const int axis = handleAxisIndex(handle);
        if (axis >= 0) position[axis] = static_cast<int>(handle) % 2 == 1 ? minimum[axis] : maximum[axis];
        return position;
    }

    bl::block_box clamp(bl::block_box bounds, const QVector3D& modelSize) {
        int* values[] = {&bounds.min_pos.x, &bounds.min_pos.y, &bounds.min_pos.z, &bounds.max_pos.x, &bounds.max_pos.y, &bounds.max_pos.z};
        for (int i = 0; i < 6; ++i) *values[i] = std::clamp(*values[i], 0, modelExtent(modelSize, i % 3));
        return bounds;
    }

    bl::block_box setFace(bl::block_box bounds, int axis, bool maximum, int value, const QVector3D& modelSize, bool allowOutside) {
        if (axis < 0 || axis > 2) return bounds;
        int* low[] = {&bounds.min_pos.x, &bounds.min_pos.y, &bounds.min_pos.z};
        int* high[] = {&bounds.max_pos.x, &bounds.max_pos.y, &bounds.max_pos.z};
        if (maximum)
            *high[axis] = allowOutside ? std::max(value, *low[axis] + 1)
                                       : std::clamp(value, *low[axis] + 1, std::max(*low[axis] + 1, modelExtent(modelSize, axis)));
        else
            *low[axis] = allowOutside ? std::min(value, *high[axis] - 1) : std::clamp(value, std::min(0, *high[axis] - 1), *high[axis] - 1);
        return bounds;
    }
}  // namespace voxel_selection

VoxelSelectionDrag VoxelSelectionDrag::begin(const bl::block_box& bounds, const QVector3D& modelSize, const VoxelSelectionView& view,
                                             const QPointF& position, bool moveMode, float pickRadius) {
    VoxelSelectionDrag drag;
    if (!bounds.is_valid() || view.viewport.isEmpty()) return drag;
    float bestDistance = pickRadius * pickRadius;
    for (int index = 1; index <= 6; ++index) {
        const auto handle = static_cast<VoxelSelectionHandle>(index);
        bool visible = false;
        const QPointF point = view.project(voxel_selection::handlePosition(bounds, handle), &visible);
        const QPointF delta = position - point;
        const float distance = static_cast<float>(QPointF::dotProduct(delta, delta));
        if (visible && distance <= bestDistance) {
            bestDistance = distance;
            drag.handle = handle;
        }
    }
    if (drag.handle == VoxelSelectionHandle::None) return drag;
    drag.start = position;
    drag.bounds = bounds;
    drag.moveMode = moveMode;
    const int axis = voxel_selection::handleAxisIndex(drag.handle);
    QVector3D step;
    step[axis] = 1.0f;
    const QVector3D point = voxel_selection::handlePosition(bounds, moveMode ? VoxelSelectionHandle::None : drag.handle);
    const auto projectedStep = [&](float count) { return view.project(point + step * count) - view.project(point); };
    drag.screenAxis = projectedStep(1.0f);
    // Native resize fallback for axes that are almost parallel to the camera.
    // Move mode intentionally has no fallback: a hidden axis cannot be moved.
    if (!moveMode && QPointF::dotProduct(drag.screenAxis, drag.screenAxis) < 4.0) {
        const float count = std::clamp(modelSize[axis], 1.0f, 256.0f);
        const QPointF sampled = projectedStep(count);
        drag.screenAxis = QPointF::dotProduct(sampled, sampled) > 1e-6 ? sampled / count : QPointF(0.0, -12.0);
    }
    return drag;
}

bl::block_box VoxelSelectionDrag::update(const QVector3D& modelSize, const QPointF& position, bool allowOutside) const {
    auto result = bounds;
    const int axis = voxel_selection::handleAxisIndex(handle);
    if (axis < 0 || !result.is_valid()) return result;
    const float lengthSquared = static_cast<float>(QPointF::dotProduct(screenAxis, screenAxis));
    if (lengthSquared < (moveMode ? 1e-6f : 1e-10f)) return result;
    const float delta = static_cast<float>(QPointF::dotProduct(position - start, screenAxis) / lengthSquared);
    int* low[] = {&result.min_pos.x, &result.min_pos.y, &result.min_pos.z};
    int* high[] = {&result.max_pos.x, &result.max_pos.y, &result.max_pos.z};
    if (moveMode) {
        const int span = *high[axis] - *low[axis];
        const int minimum = *low[axis] + static_cast<int>(std::round(delta));
        *low[axis] = allowOutside ? minimum : std::clamp(minimum, 0, std::max(0, modelExtent(modelSize, axis) - span));
        *high[axis] = *low[axis] + span;
        return result;
    }
    const bool maximum = static_cast<int>(handle) % 2 == 0;
    const int value = qRound(static_cast<float>(maximum ? *high[axis] : *low[axis]) + delta);
    return voxel_selection::setFace(result, axis, maximum, value, modelSize, allowOutside);
}
