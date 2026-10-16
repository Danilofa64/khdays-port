/* The two BIOS tables the ARM7's sound driver calls (GBATEK "BIOS Sound
 * Functions"): GetPitchTable (SWI 1Bh) and GetVolumeTable (SWI 1Ch); the
 * native driver reads them here, as the interpreted one's BIOS calls did.
 *
 * GBATEK gives their ranges, not their contents: the pitch table is
 * 0x10000 * (2^(i/768) - 1) rounded, which matches a dump of the BIOS table
 * in all 768 entries (and GBATEK's last value, FF8Ah); the volume table is
 * the ARM9's own copy of it, for its SND_CalcChannelVolume, byte for byte
 * the BIOS's (checked against a dump). */
#include "snd7_internal.h"
#include "snd7_native.h"

#include <math.h>

static uint16_t pitch_table[768];
static int pitch_ready;

uint16_t khdays_snd7_pitch_table(int index)
{
    if (!pitch_ready) {
        for (int i = 0; i < 768; ++i) {
            pitch_table[i] = (uint16_t)floor(65536.0 * (pow(2.0, i / 768.0) - 1.0) + 0.5);
        }
        pitch_ready = 1;
    }
    return pitch_table[index];
}

uint16_t snd7_pitch_table(int index)
{
    return khdays_snd7_pitch_table(index);
}

uint8_t snd7_volume_table(int index)
{
    return snd7_rd8(KHDAYS_SND7_ARM9_VOLUME_TABLE + (uint32_t)index);
}
