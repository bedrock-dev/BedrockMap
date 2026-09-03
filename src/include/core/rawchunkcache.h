#ifndef BEDROCKMAP_RAWCHUNKCACHE_H
#define BEDROCKMAP_RAWCHUNKCACHE_H

#include <optional>
#include <unordered_map>

#include "bedrock_key.h"
#include "bedrock_level.h"
#include "chunk.h"
#include "leveldb/write_batch.h"

class RawChunkCache {
   public:
    struct CachedChunk {
        bl::raw_chunk chunk;
        bool shouldDelete = false;
    };

    bool empty() const { return cache_.empty(); }
    bool isDirty() const { return !cache_.empty(); }

    void putChunk(const bl::chunk_pos &pos, const bl::raw_chunk &chunk);

    bl::chunk *getChunk(const bl::chunk_pos &pos, bl::chunk_load_policy policy = bl::chunk_load_policy::All) const;

    std::optional<bl::raw_chunk> getRawChunk(const bl::chunk_pos &pos) const;

    bool hasChunk(const bl::chunk_pos &pos) const;

    // Put an explicit missing marker for the given position (cache key present with nullopt).
    void putMissing(bl::bedrock_level &level, const bl::chunk_pos &pos);

    // Write all cached raw_chunks into the batch, then clear the cache.
    void commit(leveldb::WriteBatch &batch);

    // Returns (empty_chunk_count, non_empty_chunk_count)
    std::pair<int, int> chunkCounts() const;

    std::unordered_map<bl::chunk_pos, CachedChunk> cache_;
};

#endif  // BEDROCKMAP_RAWCHUNKCACHE_H
