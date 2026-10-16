/* The originals written out, to make replacements from: `khdays-native --dump-sounds [DIR]`, headless.
 * Every sound of the game's archive as WAV, under the identifier a replacement takes
 * (port/sound/archive.h):
 *
 *   - wave/<archive>/<index>.wav: each wave an instrument plays, as the DS's channel decodes it;
 *   - strm/<name>.wav: each stream, as NitroSystem's stream player makes it;
 *   - seq/<name>.wav, seqarc/<archive>/<entry>.wav: each sequence and archive entry, played by the
 *     native driver and the PC's mixer at 48,000 Hz.
 *
 * Loops go in `smpl`. The dump is the game's data: only under saves/, never elsewhere in a repository. */
#ifndef KHDAYS_SOUND_DUMP_H
#define KHDAYS_SOUND_DUMP_H

#include <stddef.h>

/* The default directory, from the working directory. */
#define KHDAYS_SOUND_DUMP_DIRECTORY "saves/dump/sounds"

/* After the ROM is open and the static module's image in memory (the
 * tables the sound reads are in it). Returns the process's exit code. */
int khdays_sound_dump(const char *directory);

/* Whether a dump may write to `directory`: inside a repository only under
 * its saves\ (the dumps are the game's data). Its full path into `full`;
 * says why not. */
int khdays_dump_allowed(const char *directory, char *full, size_t size);

#endif
