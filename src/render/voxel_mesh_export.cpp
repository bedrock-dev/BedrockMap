#include <QByteArray>
#include <QCoreApplication>
#include <QIODevice>
#include <QUrl>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "io/documentio.h"
#include "json/json.hpp"
#include "render/voxel_mesh.h"

namespace {
    template <typename T>
    void appendRawValue(QByteArray& data, const T& value) {
        data.append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    void padToFourBytes(QByteArray& data, char paddingByte) {
        while (data.size() % 4 != 0) {
            data.append(paddingByte);
        }
    }

    qsizetype appendAlignedBufferView(QByteArray& target, const QByteArray& source) {
        padToFourBytes(target, '\0');
        const qsizetype offset = target.size();
        target.append(source);
        padToFourBytes(target, '\0');
        return offset;
    }

    constexpr int ARRAY_BUFFER = 34962;
    constexpr int ELEMENT_ARRAY_BUFFER = 34963;
    constexpr int FLOAT_COMPONENT = 5126;
    constexpr int UNSIGNED_BYTE_COMPONENT = 5121;
    constexpr int UNSIGNED_SHORT_COMPONENT = 5123;
    constexpr int UNSIGNED_INT_COMPONENT = 5125;
    constexpr size_t VERTEX_FLOATS = 10;

    using Rgba8 = std::array<std::uint8_t, 4>;

    struct ExportVertex {
        std::array<float, 3> position;
        std::array<float, 3> normal;
        Rgba8 color;
    };

    struct ExportMesh {
        std::vector<ExportVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    Rgba8 packColor(const float* rgba) {
        Rgba8 color{};
        for (size_t i = 0; i < 4; ++i) color[i] = static_cast<std::uint8_t>(std::lround(std::clamp(rgba[i], 0.0f, 1.0f) * 255.0f));
        return color;
    }

    ExportVertex readVertex(const std::vector<float>& vertices, size_t index) {
        const float* v = &vertices[index * VERTEX_FLOATS];
        return {{v[0], v[1], v[2]}, {v[3], v[4], v[5]}, packColor(v + 6)};
    }

    ExportMesh convertMesh(const std::vector<float>& vertices, const std::vector<std::uint32_t>& indices) {
        ExportMesh result;
        result.vertices.reserve(vertices.size() / VERTEX_FLOATS);
        for (size_t i = 0; i < vertices.size() / VERTEX_FLOATS; ++i) result.vertices.push_back(readVertex(vertices, i));
        result.indices = indices;
        return result;
    }

    /// Merge coplanar unit faces whose four corners share one color. Faces with
    /// differing corner colors carry ambient occlusion gradients and are kept,
    /// so the merged mesh looks the same as the unit faces.
    ExportMesh mergeCoplanarFaces(const std::vector<float>& vertices, const std::vector<std::uint32_t>& indices) {
        const size_t quadCount = vertices.size() / (4 * VERTEX_FLOATS);
        if (vertices.size() != quadCount * 4 * VERTEX_FLOATS || indices.size() != quadCount * 6) return convertMesh(vertices, indices);
        ExportMesh result;
        result.vertices.reserve(vertices.size() / VERTEX_FLOATS);
        result.indices.reserve(indices.size());

        // Emit a source face, stretching its far corners to cover width x height cells.
        const auto appendQuad = [&](size_t quad, int axisU, int axisV, int minU, int minV, int width, int height) {
            const auto base = static_cast<std::uint32_t>(result.vertices.size());
            for (size_t i = 0; i < 4; ++i) {
                auto vertex = readVertex(vertices, quad * 4 + i);
                if (width > 1 && vertex.position[axisU] > minU + 0.5f) vertex.position[axisU] = static_cast<float>(minU + width);
                if (height > 1 && vertex.position[axisV] > minV + 0.5f) vertex.position[axisV] = static_cast<float>(minV + height);
                result.vertices.push_back(vertex);
            }
            const auto first = static_cast<std::uint32_t>(quad * 4);
            for (size_t i = 0; i < 6; ++i) result.indices.push_back(base + indices[quad * 6 + i] - first);
        };

        struct Cell {
            int u;
            int v;
            Rgba8 color;
            size_t quad;
        };
        // Faces are grouped by normal axis, normal direction and plane coordinate.
        std::map<std::tuple<int, bool, int>, std::vector<Cell>> planes;
        for (size_t quad = 0; quad < quadCount; ++quad) {
            const float* corner = &vertices[quad * 4 * VERTEX_FLOATS];
            const auto color = packColor(corner + 6);
            const int axis = std::abs(corner[3]) > 0.5f ? 0 : (std::abs(corner[4]) > 0.5f ? 1 : 2);
            const int axisU = (axis + 1) % 3;
            const int axisV = (axis + 2) % 3;
            bool uniform = true;
            float minU = corner[axisU], maxU = corner[axisU], minV = corner[axisV], maxV = corner[axisV];
            for (size_t i = 1; i < 4; ++i) {
                const float* next = corner + i * VERTEX_FLOATS;
                uniform = uniform && packColor(next + 6) == color;
                minU = std::min(minU, next[axisU]);
                maxU = std::max(maxU, next[axisU]);
                minV = std::min(minV, next[axisV]);
                maxV = std::max(maxV, next[axisV]);
            }
            const bool unitFace = std::abs(maxU - minU - 1.0f) < 1e-4f && std::abs(maxV - minV - 1.0f) < 1e-4f;
            if (!uniform || !unitFace) {
                appendQuad(quad, axisU, axisV, 0, 0, 1, 1);
                continue;
            }
            planes[{axis, corner[3 + axis] > 0.0f, static_cast<int>(std::lround(corner[axis]))}].push_back(
                {static_cast<int>(std::lround(minU)), static_cast<int>(std::lround(minV)), color, quad});
        }

        const auto cellKey = [](int u, int v) {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(u)) << 32) | static_cast<std::uint32_t>(v);
        };
        for (auto& [key, cells] : planes) {
            const int axisU = (std::get<0>(key) + 1) % 3;
            const int axisV = (std::get<0>(key) + 2) % 3;
            std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) { return a.v != b.v ? a.v < b.v : a.u < b.u; });
            std::unordered_map<std::uint64_t, size_t> lookup;
            lookup.reserve(cells.size());
            for (size_t i = 0; i < cells.size(); ++i) lookup.emplace(cellKey(cells[i].u, cells[i].v), i);
            std::vector<bool> used(cells.size(), false);
            const auto available = [&](int u, int v, const Rgba8& color) {
                const auto it = lookup.find(cellKey(u, v));
                return it != lookup.end() && !used[it->second] && cells[it->second].color == color;
            };

            for (size_t i = 0; i < cells.size(); ++i) {
                if (used[i]) continue;
                const Cell& start = cells[i];
                int width = 1;
                while (available(start.u + width, start.v, start.color)) ++width;
                int height = 1;
                for (;; ++height) {
                    bool row = true;
                    for (int du = 0; du < width && row; ++du) row = available(start.u + du, start.v + height, start.color);
                    if (!row) break;
                }
                for (int dv = 0; dv < height; ++dv)
                    for (int du = 0; du < width; ++du) used[lookup.at(cellKey(start.u + du, start.v + dv))] = true;
                appendQuad(start.quad, axisU, axisV, start.u, start.v, width, height);
            }
        }
        return result;
    }

    nlohmann::json appendMeshPrimitive(const ExportMesh& mesh, int materialIndex, QByteArray& binaryChunk, nlohmann::json& bufferViews,
                                       nlohmann::json& accessors) {
        const qsizetype vertexCount = static_cast<qsizetype>(mesh.vertices.size());
        // glTF reserves the largest index value for primitive restart.
        const bool shortIndices = vertexCount <= std::numeric_limits<std::uint16_t>::max();

        QByteArray positionData;
        QByteArray normalData;
        QByteArray colorData;
        QByteArray indexData;
        positionData.reserve(static_cast<qsizetype>(vertexCount * 3 * sizeof(float)));
        normalData.reserve(static_cast<qsizetype>(vertexCount * 3 * sizeof(float)));
        colorData.reserve(static_cast<qsizetype>(vertexCount * 4));
        indexData.reserve(static_cast<qsizetype>(mesh.indices.size() * (shortIndices ? sizeof(std::uint16_t) : sizeof(std::uint32_t))));

        std::array<float, 3> minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        std::array<float, 3> maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                     std::numeric_limits<float>::lowest()};
        for (const auto& vertex : mesh.vertices) {
            for (size_t axis = 0; axis < 3; ++axis) {
                appendRawValue(positionData, vertex.position[axis]);
                appendRawValue(normalData, vertex.normal[axis]);
                minimum[axis] = std::min(minimum[axis], vertex.position[axis]);
                maximum[axis] = std::max(maximum[axis], vertex.position[axis]);
            }
            colorData.append(reinterpret_cast<const char*>(vertex.color.data()), static_cast<qsizetype>(vertex.color.size()));
        }
        for (const std::uint32_t index : mesh.indices) {
            if (shortIndices)
                appendRawValue(indexData, static_cast<std::uint16_t>(index));
            else
                appendRawValue(indexData, index);
        }

        const int bufferViewBase = static_cast<int>(bufferViews.size());
        const int accessorBase = static_cast<int>(accessors.size());
        const qsizetype positionOffset = appendAlignedBufferView(binaryChunk, positionData);
        const qsizetype normalOffset = appendAlignedBufferView(binaryChunk, normalData);
        const qsizetype colorOffset = appendAlignedBufferView(binaryChunk, colorData);
        const qsizetype indexOffset = appendAlignedBufferView(binaryChunk, indexData);

        bufferViews.push_back({{"buffer", 0},
                               {"byteOffset", static_cast<int>(positionOffset)},
                               {"byteLength", positionData.size()},
                               {"target", ARRAY_BUFFER}});
        bufferViews.push_back(
            {{"buffer", 0}, {"byteOffset", static_cast<int>(normalOffset)}, {"byteLength", normalData.size()}, {"target", ARRAY_BUFFER}});
        bufferViews.push_back(
            {{"buffer", 0}, {"byteOffset", static_cast<int>(colorOffset)}, {"byteLength", colorData.size()}, {"target", ARRAY_BUFFER}});
        bufferViews.push_back({{"buffer", 0},
                               {"byteOffset", static_cast<int>(indexOffset)},
                               {"byteLength", indexData.size()},
                               {"target", ELEMENT_ARRAY_BUFFER}});

        accessors.push_back({{"bufferView", bufferViewBase + 0},
                             {"componentType", FLOAT_COMPONENT},
                             {"count", static_cast<int>(vertexCount)},
                             {"type", "VEC3"},
                             {"min", minimum},
                             {"max", maximum}});
        accessors.push_back({{"bufferView", bufferViewBase + 1},
                             {"componentType", FLOAT_COMPONENT},
                             {"count", static_cast<int>(vertexCount)},
                             {"type", "VEC3"}});
        accessors.push_back({{"bufferView", bufferViewBase + 2},
                             {"componentType", UNSIGNED_BYTE_COMPONENT},
                             {"normalized", true},
                             {"count", static_cast<int>(vertexCount)},
                             {"type", "VEC4"}});
        accessors.push_back({{"bufferView", bufferViewBase + 3},
                             {"componentType", shortIndices ? UNSIGNED_SHORT_COMPONENT : UNSIGNED_INT_COMPONENT},
                             {"count", static_cast<int>(mesh.indices.size())},
                             {"type", "SCALAR"}});

        return {
            {"attributes", {{"POSITION", accessorBase + 0}, {"NORMAL", accessorBase + 1}, {"COLOR_0", accessorBase + 2}}},
            {"indices", accessorBase + 3},
            {"material", materialIndex},
        };
    }
}  // namespace

bool voxel_mesh::exportGlb(const VoxelMeshBuffers& mesh, const QUrl& url, bool mergeFaces, QString* errorMessage) {
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage) *errorMessage = message;
        return false;
    };

    if (url.isEmpty()) return fail(QCoreApplication::translate("VoxelMesh", "Export path is empty."));

    const auto& opaqueVertices = mesh.opaqueVertices;
    const auto& opaqueIndices = mesh.opaqueIndices;
    const auto& transparentVertices = mesh.transparentVertices;
    const auto& transparentIndices = mesh.transparentIndices;
    if ((opaqueVertices.empty() || opaqueIndices.empty()) && (transparentVertices.empty() || transparentIndices.empty())) {
        return fail(QCoreApplication::translate("VoxelMesh", "The selected range has no visible voxel faces."));
    }

    QByteArray binaryChunk;
    nlohmann::json bufferViews = nlohmann::json::array();
    nlohmann::json accessors = nlohmann::json::array();
    nlohmann::json primitives = nlohmann::json::array();
    const auto prepare = [mergeFaces](const std::vector<float>& vertices, const std::vector<std::uint32_t>& indices) {
        return mergeFaces ? mergeCoplanarFaces(vertices, indices) : convertMesh(vertices, indices);
    };
    if (!opaqueVertices.empty() && !opaqueIndices.empty()) {
        primitives.push_back(appendMeshPrimitive(prepare(opaqueVertices, opaqueIndices), 0, binaryChunk, bufferViews, accessors));
    }
    if (!transparentVertices.empty() && !transparentIndices.empty()) {
        primitives.push_back(appendMeshPrimitive(prepare(transparentVertices, transparentIndices), 1, binaryChunk, bufferViews, accessors));
    }

    const nlohmann::json root = {
        {"asset", {{"version", "2.0"}, {"generator", "BedrockMap"}}},
        {"scene", 0},
        {"scenes", {{{"nodes", {0}}}}},
        {"nodes", {{{"mesh", 0}}}},
        {"materials",
         {
             {
                 {"name", "Opaque voxel vertex colors"},
                 {"doubleSided", true},
                 {"alphaMode", "OPAQUE"},
                 {"pbrMetallicRoughness", {{"baseColorFactor", {1.0, 1.0, 1.0, 1.0}}, {"metallicFactor", 0.0}, {"roughnessFactor", 1.0}}},
             },
             {
                 {"name", "Transparent voxel vertex colors"},
                 {"doubleSided", true},
                 {"alphaMode", "BLEND"},
                 {"pbrMetallicRoughness", {{"baseColorFactor", {1.0, 1.0, 1.0, 1.0}}, {"metallicFactor", 0.0}, {"roughnessFactor", 1.0}}},
             },
         }},
        {"meshes", {{{"name", "Voxel model"}, {"primitives", primitives}}}},
        {"buffers", {{{"byteLength", binaryChunk.size()}}}},
        {"bufferViews", bufferViews},
        {"accessors", accessors},
    };

    const std::string jsonText = root.dump();
    QByteArray jsonChunk(jsonText.data(), static_cast<qsizetype>(jsonText.size()));
    padToFourBytes(jsonChunk, ' ');

    constexpr std::uint32_t GLB_MAGIC = 0x46546C67;
    constexpr std::uint32_t GLB_VERSION = 2;
    constexpr std::uint32_t JSON_CHUNK_TYPE = 0x4E4F534A;
    constexpr std::uint32_t BINARY_CHUNK_TYPE = 0x004E4942;

    QByteArray glb;
    const std::uint32_t totalLength = static_cast<std::uint32_t>(12 + 8 + jsonChunk.size() + 8 + binaryChunk.size());
    appendRawValue(glb, GLB_MAGIC);
    appendRawValue(glb, GLB_VERSION);
    appendRawValue(glb, totalLength);
    appendRawValue(glb, static_cast<std::uint32_t>(jsonChunk.size()));
    appendRawValue(glb, JSON_CHUNK_TYPE);
    glb.append(jsonChunk);
    appendRawValue(glb, static_cast<std::uint32_t>(binaryChunk.size()));
    appendRawValue(glb, BINARY_CHUNK_TYPE);
    glb.append(binaryChunk);

    const QString writeError = io::DocumentIo::writeStream(
        url, [&glb](QIODevice& file) { return file.write(glb) == glb.size() ? QString() : file.errorString(); });
    if (!writeError.isEmpty()) return fail(writeError);

    return true;
}
