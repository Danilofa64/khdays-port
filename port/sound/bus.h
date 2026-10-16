/* The volumes the player sets. Each
 * channel gets its category's volume (music, effects, voices or movies, by
 * what it plays: categories.h), then the overall volume. The mixers (mix_ds.h,
 * mix_pc.h) apply them to what the game produced; what the game reads back
 * stays its own. With all at 100 the channels go through untouched. */
#ifndef KHDAYS_SOUND_BUS_H
#define KHDAYS_SOUND_BUS_H

#include "categories.h"

typedef struct KhdaysSoundBus {
    float volume;                            /* everything, 0-1 */
    float category[KHDAYS_SOUND_CATEGORIES]; /* 0-1 */
} KhdaysSoundBus;

/* From the settings' 0-100 (linear in amplitude). */
void khdays_sound_bus_set(KhdaysSoundBus *bus, int volume, int music, int effects, int voices, int movies);

/* Every volume at 100. */
int khdays_sound_bus_unity(const KhdaysSoundBus *bus);

#endif
