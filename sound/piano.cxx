// Created by moisrex on 9/25/26.

module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :piano;
import fs8.sound;

using fs8::piano_synth;

namespace {

    // The #322 mapping: f = base * 2^(keycode / 12), base = C1.  The main
    // alpha rows land around E3-C4; clamping keeps unmapped / huge keycodes
    // in an audible, non-piercing range (C2..C6).
    constexpr float base_freq = 32.703f;
    constexpr float min_freq  = 65.0f;   // C2
    constexpr float max_freq  = 1046.5f; // C6

    [[nodiscard]] float pitch_for(uint8_t const keycode) noexcept {
        float const freq = base_freq * std::exp2(static_cast<float>(keycode) / 12.0f);
        return std::clamp(freq, min_freq, max_freq);
    }

    // Additive partials; higher partials are quieter and decay faster
    // (piano-like damping).  Release is duller and shorter (damper falls).
    constexpr std::size_t n_partials = 4;
    constexpr float       press_amp[n_partials]{1.0f, 0.45f, 0.25f, 0.12f};
    constexpr float       press_tau_ms[n_partials]{20.0f, 11.0f, 6.0f, 3.5f};
    constexpr float       release_damp = 0.55f;
    constexpr float       release_tau  = 0.4f;
    constexpr float       press_gain   = 0.42f;
    constexpr float       release_gain = 0.24f;

} // namespace

// ---------------------------------------------------------------------------
// piano_synth::render
// ---------------------------------------------------------------------------

void piano_synth::render(
  uint8_t const          keycode,
  bool const             pressed,
  uint32_t const         sample_rate,
  uint16_t const         channels,
  std::span<float> const dest) const noexcept {
    if (sample_rate == 0 || channels == 0) [[unlikely]] {
        return;
    }
    auto const frames = dest.size() / static_cast<std::size_t>(channels);
    if (frames == 0) [[unlikely]] {
        return;
    }

    float const freq       = pitch_for(keycode);
    float const inv_sr     = 1.0f / static_cast<float>(sample_rate);
    float const gain       = pressed ? press_gain : release_gain;
    float const attack_sec = 0.0015f;
    float const attack_inv = 1.0f / attack_sec;

    // Per-partial decay constants (release: faster + damped).
    float tau[n_partials]{};
    float amp[n_partials]{};
    for (std::size_t h = 0; h < n_partials; ++h) {
        float const scale = pressed ? 1.0f : release_tau;
        tau[h]            = press_tau_ms[h] * 0.001f * scale;
        amp[h]            = press_amp[h] * (pressed ? 1.0f : release_damp);
    }

    constexpr float two_pi = 6.28318530717958647692f;
    float           phase[n_partials]{};
    float           step[n_partials]{};
    for (std::size_t h = 0; h < n_partials; ++h) {
        float const partial_freq = freq * static_cast<float>(h + 1u);
        step[h]                  = two_pi * partial_freq * inv_sr;
    }

    // Deterministic hammer / damper noise transient.
    uint32_t    rng       = static_cast<uint32_t>(keycode) * 2'654'435'761u + (pressed ? 0x9E37'79B9u : 1u);
    float const noise_amp = pressed ? 0.18f : 0.10f;
    float const noise_tau = pressed ? 0.0012f : 0.0016f;

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;

        float tone = 0.0f;
        for (std::size_t h = 0; h < n_partials; ++h) {
            tone     += amp[h] * std::exp(-t / tau[h]) * std::sin(phase[h]);
            phase[h] += step[h];
            if (phase[h] > two_pi) {
                phase[h] -= two_pi;
            }
        }

        float noise = 0.0f;
        if (t < 0.005f) {
            rng              ^= rng << 13u;
            rng              ^= rng >> 17u;
            rng              ^= rng << 5u;
            float const bits  = static_cast<float>(rng & 0x00FF'FFFFu) / 8'388'608.0f; // [-1, 1)
            noise             = bits * noise_amp * std::exp(-t / noise_tau);
        }

        float const envelope = t < attack_sec ? t * attack_inv : 1.0f;
        float const sample   = (tone + noise) * envelope * gain;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = sample;
        }
    }
}
