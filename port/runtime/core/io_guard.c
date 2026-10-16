/* A guard at the DS's I/O addresses. prepare.py (with registers.py) rewrites every register access of
 * the game's C to reach the port, so the registers' DS addresses, in the window and out of it, stay
 * inaccessible: an access there is one the rewrite missed. The game stops, naming the register and
 * the instruction (exit code 4). Any other DS address outside the window stops it too (exit code 6):
 * an access of the port's side not routed through KHDAYS_W. */
#include "../../hal/hal.h"
#include "io.h"
#include "runtime.h"
#include "session_log.h"
#include "window.h"

#include <stdio.h>
#include <windows.h>

static int is_io(u32 address)
{
    return (address >= KHDAYS_IO_BASE && address < KHDAYS_IO_BASE + KHDAYS_IO_SIZE) ||
           (address >= KHDAYS_IO2_BASE && address < KHDAYS_IO2_BASE + KHDAYS_IO2_SIZE);
}

/* The DS's names for its registers (GBATEK), for the message. */
static const char *register_name(u32 address)
{
    static const struct {
        u32 from, to;
        const char *name;
    } names[] = {
        {0x04000000, 0x04000004, "DISPCNT"}, {0x04000004, 0x04000006, "DISPSTAT"},
        {0x04000006, 0x04000008, "VCOUNT"}, {0x04000008, 0x04000010, "BGxCNT"},
        {0x04000010, 0x04000020, "BGxHOFS/VOFS"}, {0x04000020, 0x04000040, "BG2/3 affine"},
        {0x04000040, 0x0400004c, "the windows"}, {0x0400004c, 0x04000050, "MOSAIC"},
        {0x04000050, 0x04000056, "the blending"}, {0x04000060, 0x04000062, "DISP3DCNT"},
        {0x04000064, 0x04000068, "DISPCAPCNT"}, {0x04000068, 0x0400006c, "DISP_MMEM_FIFO"},
        {0x0400006c, 0x0400006e, "MASTER_BRIGHT"}, {0x040000b0, 0x040000e0, "DMA0-3"},
        {0x040000e0, 0x040000f0, "DMA fill"}, {0x04000100, 0x04000110, "TMxCNT"},
        {0x04000130, 0x04000132, "KEYINPUT"}, {0x04000132, 0x04000134, "KEYCNT"},
        {0x04000180, 0x04000184, "IPCSYNC"}, {0x04000184, 0x04000188, "IPCFIFOCNT"},
        {0x04000188, 0x0400018c, "IPCFIFOSEND"}, {0x040001a0, 0x040001a4, "AUXSPICNT/DATA"},
        {0x040001a4, 0x040001a8, "ROMCTRL"}, {0x040001a8, 0x040001b0, "the card's command"},
        {0x04000204, 0x04000206, "EXMEMCNT"}, {0x04000208, 0x0400020c, "IME"},
        {0x04000210, 0x04000214, "IE"}, {0x04000214, 0x04000218, "IF"},
        {0x04000240, 0x0400024a, "VRAMCNT/WRAMCNT"}, {0x04000280, 0x040002c0, "the math unit"},
        {0x04000300, 0x04000302, "POSTFLG"}, {0x04000304, 0x04000306, "POWCNT1"},
        {0x04000320, 0x040003c0, "the 3D's settings"}, {0x04000400, 0x04000440, "GXFIFO"},
        {0x04000440, 0x04000600, "the geometry's command ports"}, {0x04000600, 0x04000604, "GXSTAT"},
        {0x04000604, 0x04000620, "the geometry's counts"}, {0x04000620, 0x040006a4, "the geometry's results"},
        {0x04001000, 0x04001070, "engine B's 2D"}, {0x04100000, 0x04100004, "IPCFIFORECV"},
        {0x04100010, 0x04100014, "the card's data port"},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (address >= names[i].from && address < names[i].to) {
            return names[i].name;
        }
    }
    return "not named";
}

/* An address of the DS's own ranges (memory.c's map), outside the window. */
static int at_ds_address(const unsigned char *host)
{
    return khdays_window != NULL && (size_t)host >= 0x01ff8000u && (size_t)host < 0x0b000000u &&
           !(host >= khdays_window && (size_t)(host - khdays_window) <= 0xffffffffu);
}

static LONG CALLBACK io_guard(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    const CONTEXT *context = info->ContextRecord;
    u32 address;
    const void *site;
    const char *access, *function;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->ExceptionInformation[0] == 8) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    site = (const void *)context->Rip;
    access = record->ExceptionInformation[0] == 1 ? "writes" : "reads";
    {
        const unsigned char *host = (const unsigned char *)record->ExceptionInformation[1];
        /* in the window, judged by the DS address alone: the host address's low bits may look like a
         * register's anywhere in the heaps, whose faults the memory map commits */
        const int in_window =
            khdays_window != NULL && host >= khdays_window && (size_t)(host - khdays_window) <= 0xffffffffu;
        address = in_window ? (u32)(host - khdays_window) : (u32)(size_t)host;
        if (!is_io(address) && (in_window || !at_ds_address(host))) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        function = khdays_diag_function_at(site);  /* only for a stop: the names are slow */
        if (!is_io(address)) {
            fprintf(stderr,
                    "khdays-native: %s (%p) %s 0x%08x at its DS address, outside the game's memory (at %p): "
                    "stopped\n",
                    function, site, access, address, (void *)khdays_window);
            khdays_log("stop: %s (%p) %s 0x%08x at its DS address, outside the game's memory (at %p)", function,
                       site, access, address, (void *)khdays_window);
            khdays_log_close();
            fflush(stderr);
            ExitProcess(6);
        }
    }
    fprintf(stderr,
            "khdays-native: %s (%p) %s the register at 0x%08x (%s), which the port does not reach: "
            "stopped\n",
            function, site, access, address, register_name(address));
    khdays_log("stop: %s (%p) %s the register at 0x%08x (%s), which the port does not reach", function, site,
               access, address, register_name(address));
    khdays_log_close();
    fflush(stderr);
    ExitProcess(4);
}

void khdays_io_guard_init(void)
{
    AddVectoredExceptionHandler(1, io_guard);
}
