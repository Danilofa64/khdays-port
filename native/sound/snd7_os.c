/* The ARM7's operating system as the native sound driver needs it: its
 * tick, its alarms and the driver thread's message queue (OS_GetTick,
 * OS_SetAlarm, OS_SetPeriodicAlarm, OS_CancelAlarm and the alarm
 * interrupt), as the port gave them to the interpreted driver it was
 * checked against. */
#include "snd7_os.h"
#include "snd7_internal.h"

#include <string.h>

/* The alarms waiting, by the tick they fire at (a new one after those
 * due at the same tick). */
#define MAX_ALARMS 32
static Snd7OsAlarm *alarms[MAX_ALARMS];
static int alarm_count;

/* The driver thread's messages (its OSMessageQueue at 0x03807800, 8 deep):
 * sent from the alarms and the FIFO, taken by the thread after each. */
static uint32_t messages[8];
static int message_count;

uint64_t snd7_os_tick(void)
{
    return snd7_now / 64;
}

void snd7_os_reset(void)
{
    alarm_count = 0;
    message_count = 0;
}

static void unlink_alarm(Snd7OsAlarm *alarm)
{
    for (int i = 0; i < alarm_count; ++i) {
        if (alarms[i] == alarm) {
            memmove(&alarms[i], &alarms[i + 1], (size_t)(alarm_count - i - 1) * sizeof(alarms[0]));
            --alarm_count;
            return;
        }
    }
}

/* OSi_InsertAlarm: a periodic alarm fires at the first start + k * period
 * after now (or at start, if that is still ahead). */
static void insert_alarm(Snd7OsAlarm *alarm, uint64_t fire)
{
    int at;
    if (alarm->period != 0) {
        const uint64_t now = snd7_os_tick();
        fire = alarm->start;
        if (alarm->start < now) {
            fire = alarm->start + alarm->period * ((now - alarm->start) / alarm->period + 1);
        }
    }
    alarm->fire = fire;
    unlink_alarm(alarm);
    if (alarm_count == MAX_ALARMS) {
        snd7_stop("too many ARM7 alarms");
    }
    for (at = 0; at < alarm_count && (int64_t)(fire - alarms[at]->fire) >= 0; ++at) {
    }
    memmove(&alarms[at + 1], &alarms[at], (size_t)(alarm_count - at) * sizeof(alarms[0]));
    alarms[at] = alarm;
    ++alarm_count;
}

/* 0x037fd3a0 */
void snd7_os_create_alarm(Snd7OsAlarm *alarm)
{
    alarm->handler = NULL;
    alarm->period = 0;
}

void snd7_os_set_alarm(Snd7OsAlarm *alarm, uint64_t ticks, Snd7AlarmHandler handler, uint32_t arg)
{
    if (alarm->handler != NULL) {
        snd7_stop("OS_SetAlarm on an alarm in use");
    }
    alarm->period = 0;
    alarm->handler = handler;
    alarm->arg = arg;
    insert_alarm(alarm, snd7_os_tick() + ticks);
}

void snd7_os_set_periodic_alarm(Snd7OsAlarm *alarm, uint64_t start, uint64_t period, Snd7AlarmHandler handler,
                                uint32_t arg)
{
    if (alarm->handler != NULL) {
        snd7_stop("OS_SetPeriodicAlarm on an alarm in use");
    }
    alarm->period = period;
    alarm->start = start;
    alarm->handler = handler;
    alarm->arg = arg;
    insert_alarm(alarm, 0);
}

void snd7_os_cancel_alarm(Snd7OsAlarm *alarm)
{
    if (alarm->handler == NULL) {
        return;
    }
    unlink_alarm(alarm);
    alarm->handler = NULL;
    alarm->period = 0;
}

/* The alarm interrupt for the first alarm: it leaves the list, a one-time
 * alarm loses its handler, the handler runs, the ARM7's time moves past
 * the fire tick, and a periodic alarm goes back in unless it was
 * cancelled. */
static void fire_first_alarm(void)
{
    Snd7OsAlarm *alarm = alarms[0];
    const Snd7AlarmHandler handler = alarm->handler;
    const uint64_t after = (alarm->fire + 1) * 64;
    unlink_alarm(alarm);
    if (alarm->period == 0) {
        alarm->handler = NULL;
    }
    if (handler != NULL) {
        handler(alarm->arg);
    }
    if (snd7_now < after) {
        snd7_now = after;
    }
    if (alarm->period != 0) {
        alarm->handler = handler;
        insert_alarm(alarm, 0);
    }
}

int snd7_os_send_message(uint32_t message)
{
    if (message_count == 8) {
        return 0;
    }
    messages[message_count++] = message;
    return 1;
}

/* The driver thread takes its messages, one loop each, until none are
 * left (it runs as soon as a message wakes it: the port's ARM7 does
 * nothing else meanwhile). */
static void run_thread(void)
{
    while (message_count > 0) {
        const uint32_t message = messages[0];
        memmove(&messages[0], &messages[1], (size_t)(message_count - 1) * sizeof(messages[0]));
        --message_count;
        snd7_thread_loop(message);
    }
}

void snd7_os_advance(uint64_t cycle)
{
    while (alarm_count > 0 && alarms[0]->fire * 64 <= cycle) {
        const uint64_t fire = alarms[0]->fire * 64;
        if (fire > snd7_now) {
            snd7_now = fire;
        }
        snd7_host->run(snd7_host->context, snd7_now);
        fire_first_alarm();
        run_thread();
    }
    if (cycle > snd7_now) {
        snd7_now = cycle;
    }
    snd7_host->run(snd7_host->context, snd7_now);
}

void snd7_os_after_interrupt(void)
{
    run_thread();
}

uint64_t snd7_os_next_event(void)
{
    return alarm_count > 0 ? alarms[0]->fire * 64 : ~(uint64_t)0;
}
