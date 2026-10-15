/* The PC's mixer (see mix_pc.h): the DS's steps (mix_ds.c) at full
 * precision. Each channel's sample is divided by its divider (1, 2, 4 or 16),
 * multiplied by its volume and by each side's share of its pan (N/128, with
 * 127 as 128/128), and summed; channels 1 and 3 are routed as SOUNDCNT says;
 * then the master volume is applied, and the result is divided by 32768 and
 * clipped to [-1, 1], where the DS clips its 10 bits. */
#include "mix_pc.h"

#include <stddef.h>

/* 7-bit volume and panning: 0..126 are N/128, 127 is 128/128. */
static float factor(uint32_t value)
{
    return (value == 127 ? 128.0f : (float)value) / 128.0f;
}

static float clip(float v)
{
    return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
}

void khdays_sound_mix_pc(const KhdaysSoundHw *hw, const float gain[16], float volume, const float extra[2],
                         float out[2])
{
    static const float divider[4] = {1.0f, 2.0f, 4.0f, 16.0f};
    const uint32_t soundcnt = khdays_sound_hw_read(hw, 0x100, 2);
    float left[16], right[16], mix_left = 0.0f, mix_right = 0.0f;
    float master_left, master_right, master;

    for (int n = 0; n < 16; ++n) {
        const uint32_t cnt = khdays_sound_hw_control(hw, n);
        const float pan = factor((cnt >> 16) & 0x7f);
        const float v = (float)khdays_sound_hw_sample(hw, n) / divider[(cnt >> 8) & 3] * factor(cnt & 0x7f) *
                        (gain != NULL ? gain[n] : 1.0f);
        left[n] = v * (1.0f - pan);
        right[n] = v * pan;
        if ((n == 1 && (soundcnt & 0x1000)) || (n == 3 && (soundcnt & 0x2000))) {
            continue;  /* not to the mixer */
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
    master = (soundcnt & 0x8000) ? factor(soundcnt & 0x7f) * volume / 32768.0f : 0.0f;
    out[0] = clip(master_left * master + (extra != NULL ? extra[0] * volume : 0.0f));
    out[1] = clip(master_right * master + (extra != NULL ? extra[1] * volume : 0.0f));
}
