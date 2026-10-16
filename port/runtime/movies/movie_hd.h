/* The course of an HD movie: when the game opens a movie the player has an HD video for
 * (data/cutscenes/cinematics/hd<id>.mp4) and the Video setting is `hd`, the window shows that video
 * with its sound (host/movies/movie_hd.h) while the game plays its own movie unseen, silent and
 * without the player's buttons; at its movie's end the game is held until the HD video ends. START
 * stops the video where the DS lets the movie be skipped. Designed from the decompilation and the
 * measured files. */
#ifndef KHDAYS_MOVIE_HD_H
#define KHDAYS_MOVIE_HD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's thread. A stream of a movie opens (Ov024_MobiClip_OpenStreamSlot,
 * the path as the game built it); `replaced` when a mod's movie takes it. */
void khdays_movie_hd_stream(int slot, const char *path, int replaced);

/* The game's movie stops (Ov024_MobiClip_StopPlayback). */
void khdays_movie_hd_stopped(void);

/* The game's reset (start.c): an HD video showing is closed, the game shown
 * and heard again, its clock let go. */
void khdays_movie_hd_reset(void);

/* The credits scene, ov011: 1 it begins (Ov011_CreateScene), 2 its staff roll ends
 * (Ov011_StartTitleExit), 0 it ends. The HD video carries the roll too, so the game runs its roll
 * behind the video and is held where the roll ends if the video still plays. */
void khdays_movie_hd_credits(int phase);

/* The movie's loop, each turn (Ov024_MobiClip_UpdatePlayback's player flags,
 * Ov012_RunOpeningScene's with bit 3 set): bit 3 the DS lets START skip it,
 * bit 2 it has been skipped. */
void khdays_movie_hd_flags(unsigned flags);

/* The DS buttons the game is given, from those pressed (the player's and a
 * script's): none of them while a course runs -- only START while the port
 * skips the game's movie -- and, after it, none of those still held from it
 * until they are let go. A START pressed during a course asks for the skip. */
uint16_t khdays_movie_hd_game_buttons(uint16_t buttons);

/* Either thread: whether a course runs (the window shows the HD video). */
int khdays_movie_hd_showing(void);
/* Either thread: whether the game's own movie plays (a stream opened, its playback not stopped). */
int khdays_movie_game_playing(void);

/* The host's thread, each refresh while a course runs: the video's end or
 * failure. */
void khdays_movie_hd_poll(void);

/* The host's thread: the player's DS buttons now (while the game is held its
 * thread reads none). */
void khdays_movie_hd_host_buttons(uint16_t buttons);

#ifdef __cplusplus
}
#endif

#endif
