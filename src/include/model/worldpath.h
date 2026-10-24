#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QUrl>

namespace world_path {
    // Provider URLs (such as Android content://) need a storage adapter or
    // import before the local LevelDB backend can open them.
    [[nodiscard]] inline QString normalize(const QString& input) {
        if (input.isEmpty()) return {};
        QString path = input;
#ifdef Q_OS_WIN
        const bool drivePath = input.size() >= 2 && input.at(0).isLetter() && input.at(1) == QLatin1Char(':');
#else
        const bool drivePath = false;
#endif
        if (!drivePath) {
            const QUrl url(input);
            if (!url.scheme().isEmpty()) {
                if (!url.isValid() || !url.isLocalFile() || url.hasQuery() || url.hasFragment()) return {};
                path = url.toLocalFile();
            }
        }
        if (path.isEmpty()) return {};
        const QFileInfo info(QDir::fromNativeSeparators(path));
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
    }

    // Keys are for comparison only; preserve case in stored paths.
    [[nodiscard]] inline QString identityKey(const QString& normalizedPath) {
#ifdef Q_OS_WIN
        return normalizedPath.toCaseFolded();
#else
        return normalizedPath;
#endif
    }
}  // namespace world_path
