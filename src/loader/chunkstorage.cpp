#include "loader/chunkstorage.h"

#include <leveldb/write_batch.h>

#include <QCoreApplication>
#include <memory>

#include "io/documentio.h"
#include "loguru/loguru.hpp"

bl::chunk* ChunkStorage::getChunk(const bl::chunk_pos& pos, bl::chunk_load_policy policy) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_.hasChunk(pos)) return cache_.getChunk(pos, policy);
    }
    return level_.get_chunk(pos, policy);
}

std::optional<bl::raw_chunk> ChunkStorage::getRawChunk(const bl::chunk_pos& pos) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_.hasChunk(pos)) return cache_.getRawChunk(pos);
    }
    bl::raw_chunk raw(pos);
    if (raw.read(level_)) return raw;
    return std::nullopt;
}

void ChunkStorage::putMissing(const bl::chunk_pos& pos) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.putMissing(pos);
}

void ChunkStorage::putRawChunk(const bl::raw_chunk& raw) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.putChunk(raw.pos(), raw);
}

CommitResult ChunkStorage::commit(const std::unordered_map<std::string, std::string>& globalModifies,
                                  const bl::nbt::compound_tag* levelDat) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cache_.empty() && globalModifies.empty() && !levelDat) return {};

    leveldb::WriteBatch batch;
    if (!cache_.empty()) cache_.commit(level_, batch);
    for (const auto& kv : globalModifies) {
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
        levelDatCopy.reset(dynamic_cast<bl::nbt::compound_tag*>(levelDat->copy()));
        if (!levelDatCopy) {
            LOG_F(ERROR, "Failed to copy level.dat NBT before commit");
            return {false, QCoreApplication::translate("ChunkStorage", "Could not prepare level.dat data for saving.")};
        }
        levelDatRaw = level_.dat().header() + levelDatCopy->serialize();
    }

    if (!cache_.empty() || !globalModifies.empty()) {
        const auto status = level_.db()->Write(leveldb::WriteOptions(), &batch);
        if (!status.ok()) {
            LOG_F(ERROR, "Failed to commit LevelDB changes: %s", status.ToString().c_str());
            return {false, QCoreApplication::translate("ChunkStorage", "Could not save LevelDB data: %1")
                               .arg(QString::fromStdString(status.ToString()))};
        }
    }

    if (levelDat) {
        // level.dat is an ordinary file, so it goes through the VFS rather than LevelDB's Env.
        const auto levelDatPath = QString::fromStdString(level_.root_path() + "/" + bl::bedrock_level::LEVEL_DATA);
        const QString error = io::DocumentIo::writeBytes(QUrl::fromLocalFile(levelDatPath),
                                                         QByteArray(levelDatRaw.data(), static_cast<qsizetype>(levelDatRaw.size())), {false});
        if (!error.isEmpty()) {
            LOG_F(ERROR, "Cannot save level.dat: %s", error.toStdString().c_str());
            if (!cache_.empty() || !globalModifies.empty()) {
                LOG_F(ERROR, "LevelDB changes committed, but level.dat could not be replaced; edits remain dirty for retry");
            }
            return {false, QCoreApplication::translate("ChunkStorage", "LevelDB data was saved, but level.dat could not be "
                                                                      "written to %1: %2")
                               .arg(levelDatPath, error)};
        }
    }

    if (levelDatCopy) level_.dat().set_nbt(levelDatCopy.release());
    cache_.clear();
    return {};
}
