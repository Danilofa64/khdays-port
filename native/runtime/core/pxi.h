/* The SDK's PXI transport, the port's own: what PXI_InitFifo, PXI_SendWordByFifo and
 * PXIi_HandlerRecvFifoNotEmpty did through the IPC FIFOs (prepare.py HOOKS). A word sent reaches the
 * ARM7's service at once (arm7.c); a reply waits in a growing queue, raising the receive interrupt
 * (IF bit 18) while one waits. Game thread only. */
#ifndef KHDAYS_PXI_H
#define KHDAYS_PXI_H

#include "../../hal/hal.h"

/* PXI_InitFifo: the receive interrupt enabled (its IPCFIFOCNT store). */
void khdays_pxi_init(void);

/* PXI_SendWordByFifo / PXIi_SetToFifo: the message to the ARM7's service for
 * its tag. Never fails (0): the ARM7 takes every word. */
s32 khdays_pxi_send(u32 tag, u32 data, u32 err);

/* PXIi_GetFromFifo: the next reply into *word, 1; 0 when none waits. */
int khdays_pxi_take(u32 *word);

/* How many replies of `tag` wait (the touch panel's samples wait for the ARM9's). */
int khdays_pxi_waiting(u32 tag);

/* All the replies that wait. */
int khdays_pxi_pending(void);

/* A reply (arm7.c's services): `tag | err << 5 | data << 6`, queued. */
void khdays_pxi_queue(u32 word);

/* The queue emptied and the interrupt not enabled (a reset). */
void khdays_pxi_reset(void);

#endif
