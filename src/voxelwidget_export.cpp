#include <QIODevice>
#include <QSaveFile>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "json/json.hpp"
#include "voxelwidget.h"

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

    constexpr int kArrayBuffer = 34962;
    constexpr int kElementArrayBuffer = 34963;
    constexpr int kFloatComponent = 5126;
    constexpr int kUnsignedIntComponent = 5125;

    nlohmann::json appendMeshPrimitive(const std::vector<float>& vertices, const std::vector<GLuint>& indices, int materialIndex,
                                       QByteArray& binaryChunk, nlohmann::json& bufferViews, nlohmann::json& accessors) {
        const qsizetype vertexCount = static_cast<qsizetype>(vertices.size() / 10);

        QByteArray positionData;
        QByteArray normalData;
        QByteArray colorData;
        QByteArray indexData;
        positionData.reserve(static_cast<int>(vertexCount * 3 * sizeof(float)));
        normalData.reserve(static_cast<int>(vertexCount * 3 * sizeof(float)));
        colorData.reserve(static_cast<int>(vertexCount * 4 * sizeof(float)));
        indexData.reserve(static_cast<int>(indices.size() * sizeof(std::uint32_t)));

        QVector3D minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        QVector3D maximum(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

        for (qsizetype vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
            const qsizetype base = vertexIndex * 10;
            const float px = vertices[base + 0];
            const float py = vertices[base + 1];
            const float pz = vertices[base + 2];
            const float nx = vertices[base + 3];
            const float ny = vertices[base + 4];
            const float nz = vertices[base + 5];
            const float r = vertices[base + 6];
            const float g = vertices[base + 7];
            const float b = vertices[base + 8];
            const float a = vertices[base + 9];

            appendRawValue(positionData, px);
            appendRawValue(positionData, py);
            appendRawValue(positionData, pz);
            appendRawValue(normalData, nx);
            appendRawValue(normalData, ny);
            appendRawValue(normalData, nz);
            appendRawValue(colorData, r);
            appendRawValue(colorData, g);
            appendRawValue(colorData, b);
            appendRawValue(colorData, a);

            minimum.setX(std::min(minimum.x(), px));
            minimum.setY(std::min(minimum.y(), py));
            minimum.setZ(std::min(minimum.z(), pz));
            maximum.setX(std::max(maximum.x(), px));
            maximum.setY(std::max(maximum.y(), py));
            maximum.setZ(std::max(maximum.z(), pz));
        }

        for (const GLuint index : indices) {
            appendRawValue(indexData, static_cast<std::uint32_t>(index));
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
                               {"target", kArrayBuffer}});
        bufferViews.push_back(
            {{"buffer", 0}, {"byteOffset", static_cast<int>(normalOffset)}, {"byteLength", normalData.size()}, {"target", kArrayBuffer}});
        bufferViews.push_back(
            {{"buffer", 0}, {"byteOffset", static_cast<int>(colorOffset)}, {"byteLength", colorData.size()}, {"target", kArrayBuffer}});
        bufferViews.push_back({{"buffer", 0},
                               {"byteOffset", static_cast<int>(indexOffset)},
                               {"byteLength", indexData.size()},
                               {"target", kElementArrayBuffer}});

        accessors.push_back({{"bufferView", bufferViewBase + 0},
                             {"componentType", kFloatComponent},
                             {"count", static_cast<int>(vertexCount)},
                             {"type", "VEC3"},
                             {"min", {minimum.x(), minimum.y(), minimum.z()}},
                             {"max", {maximum.x(), maximum.y(), maximum.z()}}});
        accessors.push_back({{"bufferView", bufferViewBase + 1},
                             {"componentType", kFloatComponent},
                             {"count", static_cast<int>(vertexCount)},
                             {"type", "VEC3"}});
        accessors.push_back({{"bufferView", bufferViewBase + 2},
                             {"componentType", kFloatComponent},
                             {"count", static_cast<int>(vertexCount)},
                             {"type", "VEC4"}});
        accessors.push_back({{"bufferView", bufferViewBase + 3},
                             {"componentType", kUnsignedIntComponent},
                             {"count", static_cast<int>(indices.size())},
                             {"type", "SCALAR"}});

        return {
            {"attributes", {{"POSITION", accessorBase + 0}, {"NORMAL", accessorBase + 1}, {"COLOR_0", accessorBase + 2}}},
            {"indices", accessorBase + 3},
            {"material", materialIndex},
        };
    }
}  // namespace

bool VoxelWidget::exportGlb(const QString& filePath, QString* errorMessage) const {
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage) *errorMessage = message;
        return false;
    };

    if (filePath.isEmpty()) return fail(tr("Export path is empty."));

    const auto bounds = currentExportBounds();
    if (!bounds) return fail(tr("There is no voxel data to export."));

    std::vector<float> opaqueVertices;
    std::vector<GLuint> opaqueIndices;
    std::vector<float> transparentVertices;
    std::vector<GLuint> transparentIndices;
    appendVisibleVoxelMesh(*bounds, opaqueVertices, opaqueIndices, &transparentVertices, &transparentIndices,
                           MeshOcclusionMode::RenderView);

    if ((opaqueVertices.empty() || opaqueIndices.empty()) && (transparentVertices.empty() || transparentIndices.empty())) {
        return fail(tr("The selected range has no visible voxel faces."));
    }

    QByteArray binaryChunk;
    nlohmann::json bufferViews = nlohmann::json::array();
    nlohmann::json accessors = nlohmann::json::array();
    nlohmann::json primitives = nlohmann::json::array();
    if (!opaqueVertices.empty() && !opaqueIndices.empty()) {
        primitives.push_back(appendMeshPrimitive(opaqueVertices, opaqueIndices, 0, binaryChunk, bufferViews, accessors));
    }
    if (!transparentVertices.empty() && !transparentIndices.empty()) {
        primitives.push_back(appendMeshPrimitive(transparentVertices, transparentIndices, 1, binaryChunk, bufferViews, accessors));
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

    constexpr std::uint32_t kGlbMagic = 0x46546C67;
    constexpr std::uint32_t kGlbVersion = 2;
    constexpr std::uint32_t kJsonChunkType = 0x4E4F534A;
    constexpr std::uint32_t kBinaryChunkType = 0x004E4942;

    QByteArray glb;
    const std::uint32_t totalLength = static_cast<std::uint32_t>(12 + 8 + jsonChunk.size() + 8 + binaryChunk.size());
    appendRawValue(glb, kGlbMagic);
    appendRawValue(glb, kGlbVersion);
    appendRawValue(glb, totalLength);
    appendRawValue(glb, static_cast<std::uint32_t>(jsonChunk.size()));
    appendRawValue(glb, kJsonChunkType);
    glb.append(jsonChunk);
    appendRawValue(glb, static_cast<std::uint32_t>(binaryChunk.size()));
    appendRawValue(glb, kBinaryChunkType);
    glb.append(binaryChunk);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return fail(tr("Failed to open export file: %1").arg(file.errorString()));
    }
    if (file.write(glb) != glb.size()) {
        return fail(tr("Failed to write export file: %1").arg(file.errorString()));
    }
    if (!file.commit()) {
        return fail(tr("Failed to finish export file: %1").arg(file.errorString()));
    }

    return true;
}
