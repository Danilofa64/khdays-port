/* A movie a mod puts in the game's place: mods/<Mod>/movies/<id>.mpg (MPEG-1 video and Layer II
 * sound, any size) for the game's mv/<id>.mods, decoded by pl_mpeg on a thread of its own. The game
 * plays its own movie as ever (its timing, skip and end); the port shows the mod's picture where the
 * game's frames show, at the game's movie time, and plays the mod's sound instead. */
#ifndef KHDAYS_VIDEO_MOD_H
#define KHDAYS_VIDEO_MOD_H

#include "../../hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game opens stream `slot` of a movie at `path` (Ov024_MobiClip_OpenStreamSlot,
 * the path as the game built it: the opening's is slot 1's, "/mv/802.mods");
 * a movie of one stream looks for a mod's movie, one of more is not
 * replaced. */
void khdays_video_movie_opened(int slot, const char *path);

/* The game stops its movie (Ov024_MobiClip_StopPlayback). */
void khdays_video_movie_stopped(void);

/* The game blitted its movie's frame `index` (counted from 0). */
void khdays_video_mod_frame(u32 index);

/* The mod's picture (a changing replacement, textures.h), or -1 when no mod
 * movie plays. */
int khdays_video_mod_picture(void);

/* Whether the mod's sound plays in place of the game's movie channels. */
int khdays_video_mod_sound(void);

/* The sound thread, an output sample at `rate`: the mod's, times `gain`,
 * added to `out` (left, right). */
void khdays_video_mod_mix(u32 rate, float gain, float out[2]);

#ifdef __cplusplus
}
#endif

#endif
