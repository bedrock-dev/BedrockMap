#include "chunkstorage.h"

#include <leveldb/write_batch.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

#include "loguru/loguru.hpp"

namespace {

bool writeLevelDatAtomically(const std::string &fileName, const std::string &data) {
    namespace fs = std::filesystem;
    const fs::path target(fileName);
    fs::path temporary = target;
    temporary += ".tmp";

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            LOG_F(ERROR, "Can not open temporary level.dat file %s", temporary.string().c_str());
            return false;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output.good()) {
            LOG_F(ERROR, "Can not write temporary level.dat file %s", temporary.string().c_str());
            std::error_code removeError;
            fs::remove(temporary, removeError);
            return false;
        }
    }

#ifdef _WIN32
    // MoveFileEx replaces the destination in one filesystem operation. The
    // write-through flag also asks Windows to flush the replacement metadata.
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        LOG_F(ERROR, "Can not atomically replace level.dat (error %lu); temporary file kept at %s", GetLastError(),
              temporary.string().c_str());
        return false;
    }
#else
    std::error_code renameError;
    fs::rename(temporary, target, renameError);
    if (renameError) {
        LOG_F(ERROR, "Can not atomically replace level.dat: %s; temporary file kept at %s", renameError.message().c_str(),
              temporary.string().c_str());
        return false;
    }
#endif
    return true;
}

}  // namespace

bl::chunk *ChunkStorage::getChunk(const bl::chunk_pos &pos, bl::chunk_load_policy policy) {
    if (cache_.hasChunk(pos)) return cache_.getChunk(pos, policy);
    return level_.get_chunk(pos, policy);
}

std::optional<bl::raw_chunk> ChunkStorage::getRawChunk(const bl::chunk_pos &pos) {
    if (cache_.hasChunk(pos)) return cache_.getRawChunk(pos);
    bl::raw_chunk raw(pos);
    if (raw.read(level_)) return raw;
    return std::nullopt;
}

void ChunkStorage::putMissing(const bl::chunk_pos &pos) { cache_.putMissing(level_, pos); }

void ChunkStorage::putRawChunk(const bl::raw_chunk &raw) { cache_.putChunk(raw.pos(), raw); }

bool ChunkStorage::commit(const std::unordered_map<std::string, std::string> &globalModifies, const bl::nbt::compound_tag *levelDat) {
    last_commit_error_ = CommitError::None;
    if (cache_.empty() && globalModifies.empty() && !levelDat) return true;

    leveldb::WriteBatch batch;
    if (!cache_.empty()) cache_.commit(batch);
    for (const auto &kv : globalModifies) {
        if (kv.second.empty()) {
            batch.Delete(kv.first);
        } else {
            batch.Put(kv.first, kv.second);
        }
    }

    // Serialize before touching either store. The copied root is installed in
    // memory only after the file write succeeds.
    std::string levelDatRaw;
    std::unique_ptr<bl::nbt::compound_tag> levelDatCopy;
    if (levelDat) {
        levelDatCopy.reset(dynamic_cast<bl::nbt::compound_tag *>(levelDat->copy()));
        if (!levelDatCopy) {
            last_commit_error_ = CommitError::LevelDatSerialization;
            LOG_F(ERROR, "Failed to copy level.dat NBT before commit");
            return false;
        }
        levelDatRaw = level_.dat().header() + levelDatCopy->to_raw();
    }

    if (!cache_.empty() || !globalModifies.empty()) {
        const auto status = level_.db()->Write(leveldb::WriteOptions(), &batch);
        if (!status.ok()) {
            last_commit_error_ = CommitError::LevelDb;
            LOG_F(ERROR, "Failed to commit LevelDB changes: %s", status.ToString().c_str());
            return false;
        }
    }

    if (levelDat) {
        const auto levelDatPath = level_.root_path() + "/" + bl::bedrock_level::LEVEL_DATA;
        if (!writeLevelDatAtomically(levelDatPath, levelDatRaw)) {
            last_commit_error_ = CommitError::LevelDatWrite;
            if (!cache_.empty() || !globalModifies.empty()) {
                LOG_F(ERROR, "LevelDB changes committed, but level.dat could not be replaced; edits remain dirty for retry");
            }
            return false;
        }
    }

    if (levelDatCopy) level_.dat().set_nbt(levelDatCopy.release());
    cache_.clear();
    return true;
}
