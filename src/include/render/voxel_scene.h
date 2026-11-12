#ifndef BEDROCKMAP_VOXEL_SCENE_H
#define BEDROCKMAP_VOXEL_SCENE_H

#include <QVector3D>
#include <memory>

#include "render/voxel_mesh.h"

/// Immutable CPU mesh handed from a worker to the QML voxel renderer.
struct VoxelSceneData {
    VoxelMeshBuffers mesh;
    // Retain the source volume so exports can rebuild faces exposed by a selection.
    std::shared_ptr<const VoxelGrid> voxels;
    QVector3D modelSize;
    QVector3D origin;
};

/// Mesh a voxel grid into a scene. Returns null when the grid is empty.
[[nodiscard]] std::shared_ptr<VoxelSceneData> makeVoxelScene(VoxelGrid grid, const QVector3D& origin, const QVector3D& modelSize);

#endif  // BEDROCKMAP_VOXEL_SCENE_H
