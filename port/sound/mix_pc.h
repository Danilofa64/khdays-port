/* The PC's mixer, the default: the same channels as the DS's mixer (mix_ds.h), each read at the
 * instant of each output sample at the PC's rate, mixed in floating point without the DS's 10-bit
 * rounding or bias and clipped only at full scale; no interpolation, as on the DS. Its scale is the DS
 * mixer's before it rounds, divided by 32768. */
#ifndef KHDAYS_SOUND_MIX_PC_H
#define KHDAYS_SOUND_MIX_PC_H

#include "channels.h"

/* The output sample the channels make now, left and right, in [-1, 1].
 * `gain` (NULL: none) scales each channel and `volume` the whole: the
 * player's volumes (bus.h). `extra` (NULL: none), at the same scale, is
 * added before the clip, under `volume` too: the replaced sounds'. */
void khdays_sound_mix_pc(const KhdaysSoundHw *hw, const float gain[16], float volume, const float extra[2],
                         float out[2]);

#endif
