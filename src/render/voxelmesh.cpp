#include "render/voxelmesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "chunk.h"
#include "color.h"
#include "mcstructure.h"

int VoxelGrid::trimEmptyY() {
    const auto layer = static_cast<std::ptrdiff_t>(size_x_) * size_z_;
    const auto occupied = [&](int y) {
        const auto first = cells_.begin() + layer * y;
        return std::any_of(first, first + layer, [](QRgb cell) { return qAlpha(cell) != 0; });
    };
    int firstY = 0;
    while (firstY < size_y_ && !occupied(firstY)) ++firstY;
    int lastY = size_y_;
    while (lastY > firstY && !occupied(lastY - 1)) --lastY;
    if (firstY == lastY) {
        *this = VoxelGrid{};
        return firstY;
    }
    cells_.erase(cells_.begin() + layer * lastY, cells_.end());
    cells_.erase(cells_.begin(), cells_.begin() + layer * firstY);
    size_y_ = lastY - firstY;
    return firstY;
}

namespace {
    const QColor AXIS_COLORS[] = {QColor(255, 60, 60), QColor(60, 255, 60), QColor(60, 60, 255)};

    // Faces and edges index the corners returned by boxCorners().
    constexpr int BOX_FACES[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {3, 7, 6, 2}, {0, 1, 5, 4}};
    constexpr int BOX_EDGES[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

    using BoxCorners = std::array<QVector3D, 8>;

    BoxCorners boxCorners(const QVector3D& minimum, const QVector3D& maximum) {
        return {{
            {minimum.x(), minimum.y(), minimum.z()},
            {maximum.x(), minimum.y(), minimum.z()},
            {maximum.x(), maximum.y(), minimum.z()},
            {minimum.x(), maximum.y(), minimum.z()},
            {minimum.x(), minimum.y(), maximum.z()},
            {maximum.x(), minimum.y(), maximum.z()},
            {maximum.x(), maximum.y(), maximum.z()},
            {minimum.x(), maximum.y(), maximum.z()},
        }};
    }

    BoxCorners boxCorners(const bl::block_box& bounds) {
        return boxCorners(QVector3D(bounds.min_pos.x, bounds.min_pos.y, bounds.min_pos.z),
                          QVector3D(bounds.max_pos.x, bounds.max_pos.y, bounds.max_pos.z));
    }

    void appendOverlayVertex(std::vector<float>& vertices, const QVector3D& position, const QColor& color) {
        vertices.insert(vertices.end(), {position.x(), position.y(), position.z(), 0.0f, 0.0f, 0.0f, color.redF(), color.greenF(),
                                         color.blueF(), color.alphaF()});
    }

    void appendBoxFill(std::vector<float>& vertices, const BoxCorners& p, const QColor& color) {
        for (const auto& face : BOX_FACES) {
            for (const int corner : {face[0], face[1], face[2], face[0], face[2], face[3]}) appendOverlayVertex(vertices, p[corner], color);
        }
    }

    void appendBoxEdges(std::vector<float>& vertices, const BoxCorners& p, const QColor& color) {
        for (const auto& edge : BOX_EDGES) {
            appendOverlayVertex(vertices, p[edge[0]], color);
            appendOverlayVertex(vertices, p[edge[1]], color);
        }
    }

    constexpr float AO_BRIGHTNESS[4] = {0.68f, 0.80f, 0.91f, 1.0f};
    // Four corners (x, y, z) per face of a unit voxel centred on the origin.
    constexpr float FACE_TEMPLATES[6][4][3] = {
        {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}},
        {{-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}},
        {{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}},
        {{0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}},
        {{-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}},
        {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}},
    };
    // Outward normal (x, y, z) of each face, also the neighbor offset used for culling.
    constexpr int FACE_NORMALS[6][3] = {{0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};

    bool isTransparent(QRgb color) { return qAlpha(color) < 255; }

    /// bounds must lie inside the grid; VoxelMeshBuffers::fromGrid() intersects them first.
    bool occludedBy(const VoxelGrid& grid, const bl::block_box& bounds, QRgb current, int x, int y, int z) {
        if (x < bounds.min_pos.x || x >= bounds.max_pos.x || y < bounds.min_pos.y || y >= bounds.max_pos.y || z < bounds.min_pos.z ||
            z >= bounds.max_pos.z)
            return false;
        const QRgb neighbor = grid.at(x, y, z);
        if (qAlpha(neighbor) == 0) return false;
        // Any voxel hides a transparent face; only opaque voxels hide an opaque one.
        return isTransparent(current) || !isTransparent(neighbor);
    }

    std::array<float, 4> faceOcclusionFactors(const VoxelGrid& grid, const bl::block_box& bounds, QRgb current, int x, int y, int z,
                                              int face) {
        const int* normal = FACE_NORMALS[face];
        const int normalAxis = normal[0] != 0 ? 0 : (normal[1] != 0 ? 1 : 2);
        const int tangentAxes[2] = {(normalAxis + 1) % 3, (normalAxis + 2) % 3};
        const auto occludes = [&](const int offset[3]) {
            return occludedBy(grid, bounds, current, x + offset[0], y + offset[1], z + offset[2]);
        };
        std::array<float, 4> result{};
        for (int i = 0; i < 4; ++i) {
            const float* corner = FACE_TEMPLATES[face][i];
            const int sign1 = corner[tangentAxes[0]] > 0.0f ? 1 : -1;
            const int sign2 = corner[tangentAxes[1]] > 0.0f ? 1 : -1;
            int side1[3] = {normal[0], normal[1], normal[2]};
            int side2[3] = {normal[0], normal[1], normal[2]};
            int diagonal[3] = {normal[0], normal[1], normal[2]};
            side1[tangentAxes[0]] += sign1;
            side2[tangentAxes[1]] += sign2;
            diagonal[tangentAxes[0]] += sign1;
            diagonal[tangentAxes[1]] += sign2;
            const bool first = occludes(side1);
            const bool second = occludes(side2);
            const bool cornered = occludes(diagonal);
            const int level = (first && second) ? 0 : 3 - (static_cast<int>(first) + static_cast<int>(second) + static_cast<int>(cornered));
            result[i] = AO_BRIGHTNESS[level];
        }
        return result;
    }

    void appendFace(int x, int y, int z, QRgb color, int face, const std::array<float, 4>& occlusion, std::vector<float>& vertices,
                    std::vector<std::uint32_t>& indices) {
        const auto vertexOffset = static_cast<std::uint32_t>(vertices.size() / VOXEL_VERTEX_FLOATS);
        const float red = static_cast<float>(qRed(color)) / 255.0f;
        const float green = static_cast<float>(qGreen(color)) / 255.0f;
        const float blue = static_cast<float>(qBlue(color)) / 255.0f;
        const float alpha = static_cast<float>(qAlpha(color)) / 255.0f;
        const int* normal = FACE_NORMALS[face];
        for (int i = 0; i < 4; ++i) {
            const float* corner = FACE_TEMPLATES[face][i];
            vertices.insert(vertices.end(),
                            {corner[0] + static_cast<float>(x) + 0.5f, corner[1] + static_cast<float>(y) + 0.5f,
                             corner[2] + static_cast<float>(z) + 0.5f, static_cast<float>(normal[0]), static_cast<float>(normal[1]),
                             static_cast<float>(normal[2]), red * occlusion[i], green * occlusion[i], blue * occlusion[i], alpha});
        }
        // Split the quad along the diagonal with the brighter ends to avoid AO creases.
        const bool flipDiagonal = occlusion[0] + occlusion[2] > occlusion[1] + occlusion[3];
        const std::uint32_t quad[6] = {0, 1, flipDiagonal ? 3u : 2u, flipDiagonal ? 1u : 0u, 2, 3};
        for (const auto index : quad) indices.push_back(index + vertexOffset);
    }
}  // namespace

VoxelGrid VoxelGrid::fromMcstructure(const bl::mcstructure& structure) {
    const int sx = structure.size_x();
    const int sy = structure.size_y();
    const int sz = structure.size_z();
    if (sx <= 0 || sy <= 0 || sz <= 0) return {};

    VoxelGrid grid(sx, sy, sz);
    if (structure.palette_size() == 0) return grid;
    for (int x = 0; x < sx; ++x) {
        for (int y = 0; y < sy; ++y) {
            for (int z = 0; z < sz; ++z) {
                const auto* block = structure.block_at(x, y, z);
                if (!block || block->name == "minecraft:air" || block->name == "minecraft:unknown") continue;
                const auto baseColor = bl::get_block_by_name_tag(block->name);
                // mcstructure files do not carry biome data; use plains as a stable preview biome.
                const auto c = bl::blend_color_with_biome(block->name, baseColor, bl::biome::plains);
                grid.set(x, y, z, qRgba(c.r, c.g, c.b, c.a));
            }
        }
    }
    return grid;
}

VoxelGrid VoxelGrid::fromChunks(const std::vector<std::vector<bl::chunk*>>& chunks, const std::function<void(int)>& reportProgress,
                                int* firstWorldY) {
    if (firstWorldY) *firstWorldY = 0;
    if (chunks.empty()) return {};
    constexpr int chunkWidth = 16;
    const int width = static_cast<int>(chunks.size());
    int depth = 0;
    int worldMinY = std::numeric_limits<int>::max();
    int worldMaxY = std::numeric_limits<int>::min();
    for (const auto& row : chunks) {
        depth = std::max(depth, static_cast<int>(row.size()));
        for (const auto* chunk : row) {
            if (!chunk) continue;
            const auto [minY, maxY] = chunk->get_y_range();
            if (maxY < minY) continue;
            worldMinY = std::min(worldMinY, minY);
            worldMaxY = std::max(worldMaxY, maxY);
        }
    }
    if (depth == 0 || worldMaxY < worldMinY) return {};

    VoxelGrid grid(width * chunkWidth, worldMaxY - worldMinY + 1, depth * chunkWidth);
    int completed = 0;
    for (int chunkX = 0; chunkX < width; ++chunkX) {
        const auto& row = chunks[chunkX];
        for (size_t chunkZ = 0; chunkZ < row.size(); ++chunkZ) {
            auto* chunk = row[chunkZ];
            if (!chunk) continue;
            const auto [minY, maxY] = chunk->get_y_range();
            for (int worldY = minY; worldY <= maxY; ++worldY) {
                const int y = worldY - worldMinY;
                for (int x = 0; x < chunkWidth; ++x) {
                    for (int z = 0; z < chunkWidth; ++z) {
                        const auto info = chunk->get_block_with_color(x, worldY, z);
                        if (info.name == "minecraft:unknown" || info.name == "minecraft:air") continue;
                        const auto c = bl::blend_color_with_biome(info.name, info.color, chunk->get_biome(x, worldY, z));
                        grid.set(chunkX * chunkWidth + x, y, static_cast<int>(chunkZ) * chunkWidth + z, qRgba(c.r, c.g, c.b, c.a));
                    }
                }
            }
            ++completed;
            if (reportProgress) reportProgress(completed);
        }
    }

    const int trimmed = grid.trimEmptyY();
    if (firstWorldY) *firstWorldY = grid.empty() ? worldMinY : worldMinY + trimmed;
    return grid;
}

VoxelOverlayMeshBuffers VoxelOverlayMeshBuffers::fromAxes(const QVector3D& modelSize) {
    VoxelOverlayMeshBuffers result;
    for (int axis = 0; axis < 3; ++axis) {
        QColor color = AXIS_COLORS[axis];
        color.setAlpha(235);
        QVector3D endpoint;
        endpoint[axis] = modelSize[axis];
        appendOverlayVertex(result.vertices, QVector3D(), color);
        appendOverlayVertex(result.vertices, endpoint, color);
    }
    result.lineCount = result.vertexCount();
    return result;
}

VoxelOverlayMeshBuffers VoxelOverlayMeshBuffers::fromSelection(const bl::block_box& bounds, const VoxelSelectionView& view,
                                                               const QColor& selectionColor, VoxelSelectionHandle active) {
    VoxelOverlayMeshBuffers result;
    if (!bounds.is_valid()) return result;

    const BoxCorners p = boxCorners(bounds);
    const QVector3D center = (p[0] + p[6]) * 0.5f;
    constexpr float FILL_OUTSET = 0.015f;
    BoxCorners fillPoints = p;
    for (auto& point : fillPoints) {
        const QVector3D direction = point - center;
        if (direction.lengthSquared() > 1e-8f) point += direction.normalized() * FILL_OUTSET;
    }

    // The outline carries the configured selection color, alpha included, so the setting controls
    // how see-through the box is. The fill is the same hue darkened and keeps a fraction of that
    // alpha, so one setting drives both without the interior washing out the model behind it.
    constexpr float FILL_ALPHA_RATIO = 0.36f;
    QColor fillColor = selectionColor.darker(150);
    fillColor.setAlpha(static_cast<int>(std::round(selectionColor.alpha() * FILL_ALPHA_RATIO)));
    appendBoxFill(result.vertices, fillPoints, fillColor);
    result.fillCount = result.vertexCount();
    appendBoxEdges(result.vertices, p, selectionColor);
    result.lineCount = result.vertexCount() - result.fillCount;

    for (int index = 1; index <= 6; ++index) {
        const auto handle = static_cast<VoxelSelectionHandle>(index);
        // The grabbed handle keeps its axis hue and only brightens, so the axis it moves stays
        // readable while it is being dragged.
        QColor color = AXIS_COLORS[voxel_selection::handleAxisIndex(handle)];
        if (handle == active) color = color.lighter(150);
        color.setAlpha(handle == active ? 250 : 235);
        const QVector3D position = voxel_selection::handlePosition(bounds, handle);
        const float half = view.handleHalfSize(position);
        const QVector3D extent(half, half, half);
        appendBoxFill(result.vertices, boxCorners(position - extent, position + extent), color);
    }
    return result;
}

VoxelOverlayMeshBuffers VoxelOverlayMeshBuffers::fromHsaAreas(const std::vector<VoxelHsaArea>& areas) {
    VoxelOverlayMeshBuffers result;
    const auto drawable = [](const VoxelHsaArea& area) { return area.bounds.is_valid() && area.color.isValid(); };
    for (const auto& area : areas) {
        if (!drawable(area)) continue;
        QColor fill = area.color.darker(145);
        fill.setAlpha(28);
        appendBoxFill(result.vertices, boxCorners(area.bounds), fill);
    }
    // All fills come first so they share one draw call before the outlines.
    result.fillCount = result.vertexCount();
    for (const auto& area : areas) {
        if (!drawable(area)) continue;
        QColor outline = area.color;
        outline.setAlpha(190);
        appendBoxEdges(result.vertices, boxCorners(area.bounds), outline);
    }
    result.lineCount = result.vertexCount() - result.fillCount;
    return result;
}

VoxelMeshBuffers VoxelMeshBuffers::fromGrid(const VoxelGrid& grid, const bl::block_box& requested) {
    VoxelMeshBuffers result;
    const auto full = grid.bounds();
    if (!full) return result;
    const bl::block_box bounds = full->intersected(requested);
    if (!bounds.is_valid()) return result;
    for (int y = bounds.min_pos.y; y < bounds.max_pos.y; ++y) {
        for (int x = bounds.min_pos.x; x < bounds.max_pos.x; ++x) {
            for (int z = bounds.min_pos.z; z < bounds.max_pos.z; ++z) {
                const QRgb color = grid.at(x, y, z);
                if (qAlpha(color) == 0) continue;
                const bool transparent = isTransparent(color);
                auto& vertices = transparent ? result.transparentVertices : result.opaqueVertices;
                auto& indices = transparent ? result.transparentIndices : result.opaqueIndices;
                for (int face = 0; face < 6; ++face) {
                    const int* n = FACE_NORMALS[face];
                    if (occludedBy(grid, bounds, color, x + n[0], y + n[1], z + n[2])) continue;
                    appendFace(x, y, z, color, face, faceOcclusionFactors(grid, bounds, color, x, y, z, face), vertices, indices);
                }
            }
        }
    }
    return result;
}

std::shared_ptr<VoxelSceneData> VoxelSceneData::fromGrid(VoxelGrid grid, const QVector3D& origin) {
    const auto bounds = grid.bounds();
    if (!bounds) return {};
    auto scene = std::make_shared<VoxelSceneData>();
    scene->mesh = VoxelMeshBuffers::fromGrid(grid, *bounds);
    scene->modelSize = QVector3D(grid.sizeX(), grid.sizeY(), grid.sizeZ());
    scene->voxels = std::move(grid);
    scene->origin = origin;
    return scene;
}
