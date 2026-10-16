/* The SDK's tick and alarm timer on the PC's clock: the tick in its own unit, the alarm timer a
 * deadline at which the frame loop raises timer 1's interrupt (runtime/core/events.c). prepare.py's
 * hooks put these in place of the timer registers; the alarm queue stays the decomp's C
 * (docs/PLATFORM.md, "Threads, interrupts and time"). */
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
