// Created by moisrex on 9/25/26.

module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

module fs8.mods;

import :marimba;
import fs8.sound;

using fs8::marimba_synth;
using fs8::dsp::biquad_bp_gain;
using fs8::dsp::xorshift32;

namespace {

    // Pentatonic-clamped pitch: snap each semitone down to the C-minor
    // pentatonic scale, walk up octaves per keyboard row (wrapping so the
    // spacebar stays a low bar instead of a shriek).  Base = C3, which
    // keeps the formula inside the marimba's real range.
    constexpr uint8_t pentatonic[12] = {0, 0, 3, 3, 5, 5, 7, 7, 10, 10, 10, 10};
    constexpr float   base_freq      = 130.813f;

    [[nodiscard]] float pitch_for(uint8_t const keycode) noexcept {
        uint32_t const octave    = (static_cast<uint32_t>(keycode) / 12u) % 4u;
        uint32_t const pitch_cls = static_cast<uint32_t>(keycode) % 12u;
        float const    semis     = static_cast<float>(octave * 12u + static_cast<uint32_t>(pentatonic[pitch_cls]));
        return base_freq * std::exp2(semis / 12.0f);
    }

    // Bar modes: fundamental, major third partial, tenth partial.
    constexpr std::size_t n_modes = 3;
    constexpr float       mode_ratio[n_modes]{1.0f, 3.9f, 10.8f};
    constexpr float       mode_gain[n_modes]{0.55f, 0.30f, 0.16f};
    constexpr float       mode_tau_ms_press[n_modes]{20.0f, 8.0f, 4.0f};
    constexpr float       release_tau_scale  = 0.4f;
    constexpr float       release_gain_scale = 0.45f;

} // namespace

// ---------------------------------------------------------------------------
// marimba_synth::render
// ---------------------------------------------------------------------------

void marimba_synth::render(
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

    float const freq   = pitch_for(keycode);
    float const sr     = static_cast<float>(sample_rate);
    float const nyq_hi = 0.45f * sr;
    float const gain   = 0.80f * (pressed ? 1.0f : release_gain_scale);

    // Configure one constant-gain bandpass per audible mode.  Q follows the
    // wanted amplitude decay: tau = Q / (pi * f)  <=>  Q = tau * pi * f.
    biquad_bp_gain filters[n_modes]{};
    bool           active[n_modes]{};
    for (std::size_t m = 0; m < n_modes; ++m) {
        float const mode_f = freq * mode_ratio[m];
        if (mode_f > nyq_hi) {
            continue;
        }
        float const tau = mode_tau_ms_press[m] * 0.001f * (pressed ? 1.0f : release_tau_scale);
        float const q   = std::clamp(tau * std::numbers::pi_v<float> * mode_f, 2.0f, 60.0f);
        filters[m].configure(mode_f, q, sr);
        active[m] = true;
    }

    // Mallet noise burst: short and soft on press, much quieter on release.
    float const noise_amp = pressed ? 0.30f : 0.10f;

    xorshift32 rng{static_cast<uint32_t>(keycode) * 2'654'435'761u + (pressed ? 0x9E37'79B9u : 1u)};

    float const inv_sr = 1.0f / sr;

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;

        float excite = 0.0f;
        if (t < 0.006f) {
            excite = rng.uniform() * noise_amp * std::exp(-t / 0.002f);
        }

        float bar = 0.0f;
        for (std::size_t m = 0; m < n_modes; ++m) {
            if (active[m]) {
                bar += mode_gain[m] * filters[m].tick(excite);
            }
        }

        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = bar;
        }
    }

    // The bandpasses are extremely narrow (Q up to 60), so the noise burst
    // comes out quiet and by an amount that varies with the key's Q —
    // peak-normalize per render to a fixed target instead of guessing a
    // fixed boost.  Press and release keep their loudness ratio through
    // the different targets.
    float peak = 0.0f;
    for (std::size_t i = 0; i < dest.size(); ++i) {
        float const magnitude = std::fabs(dest[i]);
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    if (peak > 0.0f) {
        float const target = 0.50f * gain;
        float const scale  = target / peak;
        for (std::size_t i = 0; i < dest.size(); ++i) {
            dest[i] *= scale;
        }
    }
}
