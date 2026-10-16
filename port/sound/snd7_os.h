/* The ARM7's operating system as the native sound driver needs it
 * (snd7_os.c): the tick (the DS clock / 64), alarms, and the driver
 * thread's messages. */
#ifndef KHDAYS_SOUND_SND7_OS_H
#define KHDAYS_SOUND_SND7_OS_H

#include <stdint.h>

typedef void (*Snd7AlarmHandler)(uint32_t arg);

typedef struct Snd7OsAlarm {
    Snd7AlarmHandler handler; /* NULL: not in use */
    uint32_t arg;
    uint64_t fire;   /* the tick it fires at */
    uint64_t period; /* 0: once */
    uint64_t start;  /* a periodic alarm's first tick */
} Snd7OsAlarm;

void snd7_os_reset(void);
uint64_t snd7_os_tick(void);

void snd7_os_create_alarm(Snd7OsAlarm *alarm); /* 0x037fd3a0 */
void snd7_os_set_alarm(Snd7OsAlarm *alarm, uint64_t ticks, Snd7AlarmHandler handler, uint32_t arg);
void snd7_os_set_periodic_alarm(Snd7OsAlarm *alarm, uint64_t start, uint64_t period, Snd7AlarmHandler handler,
                                uint32_t arg);
void snd7_os_cancel_alarm(Snd7OsAlarm *alarm);

/* A message to the driver thread (OS_SendMessage, not blocking): 0 when
 * its queue is full. */
int snd7_os_send_message(uint32_t message);

/* The alarms due up to `cycle` fire, the channels running up to each, the
 * driver thread taking its messages after each; the time ends at `cycle`. */
void snd7_os_advance(uint64_t cycle);

/* After an interrupt's handler (the FIFO's): the thread takes what it was
 * sent. */
void snd7_os_after_interrupt(void);

uint64_t snd7_os_next_event(void);

/* The driver thread's loop, once per message (snd7_driver.c). */
void snd7_thread_loop(uint32_t message);

#endif
