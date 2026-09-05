#include "chunkeditservice.h"

#include "loguru/loguru.hpp"
#include "regioncachemanager.h"

ChunkEditService::ChunkEditService(ChunkStorage &storage, RegionCacheManager &cacheManager, ChunkCoordsService &coordsService,
                                   const std::atomic_bool &loaded, const bool &preloadAllChunkCoords, Callback dirtyChanged,
                                   Callback coordsChanged)
    : storage_(storage),
      cache_manager_(cacheManager),
      coords_service_(coordsService),
      loaded_(loaded),
      preload_all_chunk_coords_(preloadAllChunkCoords),
      dirty_changed_(std::move(dirtyChanged)),
      coords_changed_(std::move(coordsChanged)) {}

bool ChunkEditService::canEdit() const {
    return loaded_.load(std::memory_order_acquire) && !coords_service_.loading(preload_all_chunk_coords_);
}

bl::chunk *ChunkEditService::getChunk(const bl::chunk_pos &pos, bl::chunk_load_policy policy) {
    if (!loaded_.load(std::memory_order_acquire)) return nullptr;
    return storage_.getChunk(pos, policy);
}

std::optional<bl::raw_chunk> ChunkEditService::getRawChunk(const bl::chunk_pos &pos) {
    if (!loaded_.load(std::memory_order_acquire)) return std::nullopt;
    return storage_.getRawChunk(pos);
}

void ChunkEditService::clearChunkCache(const bl::chunk_pos &pos) { cache_manager_.removeRegion(constant::c2r(pos)); }

void ChunkEditService::markChanged(const bl::chunk_pos &pos, bool present) {
    clearChunkCache(pos);
    if (!preload_all_chunk_coords_ || !coords_service_.ready()) return;
    coords_service_.enqueueUpdate(pos, present, coords_changed_);
}

void ChunkEditService::notifyDirty() {
    if (dirty_changed_) dirty_changed_();
}

bool ChunkEditService::deleteChunk(const bl::chunk_pos &pos) {
    if (!canEdit()) return false;
    storage_.putMissing(pos);
    markChanged(pos, false);
    notifyDirty();
    return true;
}

bool ChunkEditService::putRawChunk(const bl::raw_chunk &raw) {
    LOG_F(INFO, "put raw chunk %s", raw.pos().to_string().c_str());
    if (!canEdit()) return false;
    storage_.putRawChunk(raw);
    markChanged(raw.pos(), true);
    notifyDirty();
    return true;
}

bool ChunkEditService::createVoid(const bl::chunk_pos &pos) {
    if (!canEdit()) return false;
    auto raw = getRawChunk(pos);
    if (raw.has_value()) {
        raw->clear_terrain();
        return putRawChunk(raw.value());
    }
    // Keep the existing behavior until creation of a brand-new raw chunk is
    // implemented by the bedrock-level data layer.
    return true;
}

bool ChunkEditService::setRawChunkBiome(const bl::chunk_pos &pos, bl::biome biome) {
    if (!canEdit()) return false;
    auto raw = getRawChunk(pos);
    if (!raw.has_value()) return false;
    raw->set_biome(biome);
    return putRawChunk(raw.value());
}

bool ChunkEditService::commit() {
    LOG_F(INFO, "Commit chunks change");
    if (!loaded_.load(std::memory_order_acquire) || coords_service_.loading(preload_all_chunk_coords_)) return true;
    if (!storage_.commit()) return false;
    notifyDirty();
    return true;
}

bool ChunkEditService::commitEdits(const std::unordered_map<std::string, std::string> &globalModifies,
                                   const bl::nbt::compound_tag *levelDat) {
    if (!loaded_.load(std::memory_order_acquire) || coords_service_.loading(preload_all_chunk_coords_)) return false;
    if (!storage_.commit(globalModifies, levelDat)) return false;
    notifyDirty();
    return true;
}
