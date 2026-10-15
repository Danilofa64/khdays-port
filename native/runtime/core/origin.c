/* Where the game's data in memory came from (origin.h): one sorted map of spans, each saying that the
 * bytes from `start` to `end` hold a file's data from `offset` on. Keys: main RAM by its address (the
 * mirrors folded in), the game's heaps by theirs, each VRAM bank by VRAM_KEY + bank * BANK_STRIDE +
 * offset (a bank's contents stay when the game maps it elsewhere). */
#include "origin.h"
#include "window.h"
#include "game_heap.h"
#include "rom.h"
#include "runtime.h"
#include "../mods/textures.h"
#include "vram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_KEY     0x02000000u
#define RAM_SIZE    0x00400000u
#define VRAM_KEY    0x10000000u
#define BANK_STRIDE 0x00100000u

#define VRAM_KEYS_END (VRAM_KEY + KHDAYS_VRAM_BANKS * BANK_STRIDE)

_Static_assert(VRAM_KEYS_END <= KHDAYS_HEAPS_BASE, "the VRAM keys reach the heaps'");

/* A VRAM bank's key, not memory the CPU sees (the game's heaps have keys above them). */
static int vram_key(u32 key)
{
    return key >= VRAM_KEY && key < VRAM_KEYS_END;
}

typedef struct Span {
    u32 start, end;  /* keys */
    u32 file;        /* into names */
    u32 offset;      /* into the file's data, at `start` */
} Span;

static Span *spans;
static int nspans, cap_spans;

static char (*names)[128];
static u32 nnames, cap_names;

static int trace = -1;
static int wanted; /* khdays_origin_want */
static u32 bank_changes[KHDAYS_VRAM_BANKS];

/* Asked at every copy: whether the names are wanted can change with the
 * settings (data already in memory then has none until it is loaded again). */
int khdays_origin_on(void)
{
    if (trace < 0) {
        const char *v = getenv("KHDAYS_TRACE_ORIGIN");
        trace = v != NULL && *v != '\0' && *v != '0';
    }
    return trace || wanted || khdays_textures_wanted();
}

void khdays_origin_want(void)
{
    wanted = 1;
}

u32 khdays_origin_bank_changes(int bank)
{
    return bank_changes[bank];
}

/* --- Keys --------------------------------------------------------------------- */

/* The key of `address` and how many bytes from it stay in the same memory;
 * 0 if it is neither main RAM nor a bank the CPU sees. */
static u32 key_of(u32 address, u32 *room)
{
    if (address >= KHDAYS_HEADERS_BASE && address < 0x03000000u && !khdays_game_heap_ds_memory()) {
        *room = 0x03000000u - address;  /* the headers' heaps, not a mirror */
        return address;
    }
    if (address >= 0x02000000u && address < 0x03000000u &&
        !(address >= 0x027e0000u && address < 0x027e4000u)) {  /* not the DTCM */
        const u32 at = address & (RAM_SIZE - 1);
        *room = RAM_SIZE - at;
        return RAM_KEY + at;
    }
    if (address >= KHDAYS_HEAPS_BASE && address < KHDAYS_HEAPS_END) {  /* above every VRAM key */
        *room = KHDAYS_HEAPS_END - address;
        return address;
    }
    if (address >= 0x06000000u && address < 0x068a4000u) {
        const u8 *host = (const u8 *)KHDAYS_W(address);
        for (int bank = 0; bank < KHDAYS_VRAM_BANKS; ++bank) {
            const u8 *base = khdays_vram_bank(bank);  /* where it lives (vram.c) */
            if (host >= base && (u32)(host - base) < khdays_vram_bank_size[bank]) {
                *room = khdays_vram_bank_size[bank] - (u32)(host - base);
                return VRAM_KEY + (u32)bank * BANK_STRIDE + (u32)(host - base);
            }
        }
    }
    return 0;
}

/* --- The map ---------------------------------------------------------------- */

/* The first span ending after `key`. */
static int first_after(u32 key)
{
    int lo = 0, hi = nspans;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (spans[mid].end <= key) lo = mid + 1; else hi = mid;
    }
    return lo;
}

static int room_for(int more)
{
    if (nspans + more > cap_spans) {
        const int n = cap_spans ? cap_spans * 2 + more : 4096 + more;
        Span *grown = (Span *)realloc(spans, sizeof(Span) * (size_t)n);
        if (grown == NULL) {
            return 0;
        }
        spans = grown;
        cap_spans = n;
    }
    return 1;
}

/* Nothing known of [start, end) any more: the spans there are cut away. */
static void clear(u32 start, u32 end)
{
    int i = first_after(start);
    if (vram_key(start)) {
        ++bank_changes[(start - VRAM_KEY) / BANK_STRIDE % KHDAYS_VRAM_BANKS];
    }
    while (i < nspans && spans[i].start < end) {
        Span *s = &spans[i];
        if (s->start < start && s->end > end) {
            /* the cleared range is inside: split in two */
            if (!room_for(1)) return;
            s = &spans[i];
            memmove(&spans[i + 2], &spans[i + 1], sizeof(Span) * (size_t)(nspans - i - 1));
            spans[i + 1] = *s;
            spans[i + 1].offset += end - s->start;
            spans[i + 1].start = end;
            s->end = start;
            ++nspans;
            return;
        }
        if (s->start < start) {
            s->end = start;
            ++i;
        } else if (s->end > end) {
            s->offset += end - s->start;
            s->start = end;
            ++i;
        } else {
            memmove(&spans[i], &spans[i + 1], sizeof(Span) * (size_t)(nspans - i - 1));
            --nspans;
        }
    }
}

static void put(u32 start, u32 end, u32 file, u32 offset)
{
    int i;
    clear(start, end);
    if (!room_for(1)) return;
    i = first_after(start);
    memmove(&spans[i + 1], &spans[i], sizeof(Span) * (size_t)(nspans - i));
    spans[i].start = start;
    spans[i].end = end;
    spans[i].file = file;
    spans[i].offset = offset;
    ++nspans;
}

static u32 name_index(const char *name)
{
    for (u32 i = 0; i < nnames; ++i) {
        if (strcmp(names[i], name) == 0) return i;
    }
    if (nnames == cap_names) {
        const u32 n = cap_names ? cap_names * 2 : 256;
        char (*grown)[128] = (char (*)[128])realloc(names, sizeof(names[0]) * n);
        if (grown == NULL) return 0;
        names = grown;
        cap_names = n;
    }
    snprintf(names[nnames], sizeof(names[0]), "%s", name);
    return nnames++;
}

/* --- What the game does -------------------------------------------------------- */

void khdays_origin_loaded(u32 top, u32 bottom, const void *buffer, u32 size, int compressed)
{
    char path[112], name[128];
    u32 within, key, room;
    if (!khdays_origin_on() || buffer == NULL || size == 0) {
        return;
    }
    key = key_of(KHDAYS_REF(buffer), &room);  /* its DS address */
    if (key == 0) {
        return;
    }
    if (!khdays_rom_file_at(top, path, sizeof(path), &within)) {
        snprintf(path, sizeof(path), "rom@%08x", top);
        within = 0;
    }
    if (within != 0) {
        snprintf(name, sizeof(name), "%s@%x", path, within);  /* a part of a file (a packed entry) */
    } else {
        snprintf(name, sizeof(name), "%s", path);
    }
    if (size > room) size = room;
    put(key, key + size, name_index(name), 0);
    if (trace) {
        fprintf(stderr, "origin: %s (%u bytes in the ROM%s) loaded at 0x%08x, %u bytes\n", name, bottom - top,
                compressed ? ", decompressed" : "", KHDAYS_REF(buffer), size);
    }
}

static void say_upload(u32 destination, u32 dkey, u32 n, u32 source)
{
    char chain[480];
    const int bank = (int)((dkey - VRAM_KEY) / BANK_STRIDE);
    const u32 offset = (dkey - VRAM_KEY) % BANK_STRIDE;
    int i = first_after(dkey);
    khdays_diag_callers(chain, sizeof(chain), 3);
    if (i < nspans && spans[i].start <= dkey) {
        fprintf(stderr, "origin: bank %c+0x%05x (at 0x%08x) %u bytes <- %s+0x%x  [%s]\n", 'A' + bank, offset,
                destination, n, names[spans[i].file], spans[i].offset + (dkey - spans[i].start), chain);
    } else {
        /* the first 32 bytes that are not all zero, to look for in the game's files */
        char bytes[3 * 32 + 1] = "";
        u32 room, at = 0;
        if (key_of(source, &room) != 0 && room >= n && n >= 32) {
            const u8 *p = (const u8 *)KHDAYS_W(source);
            for (at = 0; at + 32 <= n; at += 32) {
                int k = 0;
                while (k < 32 && p[at + k] == 0) ++k;
                if (k < 32) break;
            }
            if (at + 32 <= n) {
                for (int k = 0; k < 32; ++k) snprintf(bytes + 2 * k, 3, "%02x", p[at + k]);
            }
        }
        fprintf(stderr, "origin: bank %c+0x%05x (at 0x%08x) %u bytes <- no file (from 0x%08x: +%x %s)  [%s]\n",
                'A' + bank, offset, destination, n, source, at, bytes, chain);
    }
}

void khdays_origin_copy(u32 source, u32 destination, u32 size)
{
    if (!khdays_origin_on()) {
        return;
    }
    while (size != 0) {
        u32 sroom = 0, droom = 0, n;
        const u32 skey = key_of(source, &sroom);
        const u32 dkey = key_of(destination, &droom);
        if (dkey == 0) {
            return;  /* to nothing the map keeps */
        }
        n = size;
        if (n > droom) n = droom;
        if (skey != 0 && n > sroom) n = sroom;
        clear(dkey, dkey + n);
        if (skey != 0) {
            /* the source's spans, moved to the destination */
            int i = first_after(skey);
            while (i < nspans && spans[i].start < skey + n) {
                const u32 a = spans[i].start > skey ? spans[i].start : skey;
                const u32 b = spans[i].end < skey + n ? spans[i].end : skey + n;
                const u32 file = spans[i].file, offset = spans[i].offset + (a - spans[i].start);
                put(dkey + (a - skey), dkey + (b - skey), file, offset);
                if (b >= skey + n) {
                    break;  /* the copy's end: the span may go on past it */
                }
                i = first_after(b);  /* the map moved under us: find the next from b */
            }
        }
        if (trace && vram_key(dkey) && n >= 32) {
            say_upload(destination, dkey, n, source);
        }
        source += n;
        destination += n;
        size -= n;
    }
}

void khdays_origin_clear(u32 destination, u32 size)
{
    if (!khdays_origin_on()) {
        return;
    }
    while (size != 0) {
        u32 room = 0, n;
        const u32 key = key_of(destination, &room);
        if (key == 0) {
            return;
        }
        n = size < room ? size : room;
        clear(key, key + n);
        destination += n;
        size -= n;
    }
}

static int origin_of_key(u32 key, const char **file, u32 *file_offset, u32 *run)
{
    const int i = first_after(key);
    if (i >= nspans || spans[i].start > key) {
        return 0;
    }
    *file = names[spans[i].file];
    *file_offset = spans[i].offset + (key - spans[i].start);
    *run = spans[i].end - key;
    return 1;
}

int khdays_origin_of_bank(int bank, u32 offset, const char **file, u32 *file_offset, u32 *run)
{
    return origin_of_key(VRAM_KEY + (u32)bank * BANK_STRIDE + offset, file, file_offset, run);
}

int khdays_origin_of_ram(u32 address, const char **file, u32 *file_offset, u32 *run)
{
    u32 room;
    const u32 key = key_of(address, &room);
    /* main RAM, the headers' heaps or the game's heaps: any key but a bank's */
    return key != 0 && !vram_key(key) && origin_of_key(key, file, file_offset, run);
}
