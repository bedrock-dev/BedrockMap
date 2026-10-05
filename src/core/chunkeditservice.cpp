#include "chunkeditservice.h"

#include "loguru/loguru.hpp"
#include "regioncachemanager.h"

ChunkEditService::ChunkEditService(ChunkStorage& storage, RegionCacheManager& cacheManager, ChunkCoordsService& coordsService,
                                   const std::atomic_bool& loaded, const bool& preloadAllChunkCoords)
    : storage_(storage),
      cache_manager_(cacheManager),
      coords_service_(coordsService),
      loaded_(loaded),
      preload_all_chunk_coords_(preloadAllChunkCoords) {}

void ChunkEditService::markChanged(const bl::chunk_pos& pos, bool present) {
    // Update the coordinate index on the edit thread.
    if (!preload_all_chunk_coords_) return;
    coords_service_.update(pos, present);
}

bl::chunk* ChunkEditService::getChunk(const bl::chunk_pos& pos, bl::chunk_load_policy policy) {
    if (!loaded_.load(std::memory_order_acquire)) return nullptr;
    return storage_.getChunk(pos, policy);
}

std::optional<bl::raw_chunk> ChunkEditService::getRawChunk(const bl::chunk_pos& pos) {
    if (!loaded_.load(std::memory_order_acquire)) return std::nullopt;
    return storage_.getRawChunk(pos);
}

void ChunkEditService::clearChunkCache(const bl::chunk_pos& pos) { cache_manager_.removeRegion(constant::c2r(pos)); }

bool ChunkEditService::canEdit() const { return loaded_.load(std::memory_order_acquire); }

bool ChunkEditService::deleteChunk(const bl::chunk_pos& pos) {
    if (!canEdit()) return false;
    storage_.putMissing(pos);
    markChanged(pos, false);
    return true;
}

bool ChunkEditService::putRawChunk(const bl::raw_chunk& raw) {
    if (!canEdit()) return false;
    storage_.putRawChunk(raw);
    markChanged(raw.pos(), true);
    return true;
}

bool ChunkEditService::createVoid(const bl::chunk_pos& pos) {
    if (!canEdit()) return false;
    auto raw = getRawChunk(pos);
    if (raw.has_value()) {
        raw->clear_terrain();
        return putRawChunk(raw.value());
    }

    const auto format = storage_.level().chunk_format();
    bl::raw_chunk created(pos);
    created.set_chunk_format(format);
    const auto marker = bl::is_new_chunk_format(format) ? bl::chunk_key::VersionNew : bl::chunk_key::VersionOld;
    created.set_normal(marker, std::string(1, static_cast<char>(format)));
    return putRawChunk(created);
}

bool ChunkEditService::setRawChunkBiome(const bl::chunk_pos& pos, bl::biome biome) {
    if (!canEdit()) return false;
    auto raw = getRawChunk(pos);
    if (!raw.has_value()) return false;
    if (!bl::set_raw_chunk_biome(raw.value(), biome)) return false;
    return putRawChunk(raw.value());
}

bool ChunkEditService::commit() {
    LOG_F(INFO, "Commit chunks change");
    if (!loaded_.load(std::memory_order_acquire)) return true;
    return storage_.commit();
}

bool ChunkEditService::commitEdits(const std::unordered_map<std::string, std::string>& globalModifies,
                                   const bl::nbt::compound_tag* levelDat) {
    if (!loaded_.load(std::memory_order_acquire)) return false;
    return storage_.commit(globalModifies, levelDat);
}
