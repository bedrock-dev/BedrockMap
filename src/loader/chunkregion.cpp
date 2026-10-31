#include "loader/chunkregion.h"

#include <QString>
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "base/config.h"
#include "base/resourcemanager.h"
#include "bedrock_key.h"
#include "color.h"
#include "data_3d.h"
#include "loader/asynclevelloader.h"
#include "render/rendertimings.h"
#include "render/terraintiles.h"
#include "sub_chunk.h"

namespace {
    void readTerrainColumn(ChunkRegion& region, bl::chunk& chunk, const MapFilter& filter, int rw, int rh, int chx, int chz, int y,
                           int y_solid) {
        const int x = (rw << 4) + chx;
        const int z = (rh << 4) + chz;
        const auto raw_info = chunk.get_block_with_color(chx, y, chz);
        const auto biome = chunk.get_biome(chx, y, chz);

        bl::block_appearance solid_info = raw_info;
        int16_t solid_height = static_cast<int16_t>(y);
        if (y_solid >= 0 && y_solid < y) {
            solid_info = chunk.get_block_with_color(chx, y_solid, chz);
            solid_height = static_cast<int16_t>(y_solid);
        }

        bl::block_appearance render_info = raw_info;
        const bool transparent_water_overlay =
            setting::current().transparentWater && raw_info.name == "minecraft:water" && y_solid >= 0 && y_solid < y;
        if (transparent_water_overlay) render_info = solid_info;

        auto& tips = region.tips_info_[x][z];
        tips.block_id = region.internBlockName(render_info.name);
        tips.biome = chunk.get_top_biome(chx, chz);
        tips.height = static_cast<int16_t>(y);
        tips.solid_height = solid_height;
        const auto& gpu_base = transparent_water_overlay ? solid_info : raw_info;
        tips.gpu_base_color = qRgba(gpu_base.color.r, gpu_base.color.g, gpu_base.color.b, gpu_base.color.a);
        tips.gpu_tint_kind = static_cast<uint8_t>(bl::block_biome_tint_kind(gpu_base.name));
        tips.gpu_water_overlay = transparent_water_overlay;
        tips.gpu_biome_visible = (filter.biomes_list_.count(biome) == 0) == filter.biome_black_mode_;
    }

    void readChunkTerrain(bl::chunk& chunk, const MapFilter& filter, int rw, int rh, ChunkRegion& region) {
        auto [min_y, max_y] = chunk.get_y_range();

        if (filter.enable_layer_) {
            if (filter.layer > max_y || filter.layer < min_y) return;
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    const auto& block = chunk.get_block_name(x, filter.layer, z);
                    if ((filter.blocks_list_.count(block) == 0) == filter.block_black_mode_)
                        readTerrainColumn(region, chunk, filter, rw, rh, x, z, filter.layer, -1);
                }
            }
            return;
        }

        if (filter.isBlockFilterDefault()) {
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    auto [top_y, solid_y] = chunk.get_top_y(x, z, 319);
                    if (top_y >= min_y) readTerrainColumn(region, chunk, filter, rw, rh, x, z, top_y, solid_y);
                }
            }
            return;
        }

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                int y = chunk.get_height(x, z);
                if (y < min_y) continue;
                bool found = false;
                int found_y = min_y - 1;
                int solid_y = min_y - 1;
                while (y >= min_y) {
                    const auto& block = chunk.get_block_name(x, y, z);
                    if (!found && (filter.blocks_list_.count(block) == 0) == filter.block_black_mode_) {
                        found = true;
                        found_y = y;
                        if (block != "minecraft:water") {
                            solid_y = y;
                            break;
                        }
                        --y;
                        continue;
                    }
                    if (found && block != "minecraft:air" && block != "minecraft:water") {
                        solid_y = y;
                        break;
                    }
                    --y;
                }
                if (found) readTerrainColumn(region, chunk, filter, rw, rh, x, z, found_y, solid_y);
            }
        }
    }

    void readChunkActors(bl::chunk& chunk, const MapFilter& filter, ChunkRegion& region, ResourceManager* resources) {
        auto entities = chunk.entities();
        const auto mode = setting::current().actorRenderStyle;
        for (auto& entity : entities) {
            const auto identifier = entity->identifier();
            auto key = QString::fromUtf8(identifier.data(), static_cast<qsizetype>(identifier.size())).replace("minecraft:", "");
            if ((filter.actors_list_.count(key.toStdString()) == 0) == filter.actor_black_mode_) {
                const QImage* icon = resources ? resources->actorImage(key) : nullptr;
                if (mode == 0) {
                    region.actors_[icon].push_back(entity->pos());
                } else {
                    auto& count = region.actors_counts_[chunk.get_pos()][icon];
                    count = {entity->pos(), count.count + 1};
                }
            }
        }
    }
}  // namespace

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
            const RenderTimingScope scope(timings.get(), QStringLiteral("Read terrain data"));
            for (int rw = 0; rw < constant::RW; rw++) {
                for (int rh = 0; rh < constant::RW; rh++) {
                    auto* chunk = chunks_[rw * constant::RW + rh];
                    if (!chunk) continue;
                    chunk_count++;
                    readChunkTerrain(*chunk, this->filter_, rw, rh, *region);
                }
            }
        }

        {
            const RenderTimingScope scope(timings.get(), QStringLiteral("Read actor data"));
            for (int rw = 0; rw < constant::RW; rw++) {
                for (int rh = 0; rh < constant::RW; rh++) {
                    auto* chunk = chunks_[rw * constant::RW + rh];
                    if (!chunk) continue;
                    readChunkActors(*chunk, this->filter_, *region, resources_);
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
    RegionRenderResult result;
    result.pos = pos_;
    result.region = std::shared_ptr<ChunkRegion>(region);
    result.load_time = load_time;
    result.render_time = render_time;
    result.tile = std::move(tile);
    emit finish(std::move(result));
    for (auto* chunk : chunks_) delete chunk;
}

int64_t RegionTimer::mean() const {
    return this->values.empty() ? 0 : std::accumulate(values.begin(), values.end(), 0ll) / static_cast<int64_t>(values.size());
}
