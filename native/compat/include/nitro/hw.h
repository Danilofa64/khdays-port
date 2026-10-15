/* The decompilation's nitro/hw.h, with the math unit's registers (the
 * divider and the square root) reached without the trap's fault: through
 * the register model's own view, the results brought up to date when read
 * (runtime/core/io_regs.c, khdays_io_cp). Every other definition is the decomp's. */
#ifndef KHDAYS_COMPAT_NITRO_HW_H
#define KHDAYS_COMPAT_NITRO_HW_H

#include "../../../../external/khdays-decomp/include/nitro/hw.h"

#undef REG_DIVCNT
#undef REG_DIV_NUMER
#undef REG_DIV_DENOM
#undef REG_DIV_RESULT
#undef REG_SQRTCNT
#undef REG_SQRT_RESULT
#undef REG_SQRT_PARAM
#define REG_DIVCNT      (*(vu16 *)khdays_io_cp(0x280, 2))
#define REG_DIV_NUMER   (*(vu64 *)khdays_io_cp(0x290, 8))
#define REG_DIV_DENOM   (*(vs64 *)khdays_io_cp(0x298, 8))
#define REG_DIV_RESULT  (*(vs64 *)khdays_io_cp(0x2a0, 8))
#define REG_SQRTCNT     (*(vu16 *)khdays_io_cp(0x2b0, 2))
#define REG_SQRT_RESULT (*(vu32 *)khdays_io_cp(0x2b4, 4))
#define REG_SQRT_PARAM  (*(u64 *)khdays_io_cp(0x2b8, 8))

/* EXMEMCNT holds a value (the card's and the GBA slot's bus owner): plain.
 * The shared macros of the registers that act (REG_IF,
 * REG_CARD_CONTROL, the timer 0 pair) stay the decomp's: prepare.py rewrites
 * their uses (native/tools/registers.py). */
#undef REG_EXMEM_CNT
#define REG_EXMEM_CNT   (*(volatile u16 *)khdays_io_plain(0x204))

#endif
