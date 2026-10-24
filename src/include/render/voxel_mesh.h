#ifndef BEDROCKMAP_VOXEL_MESH_H
#define BEDROCKMAP_VOXEL_MESH_H

#include <QColor>
#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector3D>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "geometry.h"

namespace bl {
    class chunk;
    class mcstructure;
}

struct Voxel {
    QColor color;
    bool transparent{false};

    Voxel() : color(Qt::white) {}
    Voxel(const QColor& value, bool isTransparent = false) : color(value), transparent(isTransparent) {}
};

using VoxelGrid = std::vector<std::vector<std::vector<Voxel>>>;

enum class VoxelMeshOcclusionMode { RenderView, OccupiedVoxelShell };

struct VoxelMeshBuffers {
    std::vector<float> opaqueVertices;
    std::vector<std::uint32_t> opaqueIndices;
    std::vector<float> transparentVertices;
    std::vector<std::uint32_t> transparentIndices;
};

enum class VoxelSelectionHandle { None, MinX, MaxX, MinY, MaxY, MinZ, MaxZ };

struct VoxelSelectionView {
    QMatrix4x4 matrix;  // OpenGL clip space, before the backend's clip correction.
    QSizeF viewport;   // Logical pixels, independent of the render target's DPR.
    float voxelSize{1.0f};
};

struct VoxelSelectionDrag {
    VoxelSelectionHandle handle{VoxelSelectionHandle::None};
    QPointF start;
    QPointF screenAxis;
    bl::block_box bounds;
    bool moveMode{false};
};

struct VoxelSelectionMesh {
    std::vector<float> vertices;  // pos3 + rgba4; fill, lines, then handle triangles.
    std::uint32_t fillCount{0};
    std::uint32_t lineCount{0};
};

namespace voxel_mesh {
    [[nodiscard]] bool exportGlb(const VoxelMeshBuffers& mesh, const QString& filePath, QString* errorMessage = nullptr);
    [[nodiscard]] VoxelGrid createVoxelDataFromMcstructure(const bl::mcstructure& structure);
    [[nodiscard]] QPointF projectSelectionPoint(const VoxelSelectionView& view, const QVector3D& point,
                                                 bool* visible = nullptr);
    [[nodiscard]] QVector3D selectionHandlePosition(const bl::block_box& bounds, VoxelSelectionHandle handle,
                                                    float voxelSize = 1.0f);
    [[nodiscard]] int selectionHandleAxisIndex(VoxelSelectionHandle handle);
    [[nodiscard]] VoxelSelectionDrag beginSelectionDrag(const bl::block_box& bounds, const QVector3D& modelSize,
                                                        const VoxelSelectionView& view, const QPointF& position,
                                                        bool moveMode, float pickRadius = 10.8f);
    [[nodiscard]] bl::block_box dragSelection(const VoxelSelectionDrag& drag, const QVector3D& modelSize,
                                              const QPointF& position, bool locked = false);
    [[nodiscard]] bl::block_box clampSelection(bl::block_box bounds, const QVector3D& modelSize, bool locked = false);
    [[nodiscard]] VoxelSelectionMesh buildSelectionMesh(const bl::block_box& bounds, const VoxelSelectionView& view,
                                                        const QColor& color, VoxelSelectionHandle active);
    // Combine chunks indexed by [X][Z] before meshing so face culling and AO
    // see adjacent chunks. Trim only empty outer Y layers; preserve internal gaps.
    [[nodiscard]] VoxelGrid createVoxelDataFromChunks(const std::vector<std::vector<bl::chunk*>>& chunks,
                                                      const std::function<void(int)>& reportProgress,
                                                      int* firstWorldY = nullptr);
    [[nodiscard]] std::optional<bl::block_box> fullBounds(const VoxelGrid& grid);
    [[nodiscard]] bool hasNeighborInBounds(const VoxelGrid& grid, int layer, int x, int z, int dy, int dx, int dz,
                                           const bl::block_box& bounds, VoxelMeshOcclusionMode mode);
    [[nodiscard]] std::array<float, 4> faceOcclusionFactors(const VoxelGrid& grid, const bl::block_box& bounds, int layer, int x,
                                                            int z, const std::vector<float>& faceVertices,
                                                            const QVector3D& normal, VoxelMeshOcclusionMode mode);
    void addFaceVerticesToBuffers(int layer, int x, int z, const Voxel& voxel, const std::vector<float>& faceVertices,
                                  const QVector3D& normal, const std::array<float, 4>& occlusion,
                                  std::vector<float>& vertices, std::vector<std::uint32_t>& indices, float voxelSize,
                                  float alphaScale = 1.0f);
    void appendVisibleVoxelMesh(const VoxelGrid& grid, const bl::block_box& bounds, std::vector<float>& vertices,
                                std::vector<std::uint32_t>& indices, std::vector<float>* transparentVertices = nullptr,
                                std::vector<std::uint32_t>* transparentIndices = nullptr,
                                VoxelMeshOcclusionMode mode = VoxelMeshOcclusionMode::RenderView,
                                float alphaScale = 1.0f, float voxelSize = 1.0f);
    [[nodiscard]] VoxelMeshBuffers build(const VoxelGrid& grid, const bl::block_box& bounds,
                                         VoxelMeshOcclusionMode mode = VoxelMeshOcclusionMode::RenderView,
                                         float alphaScale = 1.0f, float voxelSize = 1.0f);
}  // namespace voxel_mesh

#endif  // BEDROCKMAP_VOXEL_MESH_H
