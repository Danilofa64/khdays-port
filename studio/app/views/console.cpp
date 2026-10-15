#include "console.h"

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS  // freopen on the std streams is intended here
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#endif

namespace studio {

#ifdef _WIN32
// Whether a standard handle is unset (nothing given to the process for that stream).
static bool std_handle_free(DWORD which) {
    HANDLE h = GetStdHandle(which);
    return h == nullptr || h == INVALID_HANDLE_VALUE;
}
#endif

void attach_parent_console() {
#ifdef _WIN32
    // A windowed program opens no console of its own. When it was started from a
    // terminal, attach to that terminal so the command line's output appears there.
    // A stream already sent somewhere (a file or a pipe: a script) stays where it goes.
    const bool out = std_handle_free(STD_OUTPUT_HANDLE), err = std_handle_free(STD_ERROR_HANDLE),
               in = std_handle_free(STD_INPUT_HANDLE);
    if ((out || err || in) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (out) (void)std::freopen("CONOUT$", "w", stdout);
        if (err) (void)std::freopen("CONOUT$", "w", stderr);
        if (in) (void)std::freopen("CONIN$", "r", stdin);
    }
#endif
}

}  // namespace studio
