#ifndef BEDROCKMAP_APPPATHS_H
#define BEDROCKMAP_APPPATHS_H

#include <QString>

// Application storage paths. Directory accessors create missing directories.
namespace app_paths {
    QString configDirectory();
    QString configFile();
    QString recentFile();
    QString cacheDirectory();
    QString logDirectory();
    QString importExportDirectory();
    void initialize();
}  // namespace app_paths

#endif  // BEDROCKMAP_APPPATHS_H
