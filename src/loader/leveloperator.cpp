#include "loader/leveloperator.h"

#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUrl>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <vector>

#include "base/platform.h"
#include "base/worldenv.h"
#include "base/worldfilesource.h"
#include "io/documentio.h"
#include "level_dat.h"

namespace {
    constexpr qint64 maxArchiveSize = std::numeric_limits<int>::max();

    struct WorldArchive {
        QZipWriter& writer;
        qint64 directorySize{0};
        int entryCount{0};

        bool reserveEntry(const QString& path, std::uint64_t size) {
            // Stay within QZipReader's signed offsets and classic ZIP's entry
            // count. Allow room for deflate overhead and the central directory.
            const qint64 nameSize = path.toUtf8().size();
            if (size > std::uint64_t(maxArchiveSize) || nameSize > 65535 || entryCount >= 65534) return false;
            const qint64 dataSize = static_cast<qint64>(size);
            const qint64 overhead = dataSize / 1000 + 65536;
            directorySize += 46 + nameSize;
            ++entryCount;
            return writer.device()->pos() + dataSize + overhead + 30 + nameSize + directorySize + 22 <= maxArchiveSize;
        }
    };

    QSet<QString> archiveDirectories(const QList<QZipReader::FileInfo>& entries) {
        QSet<QString> directories;
        for (const auto& entry : entries) {
            const QString path = QDir::cleanPath(entry.filePath);
            for (qsizetype slash = path.lastIndexOf(QLatin1Char('/')); slash > 0; slash = path.lastIndexOf(QLatin1Char('/'), slash - 1))
                directories.insert(path.left(slash));
        }
        return directories;
    }

    QString archiveWorldDirectory(platform::WorldFileSource& files, const QString& source, const QString& relative, WorldArchive& archive,
                                  int depth = 0) {
        if (depth > 64) return QCoreApplication::translate("AppController", "Directory nesting is too deep.");
        QStringList children;
        const QString childrenError = files.children(source, &children);
        if (!childrenError.isEmpty()) return childrenError;
        for (const auto& name : children) {
            if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(QLatin1Char('/')) ||
                name.contains(QLatin1Char('\\')))
                return QCoreApplication::translate("AppController", "Invalid file name in world directory.");
            if (relative == QStringLiteral("db") &&
                (name == QStringLiteral("LOCK") || name == QStringLiteral("LOG") || name == QStringLiteral("LOG.old")))
                continue;
            const QString from = QDir(source).filePath(name);
            const QString path = relative.isEmpty() ? name : relative + QLatin1Char('/') + name;
            // Match the portable entry names accepted by the importer.
            if (name.contains(QLatin1Char(':')) || name.contains(QChar::Null) || name.endsWith(QLatin1Char('.')) ||
                name.endsWith(QLatin1Char(' ')))
                return QCoreApplication::translate("AppController", "Unsupported file in world directory: %1").arg(path);
            platform::WorldFileInfo info;
            const QString infoError = files.fileInfo(from, info);
            if (!infoError.isEmpty()) return infoError;
            if (info.isLink || (!info.isFile && !info.isDirectory))
                return QCoreApplication::translate("AppController", "Unsupported file in world directory: %1").arg(path);
            if (!archive.reserveEntry(info.isDirectory ? path + QLatin1Char('/') : path, info.isFile ? info.size : 0))
                return QCoreApplication::translate("AppController", "The world is too large for this ZIP format (2 GiB or 65534 entries).");
            if (info.isDirectory) {
                archive.writer.setCreationPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup |
                                                      QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
                archive.writer.addDirectory(path);
                if (archive.writer.status() != QZipWriter::NoError)
                    return QCoreApplication::translate("AppController", "Could not write the world archive.");
                const QString error = archiveWorldDirectory(files, from, path, archive, depth + 1);
                if (!error.isEmpty()) return error;
                continue;
            }

            const auto input = files.openRead(from);
            if (!input) return QCoreApplication::translate("AppController", "Cannot read world file: %1").arg(path);
            // QZipWriter compresses one complete entry at a time.
            const QByteArray data = input->readAll();
            if (input->error() != QFileDevice::NoError) return input->errorString();
            platform::WorldFileInfo after;
            const QString afterError = files.fileInfo(from, after);
            if (!afterError.isEmpty()) return afterError;
            if (std::uint64_t(data.size()) != info.size || after.size != info.size || after.modified != info.modified)
                return QCoreApplication::translate("AppController", "The world changed during export. Close Minecraft and try again.");
            archive.writer.setCreationPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
            archive.writer.addFile(path, data);
            if (archive.writer.status() != QZipWriter::NoError)
                return QCoreApplication::translate("AppController", "Could not write the world archive.");
        }
        return {};
    }

    QString internalWorldsRoot() {
        const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        return root.isEmpty() ? QString{} : QDir(root).filePath(QStringLiteral("worlds"));
    }

    QString internalWorldPath(const QString& path) {
        const QString root = internalWorldsRoot();
        if (root.isEmpty() || path.isEmpty()) return {};
        const QFileInfo rootInfo(root);
        const QFileInfo worldInfo(path);
        if (!rootInfo.isDir() || rootInfo.isSymLink() || rootInfo.isJunction() || !worldInfo.isDir() || worldInfo.isSymLink() ||
            worldInfo.isJunction())
            return {};
        const QString canonicalRoot = rootInfo.canonicalFilePath();
        const QString canonicalWorld = worldInfo.canonicalFilePath();
        if (canonicalRoot.isEmpty() || canonicalWorld.isEmpty() || QDir(canonicalRoot) != QFileInfo(canonicalWorld).dir() ||
            QDir(canonicalRoot) != worldInfo.dir())
            return {};
        return canonicalWorld;
    }

    QString prepareImportTarget(const QString& target) {
        const QFileInfo info(target);
        if (!info.exists() && !info.isSymLink()) return {};
        if (info.isSymLink() || info.isJunction() || !info.isDir() || LevelOperator::isCompleteWorld(target))
            return QCoreApplication::translate("AppController", "The internal copy already exists and cannot be replaced.");

        // Failed imports from older versions could leave a partial directory at the
        // deterministic target. Remove only validated direct children of the app's
        // internal worlds root before publishing the new copy.
        const QString internal = internalWorldPath(target);
        if (internal.isEmpty() || !QDir(internal).removeRecursively())
            return QCoreApplication::translate("AppController", "Cannot remove the incomplete internal copy.");
        return {};
    }

    QString copyWorldDirectory(platform::WorldFileSource& files, const QString& source, const QString& target, int depth = 0) {
        if (depth > 64) return QCoreApplication::translate("AppController", "Directory nesting is too deep.");
        QStringList children;
        const QString childrenError = files.children(source, &children);
        if (!childrenError.isEmpty()) return childrenError;
        for (const auto& name : children) {
            if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(QLatin1Char('/')) ||
                name.contains(QLatin1Char('\\')))
                return QCoreApplication::translate("AppController", "Invalid file name in world directory.");
            // Preserve numbered transaction logs; only skip process-local DB files.
            if (depth == 1 && QFileInfo(target).fileName() == QStringLiteral("db") &&
                (name == QStringLiteral("LOCK") || name == QStringLiteral("LOG") || name == QStringLiteral("LOG.old")))
                continue;
            const QString from = QDir(source).filePath(name);
            const QString to = QDir(target).filePath(name);
            platform::WorldFileInfo info;
            const QString infoError = files.fileInfo(from, info);
            if (!infoError.isEmpty()) return infoError;
            if (info.isLink || (!info.isFile && !info.isDirectory))
                return QCoreApplication::translate("AppController", "Unsupported file in world directory: %1").arg(from);
            if (info.isDirectory) {
                if (!QDir().mkdir(to)) return QCoreApplication::translate("AppController", "Cannot create import directory: %1").arg(to);
                const QString error = copyWorldDirectory(files, from, to, depth + 1);
                if (!error.isEmpty()) return error;
                continue;
            }
            const auto input = files.openRead(from);
            if (!input) return QCoreApplication::translate("AppController", "Cannot read world file: %1").arg(from);
            const QString writeError = io::DocumentIo::writeStream(QUrl::fromLocalFile(to), [&](QIODevice& output) {
                char buffer[65536];
                std::uint64_t copied = 0;
                for (;;) {
                    const qint64 count = input->read(buffer, sizeof(buffer));
                    if (count < 0) return input->errorString();
                    if (count == 0) break;
                    if (output.write(buffer, count) != count) return output.errorString();
                    copied += static_cast<std::uint64_t>(count);
                }
                if (copied != info.size)
                    return QCoreApplication::translate("AppController",
                                                       "The source world changed while copying. Close Minecraft and try again.");
                return QString();
            });
            if (!writeError.isEmpty()) return to + ": " + writeError;
        }
        return {};
    }
}  // namespace

QString LevelOperator::targetForSource(const QString& source) {
    const QString root = internalWorldsRoot();
    if (root.isEmpty()) return {};
    const QString key = QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
    // Keep existing copies at their original paths, including recent-world entries.
    const QFileInfo legacyTarget(QDir(root).filePath(key));
    if (legacyTarget.exists() || legacyTarget.isSymLink()) return legacyTarget.filePath();
    return QDir(root).filePath(key.left(16));
}

bool LevelOperator::isInternalWorld(const QString& path) { return !internalWorldPath(path).isEmpty(); }

QString LevelOperator::deleteInternalWorld(const QString& path) {
    // Resolve and validate again on the worker immediately before deletion.
    // The worlds root itself and paths outside its direct children are rejected.
    const QString target = internalWorldPath(path);
    if (target.isEmpty()) return QCoreApplication::translate("AppController", "Only worlds in the app's internal storage can be deleted.");
    if (!QDir(target).removeRecursively())
        return QCoreApplication::translate("AppController", "Could not delete the world completely. Some files may remain.");
    return {};
}

QString LevelOperator::resolveContentWorldFolder(const QUrl& source) {
    const QString resolved = io::DocumentIo::resolveNativeDirectory(source);
    if (!resolved.isEmpty() && isCompleteWorld(resolved)) return resolved;
    // Reuse an existing internal copy when direct access is unavailable.
    const QString target = targetForSource(io::DocumentIo::identifier(source));
    return !target.isEmpty() && isCompleteWorld(target) ? target : QString{};
}

bool LevelOperator::isCompleteWorld(const QString& path) {
    const auto files = platform::worldFileSource(path);
    if (!files) return false;
    bl::level_dat dat;
    return loadLevelDat(*files, path, dat);
}

bool LevelOperator::loadLevelDat(platform::WorldFileSource& files, const QString& path, bl::level_dat& dat) {
    platform::WorldFileInfo info;
    const QDir directory(path);
    if (!files.fileInfo(directory.filePath(QStringLiteral("level.dat")), info).isEmpty() || !info.isFile) return false;
    if (!files.fileInfo(directory.filePath(QStringLiteral("db")), info).isEmpty() || !info.isDirectory) return false;
    const auto input = files.openRead(directory.filePath(QStringLiteral("level.dat")));
    if (!input) return false;
    const QByteArray raw = input->readAll();
    if (input->error() != QFileDevice::NoError || raw.isEmpty()) return false;
    return dat.load_from_raw_data(std::vector<byte_t>(raw.begin(), raw.end()));
}

QString LevelOperator::copyToInternal(const QUrl& source, const QString& target) {
    if (const QString error = prepareImportTarget(target); !error.isEmpty()) return error;
    const QString parent = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(parent)) return QCoreApplication::translate("AppController", "Cannot create the world import directory.");
    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".import-XXXXXX")));
    if (!staging.isValid()) return QCoreApplication::translate("AppController", "Cannot create the world import directory.");

    if (!source.isLocalFile()) {
        const QString error = io::DocumentIo::copyTree(source, QUrl::fromLocalFile(staging.path()));
        if (!error.isEmpty()) return error;
        QDir database(QDir(staging.path()).filePath(QStringLiteral("db")));
        database.remove(QStringLiteral("LOCK"));
        database.remove(QStringLiteral("LOG"));
        database.remove(QStringLiteral("LOG.old"));
    } else {
        const QString sourcePath = source.toLocalFile();
        const auto files = platform::worldFileSource(sourcePath);
        if (!files) return QCoreApplication::translate("AppController", "Cannot access the source world directory.");
        const QString error = copyWorldDirectory(*files, sourcePath, staging.path());
        if (!error.isEmpty()) return error;
    }

    if (!isCompleteWorld(staging.path())) {
        return QCoreApplication::translate("AppController",
                                           "The selected directory must contain a valid level.dat file and a db directory.");
    }
    if (const QString error = prepareImportTarget(target); !error.isEmpty()) return error;
    if (!QDir().rename(staging.path(), target)) return QCoreApplication::translate("AppController", "Cannot save the imported world.");
    staging.setAutoRemove(false);
    return {};
}

QString LevelOperator::extractToInternal(const QUrl& source, const QString& target) {
    auto opened = io::DocumentIo::openRead(source);
    const QString sourceLabel = io::DocumentIo::identifier(source);
    if (!opened.ok()) return sourceLabel + ": " + opened.error;
    // Some SAF providers return a pipe. ZIP directory lookup needs a seekable
    // device, so spool only those inputs to disk instead of holding them in RAM.
    QTemporaryFile seekableInput;
    QIODevice* archiveInput = opened.file.get();
    if (opened.file->isSequential()) {
        if (!seekableInput.open()) return seekableInput.errorString();
        for (;;) {
            const QByteArray block = opened.file->read(65536);
            if (opened.file->error() != QFileDevice::NoError) return sourceLabel + ": " + opened.file->errorString();
            if (block.isEmpty()) break;
            if (seekableInput.write(block) != block.size()) return seekableInput.errorString();
        }
        if (!seekableInput.flush() || !seekableInput.seek(0)) return seekableInput.errorString();
        archiveInput = &seekableInput;
    }
    // QZipReader uses signed 32-bit offsets and sizes internally.
    if (archiveInput->size() <= 0 || archiveInput->size() > std::numeric_limits<int>::max())
        return QCoreApplication::translate("AppController", "The world archive is invalid or unsupported.");
    QZipReader archive(archiveInput);
    const auto entries = archive.fileInfoList();
    if (archive.status() != QZipReader::NoError || entries.isEmpty())
        return QCoreApplication::translate("AppController", "The world archive is invalid or unsupported.");

    if (const QString error = prepareImportTarget(target); !error.isEmpty()) return error;
    const QString parent = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(parent)) return QCoreApplication::translate("AppController", "Cannot create the world import directory.");
    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".import-XXXXXX")));
    if (!staging.isValid()) return QCoreApplication::translate("AppController", "Cannot create the world import directory.");

    const QSet<QString> directories = archiveDirectories(entries);
    const QDir destination(staging.path());
    for (const auto& entry : entries) {
        const QString path = QDir::cleanPath(entry.filePath);
        const bool isDirectory = directories.contains(path);
        if (isDirectory && path == QStringLiteral(".")) continue;
        // ZIP entries must stay inside the new destination, with no links or
        // Windows drive/alternate-stream names that could escape it.
        if (entry.isSymLink || (!isDirectory && !entry.isFile) || path.isEmpty() || path == QStringLiteral(".") ||
            path == QStringLiteral("..") || path.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(path) ||
            path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':')) || path.contains(QChar::Null))
            return QCoreApplication::translate("AppController", "Unsupported file in world directory: %1").arg(entry.filePath);
        for (const auto& part : path.split(QLatin1Char('/'))) {
            // Windows strips these suffixes, which could turn a component into
            // a parent reference or make distinct archive entries alias.
            if (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')))
                return QCoreApplication::translate("AppController", "Unsupported file in world directory: %1").arg(entry.filePath);
        }

        const QString outputPath = destination.filePath(path);
        if (isDirectory) {
            if (!QDir().mkpath(outputPath))
                return QCoreApplication::translate("AppController", "Cannot create import directory: %1").arg(outputPath);
            continue;
        }
        if (entry.size < 0 || entry.size > std::numeric_limits<int>::max())
            return QCoreApplication::translate("AppController", "The world archive contains an unsupported or damaged file: %1")
                .arg(entry.filePath);
        if (!QDir().mkpath(QFileInfo(outputPath).absolutePath()))
            return QCoreApplication::translate("AppController", "Cannot create import directory: %1").arg(outputPath);

        const QByteArray data = archive.fileData(entry.filePath);
        const bool inputError = archiveInput == opened.file.get() ? opened.file->error() != QFileDevice::NoError
                                                                  : seekableInput.error() != QFileDevice::NoError;
        if (archive.status() != QZipReader::NoError || inputError || data.size() != entry.size)
            return QCoreApplication::translate("AppController", "The world archive contains an unsupported or damaged file: %1")
                .arg(entry.filePath);
        if (const QString error = io::DocumentIo::writeBytes(QUrl::fromLocalFile(outputPath), data); !error.isEmpty())
            return outputPath + ": " + error;
    }
    QDir database(destination.filePath(QStringLiteral("db")));
    database.remove(QStringLiteral("LOCK"));
    database.remove(QStringLiteral("LOG"));
    database.remove(QStringLiteral("LOG.old"));
    if (!isCompleteWorld(staging.path()))
        return QCoreApplication::translate("AppController", "The world archive does not contain a valid level.dat file and db directory.");
    if (const QString error = prepareImportTarget(target); !error.isEmpty()) return error;
    if (!QDir().rename(staging.path(), target)) return QCoreApplication::translate("AppController", "Cannot save the imported world.");
    staging.setAutoRemove(false);
    return {};
}

QString LevelOperator::exportMcworld(const QString& source, const QUrl& destination) {
    try {
        if (destination.isLocalFile()) {
            const QFileInfo outputInfo(destination.toLocalFile());
            const QString parent = outputInfo.dir().canonicalPath();
            const QString resolved = outputInfo.exists() ? outputInfo.canonicalFilePath() : QDir(parent).filePath(outputInfo.fileName());
            const QString relative = QDir(source).relativeFilePath(resolved);
            if (relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(relative))
                return QCoreApplication::translate("AppController", "Save the archive outside the source world folder.");
        }
        const auto files = platform::worldFileSource(source);
        if (!files) return QCoreApplication::translate("AppController", "Cannot access the source world directory.");
        bl::level_dat dat;
        if (!loadLevelDat(*files, source, dat))
            return QCoreApplication::translate("AppController",
                                               "The selected directory must contain a valid level.dat file and a db directory.");

        platform::WorldEnv env;
        // Hold the database lock while reading its files, including transaction
        // logs, so Minecraft or another process cannot modify or compact it.
        leveldb::FileLock* rawLock = nullptr;
        const auto status = env.LockFile(source.toStdString() + "/db/LOCK", &rawLock);
        if (!status.ok())
            return QCoreApplication::translate("AppController", "Close Minecraft and other programs using this world before exporting.\n%1")
                .arg(QString::fromStdString(status.ToString()));
        const auto unlock = [&env](leveldb::FileLock* lock) { env.UnlockFile(lock); };
        const std::unique_ptr<leveldb::FileLock, decltype(unlock)> lock(rawLock, unlock);

        // Always finish the seekable archive before touching the chosen output.
        // This also supports SAF providers which expose only a sequential pipe.
        QTemporaryFile temporary;
        if (!temporary.open()) return temporary.errorString();
        QZipWriter writer(&temporary);
        WorldArchive archive{writer};
        const QString error = archiveWorldDirectory(*files, source, {}, archive);
        if (!error.isEmpty()) return error;
        writer.close();
        if (writer.status() != QZipWriter::NoError || temporary.error() != QFileDevice::NoError)
            return QCoreApplication::translate("AppController", "Could not write the world archive.");
        // QZipWriter may close its device after finalizing the central directory.
        if (!temporary.isOpen() && !temporary.open()) return temporary.errorString();
        if (!temporary.flush() || !temporary.seek(0)) return temporary.errorString();

        return io::DocumentIo::writeStream(destination, [&temporary](QIODevice& output) {
            while (!temporary.atEnd()) {
                const QByteArray block = temporary.read(65536);
                if (temporary.error() != QFileDevice::NoError) return temporary.errorString();
                if (output.write(block) != block.size()) return output.errorString();
            }
            return QString();
        });
    } catch (const std::exception& error) {
        return QString::fromUtf8(error.what());
    }
}
