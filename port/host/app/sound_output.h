/* The sound output (sound_output.c): an SDL audio stream on the default
 * device. The sound thread feeds it: every millisecond it runs the sound
 * bridge up to the DS clock's current time. */
#ifndef KHDAYS_SOUND_OUTPUT_H
#define KHDAYS_SOUND_OUTPUT_H

/* Opens the stream. Call it on the host's thread after
 * SDL_Init(SDL_INIT_AUDIO). If the device cannot be opened, the port runs
 * without sound and says so. */
void khdays_sound_output_open(void);

/* Applies the player's [sound] settings: the volumes, and the mixer (the
 * PC's, at the device's own rate, unless `ds_exact` asks for the DS's). */
struct KhdaysSound;
void khdays_sound_output_set(const struct KhdaysSound *settings);

/* Closes the stream (on the host's thread, when the port quits). */
void khdays_sound_output_close(void);

/* Pauses or resumes the device. The settings menu pauses it while it is
 * open, since the game's time stands still then. */
void khdays_sound_output_pause(int paused);

/* Silences the game's sound, or brings it back: while an HD video replaces a movie the game is silent
 * (the video has its own sound). Can be called from any thread. */
void khdays_sound_output_mute(int mute);

#endif
