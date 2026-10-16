/* The game's file reads answered by the port (files.h). */
#include "files.h"

#include "card.h"
#include "rom.h"
#include "session_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int mode = -1;
static u32 reads, bytes;
static LONGLONG ticks; /* QueryPerformanceCounter's, spent reading */

/* The session log's reads, gathered by file until the next line is due (once a second, or before a
 * stall's line): one line per file, in the order first read. */
static SRWLOCK gathered_lock = SRWLOCK_INIT;
static struct Gathered {
    char name[160];
    u32 start;       /* the file's (or part's) place in the image: the key */
    u32 first;       /* where in it the first read began */
    u32 reads, bytes;
    LONGLONG ticks;
} gathered[48];
static int gathered_count;

static void log_gathered_locked(void)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    for (int n = 0; n < gathered_count; ++n) {
        const struct Gathered *g = &gathered[n];
        khdays_log("read %s from +0x%x: %u bytes in %u reads, %.2f ms", g->name, g->first, g->bytes, g->reads,
                   (double)g->ticks * 1000.0 / (double)f.QuadPart);
    }
    gathered_count = 0;
}

void khdays_files_log_reads(void)
{
    AcquireSRWLockExclusive(&gathered_lock);
    log_gathered_locked();
    ReleaseSRWLockExclusive(&gathered_lock);
}

static void log_read(u32 src, u32 len, LONGLONG spent)
{
    char name[160];
    u32 start = src, end = src + len;
    struct Gathered *g = NULL;
    if (!khdays_rom_area_at(src, name, (int)sizeof(name), &start, &end)) {
        snprintf(name, sizeof(name), "(the image's 0x%08x)", src);
        start = src;
    }
    AcquireSRWLockExclusive(&gathered_lock);
    for (int n = 0; n < gathered_count && g == NULL; ++n) {
        if (gathered[n].start == start) {
            g = &gathered[n];
        }
    }
    if (g == NULL) {
        if (gathered_count == (int)(sizeof(gathered) / sizeof(gathered[0]))) {
            log_gathered_locked();
        }
        g = &gathered[gathered_count++];
        memcpy(g->name, name, sizeof(g->name));
        g->start = start;
        g->first = src - start;
        g->reads = g->bytes = 0;
        g->ticks = 0;
    }
    ++g->reads;
    g->bytes += len;
    g->ticks += spent;
    ReleaseSRWLockExclusive(&gathered_lock);
}

int khdays_files_mode(void)
{
    if (mode < 0) {
        const char *v = getenv("KHDAYS_FILES");
        if (v == NULL || *v == '\0') {
            mode = KHDAYS_FILES_PORT;
        } else if (strcmp(v, "card") == 0) {
            mode = KHDAYS_FILES_CARD;
        } else if (strcmp(v, "check") == 0) {
            mode = KHDAYS_FILES_CHECK;
        } else {
            fprintf(stderr, "files: KHDAYS_FILES=%s is not card or check: the port's reads\n", v);
            mode = KHDAYS_FILES_PORT;
        }
    }
    return mode;
}

void khdays_files_read(u32 src, void *dst, u32 len)
{
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    khdays_rom_read(src, (u8 *)dst, len);
    QueryPerformanceCounter(&t1);
    ticks += t1.QuadPart - t0.QuadPart;
    ++reads;
    bytes += len;
    if (khdays_log_on() && len != 0) { /* the game also asks for 0 bytes at a file's end */
        log_read(src, len, t1.QuadPart - t0.QuadPart);
    }
}

u32 khdays_files_chip_id(void)
{
    return KHDAYS_CARD_CHIP_ID;
}

void khdays_files_card_page(u32 address, u8 *page)
{
    if (address < 0x8000) {
        address = 0x8000 + (address & 0x1ff);
    }
    khdays_files_read(address, page, 0x200);
}

void khdays_files_second(void)
{
    u32 card_commands, card_bytes;
    LARGE_INTEGER f;
    khdays_card_reads(&card_commands, &card_bytes);
    if (reads == 0 && card_commands == 0) {
        return;
    }
    QueryPerformanceFrequency(&f);
    fprintf(stderr, "files: %u reads, %u bytes, %.1f ms; card: %u commands, %u bytes, in the last second\n", reads, bytes,
            (double)ticks * 1000.0 / (double)f.QuadPart, card_commands, card_bytes);
    reads = bytes = 0;
    ticks = 0;
}
