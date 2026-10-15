/* The ARM7's view of sample memory, for native/sound's channels (sound.c). */
#ifndef KHDAYS_SOUND_H
#define KHDAYS_SOUND_H

#include "../../hal/hal.h"
#include "../../sound/channels.h"
#include "../../sound/mix_ds.h"  /* KHDAYS_SOUND_CYCLES_PER_SAMPLE, KHDAYS_SOUND_RATE */

/* Sample memory as the ARM7 sees it: main RAM through its 4 MB mirrors, or
 * the ARM7's WRAM; NULL (silence, said once) elsewhere. The channels'
 * KhdaysSoundMemory. */
const u8 *khdays_sound_ds_memory(u32 address, int channel);

#endif
