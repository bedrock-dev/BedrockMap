#include "render/voxel_mesh.h"

#include <QVector4D>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>

#include "chunk.h"
#include "color.h"
#include "mcstructure.h"

namespace {
    const QColor AXIS_COLORS[] = {QColor(255, 60, 60), QColor(60, 255, 60), QColor(60, 60, 255)};
    constexpr float SELECTION_HANDLE_SCREEN_SIZE_PX = 8.0f;
    void appendColoredVertex(std::vector<float>& vertices, const QVector3D& position, const QColor& color) {
        vertices.push_back(position.x());
        vertices.push_back(position.y());
        vertices.push_back(position.z());
        vertices.push_back(color.redF());
        vertices.push_back(color.greenF());
        vertices.push_back(color.blueF());
        vertices.push_back(color.alphaF());
    }

    void appendColoredQuad(std::vector<float>& vertices, const QVector3D& p0, const QVector3D& p1, const QVector3D& p2, const QVector3D& p3,
                           const QColor& color) {
        appendColoredVertex(vertices, p0, color);
        appendColoredVertex(vertices, p1, color);
        appendColoredVertex(vertices, p2, color);
        appendColoredVertex(vertices, p0, color);
        appendColoredVertex(vertices, p2, color);
        appendColoredVertex(vertices, p3, color);
    }

    void appendColoredBox(std::vector<float>& vertices, const QVector3D& center, float halfSize, const QColor& color) {
        const QVector3D h(halfSize, halfSize, halfSize);
        const QVector3D p[8] = {
            center + QVector3D(-h.x(), -h.y(), -h.z()), center + QVector3D(h.x(), -h.y(), -h.z()), center + QVector3D(h.x(), h.y(), -h.z()),
            center + QVector3D(-h.x(), h.y(), -h.z()),  center + QVector3D(-h.x(), -h.y(), h.z()), center + QVector3D(h.x(), -h.y(), h.z()),
            center + QVector3D(h.x(), h.y(), h.z()),    center + QVector3D(-h.x(), h.y(), h.z()),
        };
        const int faces[6][4] = {
            {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {3, 7, 6, 2}, {0, 1, 5, 4},
        };
        for (const auto& face : faces) {
            appendColoredQuad(vertices, p[face[0]], p[face[1]], p[face[2]], p[face[3]], color);
        }
    }

    float pixelsPerWorldUnitAt(const VoxelSelectionView& view, const QVector3D& point) {
        bool originVisible = false;
        const QPointF origin = voxel_mesh::projectSelectionPoint(view, point, &originVisible);
        if (!originVisible) return 0.0f;

        float totalPixelsPerWorld = 0.0f;
        int sampleCount = 0;
        const QVector3D axes[] = {
            QVector3D(view.voxelSize, 0.0f, 0.0f),
            QVector3D(0.0f, view.voxelSize, 0.0f),
            QVector3D(0.0f, 0.0f, view.voxelSize),
        };
        for (const auto& axis : axes) {
            bool axisVisible = false;
            const QPointF projected = voxel_mesh::projectSelectionPoint(view, point + axis, &axisVisible);
            if (!axisVisible) continue;
            const float dx = static_cast<float>(projected.x() - origin.x());
            const float dy = static_cast<float>(projected.y() - origin.y());
            const float pixels = std::sqrt(dx * dx + dy * dy);
            if (pixels <= 1e-3f) continue;
            totalPixelsPerWorld += pixels / view.voxelSize;
            ++sampleCount;
        }
        if (sampleCount == 0) return 0.0f;
        return totalPixelsPerWorld / static_cast<float>(sampleCount);
    }

    float selectionHandleHalfSize(const VoxelSelectionView& view, const QVector3D& point) {
        const float pixelsPerWorld = pixelsPerWorldUnitAt(view, point);
        if (pixelsPerWorld <= 1e-3f) {
            return 0.20f * view.voxelSize;
        }
        return (SELECTION_HANDLE_SCREEN_SIZE_PX * 0.5f) / pixelsPerWorld;
    }

    constexpr float AO_BRIGHTNESS[4] = {0.68f, 0.80f, 0.91f, 1.0f};
    constexpr std::array<std::uint32_t, 6> FACE_INDICES = {0, 1, 2, 0, 2, 3};
    const std::array<std::vector<float>, 6> FACE_TEMPLATES = {
        std::vector<float>{-0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f},
        std::vector<float>{-0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f},
        std::vector<float>{-0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f},
        std::vector<float>{0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f},
        std::vector<float>{-0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f},
        std::vector<float>{-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f},
    };
    const std::array<QVector3D, 6> FACE_NORMALS = {
        QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.0f, 0.0f, -1.0f), QVector3D(-1.0f, 0.0f, 0.0f),
        QVector3D(1.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f),  QVector3D(0.0f, -1.0f, 0.0f),
    };
}  // namespace

namespace voxel_mesh {
    VoxelGrid createVoxelDataFromMcstructure(const bl::mcstructure& structure) {
        const int sx = structure.size_x();
        const int sy = structure.size_y();
        const int sz = structure.size_z();

        if (sx <= 0 || sy <= 0 || sz <= 0) return {};
        VoxelGrid data;
        data.resize(sy);
        for (auto& yLayer : data) {
            yLayer.resize(sx);
            for (auto& xRow : yLayer) {
                xRow.resize(sz, Voxel(QColor(255, 255, 255, 0), true));
            }
        }

        if (structure.palette_size() == 0) {
            return data;
        }

        for (int x = 0; x < sx; ++x) {
            for (int y = 0; y < sy; ++y) {
                for (int z = 0; z < sz; ++z) {
                    const auto* block = structure.block_at(x, y, z);
                    if (!block) continue;
                    if (block->name == "minecraft:air" || block->name == "minecraft:unknown") continue;

                    const auto baseColor = bl::get_block_by_name_tag(block->name);
                    // mcstructure files do not carry biome data; use plains as a stable preview biome.
                    const auto c = bl::blend_color_with_biome(block->name, baseColor, bl::biome::plains);
                    data[y][x][z] = Voxel(QColor(c.r, c.g, c.b, c.a), c.a < 255);
                }
            }
        }

        return data;
    }

    QPointF projectSelectionPoint(const VoxelSelectionView& view, const QVector3D& point, bool* visible) {
        const QVector4D clip = view.matrix * QVector4D(point, 1.0f);
        if (std::abs(clip.w()) < 1e-6f) {
            if (visible) *visible = false;
            return {};
        }
        const QVector3D ndc = clip.toVector3D() / clip.w();
        if (visible) *visible = clip.w() > 0.0f && ndc.z() >= -1.0f && ndc.z() <= 1.0f;
        return {(ndc.x() + 1.0f) * 0.5f * view.viewport.width(), (1.0f - ndc.y()) * 0.5f * view.viewport.height()};
    }

    int selectionHandleAxisIndex(VoxelSelectionHandle handle) {
        return handle == VoxelSelectionHandle::None ? -1 : (static_cast<int>(handle) - 1) / 2;
    }

    bl::block_box clampSelection(bl::block_box bounds, const QVector3D& modelSize, bool locked) {
        // A locked selection is an import placement box and may extend beyond the model.
        if (!locked) {
            bounds.min_pos.x = std::clamp(bounds.min_pos.x, 0, static_cast<int>(modelSize.x()));
            bounds.min_pos.y = std::clamp(bounds.min_pos.y, 0, static_cast<int>(modelSize.y()));
            bounds.min_pos.z = std::clamp(bounds.min_pos.z, 0, static_cast<int>(modelSize.z()));
            bounds.max_pos.x = std::clamp(bounds.max_pos.x, 0, static_cast<int>(modelSize.x()));
            bounds.max_pos.y = std::clamp(bounds.max_pos.y, 0, static_cast<int>(modelSize.y()));
            bounds.max_pos.z = std::clamp(bounds.max_pos.z, 0, static_cast<int>(modelSize.z()));
        }
        return bounds;
    }

    VoxelSelectionDrag beginSelectionDrag(const bl::block_box& bounds, const QVector3D& modelSize, const VoxelSelectionView& view,
                                          const QPointF& position, bool moveMode, float pickRadius) {
        VoxelSelectionDrag drag;
        if (!bounds.is_valid() || view.viewport.isEmpty()) return drag;
        float bestDistance = pickRadius * pickRadius;
        for (int index = 1; index <= 6; ++index) {
            const auto handle = static_cast<VoxelSelectionHandle>(index);
            bool visible = false;
            const QPointF point = projectSelectionPoint(view, selectionHandlePosition(bounds, handle, view.voxelSize), &visible);
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
        const int axis = selectionHandleAxisIndex(drag.handle);
        QVector3D step;
        step[axis] = view.voxelSize;
        const QVector3D point = moveMode ? QVector3D(bounds.min_pos.x + bounds.max_pos.x, bounds.min_pos.y + bounds.max_pos.y,
                                                     bounds.min_pos.z + bounds.max_pos.z) *
                                               (view.voxelSize * 0.5f)
                                         : selectionHandlePosition(bounds, drag.handle, view.voxelSize);
        const auto projectedStep = [&](float count) {
            return projectSelectionPoint(view, point + step * count) - projectSelectionPoint(view, point);
        };
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

    bl::block_box dragSelection(const VoxelSelectionDrag& drag, const QVector3D& modelSize, const QPointF& position, bool locked) {
        auto result = drag.bounds;
        const int axis = selectionHandleAxisIndex(drag.handle);
        if (axis < 0 || !result.is_valid()) return result;
        const float lengthSquared = static_cast<float>(QPointF::dotProduct(drag.screenAxis, drag.screenAxis));
        if (lengthSquared < (drag.moveMode ? 1e-6f : 1e-10f)) return result;
        const float delta = static_cast<float>(QPointF::dotProduct(position - drag.start, drag.screenAxis) / lengthSquared);
        int* low[] = {&result.min_pos.x, &result.min_pos.y, &result.min_pos.z};
        int* high[] = {&result.max_pos.x, &result.max_pos.y, &result.max_pos.z};
        if (drag.moveMode) {
            const int span = *high[axis] - *low[axis];
            const int minimum = *low[axis] + static_cast<int>(std::round(delta));
            *low[axis] = locked ? minimum : std::clamp(minimum, 0, static_cast<int>(modelSize[axis]) - span);
            *high[axis] = *low[axis] + span;
        } else if (static_cast<int>(drag.handle) % 2 == 1) {
            *low[axis] = std::clamp(qRound(static_cast<float>(*low[axis]) + delta), 0, *high[axis] - 1);
        } else {
            *high[axis] = std::clamp(qRound(static_cast<float>(*high[axis]) + delta), *low[axis] + 1, static_cast<int>(modelSize[axis]));
        }
        return result;
    }

    QVector3D selectionHandlePosition(const bl::block_box& bounds, VoxelSelectionHandle handle, float voxelSize) {
        const QVector3D minimum(bounds.min_pos.x * voxelSize, bounds.min_pos.y * voxelSize, bounds.min_pos.z * voxelSize);
        const QVector3D maximum(bounds.max_pos.x * voxelSize, bounds.max_pos.y * voxelSize, bounds.max_pos.z * voxelSize);
        const QVector3D center = (minimum + maximum) * 0.5f;
        switch (handle) {
            case VoxelSelectionHandle::MinX:
                return {minimum.x(), center.y(), center.z()};
            case VoxelSelectionHandle::MaxX:
                return {maximum.x(), center.y(), center.z()};
            case VoxelSelectionHandle::MinY:
                return {center.x(), minimum.y(), center.z()};
            case VoxelSelectionHandle::MaxY:
                return {center.x(), maximum.y(), center.z()};
            case VoxelSelectionHandle::MinZ:
                return {center.x(), center.y(), minimum.z()};
            case VoxelSelectionHandle::MaxZ:
                return {center.x(), center.y(), maximum.z()};
            case VoxelSelectionHandle::None:
                return center;
        }
        return center;
    }

    std::vector<float> buildAxesMesh(const QVector3D& modelSize) {
        std::vector<float> vertices;
        vertices.reserve(6 * 7);
        const QVector3D origin;
        const std::array endpoints = {
            QVector3D(modelSize.x(), 0.0f, 0.0f),
            QVector3D(0.0f, modelSize.y(), 0.0f),
            QVector3D(0.0f, 0.0f, modelSize.z()),
        };
        for (size_t axis = 0; axis < endpoints.size(); ++axis) {
            QColor color = AXIS_COLORS[axis];
            color.setAlpha(235);
            appendColoredVertex(vertices, origin, color);
            appendColoredVertex(vertices, endpoints[axis], color);
        }
        return vertices;
    }

    VoxelSelectionMesh buildSelectionMesh(const bl::block_box& bounds, const VoxelSelectionView& view, const QColor& selectionColor,
                                          VoxelSelectionHandle active) {
        VoxelSelectionMesh result;
        auto& selection_vertices_ = result.vertices;
        if (!bounds.is_valid()) return result;

        const QVector3D minimum(bounds.min_pos.x * view.voxelSize, bounds.min_pos.y * view.voxelSize, bounds.min_pos.z * view.voxelSize);
        const QVector3D maximum(bounds.max_pos.x * view.voxelSize, bounds.max_pos.y * view.voxelSize, bounds.max_pos.z * view.voxelSize);
        const QVector3D p[8] = {
            {minimum.x(), minimum.y(), minimum.z()}, {maximum.x(), minimum.y(), minimum.z()}, {maximum.x(), maximum.y(), minimum.z()},
            {minimum.x(), maximum.y(), minimum.z()}, {minimum.x(), minimum.y(), maximum.z()}, {maximum.x(), minimum.y(), maximum.z()},
            {maximum.x(), maximum.y(), maximum.z()}, {minimum.x(), maximum.y(), maximum.z()},
        };
        const QVector3D center = (minimum + maximum) * 0.5f;
        const float fillOutset = std::max(0.015f * view.voxelSize, 0.01f);
        QVector3D fillPoints[8];
        for (int i = 0; i < 8; ++i) {
            QVector3D direction = p[i] - center;
            if (direction.lengthSquared() > 1e-8f) {
                direction.normalize();
                fillPoints[i] = p[i] + direction * fillOutset;
            } else {
                fillPoints[i] = p[i];
            }
        }

        // The outline carries the configured selection color, alpha included, so the setting controls
        // how see-through the box is. The fill is the same hue darkened and keeps a fraction of that
        // alpha, so one setting drives both without the interior washing out the model behind it.
        const QColor outlineColor = selectionColor;
        constexpr float FILL_ALPHA_RATIO = 0.36f;
        QColor fillColor = selectionColor.darker(150);
        fillColor.setAlpha(static_cast<int>(std::round(selectionColor.alpha() * FILL_ALPHA_RATIO)));
        const int faces[6][4] = {
            {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {3, 7, 6, 2}, {0, 1, 5, 4},
        };
        for (const auto& face : faces) {
            appendColoredQuad(selection_vertices_, fillPoints[face[0]], fillPoints[face[1]], fillPoints[face[2]], fillPoints[face[3]],
                              fillColor);
        }
        result.fillCount = static_cast<std::uint32_t>(selection_vertices_.size() / 7);

        const int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        for (const auto& edge : edges) {
            appendColoredVertex(selection_vertices_, p[edge[0]], outlineColor);
            appendColoredVertex(selection_vertices_, p[edge[1]], outlineColor);
        }
        result.lineCount = static_cast<std::uint32_t>(selection_vertices_.size() / 7) - result.fillCount;

        constexpr std::array<VoxelSelectionHandle, 6> handles = {
            VoxelSelectionHandle::MinX, VoxelSelectionHandle::MaxX, VoxelSelectionHandle::MinY,
            VoxelSelectionHandle::MaxY, VoxelSelectionHandle::MinZ, VoxelSelectionHandle::MaxZ,
        };
        for (const VoxelSelectionHandle handle : handles) {
            QColor color = AXIS_COLORS[selectionHandleAxisIndex(handle)];
            // The grabbed handle keeps its axis hue and only brightens, so the axis it moves stays
            // readable while it is being dragged.
            color = handle == active ? color.lighter(150) : color;
            color.setAlpha(handle == active ? 250 : 235);
            const QVector3D handlePosition = selectionHandlePosition(bounds, handle, view.voxelSize);
            appendColoredBox(selection_vertices_, handlePosition, selectionHandleHalfSize(view, handlePosition), color);
        }
        return result;
    }

    VoxelGrid createVoxelDataFromChunks(const std::vector<std::vector<bl::chunk*>>& chunks, const std::function<void(int)>& reportProgress,
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

        VoxelGrid grid(static_cast<size_t>(worldMaxY - worldMinY + 1));
        for (auto& layer : grid) {
            layer.resize(static_cast<size_t>(width * chunkWidth));
            for (auto& row : layer) row.resize(static_cast<size_t>(depth * chunkWidth), Voxel(QColor(0, 0, 0, 0), true));
        }

        int completed = 0;
        for (int chunkX = 0; chunkX < width; ++chunkX) {
            const auto& row = chunks[chunkX];
            for (size_t chunkZ = 0; chunkZ < row.size(); ++chunkZ) {
                auto* chunk = row[chunkZ];
                if (!chunk) continue;
                const auto [minY, maxY] = chunk->get_y_range();
                for (int worldY = minY; worldY <= maxY; ++worldY) {
                    auto& layer = grid[worldY - worldMinY];
                    for (int x = 0; x < chunkWidth; ++x) {
                        for (int z = 0; z < chunkWidth; ++z) {
                            const auto info = chunk->get_block_with_color(x, worldY, z);
                            if (info.name == "minecraft:unknown" || info.name == "minecraft:air") continue;
                            const auto color = bl::blend_color_with_biome(info.name, info.color, chunk->get_biome(x, worldY, z));
                            auto& voxel = layer[chunkX * chunkWidth + x][chunkZ * chunkWidth + z];
                            voxel.color = QColor(color.r, color.g, color.b, color.a);
                            voxel.transparent = voxel.color.alpha() < 255;
                        }
                    }
                }
                ++completed;
                if (reportProgress) reportProgress(completed);
            }
        }

        const auto occupied = [](const auto& layer) {
            return std::any_of(layer.cbegin(), layer.cend(), [](const auto& row) {
                return std::any_of(row.cbegin(), row.cend(), [](const auto& voxel) { return voxel.color.alpha() != 0; });
            });
        };
        const auto first = std::find_if(grid.begin(), grid.end(), occupied);
        if (first == grid.end()) {
            if (firstWorldY) *firstWorldY = worldMinY;
            return {};
        }
        const auto last = std::find_if(grid.rbegin(), grid.rend(), occupied).base();
        if (firstWorldY) *firstWorldY = worldMinY + static_cast<int>(std::distance(grid.begin(), first));
        // Move retained layers to avoid copying the entire area while trimming.
        return VoxelGrid(std::make_move_iterator(first), std::make_move_iterator(last));
    }

    std::optional<bl::block_box> fullBounds(const VoxelGrid& grid) {
        if (grid.empty() || grid[0].empty() || grid[0][0].empty()) return std::nullopt;
        return bl::block_box::from_min_and_size({0, 0, 0}, static_cast<int>(grid[0].size()), static_cast<int>(grid.size()),
                                                static_cast<int>(grid[0][0].size()));
    }

    bool hasNeighborInBounds(const VoxelGrid& grid, int layer, int x, int z, int dy, int dx, int dz, const bl::block_box& bounds,
                             VoxelMeshOcclusionMode mode) {
        const auto& current = grid[layer][x][z];
        const int ny = layer + dy;
        const int nx = x + dx;
        const int nz = z + dz;
        if (ny < bounds.min_pos.y || ny >= bounds.max_pos.y || nx < bounds.min_pos.x || nx >= bounds.max_pos.x || nz < bounds.min_pos.z ||
            nz >= bounds.max_pos.z || ny < 0 || ny >= static_cast<int>(grid.size()) || nx < 0 || nx >= static_cast<int>(grid[ny].size()) ||
            nz < 0 || nz >= static_cast<int>(grid[ny][nx].size())) {
            return false;
        }
        const auto& neighbor = grid[ny][nx][nz];
        if (neighbor.color.alpha() == 0) return false;
        if (mode == VoxelMeshOcclusionMode::OccupiedVoxelShell) return true;
        return current.transparent ? neighbor.color.alpha() != 0 : !neighbor.transparent;
    }

    std::array<float, 4> faceOcclusionFactors(const VoxelGrid& grid, const bl::block_box& bounds, int layer, int x, int z,
                                              const std::vector<float>& faceVertices, const QVector3D& normal,
                                              VoxelMeshOcclusionMode mode) {
        const int normalAxis = normal.x() != 0.0f ? 0 : (normal.y() != 0.0f ? 1 : 2);
        const int tangentAxes[2] = {(normalAxis + 1) % 3, (normalAxis + 2) % 3};
        const int step[3] = {static_cast<int>(normal.x()), static_cast<int>(normal.y()), static_cast<int>(normal.z())};
        const auto occludes = [&](const int offset[3]) {
            return hasNeighborInBounds(grid, layer, x, z, offset[1], offset[0], offset[2], bounds, mode);
        };
        std::array<float, 4> result{};
        for (int i = 0; i < 4; ++i) {
            const float* corner = &faceVertices[i * 3];
            const int sign1 = corner[tangentAxes[0]] > 0.0f ? 1 : -1;
            const int sign2 = corner[tangentAxes[1]] > 0.0f ? 1 : -1;
            int side1[3] = {step[0], step[1], step[2]};
            int side2[3] = {step[0], step[1], step[2]};
            int diagonal[3] = {step[0], step[1], step[2]};
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

    void addFaceVerticesToBuffers(int layer, int x, int z, const Voxel& voxel, const std::vector<float>& faceVertices,
                                  const QVector3D& normal, const std::array<float, 4>& occlusion, std::vector<float>& vertices,
                                  std::vector<std::uint32_t>& indices, float voxelSize, float alphaScale) {
        const float worldX = x * voxelSize + 0.5f * voxelSize;
        const float worldY = layer * voxelSize + 0.5f * voxelSize;
        const float worldZ = z * voxelSize + 0.5f * voxelSize;
        const std::uint32_t vertexOffset = static_cast<std::uint32_t>(vertices.size() / 10);
        for (int i = 0; i < 4; ++i) {
            vertices.push_back(faceVertices[i * 3 + 0] * voxelSize + worldX);
            vertices.push_back(faceVertices[i * 3 + 1] * voxelSize + worldY);
            vertices.push_back(faceVertices[i * 3 + 2] * voxelSize + worldZ);
            vertices.push_back(normal.x());
            vertices.push_back(normal.y());
            vertices.push_back(normal.z());
            vertices.push_back(voxel.color.redF() * occlusion[i]);
            vertices.push_back(voxel.color.greenF() * occlusion[i]);
            vertices.push_back(voxel.color.blueF() * occlusion[i]);
            vertices.push_back(voxel.color.alphaF() * alphaScale);
        }
        const bool flipDiagonal = occlusion[0] + occlusion[2] > occlusion[1] + occlusion[3];
        if (flipDiagonal) {
            indices.push_back(0 + vertexOffset);
            indices.push_back(1 + vertexOffset);
            indices.push_back(3 + vertexOffset);
            indices.push_back(1 + vertexOffset);
            indices.push_back(2 + vertexOffset);
            indices.push_back(3 + vertexOffset);
        } else {
            for (const auto index : FACE_INDICES) indices.push_back(index + vertexOffset);
        }
    }

    void appendVisibleVoxelMesh(const VoxelGrid& grid, const bl::block_box& bounds, std::vector<float>& vertices,
                                std::vector<std::uint32_t>& indices, std::vector<float>* transparentVertices,
                                std::vector<std::uint32_t>* transparentIndices, VoxelMeshOcclusionMode mode, float alphaScale,
                                float voxelSize) {
        if (!bounds.is_valid()) return;
        static constexpr int dxArr[] = {0, 0, -1, 1, 0, 0};
        static constexpr int dyArr[] = {0, 0, 0, 0, 1, -1};
        static constexpr int dzArr[] = {1, -1, 0, 0, 0, 0};
        const int maxLayer = std::min(bounds.max_pos.y, static_cast<int>(grid.size()));
        for (int layer = std::max(0, bounds.min_pos.y); layer < maxLayer; ++layer) {
            const auto& layerData = grid[layer];
            if (layerData.empty()) continue;
            const int maxX = std::min(bounds.max_pos.x, static_cast<int>(layerData.size()));
            for (int x = std::max(0, bounds.min_pos.x); x < maxX; ++x) {
                const auto& rowData = layerData[x];
                if (rowData.empty()) continue;
                const int maxZ = std::min(bounds.max_pos.z, static_cast<int>(rowData.size()));
                for (int z = std::max(0, bounds.min_pos.z); z < maxZ; ++z) {
                    const Voxel& voxel = rowData[z];
                    if (voxel.color.alpha() == 0) continue;
                    for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
                        if (hasNeighborInBounds(grid, layer, x, z, dyArr[faceIndex], dxArr[faceIndex], dzArr[faceIndex], bounds, mode)) {
                            continue;
                        }
                        auto& targetVertices = (voxel.transparent && transparentVertices) ? *transparentVertices : vertices;
                        auto& targetIndices = (voxel.transparent && transparentIndices) ? *transparentIndices : indices;
                        const auto occlusion =
                            faceOcclusionFactors(grid, bounds, layer, x, z, FACE_TEMPLATES[faceIndex], FACE_NORMALS[faceIndex], mode);
                        addFaceVerticesToBuffers(layer, x, z, voxel, FACE_TEMPLATES[faceIndex], FACE_NORMALS[faceIndex], occlusion,
                                                 targetVertices, targetIndices, voxelSize, alphaScale);
                    }
                }
            }
        }
    }

    VoxelMeshBuffers build(const VoxelGrid& grid, const bl::block_box& bounds, VoxelMeshOcclusionMode mode, float alphaScale,
                           float voxelSize) {
        VoxelMeshBuffers result;
        appendVisibleVoxelMesh(grid, bounds, result.opaqueVertices, result.opaqueIndices, &result.transparentVertices,
                               &result.transparentIndices, mode, alphaScale, voxelSize);
        return result;
    }
}  // namespace voxel_mesh
