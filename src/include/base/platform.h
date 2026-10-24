#ifndef BEDROCKMAP_PLATFORM_H
#define BEDROCKMAP_PLATFORM_H

namespace platform {
    /// Installs the native crash reporter when the target enables it.
    /// Safe to call more than once.
    void installCrashHandler();

    /// Current working-set memory of this process in MiB (0 when unsupported).
    double memoryUsageMiB();

    /// True when shared storage can be read directly. Android requires the
    /// "all files access" special permission on API 30+; other platforms have
    /// no equivalent restriction.
    bool hasAllFilesAccess();
}  // namespace platform

#endif  // BEDROCKMAP_PLATFORM_H
