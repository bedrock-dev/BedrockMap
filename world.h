#ifndef WORLD_H
#define WORLD_H
#include <QCache>
#include <QImage>
#include <QThreadPool>
#include <string>
#include <unordered_map>
#include <utility>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "bedrock_level.h"
#include "color.h"
#include "lrucache.h"

// data soruce
namespace bl {

inline uint qHash(const bl::chunk_pos& key, uint seed) {
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

    bool init(const std::string& level_path);

    QImage* topBiome(const bl::chunk_pos& p);

   private:
    static void initBiomeColorTable();

    LRUCache<bl::chunk_pos, QImage>* top_biome_image_cache_;
    LRUCache<bl::chunk_pos, QImage>* top_block_image_cache_;

    AsyncLevelLoader level_loader_;

    // thread pool
};

#endif  // WORLD_H
