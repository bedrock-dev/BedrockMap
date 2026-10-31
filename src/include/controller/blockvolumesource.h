#ifndef BEDROCKMAP_BLOCKVOLUMESOURCE_H
#define BEDROCKMAP_BLOCKVOLUMESOURCE_H

#include <QByteArray>
#include <QRegion>
#include <QString>
#include <QUrl>
#include <functional>
#include <memory>

#include "geometry.h"
#include "nbt.h"
#include "render/voxel_scene.h"

class AsyncLevelLoader;

struct BlockVolumeLoadResult {
    std::shared_ptr<const VoxelSceneData> scene;
    std::unique_ptr<bl::nbt::compound_tag> documentRoot;
    QString documentTitle;
    QString error;
    int chunkX{0};
    int chunkZ{0};
    int dimension{0};
    QString yRange;

    [[nodiscard]] bool valid() const { return scene && error.isEmpty(); }
};

struct BlockVolumeMutationResult {
    bool success{false};
    QRegion region;
    int dimension{0};
};

/// Backend-specific data access for the shared block-volume controller.
class BlockVolumeSource {
   public:
    virtual ~BlockVolumeSource() = default;

    [[nodiscard]] virtual bool blockEditingEnabled() const { return false; }
    virtual bool configureStructure(const QUrl&) { return false; }
    virtual bool configureBytes(const QByteArray&) { return false; }
    virtual bool configureArea(int, int, int, int, int) { return false; }
    virtual bool configureImport(const QUrl&) { return false; }
    virtual bool configureImportData(const QByteArray&) { return false; }
    virtual void setLevelLoader(AsyncLevelLoader*) {}
    virtual std::shared_ptr<BlockVolumeLoadResult> load(const std::function<void(int)>&) = 0;
    virtual std::shared_ptr<BlockVolumeLoadResult> loadImport(const std::function<void(int)>&) { return {}; }
    [[nodiscard]] virtual QByteArray exportStructure(const VoxelSceneData&, const bl::block_box&, bool, bool) const { return {}; }
    [[nodiscard]] virtual BlockVolumeMutationResult commitImport(const VoxelSceneData&, const bl::block_box&, bool) { return {}; }
    [[nodiscard]] virtual BlockVolumeMutationResult deleteBlocks(const VoxelSceneData&, const bl::block_box&, bool) { return {}; }
};

#endif  // BEDROCKMAP_BLOCKVOLUMESOURCE_H
