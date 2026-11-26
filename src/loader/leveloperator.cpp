#include "loader/leveloperator.h"

#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <vector>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

#include "base/platform.h"
#include "base/worldenv.h"
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
            if (entry.isDir) directories.insert(path);
            for (qsizetype slash = path.lastIndexOf(QLatin1Char('/')); slash > 0; slash = path.lastIndexOf(QLatin1Char('/'), slash - 1))
                directories.insert(path.left(slash));
        }
        return directories;
    }

    QUrl fileUrl(const QString& path) { return QUrl::fromLocalFile(path); }

    QString uniquePath(const QString& parent, const QString& prefix) {
        return QDir(parent).filePath(prefix + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces));
    }

    QUrl uniqueTemporaryFileUrl(const QString& prefix) {
        QString root = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (root.isEmpty()) root = QDir::tempPath();
        return fileUrl(uniquePath(root, prefix));
    }

    void removeIfExists(const QUrl& url, bool recursive = false) {
        const auto status = io::DocumentIo::stat(url);
        if (status.ok() && status.info.exists) static_cast<void>(io::DocumentIo::remove(url, recursive));
    }

    void removeDatabaseTransientFiles(const QString& staging_root) {
        const QDir database(QDir(staging_root).filePath(QStringLiteral("db")));
        removeIfExists(fileUrl(database.filePath(QStringLiteral("LOCK"))));
        removeIfExists(fileUrl(database.filePath(QStringLiteral("LOG"))));
        removeIfExists(fileUrl(database.filePath(QStringLiteral("LOG.old"))));
    }

    bool pathContains(const QString& root, const QString& path) {
        const QString relative = QDir(root).relativeFilePath(path);
        return relative == QStringLiteral(".") || (relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) &&
                                                   !relative.startsWith(QStringLiteral("..\\")) && !QDir::isAbsolutePath(relative));
    }

    QString archiveWorldDirectory(const QString& source, const QString& relative, WorldArchive& archive, int depth = 0) {
        if (depth > 64) return QCoreApplication::translate("AppController", "Directory nesting is too deep.");
        const auto children = io::DocumentIo::list(fileUrl(source));
        if (!children.ok()) return children.error;
        for (const auto& name : children.names) {
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
            const auto status = io::DocumentIo::stat(fileUrl(from));
            if (!status.ok()) return status.error;
            const auto& info = status.info;
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
                const QString error = archiveWorldDirectory(from, path, archive, depth + 1);
                if (!error.isEmpty()) return error;
                continue;
            }

            // QZipWriter compresses one complete entry at a time.
            const auto read = io::DocumentIo::readAll(fileUrl(from));
            if (!read.ok())
                return QCoreApplication::translate("AppController", "Cannot read world file: %1").arg(path) + QLatin1Char('\n') +
                       read.error;
            const QByteArray& data = read.data;
            const auto after = io::DocumentIo::stat(fileUrl(from));
            if (!after.ok()) return after.error;
            if (std::uint64_t(data.size()) != info.size || after.info.size != info.size || after.info.modified != info.modified)
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
        const QUrl rootUrl = fileUrl(root);
        const QUrl worldUrl = fileUrl(path);
        const auto rootStatus = io::DocumentIo::stat(rootUrl);
        const auto worldStatus = io::DocumentIo::stat(worldUrl);
        if (!rootStatus.ok() || !rootStatus.info.exists || !rootStatus.info.isDirectory || rootStatus.info.isLink || !worldStatus.ok() ||
            !worldStatus.info.exists || !worldStatus.info.isDirectory || worldStatus.info.isLink)
            return {};
        const QString canonicalRoot = io::DocumentIo::identifier(rootUrl);
        const QString canonicalWorld = io::DocumentIo::identifier(worldUrl);
        if (canonicalRoot.isEmpty() || canonicalWorld.isEmpty() || QDir(canonicalRoot) != QFileInfo(canonicalWorld).dir()) return {};
        return canonicalWorld;
    }

    QString prepareImportTarget(const QString& target) {
        const auto status = io::DocumentIo::stat(fileUrl(target));
        if (!status.ok()) return status.error;
        if (!status.info.exists) return {};
        if (status.info.isLink || !status.info.isDirectory || LevelOperator::isCompleteWorld(target))
            return QCoreApplication::translate("AppController", "The internal copy already exists and cannot be replaced.");

        // Failed imports from older versions could leave a partial directory at the
        // deterministic target. Remove only validated direct children of the app's
        // internal worlds root before publishing the new copy.
        const QString internal = internalWorldPath(target);
        if (internal.isEmpty()) return QCoreApplication::translate("AppController", "Cannot remove the incomplete internal copy.");
        if (const QString error = io::DocumentIo::remove(fileUrl(internal), true); !error.isEmpty())
            return QCoreApplication::translate("AppController", "Cannot remove the incomplete internal copy.");
        return {};
    }

    QString importDirectoryStem(const QString& display_name) {
        QString stem = display_name.trimmed();
        if (stem.endsWith(QStringLiteral(".mcworld"), Qt::CaseInsensitive)) stem.chop(8);
        stem.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])")), QStringLiteral("_"));
        stem = stem.left(96).trimmed();
        while (stem.startsWith(QLatin1Char('.')) || stem.startsWith(QLatin1Char(' '))) stem.remove(0, 1);
        while (stem.endsWith(QLatin1Char('.')) || stem.endsWith(QLatin1Char(' '))) stem.chop(1);
        return stem.isEmpty() ? QStringLiteral("World") : stem;
    }

    QString copyWorldDirectory(const QString& source, const QString& target, int depth = 0) {
        if (depth > 64) return QCoreApplication::translate("AppController", "Directory nesting is too deep.");
        const auto children = io::DocumentIo::list(fileUrl(source));
        if (!children.ok()) return children.error;
        for (const auto& name : children.names) {
            if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(QLatin1Char('/')) ||
                name.contains(QLatin1Char('\\')))
                return QCoreApplication::translate("AppController", "Invalid file name in world directory.");
            // Preserve numbered transaction logs; only skip process-local DB files.
            if (depth == 1 && QFileInfo(target).fileName() == QStringLiteral("db") &&
                (name == QStringLiteral("LOCK") || name == QStringLiteral("LOG") || name == QStringLiteral("LOG.old")))
                continue;
            const QString from = QDir(source).filePath(name);
            const QString to = QDir(target).filePath(name);
            const auto status = io::DocumentIo::stat(fileUrl(from));
            if (!status.ok()) return status.error;
            const auto& info = status.info;
            if (info.isLink || (!info.isFile && !info.isDirectory))
                return QCoreApplication::translate("AppController", "Unsupported file in world directory: %1").arg(from);
            const auto destination = io::DocumentIo::stat(fileUrl(to));
            if (!destination.ok()) return destination.error;
            if (destination.info.exists &&
                (destination.info.isLink || destination.info.isDirectory != info.isDirectory || destination.info.isFile != info.isFile))
                return QCoreApplication::translate("AppController", "Cannot replace destination entry: %1").arg(to);
            if (info.isDirectory) {
                if (!destination.info.exists) {
                    if (const QString error = io::DocumentIo::makeDirectory(fileUrl(to), false); !error.isEmpty())
                        return QCoreApplication::translate("AppController", "Cannot create import directory: %1").arg(to) +
                               QLatin1Char('\n') + error;
                }
                const QString error = copyWorldDirectory(from, to, depth + 1);
                if (!error.isEmpty()) return error;
                continue;
            }
            auto opened = io::DocumentIo::openRead(fileUrl(from));
            if (!opened.ok())
                return QCoreApplication::translate("AppController", "Cannot read world file: %1").arg(from) + QLatin1Char('\n') +
                       opened.error;
            QFile* input = opened.file.get();
            const QString writeError = io::DocumentIo::writeStream(fileUrl(to), [&](QIODevice& output) {
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
            const auto after = io::DocumentIo::stat(fileUrl(from));
            if (!after.ok()) return after.error;
            if (after.info.size != info.size || after.info.modified != info.modified)
                return QCoreApplication::translate("AppController",
                                                   "The source world changed while copying. Close Minecraft and try again.");
        }
        return {};
    }
}  // namespace

QString LevelOperator::targetForSource(const QString& source, const QString& display_name) {
    const QString root = internalWorldsRoot();
    if (root.isEmpty()) return {};
    const QString key = QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
    return QDir(root).filePath(importDirectoryStem(display_name) + QLatin1Char('-') + key.left(8));
}

bool LevelOperator::isInternalWorld(const QString& path) { return !internalWorldPath(path).isEmpty(); }

QString LevelOperator::deleteInternalWorld(const QString& path) {
    // Resolve and validate again on the worker immediately before deletion.
    // The worlds root itself and paths outside its direct children are rejected.
    const QString target = internalWorldPath(path);
    if (target.isEmpty()) return QCoreApplication::translate("AppController", "Only worlds in the app's internal storage can be deleted.");
    if (const QString error = io::DocumentIo::remove(fileUrl(target), true); !error.isEmpty())
        return QCoreApplication::translate("AppController", "Could not delete the world completely. Some files may remain.");
    return {};
}

QString LevelOperator::resolveContentWorldFolder(const QUrl& source) {
    const QString resolved = io::DocumentIo::resolveNativeDirectory(source);
    if (!resolved.isEmpty() && isCompleteWorld(resolved)) return resolved;
    // Reuse an existing internal copy when direct access is unavailable.
    const QString target = targetForSource(io::DocumentIo::identifier(source), io::DocumentIo::displayName(source));
    return !target.isEmpty() && isCompleteWorld(target) ? target : QString{};
}

bool LevelOperator::isCompleteWorld(const QString& path) {
    bl::level_dat dat;
    return loadLevelDat(path, dat);
}

bool LevelOperator::loadLevelDat(const QString& path, bl::level_dat& dat) {
    const QDir directory(path);
    const QUrl levelDat = fileUrl(directory.filePath(QStringLiteral("level.dat")));
    const auto file = io::DocumentIo::stat(levelDat);
    if (!file.ok() || !file.info.isFile) return false;
    const auto database = io::DocumentIo::stat(fileUrl(directory.filePath(QStringLiteral("db"))));
    if (!database.ok() || !database.info.isDirectory) return false;
    // level.dat is a small NBT file; reject anything implausibly large.
    const auto read = io::DocumentIo::readAll(levelDat, 64 << 20);
    if (!read.ok() || read.data.isEmpty()) return false;
    return dat.load_from_raw_data(std::vector<byte_t>(read.data.begin(), read.data.end()));
}

QString LevelOperator::copyToInternal(const QUrl& source, const QString& target) {
    if (const QString error = prepareImportTarget(target); !error.isEmpty()) return error;
    const QString parent = QFileInfo(target).absolutePath();
    if (const QString error = io::DocumentIo::makeDirectory(fileUrl(parent)); !error.isEmpty())
        return QCoreApplication::translate("AppController", "Cannot create the world import directory.");
    const QString staging_path = uniquePath(parent, QStringLiteral(".import"));
    const QUrl staging_url = fileUrl(staging_path);
    if (const QString error = io::DocumentIo::makeDirectory(staging_url, false); !error.isEmpty())
        return QCoreApplication::translate("AppController", "Cannot create the world import directory.");
    bool published = false;
    const auto cleanup = qScopeGuard([&] {
        if (!published) removeIfExists(staging_url, true);
    });

    if (!source.isLocalFile()) {
        const QString error = io::DocumentIo::copyDirectory(source, staging_url);
        if (!error.isEmpty()) return error;
    } else {
        const QString error = copyWorldDirectory(source.toLocalFile(), staging_path);
        if (!error.isEmpty()) return error;
    }
    removeDatabaseTransientFiles(staging_path);

    if (!isCompleteWorld(staging_path)) {
        return QCoreApplication::translate("AppController",
                                           "The selected directory must contain a valid level.dat file and a db directory.");
    }
    if (const QString error = prepareImportTarget(target); !error.isEmpty()) return error;
    if (const QString error = io::DocumentIo::rename(staging_url, fileUrl(target)); !error.isEmpty())
        return QCoreApplication::translate("AppController", "Cannot save the imported world.");
    published = true;
    return {};
}

QString LevelOperator::extractToInternal(const QUrl& source, const QString& target) {
    auto opened = io::DocumentIo::openRead(source);
    const QString sourceLabel = io::DocumentIo::identifier(source);
    if (!opened.ok()) return sourceLabel + ": " + opened.error;
    // Some SAF providers return a pipe. ZIP directory lookup needs a seekable
    // device, so spool only those inputs to disk instead of holding them in RAM.
    QUrl spool_url;
    io::OpenReadResult spooled;
    const auto remove_spool = qScopeGuard([&] { removeIfExists(spool_url); });
    QIODevice* archiveInput = opened.file.get();
    if (opened.file->isSequential()) {
        spool_url = uniqueTemporaryFileUrl(QStringLiteral("bedrockmap-archive"));
        const QString write_error = io::DocumentIo::writeStream(spool_url, [&](QIODevice& output) {
            for (;;) {
                const QByteArray block = opened.file->read(65536);
                if (opened.file->error() != QFileDevice::NoError) return opened.file->errorString();
                if (block.isEmpty()) break;
                if (output.write(block) != block.size()) return output.errorString();
            }
            return QString{};
        });
        if (!write_error.isEmpty()) return sourceLabel + ": " + write_error;
        spooled = io::DocumentIo::openRead(spool_url);
        if (!spooled.ok()) return sourceLabel + ": " + spooled.error;
        archiveInput = spooled.file.get();
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
    if (const QString error = io::DocumentIo::makeDirectory(fileUrl(parent)); !error.isEmpty())
        return QCoreApplication::translate("AppController", "Cannot create the world import directory.");
    const QString staging_path = uniquePath(parent, QStringLiteral(".import"));
    const QUrl staging_url = fileUrl(staging_path);
    if (const QString error = io::DocumentIo::makeDirectory(staging_url, false); !error.isEmpty())
        return QCoreApplication::translate("AppController", "Cannot create the world import directory.");
    bool published = false;
    const auto cleanup = qScopeGuard([&] {
        if (!published) removeIfExists(staging_url, true);
    });

    const QSet<QString> directories = archiveDirectories(entries);
    for (const auto& entry : entries) {
        const QString path = QDir::cleanPath(entry.filePath);
        const bool isDirectory = entry.isDir || directories.contains(path);
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

        const QString outputPath = QDir(staging_path).filePath(path);
        const QUrl outputUrl = fileUrl(outputPath);
        if (isDirectory) {
            if (!io::DocumentIo::makeDirectory(outputUrl).isEmpty())
                return QCoreApplication::translate("AppController", "Cannot create import directory: %1").arg(outputPath);
            continue;
        }
        if (entry.size < 0 || entry.size > std::numeric_limits<int>::max())
            return QCoreApplication::translate("AppController", "The world archive contains an unsupported or damaged file: %1")
                .arg(entry.filePath);
        if (!io::DocumentIo::makeDirectory(fileUrl(QFileInfo(outputPath).absolutePath())).isEmpty())
            return QCoreApplication::translate("AppController", "Cannot create import directory: %1").arg(outputPath);

        const QByteArray data = archive.fileData(entry.filePath);
        const bool inputError = archiveInput == opened.file.get() ? opened.file->error() != QFileDevice::NoError
                                                                  : spooled.file->error() != QFileDevice::NoError;
        if (archive.status() != QZipReader::NoError || inputError || data.size() != entry.size)
            return QCoreApplication::translate("AppController", "The world archive contains an unsupported or damaged file: %1")
                .arg(entry.filePath);
        if (const QString error = io::DocumentIo::writeBytes(outputUrl, data); !error.isEmpty()) return outputPath + ": " + error;
    }
    removeDatabaseTransientFiles(staging_path);
    if (!isCompleteWorld(staging_path))
        return QCoreApplication::translate("AppController", "The world archive does not contain a valid level.dat file and db directory.");
    if (const QString error = prepareImportTarget(target); !error.isEmpty()) return error;
    if (const QString error = io::DocumentIo::rename(staging_url, fileUrl(target)); !error.isEmpty())
        return QCoreApplication::translate("AppController", "Cannot save the imported world.");
    published = true;
    return {};
}

QString LevelOperator::exportMcworld(const QString& source, const QUrl& destination) {
    try {
        if (destination.isLocalFile()) {
            const QFileInfo outputInfo(destination.toLocalFile());
            const auto targetStatus = io::DocumentIo::stat(destination);
            if (!targetStatus.ok()) return targetStatus.error;
            const QString parent = io::DocumentIo::identifier(fileUrl(outputInfo.absolutePath()));
            const QString resolved =
                targetStatus.info.exists ? io::DocumentIo::identifier(destination) : QDir(parent).filePath(outputInfo.fileName());
            const QString canonicalSource = io::DocumentIo::identifier(fileUrl(source));
            const QString relative = QDir(canonicalSource).relativeFilePath(resolved);
            if (relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(relative))
                return QCoreApplication::translate("AppController", "Save the archive outside the source world folder.");
        }
        bl::level_dat dat;
        if (!loadLevelDat(source, dat))
            return QCoreApplication::translate("AppController",
                                               "The selected directory must contain a valid level.dat file and a db directory.");

        platform::WorldEnv env;
        // Hold the database lock while reading its files, including transaction
        // logs, so Minecraft or another process cannot modify or compact it.
        // The default Env cannot reach privileged paths; there the per-file
        // size and timestamp checks in archiveWorldDirectory detect changes.
        leveldb::FileLock* rawLock = nullptr;
        if (!io::isPrivilegedPath(source)) {
            const auto status = env.LockFile(source.toStdString() + "/db/LOCK", &rawLock);
            if (!status.ok())
                return QCoreApplication::translate("AppController",
                                                   "Close Minecraft and other programs using this world before exporting.\n%1")
                    .arg(QString::fromStdString(status.ToString()));
        }
        const auto unlock = [&env](leveldb::FileLock* lock) {
            if (lock) env.UnlockFile(lock);
        };
        const std::unique_ptr<leveldb::FileLock, decltype(unlock)> lock(rawLock, unlock);

        // Always finish the seekable archive before touching the chosen output.
        // This also supports SAF providers which expose only a sequential pipe.
        const QUrl temporary_url = uniqueTemporaryFileUrl(QStringLiteral("bedrockmap-world"));
        const auto remove_temporary = qScopeGuard([&] { removeIfExists(temporary_url); });
        const auto temporary_native = io::DocumentIo::openNative(
            temporary_url, io::NativeOpen::Read | io::NativeOpen::Write | io::NativeOpen::Create | io::NativeOpen::Truncate, 0600);
        if (!temporary_native.ok()) return temporary_native.error;
        QFile temporary;
        if (!temporary.open(temporary_native.descriptor, QIODevice::ReadWrite, QFileDevice::AutoCloseHandle)) {
            const QString error = temporary.errorString();
#ifdef Q_OS_WIN
            _close(temporary_native.descriptor);
#else
            ::close(temporary_native.descriptor);
#endif
            return error;
        }
        QZipWriter writer(&temporary);
        WorldArchive archive{writer};
        const QString error = archiveWorldDirectory(source, {}, archive);
        if (!error.isEmpty()) return error;
        writer.close();
        if (writer.status() != QZipWriter::NoError || temporary.error() != QFileDevice::NoError)
            return QCoreApplication::translate("AppController", "Could not write the world archive.");
        if (temporary.isOpen()) {
            if (!temporary.flush()) return temporary.errorString();
            temporary.close();
        }
        auto spooled = io::DocumentIo::openRead(temporary_url);
        if (!spooled.ok()) return spooled.error;

        return io::DocumentIo::writeStream(destination, [&spooled](QIODevice& output) {
            while (!spooled.file->atEnd()) {
                const QByteArray block = spooled.file->read(65536);
                if (spooled.file->error() != QFileDevice::NoError) return spooled.file->errorString();
                if (output.write(block) != block.size()) return output.errorString();
            }
            return QString();
        });
    } catch (const std::exception& error) {
        return QString::fromUtf8(error.what());
    }
}

QString LevelOperator::exportFolder(const QString& source, const QUrl& destination) {
    try {
        bl::level_dat dat;
        if (!loadLevelDat(source, dat))
            return QCoreApplication::translate("AppController",
                                               "The selected directory must contain a valid level.dat file and a db directory.");

        QString localTarget;
        if (destination.isLocalFile()) {
            localTarget = io::DocumentIo::identifier(destination);
        } else if (platform::isContentUri(destination)) {
            localTarget = io::DocumentIo::resolveNativeDirectory(destination);
        } else {
            return QCoreApplication::translate("AppController", "The export destination is not a folder.");
        }

        const QString canonicalSource = io::DocumentIo::identifier(fileUrl(source));
        if (!localTarget.isEmpty()) {
            const QUrl targetUrl = fileUrl(localTarget);
            const auto targetStatus = io::DocumentIo::stat(targetUrl);
            if (!targetStatus.ok() || !targetStatus.info.exists || targetStatus.info.isLink || !targetStatus.info.isDirectory)
                return QCoreApplication::translate("AppController", "The export destination is not a folder.");
            const QString canonicalTarget = io::DocumentIo::identifier(targetUrl);
            if (canonicalSource.isEmpty() || canonicalTarget.isEmpty() || pathContains(canonicalSource, canonicalTarget) ||
                pathContains(canonicalTarget, canonicalSource))
                return QCoreApplication::translate("AppController", "Choose an export folder outside the source world.");
            localTarget = canonicalTarget;
        }

        platform::WorldEnv env;
        leveldb::FileLock* rawLock = nullptr;
        if (!io::isPrivilegedPath(source)) {
            const auto status = env.LockFile(source.toStdString() + "/db/LOCK", &rawLock);
            if (!status.ok())
                return QCoreApplication::translate("AppController",
                                                   "Close Minecraft and other programs using this world before exporting.\n%1")
                    .arg(QString::fromStdString(status.ToString()));
        }
        const auto unlock = [&env](leveldb::FileLock* lock) {
            if (lock) env.UnlockFile(lock);
        };
        const std::unique_ptr<leveldb::FileLock, decltype(unlock)> lock(rawLock, unlock);

        if (localTarget.isEmpty())
            return QCoreApplication::translate("AppController", "Folder export requires access to global shared storage.");

        if (const QString error = copyWorldDirectory(source, localTarget); !error.isEmpty()) return error;
        if (!isCompleteWorld(localTarget))
            return QCoreApplication::translate("AppController",
                                               "The exported folder does not contain a valid level.dat file and db directory.");
        return {};
    } catch (const std::exception& error) {
        return QString::fromUtf8(error.what());
    }
}
