/* The ARM7's services (arm7.c). The DS's ARM7 runs Nintendo's system component (sound, touch, clock,
 * power, backup memory); here each service the ARM9 asks for over PXI is answered, the sound by the
 * ARM7's own driver in C (port/sound/snd7_*.c). */
#ifndef KHDAYS_ARM7_H
#define KHDAYS_ARM7_H

#include "../../hal/hal.h"

void khdays_arm7_reset(void);

/* A word the SDK sent (pxi.c): to the service for its tag. */
void khdays_arm7_receive(u32 word);

/* A word from the ARM7 on `tag`: queued for the SDK (pxi.c). */
void khdays_arm7_reply(u32 tag, u32 data, u32 err);

/* The end of a save action of the game's (the SDK's backup stream command,
 * prepare.py HOOKS): the chip, if changed, written to saves/khdays.sav whole
 * and atomically. 1 if nothing changed or it was written; 0 if it could not
 * be (the file as it was). */
int khdays_backup_commit(void);

/* Once a frame (VBlank): what the ARM7 does periodically. */
void khdays_arm7_frame(void);

#endif
