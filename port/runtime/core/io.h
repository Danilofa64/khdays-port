/* The DS I/O registers: what each one does (io_regs.c), which the game's C
 * reaches through the port (prepare.py, port/tools/registers.py), and the
 * guard at their DS addresses (io_guard.c). */
#ifndef KHDAYS_IO_H
#define KHDAYS_IO_H

#include "../../hal/hal.h"

#define KHDAYS_IO_BASE 0x04000000u
#define KHDAYS_IO_SIZE 0x10000u
#define KHDAYS_IO2_BASE 0x04100000u /* IPCFIFORECV, card data */
#define KHDAYS_IO2_SIZE 0x10000u

/* The two regions' own memory, in the window (memory.c); at the DS's
 * addresses, the guard's (io_guard.c). */
extern u8 *khdays_io_host;
extern u8 *khdays_io2_host;

#define IO8(offset) (*(volatile u8 *)(khdays_io_host + (offset)))
#define IO16(offset) (*(volatile u16 *)(khdays_io_host + (offset)))
#define IO32(offset) (*(volatile u32 *)(khdays_io_host + (offset)))

/* An access to the registers' DS addresses stops the game, naming it
 * (io_guard.c). */
void khdays_io_guard_init(void);

/* Before the game reads [address, address + size): bring the values up to
 * date. After it wrote them (`before` holds the bytes they replaced): apply
 * what the write means. */
void khdays_io_read(u32 address, int size);

/* The instruction that asked for the access being handled -- the caller of
 * khdays_io_load or khdays_io_store -- for the messages that name who
 * touches a register (io_regs.c). */
extern khdays_uptr khdays_io_caller;
void khdays_io_write(u32 address, int size, const u8 *before);

/* A store to a register by the runtime or the HAL, with what it means --
 * the same as the trap does for the game's own store, without the fault
 * (the geometry FIFO takes thousands of words a frame). */
int khdays_io_is_register(u32 address);
void khdays_io_store(u32 address, int size, u32 value);

/* A register's value as the game would read it: the game's C's reads of a
 * register that is computed when read (prepare.py), the runtime's and the
 * HAL's. */
u32 khdays_io_load(u32 address, int size);

/* The math unit's registers (0x280-0x2bf) for the game's C without the trap: the register at
 * `offset`, its results brought up to date for a read of `size` bytes (compat/include/nitro/hw.h). */
void *khdays_io_cp(u32 offset, int size);
/* A register that only holds a value (IME, IE, the 2D's, the DMA's addresses): a constant address for
 * the decomp's C (prepare.py, registers.py; the same in compat/decomp_compat.h). */
#define khdays_io_plain(offset) ((void *)(khdays_io_host + (offset)))

/* Initial register values, as the ARM9 sees them when crt0 starts. */
void khdays_io_reset(void);

/* Raise interrupt request bits (IF). */
void khdays_io_request_irq(u32 bits);

#endif
