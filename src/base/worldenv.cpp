#include "base/worldenv.h"

#include <QFileInfo>
#include <memory>

#ifdef _WIN32
#include <QDir>

#include "utils.h"
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace platform {
    namespace {
        std::string levelDbPath(const std::string& path) {
#ifdef _WIN32
            return bl::utils::UTF8ToGBEx(path.c_str());
#else
            return path;
#endif
        }
    }  // namespace

    WorldEnv::WorldEnv() : EnvWrapper(leveldb::Env::Default()) {}

    leveldb::Status WorldEnv::checkLocalWrite(const std::string& path, const leveldb::Status& status) {
        if (!status.ok()) {
            const QFileInfo file(QString::fromStdString(path));
            const QFileInfo access = file.exists() ? file : QFileInfo(file.absolutePath());
            if (access.exists() && !access.isWritable()) write_access_denied_ = true;
        }
        return status;
    }

    leveldb::Status WorldEnv::NewWritableFile(const std::string& path, leveldb::WritableFile** result) {
        return checkLocalWrite(path, target()->NewWritableFile(levelDbPath(path), result));
    }

    leveldb::Status WorldEnv::NewAppendableFile(const std::string& path, leveldb::WritableFile** result) {
        return checkLocalWrite(path, target()->NewAppendableFile(levelDbPath(path), result));
    }

    leveldb::Status WorldEnv::LockFile(const std::string& path, leveldb::FileLock** lock) {
        return checkLocalWrite(path, target()->LockFile(levelDbPath(path), lock));
    }

    leveldb::Status WorldEnv::NewLogger(const std::string& path, leveldb::Logger** result) {
        return checkLocalWrite(path, target()->NewLogger(levelDbPath(path), result));
    }

#ifdef _WIN32
    leveldb::Status WorldEnv::NewSequentialFile(const std::string& path, leveldb::SequentialFile** result) {
        return target()->NewSequentialFile(levelDbPath(path), result);
    }

    leveldb::Status WorldEnv::NewRandomAccessFile(const std::string& path, leveldb::RandomAccessFile** result) {
        return target()->NewRandomAccessFile(levelDbPath(path), result);
    }

    bool WorldEnv::FileExists(const std::string& path) { return target()->FileExists(levelDbPath(path)); }

    leveldb::Status WorldEnv::GetChildren(const std::string& path, std::vector<std::string>* result) {
        result->clear();
        const QDir directory(QString::fromStdString(path));
        if (!directory.exists() || !directory.isReadable()) return leveldb::Status::IOError(path, "Directory unavailable");
        for (const auto& name : directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name))
            result->push_back(name.toStdString());
        return leveldb::Status::OK();
    }

    leveldb::Status WorldEnv::RemoveFile(const std::string& path) { return target()->RemoveFile(levelDbPath(path)); }

    leveldb::Status WorldEnv::CreateDir(const std::string& path) { return target()->CreateDir(levelDbPath(path)); }

    leveldb::Status WorldEnv::RemoveDir(const std::string& path) { return target()->RemoveDir(levelDbPath(path)); }

    leveldb::Status WorldEnv::GetFileSize(const std::string& path, std::uint64_t* size) {
        return target()->GetFileSize(levelDbPath(path), size);
    }

    leveldb::Status WorldEnv::RenameFile(const std::string& from, const std::string& to) {
#ifdef Q_OS_WIN
        const QString source = QString::fromStdString(from);
        const QString destination = QString::fromStdString(to);
        if (MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()), reinterpret_cast<LPCWSTR>(destination.utf16()),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return leveldb::Status::OK();
        return leveldb::Status::IOError(from, "Rename failed: " + std::to_string(GetLastError()));
#else
        return target()->RenameFile(levelDbPath(from), levelDbPath(to));
#endif
    }
#endif

    leveldb::Status writeWorldFileAtomically(leveldb::Env& env, const std::string& path, const std::string& data) {
        const std::string temporary = path + ".tmp";
        leveldb::WritableFile* raw = nullptr;
        auto status = env.NewWritableFile(temporary, &raw);
        std::unique_ptr<leveldb::WritableFile> file(raw);
        if (!status.ok()) return status;
        status = file->Append(data);
        if (status.ok()) status = file->Sync();
        const auto closed = file->Close();
        if (status.ok()) status = closed;
        if (!status.ok()) {
            env.RemoveFile(temporary);
            return status;
        }
        return env.RenameFile(temporary, path);
    }
}  // namespace platform
