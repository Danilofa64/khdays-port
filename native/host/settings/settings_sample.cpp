// The settings menu's sample sounds (settings_sample.h).
//
// Each sample is a sound from the game's own archive (snd/sound_data.sdat),
// chosen by name and by the player the archive assigns it:
//   - music: the title's stream Title_BGM_PCM8 (stream player 0, PLAYER_STRM_BGM);
//   - voices: Roxas's rox_warai_S01 (stream player 2, PLAYER_STRM_V01);
//   - effects: the first wave of the system effects' archive SE_SYS_WAVE that
//     plays once and lasts long enough to be heard.
// They are decoded straight from the archive's bytes, not played by the sound
// driver, because the driver belongs to the paused game (native/sound/render.h:
// one driver, one render at a time). A stream plays at the volume its record
// gives it, as in the game, then at the master volume and its kind's volume,
// as the mixer combines them (khdays_sound_bridge_set_volumes).
#include "settings_sample.h"

#include "khdays/assets/audio.h"

extern "C" {
#include "../../runtime/core/rom.h"
#include "../../sound/archive.h"
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

namespace {

constexpr const char* kArchivePath = "/snd/sound_data.sdat";
constexpr float kSampleSeconds = 2.5f;      // how much of a stream is played
constexpr float kShortestEffect = 0.15f;    // effects shorter than this are too short to hear

enum Kind { MUSIC, EFFECTS, VOICES, KINDS };

struct Sample {
    bool tried = false;
    bool ok = false;
    SDL_AudioSpec spec{};
    std::vector<std::int16_t> samples;
    float volume = 1.0f; // the volume the archive gives it
};

KhdaysRomReader* reader;
u32 archive_offset, archive_size;
KhdaysSoundArchive* archive;
Sample samples[KINDS];
SDL_AudioStream* stream;

u32 read_archive(void*, u32 offset, void* destination, u32 size) {
    if (offset >= archive_size) {
        return 0;
    }
    size = std::min(size, archive_size - offset);
    khdays_rom_reader_read(reader, archive_offset + offset, static_cast<u8*>(destination), size);
    return size;
}

bool open_archive() {
    static bool tried;
    if (!tried) {
        tried = true;
        reader = khdays_rom_reader_new();
        if (reader != nullptr && khdays_rom_find(kArchivePath, &archive_offset, &archive_size)) {
            archive = khdays_sound_archive_open(read_archive, nullptr);
        }
    }
    return archive != nullptr;
}

std::vector<std::uint8_t> read_bytes(u32 offset, u32 size) {
    std::vector<std::uint8_t> bytes(size);
    if (size != 0) {
        khdays_sound_archive_read(archive, offset, bytes.data(), size);
    }
    return bytes;
}

u32 le32(const std::uint8_t* p) {
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}

// The number of the sound of `kind` named `name` (its identifier is
// "strm/<name>" or "wave/<name>/0"), or -1.
int find(int kind, const char* name) {
    char want[160], id[160];
    std::snprintf(want, sizeof(want), kind == KHDAYS_SOUND_STREAM ? "strm/%s" : "wave/%s/0", name);
    const int count = khdays_sound_archive_count(archive, kind);
    for (int n = 0; n < count; ++n) {
        if (khdays_sound_archive_identifier(archive, kind, n, 0, id, (int)sizeof(id)) && std::strcmp(id, want) == 0) {
            return n;
        }
    }
    return -1;
}

void keep(Sample& s, const khdays::assets::DecodedAudio& audio, std::size_t frames) {
    frames = std::min(frames, audio.samples.size() / std::max<std::size_t>(1, audio.channels));
    s.spec.format = SDL_AUDIO_S16;
    s.spec.channels = audio.channels;
    s.spec.freq = static_cast<int>(audio.sample_rate);
    s.samples.assign(audio.samples.begin(), audio.samples.begin() + static_cast<long long>(frames * audio.channels));
    s.ok = !s.samples.empty() && s.spec.freq > 0;
}

// Reads and decodes the first seconds of a stream, from the start of its file.
void load_stream(Sample& s, const char* name) {
    const int n = find(KHDAYS_SOUND_STREAM, name);
    const u32 record = n >= 0 ? khdays_sound_archive_record(archive, KHDAYS_SOUND_STREAM, n) : 0;
    u32 offset = 0, size = 0;
    if (record == 0) {
        return;
    }
    // NNSSndArcStrmInfo: the file (its low 24 bits), the volume, ...
    const std::vector<std::uint8_t> info = read_bytes(record, 8);
    if (!khdays_sound_archive_file(archive, le32(info.data()) & 0xffffff, &offset, &size) || size < 0x68) {
        return;
    }
    const std::vector<std::uint8_t> head = read_bytes(offset, 0x68);
    const u32 channels = std::max<u32>(1, head[0x1a] | head[0x1b] << 8);
    const u32 rate = head[0x1c] | head[0x1d] << 8;
    const u32 data = le32(&head[0x28]), block = le32(&head[0x30]), block_samples = le32(&head[0x34]);
    if (rate == 0 || block_samples == 0) {
        return;
    }
    const u32 blocks = static_cast<u32>(kSampleSeconds * (float)rate / (float)block_samples) + 1;
    const u32 want = std::min(size, data + blocks * channels * block);
    const std::vector<std::uint8_t> bytes = read_bytes(offset, want);
    keep(s, khdays::assets::decode_strm(bytes.data(), bytes.size()), static_cast<std::size_t>(kSampleSeconds * (float)rate));
    s.volume = static_cast<float>(info[4]) / 127.0f;
}

// The first wave of a wave archive that plays once and is long enough to hear.
void load_effect(Sample& s, const char* name) {
    const int n = find(KHDAYS_SOUND_WAVE, name);
    const u32 record = n >= 0 ? khdays_sound_archive_record(archive, KHDAYS_SOUND_WAVE, n) : 0;
    u32 offset = 0, size = 0;
    if (record == 0) {
        return;
    }
    const std::vector<std::uint8_t> info = read_bytes(record, 4); // NNSSndArcWaveArcInfo: the file
    if (!khdays_sound_archive_file(archive, le32(info.data()) & 0xffffff, &offset, &size) || size < 0x3c) {
        return;
    }
    const std::vector<std::uint8_t> swar = read_bytes(offset, size);
    const u32 count = le32(&swar[0x38]);
    for (u32 w = 0; w < count && 0x3c + 4 * (w + 1) <= swar.size(); ++w) {
        const u32 start = le32(&swar[0x3c + 4 * w]);
        const u32 end = w + 1 < count && 0x3c + 4 * (w + 2) <= swar.size() ? le32(&swar[0x3c + 4 * (w + 1)]) : size;
        if (start >= end || end > swar.size()) {
            continue;
        }
        const khdays::assets::DecodedAudio audio = khdays::assets::decode_swav(&swar[start], end - start);
        const float seconds = audio.sample_rate != 0 ? (float)audio.samples.size() / (float)audio.channels /
                                                           (float)audio.sample_rate
                                                     : 0.0f;
        if (!audio.loops && seconds >= kShortestEffect) {
            keep(s, audio, audio.samples.size());
            return;
        }
    }
}

Sample* sample_of(int kind) {
    Sample& s = samples[kind];
    if (!s.tried) {
        s.tried = true;
        try {
            if (open_archive()) {
                switch (kind) {
                case MUSIC: load_stream(s, "Title_BGM_PCM8"); break;
                case VOICES: load_stream(s, "rox_warai_S01"); break;
                default: load_effect(s, "SE_SYS_WAVE"); break;
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "settings: no sample for a volume: %s\n", e.what());
            s.ok = false;
        }
        static const char* const names[KINDS] = {"music", "effects", "voices"};
        if (s.ok) {
            std::fprintf(stderr, "settings: the %s volume's sample: %.2f s at %d Hz, %d channel(s)\n", names[kind],
                         (double)s.samples.size() / s.spec.channels / s.spec.freq, s.spec.freq, s.spec.channels);
        } else {
            std::fprintf(stderr, "settings: the sound archive gave no sample for the %s volume\n", names[kind]);
        }
    }
    return s.ok ? &s : nullptr;
}

void play(int kind, float gain) {
    const Sample* s = sample_of(kind);
    if (s == nullptr) {
        return;
    }
    if (stream == nullptr) {
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &s->spec, nullptr, nullptr);
        if (stream == nullptr) {
            std::fprintf(stderr, "settings: no stream for the volumes' samples: %s\n", SDL_GetError());
            return;
        }
    }
    SDL_ClearAudioStream(stream);
    SDL_SetAudioStreamFormat(stream, &s->spec, nullptr);
    SDL_SetAudioStreamGain(stream, gain * s->volume);
    SDL_PutAudioStreamData(stream, s->samples.data(), static_cast<int>(s->samples.size() * sizeof(std::int16_t)));
    SDL_ResumeAudioStreamDevice(stream);
}

} // namespace

extern "C" void khdays_settings_sample(const KhdaysSound* before, const KhdaysSound* now) {
    const float master = static_cast<float>(now->volume) / 100.0f;
    if (now->music != before->music) {
        play(MUSIC, master * static_cast<float>(now->music) / 100.0f);
    } else if (now->voices != before->voices) {
        play(VOICES, master * static_cast<float>(now->voices) / 100.0f);
    } else if (now->effects != before->effects || now->volume != before->volume) {
        play(EFFECTS, master * static_cast<float>(now->effects) / 100.0f);
    }
    // the movies volume: there is no movie sound to play here (khdays_settings_sample_has_movies)
}

extern "C" void khdays_settings_sample_stop(void) {
    if (stream != nullptr) {
        SDL_ClearAudioStream(stream);
        SDL_PauseAudioStreamDevice(stream);
    }
}

extern "C" int khdays_settings_sample_has_movies(void) {
    return 0; // playing an HD video's sound as the sample is not built
}
