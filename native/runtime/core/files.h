/* The game's file reads answered by the port: every ROM read reaches the SDK's CARDi_ReadRom, which
 * copies the bytes from the ROM image (rom.h) straight to the destination instead of the emulated
 * card's commands and data words; the SDK's own locking, threads and completion stay (prepare.py
 * HOOKS). The card stays as the reference. */
#ifndef KHDAYS_FILES_H
#define KHDAYS_FILES_H

#include "../../hal/hal.h"

/* KHDAYS_FILES: unset, the port's reads; "card", the emulated card; "check", the card, each read
 * compared with the port's when it completes. */
enum { KHDAYS_FILES_PORT, KHDAYS_FILES_CARD, KHDAYS_FILES_CHECK };
int khdays_files_mode(void);

/* `len` bytes of the ROM image at `src` into `dst` (the game's memory),
 * counted for the profile. */
void khdays_files_read(u32 src, void *dst, u32 len);

/* The card's chip ID, as the card answers it (card.h KHDAYS_CARD_CHIP_ID,
 * the value boot.c records): CARDi_ReadRomIDCore with the port's reads. */
u32 khdays_files_chip_id(void);

/* What a cartridge answers to a page read (0xb7) at `address`, 0x200-aligned: the image's 0x200
 * bytes; below 0x8000 from 0x8000 + (address & 0x1ff), as card.c does (DS Protect's flashcart check
 * reads exactly that). */
void khdays_files_card_page(u32 address, u8 *page);

/* The session log: the reads gathered since the last time, one line per
 * file, written now -- before a stall's line, and once a second. */
void khdays_files_log_reads(void);

/* KHDAYS_PROFILE, once a second (events.c): the `files:` line -- the reads the port answered, their
 * bytes and time, and what still took the card. */
void khdays_files_second(void);

#endif
