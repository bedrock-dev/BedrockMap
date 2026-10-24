#ifndef BEDROCKMAP_VOXEL_SCENE_H
#define BEDROCKMAP_VOXEL_SCENE_H

#include <QVector3D>
#include <cstdint>
#include <memory>
#include <vector>

#include "render/voxel_mesh.h"

/// Immutable CPU mesh handed from a worker to the QML voxel renderer.
struct VoxelSceneData {
    std::vector<float> opaqueVertices;
    std::vector<std::uint32_t> opaqueIndices;
    std::vector<float> transparentVertices;
    std::vector<std::uint32_t> transparentIndices;
    // Retain the source volume so exports can rebuild faces exposed by a selection.
    std::shared_ptr<const VoxelGrid> voxels;
    QVector3D modelSize;
    QVector3D origin;
    std::uint64_t revision{0};
};

#endif  // BEDROCKMAP_VOXEL_SCENE_H
