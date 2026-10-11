#include "apppaths.h"

#include <QDir>
#include <QStandardPaths>

namespace app_paths {
namespace {
QString ensureDirectory(QString path) {
    if (path.isEmpty()) path = QDir::currentPath() + QStringLiteral("/BedrockMap");
    QDir().mkpath(path);
    return path;
}
}  // namespace

QString configDirectory() { return ensureDirectory(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)); }
QString configFile() { return configDirectory() + QStringLiteral("/config.ini"); }
QString recentFile() { return configDirectory() + QStringLiteral("/recent.ini"); }
QString cacheDirectory() { return ensureDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)); }
QString logDirectory() { return ensureDirectory(cacheDirectory() + QStringLiteral("/logs")); }
QString importExportDirectory() {
    auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) documents = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return ensureDirectory(QDir(documents).absoluteFilePath(QStringLiteral("BedrockMap")));
}
void initialize() { configDirectory(); }
}  // namespace app_paths
