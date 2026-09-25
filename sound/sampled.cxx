// Created by moisrex on 9/25/26.

module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

module fs8.mods;

import :sampled;
import fs8.sound;

using fs8::sampled_synth;

void sampled_synth::render(
  uint8_t const    keycode,
  bool const       pressed,
  uint32_t const   sample_rate,
  uint16_t const   channels,
  std::span<float> dest) const noexcept {
    if (sample_rate == 0 || channels == 0 || dest.empty()) [[unlikely]] {
        return;
    }

    auto const& slice = sampled_slices[sample_index(keycode)];
    if (slice.frames == 0) [[unlikely]] {
        return;
    }

    auto const read_s16 = [offset = slice.offset](std::size_t const i) noexcept -> float {
        std::size_t const byte = offset + i * 2u;
        uint16_t const    lo   = sampled_blob[byte];
        uint16_t const    hi   = sampled_blob[byte + 1u];
        return static_cast<float>(static_cast<int16_t>(lo | (hi << 8u))) / 32768.0f;
    };

    std::size_t const n_frames = dest.size() / channels;
    float const       step     = play_rate(keycode) * static_cast<float>(sample_rate) / static_cast<float>(sampled_rate);
    float const       src_end  = pressed ? static_cast<float>(slice.frames) : static_cast<float>(slice.frames) * release_len;

    float lp_state = 0.0f;
    float lp_a     = 0.0f;
    if (!pressed) {
        lp_a = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * release_lp_hz / static_cast<float>(sample_rate));
    }
    std::size_t const fade_n = pressed ? 0u : static_cast<std::size_t>(static_cast<float>(sample_rate) * release_fade_ms / 1000.0f);

    float pos = 0.0f;
    for (std::size_t f = 0; f < n_frames; ++f) {
        float s = 0.0f;
        if (pos < src_end) {
            std::size_t const i0   = std::min(static_cast<std::size_t>(pos), static_cast<std::size_t>(slice.frames) - 1u);
            std::size_t const i1   = std::min(i0 + 1u, static_cast<std::size_t>(slice.frames) - 1u);
            float const       frac = pos - static_cast<float>(i0);
            float const       a    = read_s16(i0);
            float const       b    = read_s16(i1);
            s                      = a + (b - a) * frac;
            if (!pressed) {
                lp_state += lp_a * (s - lp_state);
                s         = lp_state * release_gain;
                if (fade_n > 0 && f + fade_n >= n_frames) {
                    s *= static_cast<float>(n_frames - f) / static_cast<float>(fade_n);
                }
            }
        }
        for (std::size_t c = 0; c < channels; ++c) {
            dest[f * channels + c] = s;
        }
        pos += step;
    }
}
