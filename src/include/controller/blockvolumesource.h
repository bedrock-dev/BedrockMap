#ifndef BEDROCKMAP_BLOCKVOLUMESOURCE_H
#define BEDROCKMAP_BLOCKVOLUMESOURCE_H

#include <QByteArray>
#include <QRegion>
#include <QString>
#include <QUrl>
#include <functional>
#include <memory>
#include <variant>

#include "geometry.h"
#include "nbt.h"
#include "render/voxel_scene.h"

class AsyncLevelLoader;
namespace bl {
    class mcstructure;
}

struct ChunkArea {
    int minChunkX{0};
    int minChunkZ{0};
    int maxChunkX{-1};
    int maxChunkZ{-1};
    int dimension{0};
};

/// What to load: an mcstructure file, raw mcstructure bytes, a parsed
/// mcstructure root, or a chunk area of the open world.
using BlockVolumeRequest = std::variant<QUrl, QByteArray, std::shared_ptr<const bl::nbt::compound_tag>, ChunkArea>;

/// An immutable loaded volume. Fields a backend does not use stay empty.
struct BlockVolume {
    std::shared_ptr<const VoxelSceneData> scene;
    std::shared_ptr<const bl::mcstructure> structure;
    std::shared_ptr<const bl::nbt::compound_tag> root;
    int dimension{0};
};

struct BlockVolumeLoadResult {
    std::shared_ptr<const BlockVolume> volume;
    QString error;

    [[nodiscard]] bool valid() const { return volume && volume->scene && error.isEmpty(); }
};

struct BlockVolumeMutationResult {
    bool success{false};
    QRegion region;
    int dimension{0};
};

/// Backend-specific data access for the shared block-volume controller.
/// Loads run on worker threads and must not depend on earlier requests.
class BlockVolumeSource {
   public:
    virtual ~BlockVolumeSource() = default;

    [[nodiscard]] virtual bool blockEditingEnabled() const { return false; }
    virtual void setLevelLoader(AsyncLevelLoader*) {}
    [[nodiscard]] virtual BlockVolumeLoadResult load(const BlockVolumeRequest& request,
                                                     const std::function<void(int)>& reportProgress) const = 0;
    [[nodiscard]] virtual QByteArray exportStructure(const BlockVolume& volume, const bl::block_box& localBounds, bool exportEntities,
                                                     bool useNewFormat) const = 0;
    [[nodiscard]] virtual BlockVolumeMutationResult commitImport(const BlockVolume&, const BlockVolume&, const bl::block_box&, bool) {
        return {};
    }
    [[nodiscard]] virtual BlockVolumeMutationResult deleteBlocks(const BlockVolume&, const bl::block_box&, bool) { return {}; }
};

#endif  // BEDROCKMAP_BLOCKVOLUMESOURCE_H
