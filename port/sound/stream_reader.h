/* The music streams, read by the port itself: the same samples NitroSystem's stream player loads into
 * each channel's ring, made from the stream's file by the same steps, so a channel gets them when every
 * load is in time whatever the game's stream thread does; the game's own player goes on as before.
 * Plain C, the file through a read callback; the channels' source too (docs/SOUND.md). */
#ifndef KHDAYS_SOUND_STREAM_READER_H
#define KHDAYS_SOUND_STREAM_READER_H

#include "channels.h"

#include <stdint.h>

#define KHDAYS_STREAM_PLAYERS     4     /* NNS_SND_STRM_PLAYER_NUM */
#define KHDAYS_STREAM_CHANNELS    16    /* NNSSndStrm.channelNo */
#define KHDAYS_STREAM_HEADER_SIZE 0x40  /* NNSSndStrmData: the file's first bytes, as the player keeps them */

/* The stream's file: `size` bytes at `offset` from its start; returns how
 * many it read. */
typedef struct KhdaysStreamFile {
    void *context;
    uint32_t (*read)(void *context, uint32_t offset, void *destination, uint32_t size);
} KhdaysStreamFile;

/* What PrepareStrm (0x0201ce18) has set up when it returns TRUE. */
typedef struct KhdaysStreamPrepared {
    int player;                                 /* the stream player, 0-3 */
    int channels;                               /* the channels that play it */
    uint8_t channel[KHDAYS_STREAM_CHANNELS];    /* their numbers */
    uint32_t ring;                              /* the first channel's ring; each next one follows */
    uint32_t ring_bytes;                        /* a ring's size (chBufLen) */
    uint8_t header[KHDAYS_STREAM_HEADER_SIZE];  /* player->info */
    uint32_t start_sample;                      /* player->curSample */
    int mono;                                   /* a channel past the file's is silent, not a copy of the first */
} KhdaysStreamPrepared;

typedef struct KhdaysStreams KhdaysStreams;

/* The streams, with NitroSystem's IMA-ADPCM tables as the game has them
 * (cAdpcmIndexTable: 16 entries; cAdpcmStepSizeTable: 89). */
KhdaysStreams *khdays_streams_new(const int8_t *index_table, const int16_t *step_table);

/* A stream player prepared a stream: its channels' next starts on its rings
 * take their samples from here. */
void khdays_streams_prepared(KhdaysStreams *streams, const KhdaysStreamPrepared *prepared, KhdaysStreamFile file);

/* The channels' side (KhdaysSoundSource): a channel starting on one of the
 * rings is bound to it -- a handle below 1 << 30 -- then its index-th
 * sample since its start. */
int khdays_streams_bind(void *streams, int channel, uint32_t sad, uint32_t len, int format);
int khdays_streams_sample(void *streams, int handle, uint64_t index, int32_t *sample);

/* Where the ring holds a bound channel's index-th sample, and its size (1
 * PCM8, 2 PCM16), for comparing with the ring; 0 if the handle is no longer
 * bound. */
uint32_t khdays_streams_ring_address(const KhdaysStreams *streams, int handle, uint64_t index, int *unit);

/* The stream player (0-3) a channel's handle is from, and which of the
 * stream's channels it plays. */
int khdays_streams_handle_player(int handle);
int khdays_streams_handle_channel(int handle);

/* A bound channel's ring, in samples (0 if the handle is no longer bound). */
uint32_t khdays_streams_ring_samples(const KhdaysStreams *streams, int handle);

#endif
