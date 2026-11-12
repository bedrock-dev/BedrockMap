#include "render/voxel_scene.h"

#include <utility>

std::shared_ptr<VoxelSceneData> makeVoxelScene(VoxelGrid grid, const QVector3D& origin, const QVector3D& modelSize) {
    const auto bounds = voxel_mesh::fullBounds(grid);
    if (!bounds) return {};
    auto scene = std::make_shared<VoxelSceneData>();
    scene->mesh = voxel_mesh::build(grid, *bounds);
    scene->voxels = std::make_shared<const VoxelGrid>(std::move(grid));
    scene->modelSize = modelSize;
    scene->origin = origin;
    return scene;
}
