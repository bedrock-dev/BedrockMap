#ifndef BEDROCKMAP_CRASHHANDLER_H
#define BEDROCKMAP_CRASHHANDLER_H

namespace crashhandler {

// Install crash reporting for this process. Hardware faults (access violation,
// divide-by-zero, ...) are caught via an SEH unhandled-exception filter, and
// abort-style signals (SIGABRT/SIGFPE) via signal handlers. Each prints a
// symbolized stack trace (libbacktrace reads the DWARF straight from the exe)
// to stderr and ./logs/crash_<ts>.log, then lets the process die normally.
// Call once, early in main().
void install();

} // namespace crashhandler

#endif // BEDROCKMAP_CRASHHANDLER_H
