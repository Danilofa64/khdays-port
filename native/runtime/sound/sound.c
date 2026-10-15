/* Where the ARM7 finds sample data, for native/sound's channels (sound.h). */
#include "sound.h"
#include "sound_bridge.h"
#include "../core/window.h"
#include "../core/game_heap.h"
#include "khdays_ds_symbols.h" /* the DS addresses of what the port reads of the game's (prepare.py) */

#include <stdio.h>

/* Report an oddity once. */
static void report(const char *what, int channel)
{
    static const char *seen[8];
    static int count;
    for (int i = 0; i < count; ++i) {
        if (seen[i] == what) {
            return;
        }
    }
    if (count < 8) {
        seen[count++] = what;
    }
    fprintf(stderr, "sound: %s (channel %d)\n", what, channel);
    fflush(stderr);
}

const u8 *khdays_sound_ds_memory(u32 address, int channel)
{
    if (address >= KHDAYS_HEADERS_BASE && address < 0x03000000u && !khdays_game_heap_ds_memory()) {
        return (const u8 *)KHDAYS_W(address);  /* the headers' heaps, not a mirror */
    }
    if (address >= 0x02000000u && address < 0x03000000u) {
        return (const u8 *)KHDAYS_W(0x02000000u | (address & 0x3fffffu));
    }
    if (address >= KHDAYS_HEAPS_BASE && address < KHDAYS_HEAPS_END) {  /* the game's heaps */
        return (const u8 *)KHDAYS_W(address);
    }
    if (address >= 0x037f8000u && address < 0x03810000u) {
        return (const u8 *)KHDAYS_W(address);
    }
    {
        static int told;
        if (!told) {
            told = 1;
            fprintf(stderr, "sound: the first sample data outside the game's memory was at 0x%08x\n", address);
        }
    }
    report("sample data outside the game's memory and WRAM, played as silence", channel);
    return NULL;
}

const char *khdays_sound_data_path(void)
{
    return (const char *)KHDAYS_W(KHDAYS_DS_gSndSoundDataPath);
}
