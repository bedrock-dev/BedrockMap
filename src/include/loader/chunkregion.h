#ifndef BEDROCKMAP_CHUNKREGION_H
#define BEDROCKMAP_CHUNKREGION_H
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QRunnable>
#include <algorithm>
#include <bitset>
#include <deque>
#include <memory>
#include <utility>

#include "base/config.h"
#include "bedrock_key.h"
#include "chunk.h"
#include "data_3d.h"
#include "loader/mapfilter.h"
class AsyncLevelLoader;
class ResourceManager;

namespace bl {

    inline uint qHash(const bl::chunk_pos& key, uint seed) {
        uint hash = 3241;
        hash = 3457689L * hash + key.x;
        hash = 8734625L * hash + key.z;
        hash = 2873465L * hash + key.dim;
        return hash;
    }
}  // namespace bl

struct BlockTipsInfo {
    // top
    int16_t height{-128};
    int16_t block_id{-1};
    bl::biome biome{bl::none};
    // top solid
    int16_t solid_height{-128};
    // Terrain keeps the unblended base colour and applies the biome palette
    // after sampling. Tint kind uses bl::biome_tint_kind's underlying values.
    uint32_t gpu_base_color{0};
    uint8_t gpu_tint_kind{0};
    bool gpu_water_overlay{false};
    // The tile sampler uses this bit to apply the biome filter.
    bool gpu_biome_visible{false};
};

struct ChunkRegion {
    ~ChunkRegion();
    struct ActorCount {
        bl::vec3 pos{0, 0, 0};
        int count{0};
    };

    std::array<std::array<BlockTipsInfo, constant::RW << 4>, constant::RW << 4> tips_info_{};
    std::bitset<constant::RW * constant::RW> chunk_bit_map_;
    bool valid{false};
    /// Monotonic identity assigned when the loader caches this bake. It is used
    /// instead of the heap address so recycled allocator addresses cannot make a
    /// stale tile cache entry look current.
    quint64 revision{0};
    std::unordered_map<const QImage*, std::vector<bl::vec3>> actors_;             // for render mode 0
    std::map<bl::chunk_pos, std::map<const QImage*, ActorCount>> actors_counts_;  // for render mode 1
    std::vector<bl::hardcoded_spawn_area> HSAs_;

    // Resolve a block name to an id. Known blocks get a global id >= 0 (from the bedrock-level
    // table, stored without "minecraft:"); unknown/mod blocks get a negative local id backed by
    // local_block_names_ which keeps the full name (with namespace) for later display.
    int internBlockName(const std::string& name);
    // Reverse of internBlockName; returns the full display name ("minecraft:" + name or the
    // original mod name), empty string if the id is invalid.
    std::string blockName(int id) const;

    // Region-local table for block names that are absent from the global color table.
    std::vector<std::string> local_block_names_;
    std::unordered_map<std::string, int> local_block_ids_;

    /// Approximate retained heap size, including the block tips grid, actors
    /// and local block-name tables.
    [[nodiscard]] qint64 memoryCost() const;
};

struct RegionTimer {
    std::deque<int64_t> values;

    [[nodiscard]] int64_t mean() const;

    void push(int64_t value);
};

struct TerrainTileRequest {
    int blocks_per_texel{0};
    int layer{0};
    bool enabled{false};
};

struct TerrainTileSnapshot {
    region_pos region;
    int blocks_per_texel{1};
    int layer{0};
    QImage color;
    QImage surface;
    /// Whether this region contains at least one existing chunk. Present
    /// chunks may still contain only void terrain.
    bool has_chunks{false};
};

struct RegionRenderResult {
    region_pos pos;
    std::shared_ptr<ChunkRegion> region;
    qint64 load_time{0};
    qint64 render_time{0};
    TerrainTileSnapshot tile;
};

Q_DECLARE_METATYPE(RegionRenderResult)

class LoadRegionTask : public QObject, public QRunnable {
    Q_OBJECT

   public:
    LoadRegionTask(AsyncLevelLoader* loader, ResourceManager* resources, const bl::chunk_pos& pos, MapFilter filter,
                   TerrainTileRequest tile_request)
        : QRunnable(), loader_(loader), resources_(resources), pos_(pos), filter_(std::move(filter)), tile_request_(tile_request) {}
    void run() override;

   public:
   signals:

    void finish(RegionRenderResult result);

   private:
    AsyncLevelLoader* loader_;
    ResourceManager* resources_;
    region_pos pos_;
    MapFilter filter_;
    TerrainTileRequest tile_request_;
};

#endif  // BEDROCKMAP_CHUNKREGION_H
