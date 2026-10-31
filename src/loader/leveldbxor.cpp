#include "loader/leveldbxor.h"

#include <QByteArray>
#include <QDir>
#include <array>
#include <cstddef>

#include "base/worldfilesource.h"

namespace {
    constexpr std::array<unsigned char, 4> kXorMagic{0x80, 0x1d, 0x30, 0x01};

    [[nodiscard]] bool hasXorMagic(platform::WorldFileSource& files, const QString& path) {
        const auto input = files.openRead(path);
        const QByteArray header = input ? input->read(static_cast<qint64>(kXorMagic.size())) : QByteArray{};
        if (header.size() != static_cast<qsizetype>(kXorMagic.size())) return false;
        for (qsizetype index = 0; index < header.size(); ++index)
            if (static_cast<unsigned char>(header[index]) != kXorMagic[static_cast<std::size_t>(index)]) return false;
        return true;
    }
}  // namespace

namespace leveldb_xor {

    bool isEncrypted(platform::WorldFileSource& files, const QString& worldRoot) {
        const QString database = QDir(worldRoot).filePath(QStringLiteral("db"));
        QStringList children;
        if (!files.children(database, &children).isEmpty()) return false;
        for (const auto& name : children) {
            // The mcne format deliberately leaves log files unencrypted.
            if (name.endsWith(QStringLiteral(".log"), Qt::CaseInsensitive)) continue;
            platform::WorldFileInfo info;
            const QString path = QDir(database).filePath(name);
            if (!files.fileInfo(path, info).isEmpty() || !info.isFile) continue;
            if (hasXorMagic(files, path)) return true;
        }
        return false;
    }

    bool isEncrypted(const QString& worldRoot) {
        const auto files = platform::worldFileSource(worldRoot);
        return files && isEncrypted(*files, worldRoot);
    }

}  // namespace leveldb_xor
