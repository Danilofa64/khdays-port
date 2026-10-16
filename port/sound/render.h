/* A sequence played offline as the game plays it: the native driver takes StartSeq's commands, its
 * time as fast as the mixing goes, the PC's mixer makes the output, the player's volume at 0 dB. A
 * sequence that ends plays until its last note dies away, one that loops twice (docs/SOUND.md). The
 * caller gives the driver its ARM7 segment and the DS's memory with the sequence, bank and wave
 * archives in it. One render at a time. */
#ifndef KHDAYS_SOUND_RENDER_H
#define KHDAYS_SOUND_RENDER_H

#include "channels.h"

#include <stdint.h>

typedef struct KhdaysRenderRequest {
    uint32_t sequence;          /* its data: the file + its baseOffset (NNSSndSeqData, NNSSndSeqArc) */
    uint32_t offset;            /* its first command from there: 0, or an archive entry's offset */
    uint32_t bank;              /* the bank (SBNK), its wave archives assigned as SND_AssignWaveArc does */
    uint16_t channels;          /* the channels its player may take (NNSSndArcPlayerInfo.allocChBitFlag), 0 any */
    uint8_t channel_priority;   /* NNSSndSeqParam.channelPrio */
    uint32_t work;              /* 0x400 bytes of the DS's memory: the command lists, the shared work */
    uint8_t *work_bytes;        /* where they are in the process */
    KhdaysSoundMemory memory;   /* the channels' view of the DS's memory */
    uint32_t rate;              /* the output's, in Hz */
    uint32_t limit_seconds;     /* the most it plays */
} KhdaysRenderRequest;

typedef struct KhdaysRendered {
    int16_t *samples;           /* stereo, interleaved (free()) */
    uint64_t frames;
    int looped;                 /* the loop, in frames, the second time round */
    uint64_t loop_start, loop_end;
    int ended;                  /* it ended; 0: it neither ended nor looped by the limit */
} KhdaysRendered;

/* 0, and a message, when the driver cannot start or memory runs out. */
int khdays_render_sequence(const KhdaysRenderRequest *request, KhdaysRendered *rendered);

#endif
