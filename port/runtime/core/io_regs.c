/* What the ARM9's I/O registers do (io.h). A register not handled here is plain storage, which is
 * right for the display and control registers. A status register that should change by itself and is
 * not modelled keeps its last value, so a game polling it stalls visibly instead of reading an
 * invented value. */
#include "io.h"
#include "window.h"
#include "arm7.h"
#include "card.h"
#include "clock.h"
#include "events.h"
#include "runtime.h"
#include "../input/input.h"
#include "../../gfx/g3d.h"
#include "origin.h"
#include "vram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* The address a function returns to: its caller's instruction. */
#if defined(_MSC_VER)
#include <intrin.h>
#define KHDAYS_CALLER() ((khdays_uptr)_ReturnAddress())
#else
#define KHDAYS_CALLER() ((khdays_uptr)__builtin_return_address(0))
#endif

static void unimplemented(const char *what, u32 address)
{
    fprintf(stderr, "io: %s (0x%08x) is not modelled\n", what, address);
    fflush(stderr);
    exit(7);
}

static int overlaps(u32 offset, int size, u32 reg, u32 reg_size)
{
    return offset < reg + reg_size && reg < offset + (u32)size;
}

/* --- Interrupts ------------------------------------------------------------ */

void khdays_io_request_irq(u32 bits)
{
    IO32(0x214) |= bits;
}

/* --- The timers and the display line --------------------------------------- */

/* Neither the DS's timers nor its display scan are modelled: the SDK's tick and alarms run on the
 * PC's clock (hal/sdk_time.c), and the display line's readers are answered by prepare.py's hooks. An
 * access left is reported once, with the game's instruction, and reads 0. */
static void said_once(const char *what, u32 address)
{
    static khdays_uptr *seen;
    static int count, room;
    for (int i = 0; i < count; ++i) {
        if (seen[i] == khdays_io_caller) {
            return;
        }
    }
    if (count == room) {
        const int grown_room = room ? room * 2 : 16;
        khdays_uptr *grown = (khdays_uptr *)realloc(seen, sizeof(khdays_uptr) * (size_t)grown_room);
        if (grown != NULL) {
            seen = grown;
            room = grown_room;
        }
    }
    if (count < room) {
        seen[count++] = khdays_io_caller;
    }
    fprintf(stderr, "io: %s 0x%08x by %s (%p): no model of it, 0 read\n", what, address,
            khdays_diag_function_at((const void *)khdays_io_caller), (const void *)khdays_io_caller);
}

/* --- Divider and square root ---------------------------------------------- */

/* Result words whose value GBATEK does not pin down for the operands as they stand (bits 0-3:
 * DIV_RESULT and DIVREM_RESULT, low and high). Only reading one is an error: the game writes the
 * operands a word at a time. */
static u32 division_unmodelled;

/* GBATEK, "DS Maths": DIV0 (bit 14) tests the whole 64-bit DENOM. Dividing by zero gives REMAIN =
 * NUMER and RESULT = +1 or -1, opposite to NUMER's sign (in 32/32 mode its upper half inverted).
 * -80000000h / -1 in 32-bit mode gives -MAX. */
static void divide(void)
{
    const u32 mode = IO16(0x280) & 3;
    const s64 numer64 = (s64)((u64)IO32(0x290) | (u64)IO32(0x294) << 32);
    const s64 denom64 = (s64)((u64)IO32(0x298) | (u64)IO32(0x29c) << 32);
    s64 numer, denom;
    u64 quotient, remainder;
    if (denom64 == 0) {
        IO16(0x280) |= 0x4000;
    } else {
        IO16(0x280) &= ~0x4000;
    }
    if (mode == 0) {
        numer = (s32)IO32(0x290);
        denom = (s32)IO32(0x298);
    } else if (mode == 2) {
        numer = numer64;
        denom = denom64;
    } else {
        numer = numer64;
        denom = (s32)IO32(0x298);
    }
    division_unmodelled = 0;
    if (denom == 0) {
        quotient = (u64)(numer < 0 ? 1 : -1);
        remainder = (u64)numer;
        if (mode == 0) {
            quotient ^= 0xffffffff00000000ull;
            division_unmodelled = 1u << 3; /* the remainder's upper half */
        }
    } else if (mode == 0 && numer == -0x80000000ll && denom == -1) {
        quotient = 0x80000000u; /* -MAX, as its low word */
        remainder = 0;
        division_unmodelled = (1u << 1) | (1u << 3);
    } else if (numer == (s64)0x8000000000000000ull && denom == -1) {
        quotient = remainder = 0;
        division_unmodelled = 0xf; /* the 64-bit overflow is not documented */
    } else {
        quotient = (u64)(numer / denom);
        remainder = (u64)(numer % denom);
    }
    IO32(0x2a0) = (u32)quotient;
    IO32(0x2a4) = (u32)(quotient >> 32);
    IO32(0x2a8) = (u32)remainder;
    IO32(0x2ac) = (u32)(remainder >> 32);
}

static void square_root(void)
{
    u64 value = (u64)IO32(0x2b8);
    u64 root = 0, bit = 1ull << 62;
    if (IO16(0x2b0) & 1) {
        value |= (u64)IO32(0x2bc) << 32;
    }
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    IO32(0x2b4) = (u32)root;
}

/* --- DMA ------------------------------------------------------------------- */

static int is_io_address(u32 address)
{
    return (address >= KHDAYS_IO_BASE && address < KHDAYS_IO_BASE + KHDAYS_IO_SIZE) ||
           (address >= KHDAYS_IO2_BASE && address < KHDAYS_IO2_BASE + KHDAYS_IO2_SIZE);
}

static u8 *host_address(u32 address)
{
    if (address >= KHDAYS_IO2_BASE) {
        return khdays_io2_host + (address - KHDAYS_IO2_BASE);
    }
    return khdays_io_host + (address - KHDAYS_IO_BASE);
}

int khdays_io_is_register(u32 address)
{
    return is_io_address(address);
}

khdays_uptr khdays_io_caller;

u32 khdays_io_load(u32 address, int size)
{
    u32 value = 0;
    khdays_io_caller = KHDAYS_CALLER();
    khdays_io_read(address, size);
    memcpy(&value, host_address(address), (size_t)size);
    return value;
}

/* The math unit computes when its results are read, from the operands as they stand (they arrive as
 * plain stores, khdays_io_cp). The values are the finished ones; the busy bits are never set. */
static void math_results(u32 offset, int size)
{
    if (overlaps(offset, size, 0x280, 0x10) || overlaps(offset, size, 0x2a0, 0x10)) {
        divide();
    }
    if (overlaps(offset, size, 0x2b0, 8)) {
        square_root();
    }
    for (int word = 0; word < 4; ++word) {
        if ((division_unmodelled & (1u << word)) && overlaps(offset, size, 0x2a0 + 4 * word, 4)) {
            unimplemented("a division result word GBATEK leaves undefined", KHDAYS_IO_BASE + offset);
        }
    }
}

void *khdays_io_cp(u32 offset, int size)
{
    math_results(offset, size);
    return khdays_io_host + offset;
}

void khdays_io_store(u32 address, int size, u32 value)
{
    u8 before[4];
    u8 *host = host_address(address);
    khdays_io_caller = KHDAYS_CALLER();
    memcpy(before, host, (size_t)size);
    memcpy(host, &value, (size_t)size);
    khdays_io_write(address, size, before);
}

/* Where a DMA access lands: the system bus does not see the TCMs, so below main RAM there is nothing
 * (0) and the DTCM's address reaches the main RAM mirror underneath. */
static u32 dma_bus(u32 address)
{
    if (address < 0x02000000u) {
        return 0;
    }
    if (address >= 0x027e0000u && address < 0x027e4000u) {
        return address - 0x00400000u;
    }
    return address;
}

/* One DMA unit, with the register semantics when either side is I/O. */
static void dma_unit(u32 source, u32 destination, int size)
{
    u8 value[4];
    source = dma_bus(source);
    destination = dma_bus(destination);
    if (destination == 0) {
        return; /* a write to nothing */
    }
    if (source == 0) {
        /* Reading where the bus has nothing (a null source for an empty player slot's gauge): GBATEK does
         * not say what that reads, so the destination is left as it is; the first one is reported. */
        static int reported;
        if (!reported) {
            reported = 1;
            fprintf(stderr,
                    "io: a DMA read from outside the bus into 0x%08x; its value is not "
                    "known, the destination is left unchanged\n",
                    destination);
        }
        return;
    }
    if (is_io_address(source)) {
        khdays_io_read(source, size);
        memcpy(value, host_address(source), size);
    } else {
        memcpy(value, KHDAYS_W(source), size);
    }
    if (is_io_address(destination)) {
        u8 before[4];
        memcpy(before, host_address(destination), size);
        memcpy(host_address(destination), value, size);
        khdays_io_write(destination, size, before);
    } else {
        memcpy(KHDAYS_W(destination), value, size);
    }
}

/* Each channel's address counters: loaded from SAD/DAD when it is enabled, then advanced by its
 * transfers (a repeating channel keeps them, reloading DAD only in increment/reload mode). */
static struct {
    u32 source;
    u32 destination;
} dma[4];

/* One burst: the CNT word count of units. */
void khdays_dma_run(int channel)
{
    const u32 base = 0x0b0 + 12 * channel;
    const u32 control = IO32(base + 8);
    const int size = (control & (1u << 26)) ? 4 : 2;
    const u32 dst_mode = (control >> 21) & 3;
    const u32 src_mode = (control >> 23) & 3;
    u32 count = control & 0x1fffff;
    if (count == 0) {
        count = 0x200000;
    }
    if (dst_mode == 0 || dst_mode == 3) {
        /* where the bytes came from (origin.h): a copy, or a fill */
        if (src_mode == 0) {
            khdays_origin_copy(dma[channel].source, dma[channel].destination, count * (u32)size);
        } else if (src_mode == 2) {
            khdays_origin_clear(dma[channel].destination, count * (u32)size);
        }
    }
    for (u32 i = 0; i < count; ++i) {
        dma_unit(dma[channel].source & ~(u32)(size - 1), dma[channel].destination & ~(u32)(size - 1), size);
        if (src_mode == 0)
            dma[channel].source += size;
        else if (src_mode == 1)
            dma[channel].source -= size;
        if (dst_mode == 0 || dst_mode == 3)
            dma[channel].destination += size;
        else if (dst_mode == 1)
            dma[channel].destination -= size;
    }
    if (control & (1u << 25)) {
        if (dst_mode == 3) {
            dma[channel].destination = IO32(base + 4);
        }
    } else {
        IO32(base + 8) = control & ~0x80000000u;
    }
    if (control & (1u << 30)) {
        khdays_io_request_irq(1u << (8 + channel));
    }
}

/* A channel armed for the card: a burst per data word the card has ready. */
int khdays_dma_card_channel(void)
{
    for (int channel = 0; channel < 4; ++channel) {
        const u32 control = IO32(0x0b8 + 12 * channel);
        if ((control & 0x80000000u) && ((control >> 27) & 7) == 5) {
            return channel;
        }
    }
    return -1;
}

static void dma_control_written(int channel, int was_enabled)
{
    const u32 control = IO32(0x0b8 + 12 * channel);
    if (!(control & 0x80000000u) || was_enabled) {
        return;
    }
    dma[channel].source = IO32(0x0b0 + 12 * channel);
    dma[channel].destination = IO32(0x0b4 + 12 * channel);
    switch ((control >> 27) & 7) {
    case 0: /* immediately */
    case 7: /* geometry FIFO: it drains as fast as it is fed natively */
        if (control & (1u << 25)) {
            unimplemented("a repeating DMA started immediately", 0x040000b8 + 12 * channel);
        }
        khdays_dma_run(channel);
        break;
    case 1: /* at the next VBlank */ khdays_events_dma_at_vblank(channel); break;
    case 5: /* when the card has a word: card.c runs it */ break;
    default: unimplemented("DMA start mode", 0x040000b8 + 12 * channel);
    }
}

/* --- Reset, reads and writes ----------------------------------------------- */

void khdays_io_reset(void)
{
    memset(khdays_io_host, 0, KHDAYS_IO_SIZE);
    memset(khdays_io2_host, 0, KHDAYS_IO2_SIZE);
    IO16(0x130) = 0x03ff; /* KEYINPUT: active low, nothing held */
    IO8(0x300) = 0x01;    /* POSTFLG: the firmware finished booting */
    khdays_vram_reset();
    khdays_g3d_reset();
    khdays_arm7_reset();
}

void khdays_io_read(u32 address, int size)
{
    u32 offset;
    if (address >= KHDAYS_IO2_BASE) {
        offset = address - KHDAYS_IO2_BASE;
        if (overlaps(offset, size, 0x000, 4)) { /* IPCFIFORECV: the SDK's PXI is the port's (pxi.h) */
            said_once("the IPC receive FIFO read at", 0x04100000);
            *(u32 *)khdays_io2_host = 0;
        }
        if (overlaps(offset, size, 0x010, 4)) { /* the card's data port */
            *(u32 *)(khdays_io2_host + 0x010) = khdays_card_read_word();
        }
        return;
    }
    offset = address - KHDAYS_IO_BASE;
    if (khdays_timing_on()) {
        if (overlaps(offset, size, 0x004, 2)) {
            khdays_timing_access(KHDAYS_TIMING_DISPSTAT_READ, khdays_io_caller);
        }
        if (overlaps(offset, size, 0x006, 2)) {
            khdays_timing_access(KHDAYS_TIMING_VCOUNT_READ, khdays_io_caller);
        }
        if (overlaps(offset, size, 0x100, 16)) {
            khdays_timing_access(KHDAYS_TIMING_TIMER_READ, khdays_io_caller);
        }
    }
    khdays_events_update();
    if (overlaps(offset, size, 0x004, 2)) {
        /* bit 0, in VBlank, from the frame loop; no V-count match (the game sets no V-count interrupt) */
        IO16(0x004) = (u16)((IO16(0x004) & ~7u) | (khdays_events_in_vblank_period() ? 1 : 0));
    }
    if (overlaps(offset, size, 0x006, 2)) {
        said_once("VCOUNT read at", 0x04000006);
        IO16(0x006) = 0;
    }
    for (int n = 0; n < 4; ++n) {
        if (overlaps(offset, size, 0x100 + 4 * n, 2)) {
            said_once("a timer's count read at", 0x04000100 + 4 * n);
            IO16(0x100 + 4 * n) = 0;
        }
    }
    math_results(offset, size);
    if (overlaps(offset, size, 0x130, 2)) {
        IO16(0x130) = khdays_input_keyinput();
    }
    for (u32 reg = 0x600; reg < 0x6a4; reg += 4) { /* the port's geometry's status and results */
        if (overlaps(offset, size, reg, 4)) {
            IO32(reg) = khdays_g3d_read(reg);
        }
    }
    /* IPCSYNC, IPCFIFOCNT: not modelled, the SDK's PXI is the port's (pxi.h); an access left is reported
     * once and reads 0 */
    if (overlaps(offset, size, 0x180, 2)) {
        said_once("IPCSYNC read at", 0x04000180);
        IO16(0x180) = 0;
    }
    if (overlaps(offset, size, 0x184, 2)) {
        said_once("IPCFIFOCNT read at", 0x04000184);
        IO16(0x184) = 0;
    }
}

void khdays_io_write(u32 address, int size, const u8 *before)
{
    u32 offset;
    if (address >= KHDAYS_IO2_BASE) {
        unimplemented("write to the IPC/card port region", address);
    }
    offset = address - KHDAYS_IO_BASE;
    if (khdays_timing_on() && overlaps(offset, size, 0x100, 16)) {
        khdays_timing_access(KHDAYS_TIMING_TIMER_WRITE, khdays_io_caller);
    }
#define BEFORE8(reg) (before[(reg) - offset])
    if (overlaps(offset, size, 0x004, 2)) { /* DISPSTAT: bits 0-2 are status */
        const u32 lo = offset <= 0x004 ? BEFORE8(0x004) : IO8(0x004);
        IO8(0x004) = (u8)((IO8(0x004) & ~7u) | (lo & 7u));
    }
    if (overlaps(offset, size, 0x006, 2)) {
        unimplemented("writing VCOUNT", 0x04000006);
    }
    for (int n = 0; n < 4; ++n) {
        const u32 high = 0x0b8 + 12 * n + 3; /* the byte holding the enable bit */
        if (overlaps(offset, size, 0x0b8 + 12 * n + 2, 2)) {
            const int was_enabled = overlaps(offset, size, high, 1) ? (BEFORE8(high) & 0x80) != 0 : 1;
            dma_control_written(n, was_enabled);
        }
    }
    if (overlaps(offset, size, 0x100, 16)) {
        said_once("a timer register written at", 0x04000100 + ((offset - 0x100) & ~1u));
    }
    if (overlaps(offset, size, 0x180, 2)) { /* IPCSYNC, IPCFIFOCNT, IPCFIFOSEND: as for the reads */
        said_once("IPCSYNC written at", 0x04000180);
    }
    if (overlaps(offset, size, 0x184, 2)) {
        said_once("IPCFIFOCNT written at", 0x04000184);
    }
    if (overlaps(offset, size, 0x188, 4)) {
        said_once("IPCFIFOSEND written at", 0x04000188);
    }
    for (u32 reg = 0x214; reg < 0x218; ++reg) { /* IF: writing 1 clears */
        if (overlaps(offset, size, reg, 1)) {
            IO8(reg) = (u8)(BEFORE8(reg) & ~IO8(reg));
        }
    }
    if (overlaps(offset, size, 0x1a2, 2)) {
        unimplemented("the card's SPI (backup) port", 0x040001a2);
    }
    if (overlaps(offset, size, 0x1a7, 1) && (IO32(0x1a4) & 0x80000000u)) {
        khdays_card_start(); /* ROMCTRL written with its start bit */
    }
    if (overlaps(offset, size, 0x240, 7) || overlaps(offset, size, 0x248, 2)) {
        khdays_vram_control_written(); /* VRAMCNT_A-G, H, I (0x247 is WRAMCNT) */
    }
    if (overlaps(offset, size, 0x400, 0x200)) { /* GXFIFO and the geometry command ports: the port's geometry */
        if (size != 4 || (offset & 3)) {
            unimplemented("a geometry command write that is not a word", address);
        }
        if (offset < 0x440) {
            const u32 word = IO32(offset);
            khdays_g3d_words(&word, 1);
        } else {
            khdays_g3d_port(offset, IO32(offset));
        }
    }
    if (overlaps(offset, size, 0x600, 4)) {
        khdays_g3d_status_written(IO32(0x600));
    }
    if (overlaps(offset, size, 0x280, 0x20)) {
        divide();
    }
    if (overlaps(offset, size, 0x2b0, 2) || overlaps(offset, size, 0x2b8, 8)) {
        square_root();
    }
#undef BEFORE8
}
