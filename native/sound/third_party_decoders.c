/* The decoders the replaced sounds use (replace.c), compiled here once:
 * Ogg Vorbis, stb_vorbis.c (stb, public domain or MIT), and FLAC, dr_flac.h
 * (dr_libs, public domain or MIT-0) -- THIRD_PARTY_NOTICES.md records both
 * and their pinned commits. Their warnings stay theirs (native/CMakeLists.txt). */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_WCHAR
#include "dr_flac.h"

#include "stb_vorbis.c"
