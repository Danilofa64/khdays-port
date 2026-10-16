/* The native sound driver's alarms for the ARM9 (0x03808c98, 8 of 0x40 bytes): set up and started by
 * the ARM9, each firing tells it its number and id -- the streams' and the movies' refills. Each
 * function is the routine at the ARM7 address it names. */
#include "snd7_internal.h"
#include "snd7_os.h"

typedef struct Snd7Alarm {
    uint8_t active;    /* +0x00 */
    uint8_t id;        /* +0x01: which setting, so the ARM9 tells a late one */
    uint64_t ticks;    /* +0x04: to the first */
    uint64_t period;   /* +0x0c: 0 once */
    Snd7OsAlarm alarm; /* +0x14 */
} Snd7Alarm;

static Snd7Alarm alarms[8];

/* 0x03801ea8: the alarm's handler: its argument to the ARM9, on tag 7 (the
 * ARM7 sends again while the FIFO is full; the bridge keeps every reply). */
static void fired(uint32_t arg)
{
    snd7_host->reply(snd7_host->context, 7, arg, 0);
}

/* 0x03801d24 */
void snd7_alarm_init(void)
{
    for (int i = 0; i < 8; ++i) {
        alarms[i].active = 0;
        alarms[i].id = 0;
    }
}

/* 0x03801d50 */
void snd7_alarm_setup(int n, uint64_t ticks, uint64_t period, int id)
{
    Snd7Alarm *a = &alarms[n];
    if (a->active) {
        snd7_os_cancel_alarm(&a->alarm);
        a->active = 0;
    }
    a->ticks = ticks;
    a->period = period;
    a->id = (uint8_t)id;
}

/* 0x03801dac: once `ticks` from now, or every `period` from then; the
 * ARM9 hears n | id << 8. */
void snd7_alarm_start(int n)
{
    Snd7Alarm *a = &alarms[n];
    const uint32_t arg = (uint32_t)n | (uint32_t)a->id << 8;
    if (a->active) {
        snd7_os_cancel_alarm(&a->alarm);
        a->active = 0;
    }
    snd7_os_create_alarm(&a->alarm);
    if (a->period == 0) {
        snd7_os_set_alarm(&a->alarm, a->ticks, fired, arg);
    } else {
        snd7_os_set_periodic_alarm(&a->alarm, snd7_os_tick() + a->ticks, a->period, fired, arg);
    }
    a->active = 1;
}

/* 0x03801e68: a late firing's id no longer matches. */
void snd7_alarm_stop(int n)
{
    Snd7Alarm *a = &alarms[n];
    if (a->active) {
        snd7_os_cancel_alarm(&a->alarm);
        ++a->id;
        a->active = 0;
    }
}
