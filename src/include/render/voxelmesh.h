#ifndef BEDROCKMAP_VOXEL_MESH_H
#define BEDROCKMAP_VOXEL_MESH_H

#include <QColor>
#include <QString>
#include <QUrl>
#include <QVector3D>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "geometry.h"
#include "model/voxelselection.h"

namespace bl {
    class chunk;
    class mcstructure;
}  // namespace bl

/// Dense voxel volume stored as one QRgb per cell. Alpha 0 is empty and
/// alpha below 255 is transparent.
class VoxelGrid {
   public:
    VoxelGrid() = default;
    VoxelGrid(int sizeX, int sizeY, int sizeZ)
        : size_x_(sizeX), size_y_(sizeY), size_z_(sizeZ), cells_(static_cast<std::size_t>(sizeX) * sizeY * sizeZ, 0) {}

    [[nodiscard]] static VoxelGrid fromMcstructure(const bl::mcstructure& structure);
    // Combine chunks indexed by [X][Z] before meshing so face culling and AO
    // see adjacent chunks. Trim only empty outer Y layers; preserve internal gaps.
    [[nodiscard]] static VoxelGrid fromChunks(const std::vector<std::vector<bl::chunk*>>& chunks,
                                              const std::function<void(int)>& reportProgress, int* firstWorldY = nullptr);

    [[nodiscard]] int sizeX() const { return size_x_; }
    [[nodiscard]] int sizeY() const { return size_y_; }
    [[nodiscard]] int sizeZ() const { return size_z_; }
    [[nodiscard]] bool empty() const { return cells_.empty(); }
    [[nodiscard]] std::optional<bl::block_box> bounds() const {
        if (empty()) return std::nullopt;
        return bl::block_box::from_min_and_size({0, 0, 0}, size_x_, size_y_, size_z_);
    }
    [[nodiscard]] bool contains(int x, int y, int z) const {
        return x >= 0 && x < size_x_ && y >= 0 && y < size_y_ && z >= 0 && z < size_z_;
    }
    [[nodiscard]] QRgb at(int x, int y, int z) const { return cells_[index(x, y, z)]; }
    void set(int x, int y, int z, QRgb color) { cells_[index(x, y, z)] = color; }
    /// Drop empty bottom and top layers, keeping internal gaps. Returns how many
    /// bottom layers were removed; an all-empty grid becomes empty.
    int trimEmptyY();

   private:
    [[nodiscard]] std::size_t index(int x, int y, int z) const { return (static_cast<std::size_t>(y) * size_x_ + x) * size_z_ + z; }

    int size_x_{0};
    int size_y_{0};
    int size_z_{0};
    std::vector<QRgb> cells_;
};

/// Vertex layout shared by every voxel pipeline: pos3 + normal3 + rgba4.
inline constexpr int VOXEL_VERTEX_FLOATS = 10;

struct VoxelMeshBuffers {
    std::vector<float> opaqueVertices;
    std::vector<std::uint32_t> opaqueIndices;
    std::vector<float> transparentVertices;
    std::vector<std::uint32_t> transparentIndices;

    [[nodiscard]] static VoxelMeshBuffers fromGrid(const VoxelGrid& grid, const bl::block_box& bounds);
    /// mergeFaces merges coplanar faces of equal color, which shrinks the file
    /// but leaves T-junctions where merged and unit faces meet.
    [[nodiscard]] bool exportGlb(const QUrl& url, bool mergeFaces, QString* errorMessage = nullptr) const;
};

struct VoxelHsaArea {
    bl::block_box bounds;
    QColor color;
};

/// Non-indexed overlay geometry in the voxel vertex layout (normals are zero):
/// fillCount triangle vertices, then lineCount line vertices, then the rest
/// (selection handle triangles).
struct VoxelOverlayMeshBuffers {
    std::vector<float> vertices;
    std::uint32_t fillCount{0};
    std::uint32_t lineCount{0};

    /// Colored X/Y/Z lines from the model origin (lineCount only).
    [[nodiscard]] static VoxelOverlayMeshBuffers fromAxes(const QVector3D& modelSize);
    [[nodiscard]] static VoxelOverlayMeshBuffers fromSelection(const bl::block_box& bounds, const VoxelSelectionView& view,
                                                               const QColor& color, VoxelSelectionHandle active);
    [[nodiscard]] static VoxelOverlayMeshBuffers fromHsaAreas(const std::vector<VoxelHsaArea>& areas);

    [[nodiscard]] std::uint32_t vertexCount() const { return static_cast<std::uint32_t>(vertices.size() / VOXEL_VERTEX_FLOATS); }
};

/// Immutable result of one load, handed from a worker to the controller and the
/// QML voxel renderer. Shared because the render thread and export workers keep
/// using a scene after the controller has moved on to a newer one.
struct VoxelSceneData {
    VoxelMeshBuffers mesh;
    VoxelGrid voxels;
    QVector3D modelSize;
    QVector3D origin;

    /// Build a scene sized to the populated grid. Returns null for an empty grid.
    [[nodiscard]] static std::shared_ptr<VoxelSceneData> fromGrid(VoxelGrid grid, const QVector3D& origin);
};

#endif  // BEDROCKMAP_VOXEL_MESH_H
