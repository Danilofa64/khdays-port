/* The SDK's PXI transport (pxi.h). */
#include "pxi.h"
#include "arm7.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>

static u32 *queue;
static u32 queue_room, queue_head, queue_count;
static int receive_interrupt;

void khdays_pxi_reset(void)
{
    queue_head = queue_count = 0;
    receive_interrupt = 0;
}

void khdays_pxi_init(void)
{
    receive_interrupt = 1;
    if (queue_count > 0) {
        khdays_io_request_irq(1u << 18);
    }
}

s32 khdays_pxi_send(u32 tag, u32 data, u32 err)
{
    khdays_arm7_receive((tag & 0x1fu) | (err & 1u) << 5 | data << 6);
    return 0;
}

void khdays_pxi_queue(u32 word)
{
    if (queue_count == queue_room) {
        const u32 room = queue_room != 0 ? queue_room * 2 : 64;
        u32 *grown = (u32 *)malloc(sizeof(u32) * room);
        if (grown == NULL) {
            fprintf(stderr, "pxi: out of memory keeping %u replies to the ARM9\n", queue_count);
            exit(6);
        }
        for (u32 i = 0; i < queue_count; ++i) {
            grown[i] = queue[(queue_head + i) % queue_room];
        }
        free(queue);
        queue = grown;
        queue_room = room;
        queue_head = 0;
    }
    queue[(queue_head + queue_count) % queue_room] = word;
    ++queue_count;
    /* the receive interrupt while a reply waits */
    if (receive_interrupt) {
        khdays_io_request_irq(1u << 18);
    }
}

int khdays_pxi_take(u32 *word)
{
    if (queue_count == 0) {
        return 0;
    }
    *word = queue[queue_head];
    queue_head = (queue_head + 1) % queue_room;
    --queue_count;
    return 1;
}

int khdays_pxi_waiting(u32 tag)
{
    int n = 0;
    for (u32 i = 0; i < queue_count; ++i) {
        n += (queue[(queue_head + i) % queue_room] & 0x1fu) == tag;
    }
    return n;
}

int khdays_pxi_pending(void)
{
    return (int)queue_count;
}
