/* The native sound driver's side of the FIFO (snd7_internal.h): the command
 * lists the ARM9 sends, queued as they come and taken at the driver's next
 * loop; the 34 commands (include/nitro/snd.h); and the shared work the
 * ARM9 reads back. Each function is the routine at the ARM7 address it
 * names. */
#include "snd7_internal.h"
#include "snd7_native.h"
#include "snd7_os.h"

/* 0x03807d14: where the ARM9 put the shared work. */
uint32_t snd7_shared_work;

static uint8_t *shared_at(uint32_t offset)
{
    return snd7_ds(snd7_shared_work + offset);
}

uint32_t snd7_shared_read32(uint32_t offset)
{
    const uint8_t *p = shared_at(offset);
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint16_t snd7_shared_read16(uint32_t offset)
{
    const uint8_t *p = shared_at(offset);
    return (uint16_t)(p[0] | p[1] << 8);
}

void snd7_shared_write32(uint32_t offset, uint32_t value)
{
    uint8_t *p = shared_at(offset);
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

void snd7_shared_write16(uint32_t offset, uint16_t value)
{
    uint8_t *p = shared_at(offset);
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

/* 0x03801c8c: a bit per channel playing (+8), per capture unit (+0xa). */
void snd7_update_shared_status(void)
{
    uint16_t channels = 0, captures = 0;
    if (snd7_shared_work == 0) {
        return;
    }
    for (int n = 0; n < 16; ++n) {
        if (snd7_hw_is_active(n)) {
            channels = (uint16_t)(channels | 1u << n);
        }
    }
    if (snd7_capture_is_active(0)) {
        captures |= 1;
    }
    if (snd7_capture_is_active(1)) {
        captures |= 2;
    }
    snd7_shared_write16(8, channels);
    snd7_shared_write16(0xa, captures);
}

/* --- The list queue ------------------------------------------------------------------ */

/* 0x03808e98: the lists waiting (an OSMessageQueue of 8). */
static uint32_t lists[8];
static int list_count;

/* 0x03801ed8 (with the FIFO callback installed and the shared work unset). */
void snd7_command_init(void)
{
    list_count = 0;
    snd7_shared_work = 0;
}

/* 0x038025cc: the sound tag's receive callback. A list (an address of
 * 0x02000000 or more) waits for the driver's next loop -- or is lost if 8
 * are waiting, as OS_SendMessage refuses it; a word of 0 wakes the driver
 * at once (0x037fefc8, message 2). */
void snd7_command_receive(uint32_t data)
{
    if (data >= 0x02000000u) {
        if (list_count < 8) {
            lists[list_count++] = data;
        }
    } else if (data == 0) {
        snd7_os_send_message(2);
    }
}

static int take_list(uint32_t *list)
{
    if (list_count == 0) {
        return 0;
    }
    *list = lists[0];
    for (int i = 1; i < list_count; ++i) {
        lists[i - 1] = lists[i];
    }
    --list_count;
    return 1;
}

/* --- The commands ------------------------------------------------------------------- */

enum {
    START_SEQ, STOP_SEQ, PREPARE_SEQ, START_PREPARED_SEQ, PAUSE_SEQ, SKIP_SEQ, PLAYER_PARAM, TRACK_PARAM,
    MUTE_TRACK, ALLOCATABLE_CHANNEL, PLAYER_LOCAL_VAR, PLAYER_GLOBAL_VAR, START_TIMER, STOP_TIMER,
    SETUP_CHANNEL_PCM, SETUP_CHANNEL_PSG, SETUP_CHANNEL_NOISE, SETUP_CAPTURE, SETUP_ALARM, CHANNEL_TIMER,
    CHANNEL_VOLUME, CHANNEL_PAN, SURROUND_DECAY, MASTER_VOLUME, MASTER_PAN, OUTPUT_SELECTOR, LOCK_CHANNEL,
    UNLOCK_CHANNEL, STOP_UNLOCKED_CHANNEL, SHARED_WORK, INVALIDATE_SEQ, INVALIDATE_BANK, INVALIDATE_WAVE,
    READ_DRIVER_INFO
};

/* SND_COMMAND_START_TIMER: channels, capture units and alarms at once. */
static void start_timer(uint32_t channels, uint32_t captures, uint32_t alarm_mask)
{
    for (int n = 0; n < 16 && channels != 0; ++n, channels >>= 1) {
        if (channels & 1) {
            snd7_reg_write((uint32_t)n * 16 + 3, 1, snd7_reg_read((uint32_t)n * 16 + 3, 1) | 0x80);
        }
    }
    if (captures & 1) {
        if (captures & 2) {
            snd7_reg_write(0x108, 2, snd7_reg_read(0x108, 2) | 0x8080);
        } else {
            snd7_reg_write(0x108, 1, snd7_reg_read(0x108, 1) | 0x80);
        }
    } else if (captures & 2) {
        snd7_reg_write(0x109, 1, snd7_reg_read(0x109, 1) | 0x80);
    }
    for (int i = 0; i < 8 && alarm_mask != 0; ++i, alarm_mask >>= 1) {
        if (alarm_mask & 1) {
            snd7_alarm_start(i);
        }
    }
    snd7_update_shared_status();
}

/* SND_COMMAND_STOP_TIMER: the alarms, then the channels, then the capture
 * units. */
static void stop_timer(uint32_t channels, uint32_t captures, uint32_t alarm_mask, int hold)
{
    for (int i = 0; i < 8 && alarm_mask != 0; ++i, alarm_mask >>= 1) {
        if (alarm_mask & 1) {
            snd7_alarm_stop(i);
        }
    }
    for (int n = 0; n < 16 && channels != 0; ++n, channels >>= 1) {
        if (channels & 1) {
            snd7_hw_stop(n, hold);
        }
    }
    if (captures & 1) {
        snd7_reg_write(0x108, 1, 0);
    }
    if (captures & 2) {
        snd7_reg_write(0x109, 1, 0);
    }
    snd7_update_shared_status();
}

static void each_channel(uint32_t mask, void (*apply)(int channel, uint32_t a, uint32_t b), uint32_t a, uint32_t b)
{
    for (int n = 0; n < 16 && mask != 0; ++n, mask >>= 1) {
        if (mask & 1) {
            apply(n, a, b);
        }
    }
}

static void set_timer(int n, uint32_t timer, uint32_t unused)
{
    (void)unused;
    snd7_hw_set_timer(n, (int)timer);
}

static void set_volume(int n, uint32_t volume, uint32_t shift)
{
    snd7_hw_set_volume(n, (int)volume, (int)shift);
}

static void set_pan(int n, uint32_t pan, uint32_t unused)
{
    (void)unused;
    snd7_hw_set_pan(n, (int)pan);
}

/* One command (SNDCommand: next +0, id +4, arg[4] +8), as copied. */
static void run_command(uint32_t id, const uint32_t arg[4])
{
    switch (id) {
    case START_SEQ:
        snd7_seq_start((int)arg[0], arg[1], arg[2], arg[3]);
        break;
    case STOP_SEQ:
        snd7_seq_stop((int)arg[0]);
        break;
    case PREPARE_SEQ:
        snd7_seq_prepare((int)arg[0], arg[1], arg[2], arg[3]);
        break;
    case START_PREPARED_SEQ:
        snd7_seq_start_prepared((int)arg[0]);
        break;
    case PAUSE_SEQ:
        snd7_seq_pause((int)arg[0], (int)arg[1]);
        break;
    case SKIP_SEQ:
        snd7_seq_skip((int)arg[0], arg[1]);
        break;
    case PLAYER_PARAM:
        snd7_seq_player_param((int)arg[0], arg[1], arg[2], (int)arg[3]);
        break;
    case TRACK_PARAM:
        snd7_seq_track_param((int)(arg[0] & 0x00ffffffu), arg[1], arg[2], arg[3], (int)(arg[0] >> 24));
        break;
    case MUTE_TRACK:
        snd7_seq_mute_track((int)arg[0], arg[1], (int)arg[2]);
        break;
    case ALLOCATABLE_CHANNEL:
        snd7_seq_allocatable_channels((int)arg[0], arg[1], arg[2]);
        break;
    case PLAYER_LOCAL_VAR:
        snd7_seq_set_local_variable((int)arg[0], (int)arg[1], (int16_t)arg[2]);
        break;
    case PLAYER_GLOBAL_VAR:
        snd7_seq_set_global_variable((int)arg[0], (int16_t)arg[1]);
        break;
    case START_TIMER:
        start_timer(arg[0], arg[1], arg[2]);
        break;
    case STOP_TIMER:
        stop_timer(arg[0], arg[1], arg[2], (int)arg[3]);
        break;
    case SETUP_CHANNEL_PCM:
        /* the data's whole address: the ARM7 kept its 27 bits (a DS channel's
         * source register's width), and SND_SetupChannelPcm puts nothing but
         * the address there -- the game's heaps lie above 27 bits */
        snd7_hw_setup_pcm((int)(arg[0] & 0xffff), arg[1], (int)((arg[3] >> 24) & 3),
                          (int)((arg[3] >> 26) & 3), (int)(arg[3] & 0xffff), arg[2] & 0x3fffff,
                          (int)((arg[2] >> 24) & 0x7f), (int)((arg[2] >> 22) & 3), (int)(arg[0] >> 16),
                          (int)((arg[3] >> 16) & 0x7f));
        break;
    case SETUP_CHANNEL_PSG:
        snd7_hw_setup_psg((int)arg[0], (int)arg[3], (int)(arg[1] & 0x7f), (int)((arg[1] >> 8) & 3),
                          (int)((arg[2] >> 8) & 0xffff), (int)(arg[2] & 0x7f));
        break;
    case SETUP_CHANNEL_NOISE:
        snd7_hw_setup_noise((int)arg[0], (int)(arg[1] & 0x7f), (int)((arg[1] >> 8) & 3),
                            (int)((arg[2] >> 8) & 0xffff), (int)(arg[2] & 0x7f));
        break;
    case SETUP_CAPTURE:
        snd7_capture_setup((int)((arg[2] >> 31) & 1), (int)((arg[2] >> 30) & 1), arg[0], (int)arg[1],
                           (int)((arg[2] >> 29) & 1), (int)((arg[2] >> 28) & 1), (int)((arg[2] >> 27) & 1));
        break;
    case SETUP_ALARM:
        snd7_alarm_setup((int)arg[0], arg[1], arg[2], (int)arg[3]);
        break;
    case CHANNEL_TIMER:
        each_channel(arg[0], set_timer, arg[1], 0);
        break;
    case CHANNEL_VOLUME:
        each_channel(arg[0], set_volume, arg[1], arg[2]);
        break;
    case CHANNEL_PAN:
        each_channel(arg[0], set_pan, arg[1], 0);
        break;
    case SURROUND_DECAY:
        snd7_hw_set_surround_decay((int)arg[0]);
        break;
    case MASTER_VOLUME:
        snd7_hw_set_master_volume((int)arg[0]);
        break;
    case MASTER_PAN:
        snd7_hw_set_master_pan((int)arg[0]);
        break;
    case OUTPUT_SELECTOR:
        snd7_hw_set_output_selector((int)arg[0], (int)arg[1], (int)arg[2], (int)arg[3]);
        break;
    case LOCK_CHANNEL:
        snd7_channel_lock(arg[0], (int)arg[1]);
        break;
    case UNLOCK_CHANNEL:
        snd7_channel_unlock(arg[0], (int)arg[1]);
        break;
    case STOP_UNLOCKED_CHANNEL:
        snd7_channel_free(arg[0]);
        break;
    case SHARED_WORK:
        snd7_shared_work = arg[0];
        break;
    case INVALIDATE_SEQ:
        snd7_seq_invalidate(arg[0], arg[1]);
        break;
    case INVALIDATE_BANK:
        snd7_seq_invalidate_bank(arg[0], arg[1]);
        break;
    case INVALIDATE_WAVE:
        snd7_channel_invalidate_wave(arg[0], arg[1]);
        break;
    case READ_DRIVER_INFO:
        /* 0x03802520 copies the ARM7's own records (0x1180 bytes from
         * 0x03807d18: channels, players, tracks, alarms) and the channels'
         * CNT to the ARM9; the native driver keeps its channels and alarms
         * in C, not in that layout, and the game never sends this
         * (docs/RUNTIME_MAP.md section 6). */
        snd7_stop("SND_COMMAND_READ_DRIVER_INFO (0x%08x) is not in the native driver", arg[0]);
        break;
    default:
        /* the ARM7's switch takes 0-33 and does nothing for the rest */
        break;
    }
}

/* 0x03801f1c: every list waiting, each command as the ARM9 wrote it, and
 * the finished tag (+0 of the shared work) one up after each list. */
void snd7_command_process(void)
{
    uint32_t list;
    while (take_list(&list)) {
        uint32_t command = list;
        while (command != 0) {
            uint32_t arg[4];
            const uint32_t next = snd7_rd32(command);
            const uint32_t id = snd7_rd32(command + 4);
            for (int i = 0; i < 4; ++i) {
                arg[i] = snd7_rd32(command + 8 + 4u * (uint32_t)i);
            }
            run_command(id, arg);
            command = next;
        }
        if (snd7_shared_work != 0) {
            snd7_shared_write32(0, snd7_shared_read32(0) + 1);
        }
    }
}
