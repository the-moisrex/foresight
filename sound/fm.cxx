// Created by moisrex on 9/25/26.

module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :fm;
import fs8.sound;

using fs8::fm_synth;

namespace {

    // Base pitch C2: f = base * 2^(keycode / 12), clamped so outliers
    // (media keys, KEY_PAUSE) stay in an audible, non-piercing range.
    constexpr float base_freq = 65.406f;
    constexpr float min_freq  = 98.0f;   // G2
    constexpr float max_freq  = 1568.0f; // G6

    [[nodiscard]] float pitch_for(uint8_t const keycode) noexcept {
        float const freq = base_freq * std::exp2(static_cast<float>(keycode) / 12.0f);
        return std::clamp(freq, min_freq, max_freq);
    }

    struct fm_voice {
        float index;       // modulation index at t = 0
        float index_tau;   // index decay time constant (s)
        float carrier_tau; // carrier envelope decay (s)
        float attack;      // attack ramp (s)
        float gain;
    };

} // namespace

// ---------------------------------------------------------------------------
// fm_synth::render
// ---------------------------------------------------------------------------

void fm_synth::render(
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

    // Press: fast index decay = the tine bite settling into a clean tone.
    // Release: muted, lower index, shorter ring.
    fm_voice const v = pressed ? fm_voice{.index = 3.0f, .index_tau = 0.015f, .carrier_tau = 0.020f, .attack = 0.0015f, .gain = 0.50f}
                               : fm_voice{.index = 1.0f, .index_tau = 0.008f, .carrier_tau = 0.010f, .attack = 0.0010f, .gain = 0.32f};

    float const inv_sr    = 1.0f / static_cast<float>(sample_rate);
    float const carrier_f = pitch_for(keycode);
    float const mod_f     = carrier_f; // ratio 1:1

    // Carson's-rule guard: keep the significant sidebands below 0.45 * sr so
    // even the highest pitch cannot alias.
    float const index_cap = (0.45f * static_cast<float>(sample_rate) - carrier_f) / mod_f;
    float const index0    = v.index < index_cap ? v.index : index_cap;

    constexpr float two_pi       = 6.28318530717958647692f;
    float const     carrier_step = two_pi * carrier_f * inv_sr;
    float const     mod_step     = two_pi * mod_f * inv_sr;
    float const     attack_sec   = v.attack;
    float const     attack_inv   = attack_sec > 0.0f ? 1.0f / attack_sec : 0.0f;
    float const     index_inv    = v.index_tau > 0.0f ? 1.0f / v.index_tau : 0.0f;
    float const     carrier_inv  = v.carrier_tau > 0.0f ? 1.0f / v.carrier_tau : 0.0f;

    float phase_c = 0.0f;
    float phase_m = 0.0f;

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;

        // Decaying modulation index: bright attack into a plain sine.
        float const index = index0 * std::exp(-t * index_inv);
        float const wave  = std::sin(phase_c + index * std::sin(phase_m));

        float envelope;
        if (t < attack_sec) {
            envelope = t * attack_inv;
        } else {
            envelope = std::exp(-(t - attack_sec) * carrier_inv);
        }

        float const sample = wave * envelope * v.gain;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = sample;
        }

        phase_c += carrier_step;
        if (phase_c > two_pi) {
            phase_c -= two_pi;
        }
        phase_m += mod_step;
        if (phase_m > two_pi) {
            phase_m -= two_pi;
        }
    }
}
