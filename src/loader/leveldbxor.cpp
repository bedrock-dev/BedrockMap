#include "loader/leveldbxor.h"

#include <QByteArray>
#include <QDir>
#include <QUrl>
#include <array>
#include <cstddef>

#include "io/documentio.h"

namespace {
    constexpr std::array<unsigned char, 4> kXorMagic{0x80, 0x1d, 0x30, 0x01};

    [[nodiscard]] bool hasXorMagic(const QString& path) {
        const auto input = io::DocumentIo::openRead(QUrl::fromLocalFile(path));
        const QByteArray header = input.ok() ? input.file->read(static_cast<qint64>(kXorMagic.size())) : QByteArray{};
        if (header.size() != static_cast<qsizetype>(kXorMagic.size())) return false;
        for (qsizetype index = 0; index < header.size(); ++index)
            if (static_cast<unsigned char>(header[index]) != kXorMagic[static_cast<std::size_t>(index)]) return false;
        return true;
    }
}  // namespace

namespace leveldb_xor {

    bool isEncrypted(const QString& worldRoot) {
        const QString database = QDir(worldRoot).filePath(QStringLiteral("db"));
        const auto children = io::DocumentIo::list(QUrl::fromLocalFile(database));
        if (!children.ok()) return false;
        for (const auto& name : children.names) {
            // The mcne format deliberately leaves log files unencrypted.
            if (name.endsWith(QStringLiteral(".log"), Qt::CaseInsensitive)) continue;
            const QString path = QDir(database).filePath(name);
            const auto status = io::DocumentIo::stat(QUrl::fromLocalFile(path));
            if (!status.ok() || !status.info.isFile) continue;
            if (hasXorMagic(path)) return true;
        }
        return false;
    }

}  // namespace leveldb_xor
