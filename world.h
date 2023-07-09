#ifndef WORLD_H
#define WORLD_H

#include <QCache>
#include <QFuture>
#include <QImage>
#include <QThreadPool>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "bedrock_level.h"
#include "color.h"
#include "config.h"
// data source
namespace bl {

    inline uint qHash(const bl::chunk_pos &key, uint seed) {
        uint hash = 3241;
        hash = 3457689L * hash + key.x;
        hash = 8734625L * hash + key.z;
        hash = 2873465L * hash + key.dim;
        return hash;
    }

}  // namespace bl

struct RenderFilter {
    bool enable_layer_{false};
    int layer_ = {0};
};


struct LayerCacheInfo {
    QImage *terrain{nullptr};
    QImage *biome{nullptr};
    std::array<std::array<BlockTipsInfo, cfg::RW << 4>, cfg::RW << 4> info_;
    std::unordered_map<QImage *, std::vector<bl::vec3>> actor_list_;


    static LayerCacheInfo *fromRegion(chunk_region *r);


};


class world {
public:
    world();


    static QImage *EMPTY_IMAGE();

    inline bool is_open() const { return this->loaded_; }

    QFuture<bl::chunk *> getChunkDirect(const bl::chunk_pos &p);

    AsyncLevelLoader &getLevelLoader() { return this->level_loader_; }

    bool init(const std::string &level_path);

    void close();

    inline void setFilter(const RenderFilter &filter) { this->render_filter_ = filter; }

    QImage *topBiome(const region_pos &p);

    QImage *topBlock(const region_pos &p);

    QImage *height(const region_pos &p);

    QImage *slimeChunk(const bl::chunk_pos &p);


    std::unordered_map<QImage *, std::vector<bl::vec3>> getActorList(const bl::chunk_pos &p);

    std::vector<QString> debug_info() {
        if (!this->loaded_) return {};
        auto r = this->level_loader_.debug_info();
        r.push_back(
                QString("layer image cache: %1/%2")
                        .arg(QString::number(layer_cache_->totalCost()),
                             QString::number(layer_cache_->maxCost())));
        return r;
    }


    void clear_all_cache();

    bl::bedrock_level &level() { return this->level_loader_.level(); }

    BlockTipsInfo getBlockTips(const bl::block_pos &p, int dim) {
        auto cp = p.to_chunk_pos();
        cp.dim = dim;
        auto region_pos = cfg::c2r(cp);
        auto *info = this->layer_cache_->operator[](region_pos);
        if (!info)return BlockTipsInfo{};
        auto bp = region_pos.get_min_pos(bl::ChunkVersion::New);
        return info->info_[p.x - bp.x][p.z - bp.z];
    }

private:
    static void initBiomeColorTable();

    QCache<bl::chunk_pos, LayerCacheInfo> *layer_cache_;
    AsyncLevelLoader level_loader_;
    bool loaded_{false};
    RenderFilter render_filter_;
    // thread pool

};

#endif  // WORLD_H
