/* What kind of sound a channel plays (music, an effect, a voice or a movie's
 * sound), for the volumes. It comes from the name the sound archive gives the
 * player the sound plays on; a movie's sound is on
 * the channels the movie player locks. */
#ifndef KHDAYS_SOUND_CATEGORIES_H
#define KHDAYS_SOUND_CATEGORIES_H

enum { KHDAYS_SOUND_MUSIC, KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_VOICES, KHDAYS_SOUND_MOVIES, KHDAYS_SOUND_CATEGORIES };

/* A sequence player's (`stream` 0) or a stream player's (1) category, by
 * its name; a name the rules do not know is an effect, and said once. */
int khdays_sound_category(const char *player_name, int stream);

#endif
