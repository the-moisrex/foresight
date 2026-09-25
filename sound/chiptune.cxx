// Created by moisrex on 9/25/26.

module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :chiptune;
import fs8.sound;

using fs8::chiptune_synth;
using fs8::dsp::dc_blocker;

namespace {

    // Snap every semitone down to the C-minor-pentatonic scale so random
    // typing never sounds dissonant (a plain `code * n` mapping produces
    // semitone clashes).
    constexpr uint8_t pentatonic[12] = {0, 0, 3, 3, 5, 5, 7, 7, 10, 10, 10, 10};

    // C3.  Keyboard rows walk up octaves; wrapping every 4 octaves keeps
    // the spacebar (row 4) a low blip instead of a shriek.
    constexpr float base_freq = 130.813f;

    [[nodiscard]] float pitch_for(uint8_t const keycode) noexcept {
        uint32_t const octave    = (static_cast<uint32_t>(keycode) / 12u) % 4u;
        uint32_t const pitch_cls = static_cast<uint32_t>(keycode) % 12u;
        float const    semis     = static_cast<float>(octave * 12u + static_cast<uint32_t>(pentatonic[pitch_cls]));
        return base_freq * std::exp2(semis / 12.0f);
    }

    // 12.5 / 25 / 50 % pulse widths, picked per key.
    [[nodiscard]] constexpr float duty_for(uint8_t const keycode) noexcept {
        constexpr float duties[3]{0.125f, 0.25f, 0.5f};
        return duties[static_cast<std::size_t>(keycode) % 3u];
    }

    [[nodiscard]] constexpr uint32_t step_for(float const freq, uint32_t const sample_rate) noexcept {
        return static_cast<uint32_t>(static_cast<double>(freq) / static_cast<double>(sample_rate) * 4294967296.0);
    }

} // namespace

// ---------------------------------------------------------------------------
// chiptune_synth::render
// ---------------------------------------------------------------------------

void chiptune_synth::render(
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

    // Press: pulse channel + a short noise-channel tick (keydown transient).
    // Release: quiet square blip an octave up.
    float const freq      = pressed ? pitch_for(keycode) : pitch_for(keycode) * 2.0f;
    float const duty      = pressed ? duty_for(keycode) : 0.5f;
    float const gain      = pressed ? 0.32f : 0.18f;
    float const tau       = pressed ? 0.013f : 0.006f;
    float const noise_amp = pressed ? 0.28f : 0.0f;

    // Integer phase accumulator (NES-style); the inherent aliasing is the
    // point.
    uint32_t const duty_bits = static_cast<uint32_t>(static_cast<double>(duty) * 4294967296.0);
    uint32_t const step      = step_for(freq, sample_rate);
    uint32_t       phase     = 0;

    // Deterministic per-key noise for the keydown tick.
    uint32_t rng = static_cast<uint32_t>(keycode) * 2'654'435'761u + (pressed ? 0x9E37'79B9u : 1u);

    float const inv_sr  = 1.0f / static_cast<float>(sample_rate);
    float const tau_inv = tau > 0.0f ? 1.0f / tau : 0.0f;

    // Asymmetric duty cycles (12.5 / 25 %) have a DC offset; remove it so
    // it cannot eat the downstream headroom.
    dc_blocker blocker;
    blocker.configure(80.0f, static_cast<float>(sample_rate));

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;

        float const pulse  = phase < duty_bits ? 1.0f : -1.0f;
        phase             += step; // wraps modulo 2^32 by construction

        float noise = 0.0f;
        if (noise_amp > 0.0f && t < 0.006f) {
            rng              ^= rng << 13u;
            rng              ^= rng >> 17u;
            rng              ^= rng << 5u;
            float const bits  = static_cast<float>(rng & 0x00FF'FFFFu) / 8'388'608.0f; // [-1, 1)
            noise             = bits * noise_amp * std::exp(-t / 0.0015f);
        }

        float const envelope = std::exp(-t * tau_inv);
        float const sample   = blocker.tick((pulse * gain + noise) * envelope);
        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = sample;
        }
    }
}
