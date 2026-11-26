#ifndef BEDROCKMAP_PLATFORM_H
#define BEDROCKMAP_PLATFORM_H

#include <QString>
#include <QUrl>

// OS services and general file URL handling, independent of world structure.
namespace platform {
    [[nodiscard]] bool isContentUri(const QUrl& url);

    /// Opens a local file's containing folder or a local directory.
    [[nodiscard]] bool revealInFileManager(const QString& path);
    [[nodiscard]] bool openDirectory(const QString& path);

    /// Opens a temporary local export through the Android application chooser.
    /// Returns an error, empty on success, or an unsupported error elsewhere.
    [[nodiscard]] QString openExportedWorld(const QUrl& url, const QString& chooserTitle);

    /// Installs the native crash reporter when the target enables it.
    /// Safe to call more than once.
    void installCrashHandler();

    /// Current resident memory (RSS/working set) in MiB; 0 if unavailable.
    double memoryUsageMiB();

    /// True when shared storage can be read directly. Android requires the
    /// "all files access" special permission on API 30+; other platforms have
    /// no equivalent restriction.
    bool hasAllFilesPermission();

    /// Opens Android's storage permission request after a user clicks a button.
    void requestAllFilesPermission();

    /// Current Shizuku connection/permission state; unavailable outside Android.
    [[nodiscard]] QString shizukuPermission();
    void requestShizukuPermission();
    [[nodiscard]] QString rootPermission();
    void requestRootPermission();
}  // namespace platform

#endif  // BEDROCKMAP_PLATFORM_H
