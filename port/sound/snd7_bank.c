/* The native sound driver's banks (snd7_internal.h): the instrument a bank's program gives a key, and
 * the note it starts on a channel. Each function is the routine at the ARM7 address it names; the
 * SBNK and SWAR layouts: docs/SOUND.md, "The driver". */
#include "snd7_internal.h"

/* An instrument's 10 bytes of settings at `address`. */
static void read_settings(uint32_t address, Snd7Instrument *instrument)
{
    instrument->wave = snd7_rd16(address);
    instrument->archive = snd7_rd16(address + 2);
    instrument->original_key = snd7_rd8(address + 4);
    instrument->attack = snd7_rd8(address + 5);
    instrument->decay = snd7_rd8(address + 6);
    instrument->sustain = snd7_rd8(address + 7);
    instrument->release = snd7_rd8(address + 8);
    instrument->pan = snd7_rd8(address + 9);
}

/* 0x03801948: program `program`'s instrument for `key`. A drum set (type
 * 16) has one for each key from its lowest to its highest, a key split
 * (17) one for each of up to 8 ranges of keys, each up to its key; either
 * is an instrument's type and settings, 12 bytes. 0 when there is none. */
int snd7_bank_read_instrument(uint32_t bank, int program, int key, Snd7Instrument *instrument)
{
    uint32_t word, at;
    if (program < 0 || (uint32_t)program >= snd7_rd32(bank + 0x38)) {
        return 0;
    }
    word = snd7_rd32(bank + 0x3c + 4u * (uint32_t)program);
    at = bank + (word >> 8);
    instrument->type = (uint8_t)word;
    switch (instrument->type) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5: read_settings(at, instrument); return 1;
    case 16: {
        const int lowest = snd7_rd8(at), highest = snd7_rd8(at + 1);
        if (key < lowest || key > highest) {
            return 0;
        }
        at += 2 + 12u * (uint32_t)(key - lowest);
        break;
    }
    case 17: {
        uint32_t range = 0;
        while (key > snd7_rd8(at + range)) {
            if (++range >= 8) {
                return 0;
            }
        }
        at += 8 + 12u * range;
        break;
    }
    default: return 0;
    }
    instrument->type = snd7_rd8(at);
    read_settings(at + 2, instrument);
    return 1;
}

/* 0x03801ac8: where wave `n` of the archive at `archive` is, 0 if it has
 * none. */
uint32_t snd7_bank_wave(uint32_t archive, int n)
{
    const uint32_t at = snd7_rd32(archive + 0x3c + 4u * (uint32_t)n);
    if (at == 0) {
        return 0;
    }
    return at < 0x02000000u ? archive + at : at;
}

/* 0x03801b08: the note `key` at `velocity` starts on `channel` with the
 * instrument, for `length` ticks (-1: until it is released, as with an
 * instrument whose release is 0xff), its envelope and pan the
 * instrument's. 0 if it has no sound to play. */
int snd7_bank_note_on(Snd7Channel *channel, int key, int velocity, int32_t length, uint32_t bank,
                      const Snd7Instrument *instrument)
{
    int release = instrument->release;
    int started;
    if (release == 0xff) {
        length = -1;
        release = 0;
    }
    switch (instrument->type) {
    case 1:
    case 4: {
        uint32_t wave;
        if (instrument->type == 1) {
            const uint32_t archive = snd7_rd32(bank + 0x18 + 8u * instrument->archive);
            if (archive == 0 || instrument->wave >= snd7_rd32(archive + 0x38)) {
                wave = 0;
            } else {
                wave = snd7_bank_wave(archive, instrument->wave);
            }
        } else {
            wave = instrument->wave | (uint32_t)instrument->archive << 16;
        }
        if (wave == 0) {
            return 0;
        }
        started = snd7_channel_start_pcm(channel, wave, wave + 0xc, length);
        break;
    }
    case 2: started = snd7_channel_start_psg(channel, instrument->wave, length); break;
    case 3: started = snd7_channel_start_noise(channel, length); break;
    default: return 0;
    }
    if (!started) {
        return 0;
    }
    channel->key = (uint8_t)key;
    channel->original_key = instrument->original_key;
    channel->velocity = (uint8_t)velocity;
    snd7_channel_set_attack(channel, instrument->attack);
    snd7_channel_set_decay(channel, instrument->decay);
    snd7_channel_set_sustain(channel, instrument->sustain);
    snd7_channel_set_release(channel, release);
    channel->init_pan = (int8_t)(instrument->pan - 0x40);
    return 1;
}
