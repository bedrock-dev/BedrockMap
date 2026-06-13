#include "rawchunkcache.h"

#include "chunk.h"
#include "loguru/loguru.hpp"

std::pair<int, int> RawChunkCache::chunkCounts() const {
    int empty = 0, nonEmpty = 0;
    for (const auto &kv : cache_) {
        if (!kv.second.shouldDelete && !kv.second.chunk.get_sub_chunks().empty()) {
            nonEmpty++;
        } else {
            empty++;
        }
    }
    return {empty, nonEmpty};
}

void RawChunkCache::putChunk(const bl::chunk_pos &pos, const bl::raw_chunk &chunk) { cache_.insert_or_assign(pos, CachedChunk{chunk}); }

bl::chunk *RawChunkCache::getChunk(const bl::chunk_pos &pos) const {
    auto it = cache_.find(pos);
    // if a chunk are mark as deleted in cache, we think is does not exist
    if (it == cache_.end() || it->second.shouldDelete) return nullptr;
    auto *c = new bl::chunk(pos);
    c->load_from_raw_chunk(it->second.chunk);
    return c;
}

std::optional<bl::raw_chunk> RawChunkCache::getRawChunk(const bl::chunk_pos &pos) const {
    auto it = cache_.find(pos);
    // if a chunk are mark as deleted in cache, we think is does not exist
    if (it == cache_.end() || it->second.shouldDelete) return std::nullopt;
    return it->second.chunk;
}

bool RawChunkCache::hasChunk(const bl::chunk_pos &pos) const { return cache_.find(pos) != cache_.end(); }

void RawChunkCache::putMissing(bl::bedrock_level &level, const bl::chunk_pos &pos) {
    auto it = cache_.find(pos);
    if (it != cache_.end()) {
        it->second.shouldDelete = true;
    } else {
        bl::raw_chunk raw(pos);
        raw.read(level);
        cache_.emplace(pos, CachedChunk{std::move(raw), true});
    }
}

void RawChunkCache::commit(leveldb::WriteBatch &batch) {
    LOG_F(INFO, "Cache size is %zu", cache_.size());
    for (auto &[pos, entry] : cache_) {
        LOG_F(INFO, "Commit pos: %s delete=%d", pos.to_string().c_str(), entry.shouldDelete);
        entry.chunk.write(batch, entry.shouldDelete);
    }
    cache_.clear();
}
