#include "chunk_task.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "asynclevelloader.h"
#include "bedrock_key.h"
#include "color.h"
#include "config.h"
#include "maptile.h"
#include "renderprofile.h"

void RegionTimer::push(int64_t value) {
    this->values.push_back(value);
    if (this->values.size() > 20) {
        this->values.pop_front();
    }
}

ChunkRegion::~ChunkRegion() = default;

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
    RenderProfile::instance().regions.fetch_add(1, std::memory_order_relaxed);

    auto* region = new ChunkRegion();
    bl::chunk* chunks_[constant::RW * constant::RW]{nullptr};
    {
        ScopedPhase phase(RenderPhase::ChunkLoad);
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

    const auto IMG_WIDTH = constant::RW << 4;
    int chunk_count = 0;

    if (region->valid) {
        for (int rw = 0; rw < constant::RW; rw++) {
            for (int rh = 0; rh < constant::RW; rh++) {
                auto* chunk = chunks_[rw * constant::RW + rh];
                region->chunk_bit_map_.set(rw * constant::RW + rh, chunk != nullptr);
            }
        }

        {
            ScopedPhase phase(RenderPhase::HeightMap);
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

        auto img = MapTile::CREATE_REGION_TILE(region->chunk_bit_map_, !this->loader_->transparentVoid());
        region->flat_color_image_ = img;
        region->terrain_bake_image_ = img;
        region->biome_bake_image_ = img;
        for (int rw = 0; rw < constant::RW; rw++) {
            for (int rh = 0; rh < constant::RW; rh++) {
                auto* chunk = chunks_[rw * constant::RW + rh];
                if (!chunk) continue;
                chunk_count++;
                MapTile::bakeChunkTerrain(chunk, this->filter_, rw, rh, region);
                {
                    ScopedPhase phase(RenderPhase::Actors);
                    MapTile::bakeChunkActors(chunk, this->filter_, region);
                }
                auto& hss = chunk->HSAs();
                region->HSAs_.insert(region->HSAs_.end(), hss.begin(), hss.end());
            }
        }

        if (setting::current().MAP_RENDER_STYLE == 0) {
            MapTile::renderStyle0(region, IMG_WIDTH);
        } else if (setting::current().MAP_RENDER_STYLE == 1) {
            MapTile::renderStyle1(region, IMG_WIDTH);
        } else if (setting::current().MAP_RENDER_STYLE == 2) {
            MapTile::renderStyle2(region, IMG_WIDTH, this->loader_, this->pos_);
        }
    }

    auto total_end = std::chrono::steady_clock::now();
    auto load_time = std::chrono::duration_cast<std::chrono::microseconds>(load_end - begin).count();
    auto render_time = std::chrono::duration_cast<std::chrono::microseconds>(total_end - load_end).count();
    if (chunk_count > 0) {
        load_time /= chunk_count;
        render_time /= chunk_count;
    }
    emit finish(this->pos_.x, this->pos_.z, this->pos_.dim, region, load_time, render_time, chunks_);
    for (auto* chunk : chunks_) delete chunk;
}

int64_t RegionTimer::mean() const {
    return this->values.empty() ? 0 : std::accumulate(values.begin(), values.end(), 0ll) / static_cast<int64_t>(values.size());
}
