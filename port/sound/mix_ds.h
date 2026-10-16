/* The DS's own mixer (GBATEK "DS Sound Control Registers", "Notes"): the
 * channels at GBATEK's bit widths down to the 10-bit output, one stereo
 * sample per 1024 cycles (33513982 / 1024 = 32728.5 Hz). The reference the
 * port's sound is checked against, and the "DS-exact sound" setting. */
#ifndef KHDAYS_SOUND_MIX_DS_H
#define KHDAYS_SOUND_MIX_DS_H

#include "channels.h"

#define KHDAYS_SOUND_CYCLES_PER_SAMPLE 1024u
#define KHDAYS_SOUND_RATE 32728

/* The output sample the channels make now, left and right, as PCM16 (the
 * 10-bit value less the bias, times 64). `gain` (NULL: none) scales each
 * channel's sample and `volume` the output: the player's volumes (bus.h),
 * both 1 for the DS's own. */
void khdays_sound_mix_ds(const KhdaysSoundHw *hw, const float gain[16], float volume, int16_t out[2]);

#endif
