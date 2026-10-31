#ifndef BEDROCKMAP_WORLDFILESOURCE_H
#define BEDROCKMAP_WORLDFILESOURCE_H

#include <QFile>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <memory>

namespace platform {
    struct WorldFileInfo {
        bool isFile{false};
        bool isDirectory{false};
        bool isLink{false};
        std::uint64_t size{0};
        std::int64_t modified{0};
    };

    /// Read-only tree access for scanning and importing external worlds.
    class WorldFileSource {
       public:
        virtual ~WorldFileSource() = default;

        [[nodiscard]] virtual QString fileInfo(const QString& path, WorldFileInfo& info) const = 0;
        [[nodiscard]] virtual QString children(const QString& path, QStringList* result) const = 0;
        [[nodiscard]] virtual std::unique_ptr<QFile> openRead(const QString& path) const = 0;
    };

    [[nodiscard]] std::unique_ptr<WorldFileSource> worldFileSource(const QString& path);

    /// Android private/root paths and Android/data worlds must be copied first.
    [[nodiscard]] bool requiresWorldCopy(const QString& path);
}  // namespace platform

#endif  // BEDROCKMAP_WORLDFILESOURCE_H
