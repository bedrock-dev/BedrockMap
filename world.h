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


class world {
public:
    world();


    static QImage *EMPTY_IMAGE();

    inline bool is_open() const { return this->loaded_; }


    QFuture<bl::chunk *> getChunkDirect(const bl::chunk_pos &p);

    bool init(const std::string &level_path);

    void close();

    QImage *topBiome(const region_pos &p);

    QImage *topBlock(const region_pos &p);

    QImage *height(const region_pos &p);

    QImage *slimeChunk(const bl::chunk_pos &p);

    std::vector<QString> debug_info() {
        if (!this->loaded_) return {};
        auto r = this->level_loader_.debug_info();
        r.push_back(
                QString("Top biome image cache: %1/%2")
                        .arg(QString::number(top_biome_image_cache_->totalCost()),
                             QString::number(top_biome_image_cache_->maxCost())));
        r.push_back(QString("Height image cache: %1/%2")
                            .arg(QString::number(height_image_cache_->totalCost()),
                                 QString::number(height_image_cache_->maxCost())));
        return r;
    }

private:
    static void initBiomeColorTable();

    QCache<bl::chunk_pos, QImage> *top_biome_image_cache_;

    QCache<bl::chunk_pos, QImage> *top_terrain_image_cache_;

    QCache<bl::chunk_pos, QImage> *height_image_cache_;

    AsyncLevelLoader level_loader_;
    bool loaded_{false};


    QThreadPool image_bake_pool_;

    // thread pool
};

#endif  // WORLD_H
