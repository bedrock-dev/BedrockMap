#ifndef BEDROCKMAP_IO_PRIVILEGEDVFS_H
#define BEDROCKMAP_IO_PRIVILEGEDVFS_H

#include "io/vfs.h"

#ifdef Q_OS_ANDROID
namespace io {
    /// Local paths reached through the Shizuku or root file service. The service
    /// passes descriptors back, so reads, writes and future LevelDB access use
    /// ordinary POSIX I/O in this process.
    class PrivilegedVfsBackend final : public Vfs {
       public:
        /// True when an authorized service is connected. Blocks while binding,
        /// and always returns false on Android's main looper thread.
        [[nodiscard]] bool available() const;

        [[nodiscard]] OpenReadResult openRead(const QUrl& url) const override;
        [[nodiscard]] QString writeStream(const QUrl& url, const StreamWriter& writer, const WriteOptions& options = {}) const override;
        [[nodiscard]] QString displayName(const QUrl& url) const override;
        [[nodiscard]] StatResult stat(const QUrl& url) const override;
        [[nodiscard]] ListResult list(const QUrl& url) const override;
        [[nodiscard]] QString makeDirectory(const QUrl& url, bool recursive = true) const override;
        [[nodiscard]] QString remove(const QUrl& url) const override;
        [[nodiscard]] QString rename(const QUrl& from, const QUrl& to) const override;
        [[nodiscard]] NativeFileResult openNative(const QUrl& url, NativeOpen flags, int mode = 0644) const override;
    };
}  // namespace io
#endif

#endif  // BEDROCKMAP_IO_PRIVILEGEDVFS_H
