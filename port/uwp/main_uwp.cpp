/* The UWP app's entry: SDL3-UWP (github.com/SternXD/SDL3-uwp) provides the
 * Windows Runtime plumbing (CoreApplication and its IFrameworkView) and calls
 * the game's khdays_main (start.c renames its main under KHDAYS_UWP). This
 * file must be compiled as C++ with /ZW: SDL_main_impl.h requires it on WinRT. */
#include <SDL3/SDL_main.h>

#ifdef SDL_PLATFORM_WINRT
#include <windows.h>
#include <stdio.h>
#include <string>

extern "C" int khdays_main(int argc, char *argv[]);

/* The game takes its working folder (and opens its log) only after the memory map, and the memory
 * map's failures go to stderr, which nobody reads on a console: the game would close with nothing
 * written. So stderr is pointed at LocalState\boot.log first thing. */
static LONG WINAPI boot_crash(EXCEPTION_POINTERS *info)
{
    fprintf(stderr, "boot: unhandled exception 0x%08lx at %p\n", (unsigned long)info->ExceptionRecord->ExceptionCode,
            info->ExceptionRecord->ExceptionAddress);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void open_boot_log()
{
    try {
        Platform::String ^folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path;
        std::wstring file = std::wstring(folder->Data()) + L"\\boot.log";
        FILE *opened = nullptr;
        if (_wfreopen_s(&opened, file.c_str(), L"w", stderr) == 0) {
            setvbuf(stderr, NULL, _IONBF, 0);
            fprintf(stderr, "boot: main reached\n");
        }
    } catch (Platform::Exception ^) {
        /* no log: the game still runs */
    }
}

int main(int argc, char *argv[])
{
    open_boot_log();
    SetUnhandledExceptionFilter(boot_crash);
    const int result = khdays_main(argc, argv);
    fprintf(stderr, "boot: khdays_main returned %d\n", result);
    fflush(stderr);
    return result;
}
#endif
