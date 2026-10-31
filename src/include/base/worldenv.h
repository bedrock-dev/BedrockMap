#ifndef BEDROCKMAP_WORLDENV_H
#define BEDROCKMAP_WORLDENV_H

#include <leveldb/env.h>

#include <atomic>
#include <string>

#ifdef _WIN32
#include <cstdint>
#include <vector>
#endif

namespace platform {
    /// Native LevelDB Env for local worlds. It translates UTF-8 paths on Windows
    /// and records write failures on every platform.
    class WorldEnv final : public leveldb::EnvWrapper {
       public:
        WorldEnv();
        [[nodiscard]] bool writeAccessDenied() const { return write_access_denied_.load(); }

        leveldb::Status NewWritableFile(const std::string& path, leveldb::WritableFile** result) override;
        leveldb::Status NewAppendableFile(const std::string& path, leveldb::WritableFile** result) override;
        leveldb::Status LockFile(const std::string& path, leveldb::FileLock** lock) override;
        leveldb::Status NewLogger(const std::string& path, leveldb::Logger** result) override;

#ifdef _WIN32
        leveldb::Status NewSequentialFile(const std::string& path, leveldb::SequentialFile** result) override;
        leveldb::Status NewRandomAccessFile(const std::string& path, leveldb::RandomAccessFile** result) override;
        bool FileExists(const std::string& path) override;
        leveldb::Status GetChildren(const std::string& path, std::vector<std::string>* result) override;
        leveldb::Status RemoveFile(const std::string& path) override;
        leveldb::Status CreateDir(const std::string& path) override;
        leveldb::Status RemoveDir(const std::string& path) override;
        leveldb::Status GetFileSize(const std::string& path, std::uint64_t* size) override;
        leveldb::Status RenameFile(const std::string& from, const std::string& to) override;
#endif

       private:
        leveldb::Status checkLocalWrite(const std::string& path, const leveldb::Status& status);
        std::atomic_bool write_access_denied_{false};
    };

    [[nodiscard]] leveldb::Status writeWorldFileAtomically(leveldb::Env& env, const std::string& path, const std::string& data);
}  // namespace platform

#endif  // BEDROCKMAP_WORLDENV_H
