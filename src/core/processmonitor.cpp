#include "processmonitor.h"

#ifdef WIN32
// clang-format off
#include <Windows.h>
#include <Psapi.h>
// clang-format on
#endif

namespace processmonitor {

    double memoryUsageMiB() {
#ifdef WIN32
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc));
        return static_cast<double>(pmc.WorkingSetSize >> 20);
#else
        return 0.0;
#endif
    }

}  // namespace processmonitor
