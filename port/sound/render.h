/* A sequence played offline, as the game plays it: the native driver (snd7_native.h) takes the commands
 * StartSeq and StartSeqArc send, its time runs as fast as the mixing goes, and the PC's mixer
 * (mix_pc.h) makes the output. The player's volume stays at 0 dB (a replacement plays under the game's
 * volume, so a rendered sequence put back sounds as the original); the sequence's variables stay as
 * SND_PrepareSeq leaves them.
 *
 * How long: a sequence that ends, until its last note has died away; one that loops, until it has
 * played its loop twice (found where the sequencer's state first comes back to one it had), the second
 * time as the file's loop; anything else up to a limit.
 *
 * The caller gives the driver its ARM7 segment and the DS's memory, with the sequence, its bank and
 * wave archives in it. One render at a time. */
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
