/* The native sound driver: the ARM7 binary's sound driver written in C (snd7_*.c), each function
 * citing its ARM7 address (docs/RUNTIME_MAP.md section 6). Its state is C's; it reaches the DS's
 * memory (the command lists, sequences, banks, waves and the shared work) through `ds_base`. */
#ifndef KHDAYS_SOUND_SND7_NATIVE_H
#define KHDAYS_SOUND_SND7_NATIVE_H

#include "snd7.h"

#include <stdint.h>

extern const KhdaysSnd7Driver khdays_snd7_native;

/* Where the DS's address 0 is in the process. */
void khdays_snd7_native_set_ds_base(uintptr_t base);

/* The lockstep check: the sequencer's records, in the ARM7's layout --
 * player n's 0x24 bytes (the ARM7's at 0x03808258 + 0x24 * n) and the
 * first 0x3c bytes of track n (0x03808498 + 0x40 * n; the last word, its
 * notes, is the ARM7's pointer). */
#define KHDAYS_SND7_PLAYERS         0x03808258u
#define KHDAYS_SND7_PLAYER_SIZE     0x24u
#define KHDAYS_SND7_TRACKS          0x03808498u
#define KHDAYS_SND7_TRACK_SIZE      0x40u
#define KHDAYS_SND7_TRACK_COMPARED  0x3cu
const uint8_t *khdays_snd7_native_player_record(int n);
const uint8_t *khdays_snd7_native_track_record(int n);

/* This ROM's ARM7 binary (loaded at 0x02380000): its first autoload
 * segment, which holds the driver, goes to 0x037f8000 (crt0's module
 * parameters at +0x1d0 list it), BSS after it. Every ARM7 address the
 * drivers name is in it; the hash makes sure it is this one. */
#define KHDAYS_SND7_ARM7_RAM        0x02380000u
#define KHDAYS_SND7_SEGMENT_OFFSET  0x01e8u  /* in the ARM7 binary */
#define KHDAYS_SND7_SEGMENT_ADDRESS 0x037f8000u
#define KHDAYS_SND7_SEGMENT_SIZE    0xf4b0u
#define KHDAYS_SND7_SEGMENT_BSS     0x3e64u
#define KHDAYS_SND7_SEGMENT_FNV1A   0x9f4bf423u

/* The segment as the ROM has it (KHDAYS_SND7_SEGMENT_SIZE bytes, checked),
 * for the tables and the initialised data in it; the runtime reads it. */
void khdays_snd7_native_set_segment(const uint8_t *segment);

/* The BIOS's pitch table (GetPitchTable, SWI 1Bh), index < 768; both
 * drivers use it (snd7_tables.c). */
uint16_t khdays_snd7_pitch_table(int index);

/* Where the ARM9 keeps its copy of the BIOS's volume table (724 bytes). */
#define KHDAYS_SND7_ARM9_VOLUME_TABLE 0x02041588u

#endif
