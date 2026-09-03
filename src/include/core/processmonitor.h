#ifndef BEDROCKMAP_PROCESSMONITOR_H
#define BEDROCKMAP_PROCESSMONITOR_H

// Isolates platform-specific process/system sampling (Windows.h/Psapi) so
// widgets never include OS headers directly. Non-Windows builds return 0.
namespace processmonitor {

    /// Current working-set memory of this process in MiB (0 when unsupported).
    double memoryUsageMiB();

}  // namespace processmonitor
#endif
