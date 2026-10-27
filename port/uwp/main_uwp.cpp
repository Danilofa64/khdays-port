/* The UWP app's entry: SDL3-UWP (github.com/SternXD/SDL3-uwp) provides the
 * Windows Runtime plumbing (CoreApplication and its IFrameworkView) and calls
 * the game's khdays_main (start.c renames its main under KHDAYS_UWP). This
 * file must be compiled as C++ with /ZW: SDL_main_impl.h requires it on WinRT. */
#include <SDL3/SDL_main.h>

#ifdef SDL_PLATFORM_WINRT
extern "C" int khdays_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    return khdays_main(argc, argv);
}
#endif
