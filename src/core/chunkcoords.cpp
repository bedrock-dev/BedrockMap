#include "chunkcoords.h"

#include <leveldb/db.h>
#include <leveldb/iterator.h>

#include "chunk.h"
#include "loguru/loguru.hpp"
#include "maptile.h"

namespace bl::config {
    bool strict_chunk_existence();
}

void CoordsRegion::generateImage() const {
    auto image = MapTile::COORDS_EMPTY_TILE().copy();
    for (int x = 0; x < SIZE; ++x) {
        for (int z = 0; z < SIZE; ++z) {
            if (chunk_mask_.test(static_cast<std::size_t>(x * SIZE + z))) image.setPixel(x, z, 0xffffffffu);
        }
    }
    image_ = std::move(image);
}

bool ChunkCoordsIndex::load(leveldb::DB *db, const std::atomic_bool &stop) {
    if (!db) return false;

    clear();
    auto *iterator = db->NewIterator(leveldb::ReadOptions());
    for (iterator->SeekToFirst(); iterator->Valid(); iterator->Next()) {
        if (stop.load(std::memory_order_acquire)) {
            delete iterator;
            return false;
        }

        const auto chunk_key = bl::chunk_key::parse(iterator->key().ToString());
        for (auto marker : bl::raw_chunk::MARKER_KEYS) {
            if (chunk_key.type == marker && (!bl::config::strict_chunk_existence() || !iterator->value().empty())) {
                insertUnlocked(chunk_key.cp);
                break;
            }
        }
    }

    const auto status = iterator->status();
    delete iterator;
    if (!status.ok()) {
        LOG_F(WARNING, "Failed while preloading chunk coordinates: %s", status.ToString().c_str());
        return false;
    }
    if (stop.load(std::memory_order_acquire)) return false;

    for (auto &[dim, regions] : regions_by_dimension_) {
        (void)dim;
        for (auto &region : regions) region.generateImage();
    }
    for (const auto &[dim, count] : dimensionCounts()) {
        LOG_F(INFO, "Preloaded chunk coordinates: dimension %d, %zu chunks", dim, count);
    }
    beginInteractivePhase();
    return true;
}

bool ChunkCoordsIndex::remove(const bl::chunk_pos &pos) {
    auto lock = lockForInteractive();
    if (!lock.owns_lock()) return false;
    return removeUnlocked(pos);
}

bool ChunkCoordsIndex::removeUnlocked(const bl::chunk_pos &pos) {
    if (!validDimension(pos.dim)) return false;
    const auto dim_it = regions_by_dimension_.find(pos.dim);
    if (dim_it == regions_by_dimension_.end()) return false;

    auto region_it = dim_it->second.find(CoordsRegion::fromChunk(pos));
    if (region_it == dim_it->second.end()) return false;
    const bool removed = region_it->removeChunk(pos);
    if (!removed) return false;
    if (region_it->empty()) {
        dim_it->second.erase(region_it);
    } else {
        region_it->generateImage();
    }
    rebuildBoundingBox(pos.dim);
    return true;
}

bool ChunkCoordsIndex::updateChunk(const bl::chunk_pos &pos, bool present) {
    auto lock = lockForInteractive();
    if (!lock.owns_lock()) return false;
    const bool changed = present ? insertUnlocked(pos) : removeUnlocked(pos);
    if (!changed) return false;

    if (present) {
        const auto dim_it = regions_by_dimension_.find(pos.dim);
        if (dim_it != regions_by_dimension_.end()) {
            const auto region_it = dim_it->second.find(CoordsRegion::fromChunk(pos));
            if (region_it != dim_it->second.end()) region_it->generateImage();
        }
    }
    rebuildBoundingBox(pos.dim);
    return true;
}

void ChunkCoordsIndex::enqueueUpdate(const bl::chunk_pos &pos, bool present, std::function<void()> finished) {
    update_pool_.start(QRunnable::create([this, pos, present, finished = std::move(finished)]() mutable {
        updateChunk(pos, present);
        if (finished) finished();
    }));
}

void ChunkCoordsIndex::rebuildBoundingBox(int32_t dim) {
    ChunkCoordsBoundingBox bounds;
    const auto dim_it = regions_by_dimension_.find(dim);
    if (dim_it != regions_by_dimension_.end()) {
        for (const auto &region : dim_it->second) {
            region.forEachPresentChunk([&bounds](int32_t x, int32_t z) { bounds.include(bl::chunk_pos{x, z, 0}); });
        }
    }
    if (bounds.valid) {
        bounds_by_dimension_[dim] = bounds;
    } else {
        bounds_by_dimension_.erase(dim);
    }
}
