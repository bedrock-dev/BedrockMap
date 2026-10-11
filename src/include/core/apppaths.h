#pragma once

#include <QString>

namespace app_paths {
QString configDirectory();
QString configFile();
QString recentFile();
QString cacheDirectory();
QString logDirectory();
// User-visible files selected for import/export. This is deliberately kept
// outside the persistent settings and disposable cache locations.
QString importExportDirectory();
void initialize();
}  // namespace app_paths
