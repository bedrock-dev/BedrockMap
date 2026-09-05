#ifndef BEDROCKMAP_CHUNKSTORAGE_H
#define BEDROCKMAP_CHUNKSTORAGE_H

#include <array>
#include <optional>
#include <string>
#include <unordered_map>

#include "bedrock_level.h"
#include "chunk.h"
#include "nbt.h"
#include "rawchunkcache.h"

class ChunkStorage {
   public:
    enum class CommitError {
        None,
        LevelDb,
        LevelDatSerialization,
        LevelDatWrite,
    };

    bool open(const std::string &path) { return level_.open(path); }
    void close() { level_.close(); }

    bl::bedrock_level &level() { return level_; }
    const bl::bedrock_level &level() const { return level_; }

    bool isOpen() const { return level_.is_open(); }
    bool isDirty() const { return cache_.isDirty(); }
    std::pair<int, int> chunkModifyCounts() const { return cache_.chunkCounts(); }
    CommitError lastCommitError() const { return last_commit_error_; }

    bl::chunk *getChunk(const bl::chunk_pos &pos, bl::chunk_load_policy policy);
    std::optional<bl::raw_chunk> getRawChunk(const bl::chunk_pos &pos);
    void putMissing(const bl::chunk_pos &pos);
    void putRawChunk(const bl::raw_chunk &raw);

    // Commit chunk and global database edits as one LevelDB batch, then persist
    // an optional level.dat snapshot. LevelDB and level.dat are separate
    // stores, so a level.dat write failure is reported and dirty state remains.
    bool commit(const std::unordered_map<std::string, std::string> &globalModifies = {}, const bl::nbt::compound_tag *levelDat = nullptr);

   private:
    bl::bedrock_level level_{};
    RawChunkCache cache_;
    CommitError last_commit_error_{CommitError::None};
};

#endif  // BEDROCKMAP_CHUNKSTORAGE_H
