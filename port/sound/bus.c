/* The player's volumes (see bus.h). */
#include "bus.h"

static float fraction(int percent)
{
    return percent <= 0 ? 0.0f : percent >= 100 ? 1.0f : (float)percent / 100.0f;
}

void khdays_sound_bus_set(KhdaysSoundBus *bus, int volume, int music, int effects, int voices, int movies)
{
    bus->volume = fraction(volume);
    bus->category[KHDAYS_SOUND_MUSIC] = fraction(music);
    bus->category[KHDAYS_SOUND_EFFECTS] = fraction(effects);
    bus->category[KHDAYS_SOUND_VOICES] = fraction(voices);
    bus->category[KHDAYS_SOUND_MOVIES] = fraction(movies);
}

int khdays_sound_bus_unity(const KhdaysSoundBus *bus)
{
    for (int c = 0; c < KHDAYS_SOUND_CATEGORIES; ++c) {
        if (bus->category[c] != 1.0f) {
            return 0;
        }
    }
    return bus->volume == 1.0f;
}
