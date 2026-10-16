/* The decompilation's nitro/mi.h, with the DMA's waits reaching the
 * channel's control register through the register model (khdays_io_load:
 * the port's DMA has finished when the game asks, runtime/core/io_regs.c) rather
 * than through the trap. Every other definition is the decomp's. */
#ifndef KHDAYS_COMPAT_NITRO_MI_H
#define KHDAYS_COMPAT_NITRO_MI_H

#include "../../../../external/khdays-decomp/include/nitro/mi.h"

#undef MIi_Wait_BeforeDMA
#undef MIi_Wait_AfterDMA
#define MIi_Wait_BeforeDMA(dmaCntp, dmaNo)                                                                             \
    do {                                                                                                               \
        dmaCntp = &((vu32 *)REG_DMA0SAD_ADDR)[dmaNo * 3 + 2];                                                          \
        while (khdays_io_load(KHDAYS_IO_ADDRESS(dmaCntp), 4) & REG_MI_DMA0CNT_E_MASK) {                                \
        }                                                                                                              \
    } while (0)
#define MIi_Wait_AfterDMA(dmaCntp)                                                                                     \
    do {                                                                                                               \
        while (khdays_io_load(KHDAYS_IO_ADDRESS(dmaCntp), 4) & REG_MI_DMA0CNT_E_MASK) {                                \
        }                                                                                                              \
    } while (0)

#endif
