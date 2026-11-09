#ifndef BEDROCKMAP_PLATFORM_H
#define BEDROCKMAP_PLATFORM_H

#include <QFile>
#include <QString>
#include <QUrl>

// OS services and general file URL handling, independent of world structure.
namespace platform {
    [[nodiscard]] bool isContentUri(const QUrl& url);

    /// Resolves a local path or file URL; returns empty for provider URLs.
    [[nodiscard]] QString normalizeLocalPath(const QString& input);

    /// Queries the provider's display name for content URIs, or a local filename.
    /// Falls back to the URI name or original input when metadata is unavailable.
    [[nodiscard]] QString fileDisplayName(const QString& input);

    /// Opens an existing provider document through its original URI on Android,
    /// or a regular QFile elsewhere. Returns an error, or empty on success.
    /// The QFile owns the resulting descriptor and closes it automatically.
    [[nodiscard]] QString openDocumentFile(QFile& file, QIODevice::OpenMode mode);

    /// Resolves an Android SAF directory to a native path using existing access.
    /// Returns empty when the provider or current permissions prevent resolution.
    [[nodiscard]] QString resolveContentDirectory(const QUrl& url);

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
