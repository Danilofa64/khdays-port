#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace khdays::assets {

// Neutral decoded audio: interleaved signed 16-bit PCM. The engine and the
// platform audio backend depend only on this, never on the DS SWAV format.
struct DecodedAudio final {
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 1;
    std::vector<std::int16_t> samples;  // interleaved
    bool loops = false;
    std::uint32_t loop_start = 0;  // loop point, in sample frames
};

// Decode one SWAV waveform to PCM16. `swav` points at the SWAV header
// (u8 format [0=PCM8, 1=PCM16, 2=IMA-ADPCM]; u8 loop; u16 rate; u16 time;
// u16 loop_start; u32 non_loop_len; then sample data). Throws on malformed data.
DecodedAudio decode_swav(const std::uint8_t* swav, std::size_t size);

// Decode one STRM file's bytes. A prefix of the file decodes the blocks it
// holds (the settings menu's samples read only the first seconds).
DecodedAudio decode_strm(const std::uint8_t* strm, std::size_t size);

// Load a canonical PCM WAV file (8- or 16-bit, mono or stereo) into neutral
// PCM16. Used by the modding layer to replace a game waveform. Throws
// std::runtime_error on a non-PCM or malformed file.
DecodedAudio load_wav(const std::filesystem::path& path);

}  // namespace khdays::assets
