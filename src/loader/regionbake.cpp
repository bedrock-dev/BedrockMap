#include "loader/regionbake.h"

#include <QString>
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "base/config.h"
#include "bedrock_key.h"
#include "color.h"
#include "loader/asynclevelloader.h"
#include "render/maptile.h"
#include "render/rendertimings.h"
#include "render/terraintiles.h"

void RegionTimer::push(int64_t value) {
    this->values.push_back(value);
    if (this->values.size() > 20) {
        this->values.pop_front();
    }
}

ChunkRegion::~ChunkRegion() = default;

qint64 ChunkRegion::memoryCost() const {
    qint64 bytes = static_cast<qint64>(sizeof(ChunkRegion));
    for (const auto& [image, positions] : actors_) {
        Q_UNUSED(image);
        bytes += static_cast<qint64>(positions.capacity() * sizeof(bl::vec3));
    }
    for (const auto& [chunk, counts] : actors_counts_) {
        Q_UNUSED(chunk);
        for (const auto& [image, count] : counts) {
            Q_UNUSED(image);
            bytes += static_cast<qint64>(sizeof(count));
        }
    }
    bytes += static_cast<qint64>(HSAs_.capacity() * sizeof(bl::hardcoded_spawn_area));
    for (const auto& name : local_block_names_) bytes += static_cast<qint64>(name.capacity());
    for (const auto& [name, id] : local_block_ids_) {
        Q_UNUSED(id);
        bytes += static_cast<qint64>(sizeof(name) + name.capacity());
    }
    return std::max<qint64>(bytes, 1);
}

int ChunkRegion::internBlockName(const std::string& name) {
    const int gid = bl::block_name_to_runtime_id(name);
    if (gid >= 0) return gid;
    auto it = this->local_block_ids_.find(name);
    if (it != this->local_block_ids_.end()) return it->second;
    this->local_block_names_.push_back(name);
    const int lid = -static_cast<int>(this->local_block_names_.size());
    this->local_block_ids_.emplace(name, lid);
    return lid;
}

std::string ChunkRegion::blockName(int id) const {
    if (id >= 0) return bl::block_runtime_id_to_full_name(id);
    const int idx = -id - 1;
    if (idx >= 0 && idx < static_cast<int>(this->local_block_names_.size())) {
        return this->local_block_names_[idx];
    }
    return std::string();
}

void LoadRegionTask::run() {
    auto begin = std::chrono::steady_clock::now();
    const auto timings = loader_ ? loader_->timings() : std::shared_ptr<RenderTimings>{};
    const RenderTimingScope region_scope(timings.get(), QStringLiteral("Region bake"));

    auto* region = new ChunkRegion();
    bl::chunk* chunks_[constant::RW * constant::RW]{nullptr};
    {
        const RenderTimingScope scope(timings.get(), QStringLiteral("Chunk load"));
        for (int i = 0; i < constant::RW; i++) {
            for (int j = 0; j < constant::RW; j++) {
                bl::chunk_pos p{this->pos_.x + i, this->pos_.z + j, this->pos_.dim};
                if (this->loader_->isChunkAbsent(p)) continue;
                chunks_[i * constant::RW + j] = this->loader_->getChunk(
                    p, bl::chunk_load_policy::Terrain | bl::chunk_load_policy::Actor | bl::chunk_load_policy::Others);
            }
        }
    }

    auto load_end = std::chrono::steady_clock::now();

    for (auto& chunk : chunks_) {
        if (chunk && chunk->loaded()) {
            region->valid = true;
            break;
        }
    }

    int chunk_count = 0;

    if (region->valid) {
        for (int rw = 0; rw < constant::RW; rw++) {
            for (int rh = 0; rh < constant::RW; rh++) {
                auto* chunk = chunks_[rw * constant::RW + rh];
                region->chunk_bit_map_.set(rw * constant::RW + rh, chunk != nullptr);
            }
        }

        {
            const RenderTimingScope scope(timings.get(), QStringLiteral("Height map"));
            for (int i = 0; i < constant::RW; i++) {
                for (int j = 0; j < constant::RW; j++) {
                    auto* chunk = chunks_[i * constant::RW + j];
                    if (!chunk || !chunk->loaded()) continue;
                    bl::chunk_pos cp(this->pos_.x + i, this->pos_.z + j, this->pos_.dim);
                    int miny = chunk->get_y_range().first;
                    std::array<int16_t, 256> hm;
                    for (int x = 0; x < 16; x++) {
                        for (int z = 0; z < 16; z++) {
                            int h = chunk->get_height(x, z);
                            // Convert non-void heights to world coordinates.
                            hm[x + z * 16] = (h <= -128 + miny) ? static_cast<int16_t>(-128) : static_cast<int16_t>(h);
                        }
                    }
                    this->loader_->putHeightMap(cp, hm);
                }
            }
        }

        {
            const RenderTimingScope scope(timings.get(), QStringLiteral("Terrain bake"));
            for (int rw = 0; rw < constant::RW; rw++) {
                for (int rh = 0; rh < constant::RW; rh++) {
                    auto* chunk = chunks_[rw * constant::RW + rh];
                    if (!chunk) continue;
                    chunk_count++;
                    MapTile::bakeChunkTerrain(chunk, this->filter_, rw, rh, region);
                }
            }
        }

        {
            const RenderTimingScope scope(timings.get(), QStringLiteral("Actors bake"));
            for (int rw = 0; rw < constant::RW; rw++) {
                for (int rh = 0; rh < constant::RW; rh++) {
                    auto* chunk = chunks_[rw * constant::RW + rh];
                    if (!chunk) continue;
                    MapTile::bakeChunkActors(chunk, this->filter_, region);
                    auto& hss = chunk->HSAs();
                    region->HSAs_.insert(region->HSAs_.end(), hss.begin(), hss.end());
                }
            }
        }

    }

    TerrainTileSnapshot tile;
    if (tile_request_.enabled && region->valid) {
        tile = TerrainTiles::buildSnapshot(*region, this->pos_, tile_request_.blocks_per_texel, tile_request_.layer);
    }

    auto total_end = std::chrono::steady_clock::now();
    auto load_time = std::chrono::duration_cast<std::chrono::microseconds>(load_end - begin).count();
    auto render_time = std::chrono::duration_cast<std::chrono::microseconds>(total_end - load_end).count();
    if (chunk_count > 0) {
        load_time /= chunk_count;
        render_time /= chunk_count;
    }
    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, region, load_time, render_time, chunks_, tile.blocks_per_texel, tile.layer,
                tile.color, tile.height, tile.material);
    for (auto* chunk : chunks_) delete chunk;
}

int64_t RegionTimer::mean() const {
    return this->values.empty() ? 0 : std::accumulate(values.begin(), values.end(), 0ll) / static_cast<int64_t>(values.size());
}
