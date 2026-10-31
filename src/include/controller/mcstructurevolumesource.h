#ifndef BEDROCKMAP_MCSTRUCTUREVOLUMESOURCE_H
#define BEDROCKMAP_MCSTRUCTUREVOLUMESOURCE_H

#include <QUrl>
#include <memory>

#include "controller/blockvolumesource.h"

namespace bl {
    class mcstructure;
}

/// Loads block volumes from mcstructure files. Editing is intentionally unsupported.
class McstructureVolumeSource final : public BlockVolumeSource {
   public:
    bool configureStructure(const QUrl& url) override;
    bool configureBytes(const QByteArray& raw) override;
    std::shared_ptr<BlockVolumeLoadResult> load(const std::function<void(int)>& reportProgress) override;
    [[nodiscard]] QByteArray exportStructure(const VoxelSceneData&, const bl::block_box& localBounds, bool exportEntities,
                                             bool useNewFormat) const override;
    [[nodiscard]] const bl::mcstructure* volume() const { return volume_.get(); }

   private:
    QUrl url_;
    QByteArray raw_;
    std::shared_ptr<const bl::mcstructure> volume_;
};

#endif  // BEDROCKMAP_MCSTRUCTUREVOLUMESOURCE_H
