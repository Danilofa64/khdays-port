/* The DS's time from the PC's performance counter (clock.h), less the time spent held. */
#include "clock.h"

#include <windows.h>

static LARGE_INTEGER start;
static LARGE_INTEGER frequency;

/* The hold: the time is the counter less all the time spent held, so after a hold it goes on from
 * where it stopped. */
static SRWLOCK pause_lock = SRWLOCK_INIT;
static unsigned reasons;      /* what holds the time now (KHDAYS_HOLD_*) */
static int paused;            /* reasons != 0 */
static LONGLONG paused_at;    /* the counter when the current hold began */
static LONGLONG paused_ticks; /* all the counter ticks spent held before */

static LONGLONG counter_now(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

void khdays_clock_init(void)
{
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    timeBeginPeriod(1);
}

u64 khdays_clock_cycles(void)
{
    LONGLONG now;
    u64 ticks;
    AcquireSRWLockShared(&pause_lock);
    now = (paused ? paused_at : counter_now()) - paused_ticks;
    ReleaseSRWLockShared(&pause_lock);
    ticks = (u64)(now - start.QuadPart);
    /* ticks * HZ / frequency, without overflowing 64 bits */
    return ticks / (u64)frequency.QuadPart * KHDAYS_CLOCK_HZ +
           ticks % (u64)frequency.QuadPart * KHDAYS_CLOCK_HZ / (u64)frequency.QuadPart;
}

void khdays_clock_hold(unsigned reason, int on)
{
    AcquireSRWLockExclusive(&pause_lock);
    if (on) {
        if (reasons == 0) {
            paused_at = counter_now();
        }
        reasons |= reason;
    } else if (reasons & reason) {
        reasons &= ~reason;
        if (reasons == 0) {
            paused_ticks += counter_now() - paused_at;
        }
    }
    paused = reasons != 0;
    ReleaseSRWLockExclusive(&pause_lock);
}

int khdays_clock_held(unsigned reason)
{
    int held;
    AcquireSRWLockShared(&pause_lock);
    held = (reasons & reason) != 0;
    ReleaseSRWLockShared(&pause_lock);
    return held;
}

/* An auto-reset event: set by khdays_clock_wake, it ends the wait of
 * khdays_clock_sleep_until_or_woken (and, set with no one waiting, the next
 * one at once, so a wake is never lost). */
static HANDLE wake_event;
static INIT_ONCE wake_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK create_wake_event(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    return wake_event != NULL;
}

void khdays_clock_wake(void)
{
    InitOnceExecuteOnce(&wake_once, create_wake_event, NULL, NULL);
    SetEvent(wake_event);
}

static void sleep_until(u64 cycle, int wakeable)
{
    if (wakeable) {
        InitOnceExecuteOnce(&wake_once, create_wake_event, NULL, NULL);
    }
    for (;;) {
        const u64 now = khdays_clock_cycles();
        u64 ms;
        if (now >= cycle) {
            return;
        }
        if (paused) {
            /* the time stands still: nothing to wait for yet */
            if (!wakeable) {
                Sleep(10);
            } else if (WaitForSingleObject(wake_event, 10) == WAIT_OBJECT_0) {
                return;
            }
            continue;
        }
        ms = (cycle - now) * 1000 / KHDAYS_CLOCK_HZ;
        /* Sleep coarsely, then spin the last millisecond. */
        if (ms > 1) {
            if (wakeable) {
                if (WaitForSingleObject(wake_event, (DWORD)(ms - 1)) == WAIT_OBJECT_0) {
                    return;
                }
            } else {
                Sleep((DWORD)(ms - 1));
            }
        } else {
            if (wakeable && WaitForSingleObject(wake_event, 0) == WAIT_OBJECT_0) {
                return;
            }
            YieldProcessor();
        }
    }
}

void khdays_clock_sleep_until(u64 cycle)
{
    sleep_until(cycle, 0);
}

void khdays_clock_sleep_until_or_woken(u64 cycle)
{
    sleep_until(cycle, 1);
}
