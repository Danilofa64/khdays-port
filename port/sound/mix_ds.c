/* The DS's mixer (mix_ds.h): divider, volume and pan in GBATEK's fixed point, summed, master volume,
 * the bias added and clipped to 10 bits (docs/SOUND.md). The channels are read as each output sample
 * is due. */
#include "mix_ds.h"

#include <stddef.h>

/* 7-bit volume and panning: 0..126 are N/128, 127 is 128/128. */
static int32_t factor(uint32_t value)
{
    return value == 127 ? 128 : (int32_t)value;
}

void khdays_sound_mix_ds(const KhdaysSoundHw *hw, const float gain[16], float volume, int16_t out[2])
{
    static const int divider[4] = {0, 1, 2, 4};
    const uint32_t soundcnt = khdays_sound_hw_read(hw, 0x100, 2);
    const uint32_t bias = khdays_sound_hw_read(hw, 0x104, 2) & 0x3ff;
    int64_t left[16], right[16], mix_left = 0, mix_right = 0;
    int64_t master_left, master_right;

    for (int n = 0; n < 16; ++n) {
        const uint32_t cnt = khdays_sound_hw_control(hw, n);
        const int32_t sample = gain == NULL || gain[n] == 1.0f
                                   ? khdays_sound_hw_sample(hw, n)
                                   : (int32_t)((float)khdays_sound_hw_sample(hw, n) * gain[n]);
        int32_t v = (sample * 16) >> divider[(cnt >> 8) & 3];              /* 16.4 */
        v *= factor(cnt & 0x7f);                                           /* 16.11 */
        left[n] = ((int64_t)v * (128 - factor((cnt >> 16) & 0x7f))) >> 10; /* 16.18 -> 16.8 */
        right[n] = ((int64_t)v * factor((cnt >> 16) & 0x7f)) >> 10;
        if ((n == 1 && (soundcnt & 0x1000)) || (n == 3 && (soundcnt & 0x2000))) {
            continue; /* not to the mixer */
        }
        mix_left += left[n];
        mix_right += right[n];
    }
    switch ((soundcnt >> 8) & 3) {
    case 0: master_left = mix_left; break;
    case 1: master_left = left[1]; break;
    case 2: master_left = left[3]; break;
    default: master_left = left[1] + left[3]; break;
    }
    switch ((soundcnt >> 10) & 3) {
    case 0: master_right = mix_right; break;
    case 1: master_right = right[1]; break;
    case 2: master_right = right[3]; break;
    default: master_right = right[1] + right[3]; break;
    }
    for (int side = 0; side < 2; ++side) {
        int64_t v = side == 0 ? master_left : master_right;
        v = (soundcnt & 0x8000) ? (v * factor(soundcnt & 0x7f)) >> 21 : 0; /* 14.0 */
        v += bias;
        if (v < 0)
            v = 0;
        if (v > 0x3ff)
            v = 0x3ff;
        out[side] = (int16_t)((v - (int64_t)bias) * 64);
        if (volume != 1.0f) {
            out[side] = (int16_t)((float)out[side] * volume);
        }
    }
}
