#ifndef BEDROCKMAP_VOXELSELECTION_H
#define BEDROCKMAP_VOXELSELECTION_H

#include <QColor>
#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector3D>

#include "geometry.h"

enum class VoxelSelectionHandle { None, MinX, MaxX, MinY, MaxY, MinZ, MaxZ };

/// Projection input shared by voxel selection hit testing and rendering.
struct VoxelSelectionView {
    QMatrix4x4 matrix;  // OpenGL clip space, before the backend's clip correction.
    QSizeF viewport;    // Logical pixels, independent of the render target's DPR.

    [[nodiscard]] QPointF project(const QVector3D& point, bool* visible = nullptr) const;
    [[nodiscard]] float handleHalfSize(const QVector3D& point) const;
};

/// Transient state for one handle drag.
struct VoxelSelectionDrag {
    VoxelSelectionHandle handle{VoxelSelectionHandle::None};
    QPointF start;
    QPointF screenAxis;
    bl::block_box bounds;
    bool moveMode{false};

    [[nodiscard]] static VoxelSelectionDrag begin(const bl::block_box& bounds, const QVector3D& modelSize, const VoxelSelectionView& view,
                                                  const QPointF& position, bool moveMode, float pickRadius = 10.8f);
    [[nodiscard]] bl::block_box update(const QVector3D& modelSize, const QPointF& position, bool allowOutside) const;
};

struct VoxelSelectionState {
    bl::block_box bounds;
    QColor color{QStringLiteral("#e641b9dc")};
    VoxelSelectionHandle active{VoxelSelectionHandle::None};
    bool enabled{false};
};

namespace voxel_selection {
    [[nodiscard]] int handleAxisIndex(VoxelSelectionHandle handle);
    [[nodiscard]] QVector3D handlePosition(const bl::block_box& bounds, VoxelSelectionHandle handle);

    /// Move one face of the box to value, keeping it at least one voxel thick.
    [[nodiscard]] bl::block_box setFace(bl::block_box bounds, int axis, bool maximum, int value, const QVector3D& modelSize,
                                        bool allowOutside);
    [[nodiscard]] bl::block_box clamp(bl::block_box bounds, const QVector3D& modelSize);
}  // namespace voxel_selection

#endif  // BEDROCKMAP_VOXELSELECTION_H
