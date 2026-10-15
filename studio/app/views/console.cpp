#include "console.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace studio {

void leave_own_console() {
#ifdef _WIN32
    // Only this process on the console: the system opened it for us (a
    // double click), not a terminal the modder typed in.
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1) FreeConsole();
#endif
}

}  // namespace studio
