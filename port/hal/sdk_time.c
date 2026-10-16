/* The SDK's tick and alarm timer on the PC's clock.
 *
 * NitroSDK counts its tick (OS_GetTick) with hardware timer 0, at 64 cycles
 * per tick, with the overflows carried in software by OSi_CountUpTick. It
 * wakes its alarms with timer 1, which OSi_SetTimer programs to overflow when
 * the alarm queue's head is due.
 *
 * Natively the tick is the port's clock in the same unit, and the alarm timer
 * is a deadline at which the frame loop raises timer 1's interrupt
 * (runtime/core/events.c). prepare.py HOOKS put these functions in place of the
 * timer registers in OS_InitTick, OS_GetTick, OS_GetTickLo, OS_SetTick,
 * OSi_SetTimer and OSi_ArrangeTimer. The alarm queue, its handler and their
 * callers stay the decomp's C. */
#include "hal.h"
#include "../runtime/core/clock.h"
#include "../runtime/core/events.h"

static u64 offset; /* the tick is cycles / 64 + offset */

u64 khdays_tick_now(void)
{
    return khdays_clock_cycles() / 64 + offset;
}

/* The tick reads `tick` now (OS_InitTick: 0; OS_SetTick: the value
 * it is given -- Scene_Leave's way back from the pause scene would put the
 * tick where Game_EnterPauseScene saved it, a path no walk and no pause
 * tried reaches: in Mission Mode the pause keeps the time running). */
void khdays_tick_set(u64 tick)
{
    offset = tick - khdays_clock_cycles() / 64;
}

/* OSi_SetTimer's timer 1 for an alarm due at tick `fire`: the timer counts
 * up from ~delta and overflows delta + 1 ticks on; 2 ticks when the alarm is
 * already past; 0x10000 at most (the handler then programs it again). */
void khdays_alarm_timer_for(u64 fire)
{
    const s64 delta = (s64)(fire - khdays_tick_now());
    khdays_alarm_set_after(delta < 0 ? 2 : delta < 0x10000 ? (u64)delta + 1 : 0x10000);
}
