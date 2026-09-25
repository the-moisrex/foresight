// Created by moisrex on 9/25/26.

module;
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :wavetable;
import fs8.sound;

using fs8::wavetable_synth;
using fs8::dsp::one_pole_lp;

namespace {

    // Pentatonic-clamped pitch (same consonant mapping as the chiptune and
    // marimba palettes): snap each semitone down to the C-minor pentatonic
    // scale, walk up octaves per keyboard row with a wrap so outliers stay
    // in a sane register.  Base = C3.
    constexpr uint8_t pentatonic[12] = {0, 0, 3, 3, 5, 5, 7, 7, 10, 10, 10, 10};
    constexpr float   base_freq      = 130.813f;

    [[nodiscard]] float pitch_for(uint8_t const keycode) noexcept {
        uint32_t const octave    = (static_cast<uint32_t>(keycode) / 12u) % 4u;
        uint32_t const pitch_cls = static_cast<uint32_t>(keycode) % 12u;
        float const    semis     = static_cast<float>(octave * 12u + static_cast<uint32_t>(pentatonic[pitch_cls]));
        return base_freq * std::exp2(semis / 12.0f);
    }

    constexpr std::size_t                                                         wt_size = 64;
    constexpr std::array<std::array<float, wt_size> const*, fs8::wavetable_count> wavetables{
      &fs8::wavetable_triangle,
      &fs8::wavetable_saw,
      &fs8::wavetable_square,
      &fs8::wavetable_organ,
      &fs8::wavetable_voice,
      &fs8::wavetable_bell,
    };

} // namespace

// ---------------------------------------------------------------------------
// wavetable_synth::render
// ---------------------------------------------------------------------------

void wavetable_synth::render(
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

    // Per-keycode table select + pentatonic pitch.
    auto const& table = *wavetables[static_cast<std::size_t>(keycode) % wavetables.size()];
    float const freq  = pitch_for(keycode);

    // Press: full envelope.  Release: shorter, darker (damper-ish).
    float const gain   = pressed ? 0.45f : 0.30f;
    float const tau    = pressed ? 0.020f : 0.008f;
    float const attack = 0.001f;

    one_pole_lp release_lp;
    release_lp.configure(900.0f, static_cast<float>(sample_rate));

    float const inv_sr     = 1.0f / static_cast<float>(sample_rate);
    float const tau_inv    = 1.0f / tau;
    float const attack_inv = 1.0f / attack;

    float phase = 0.0f;

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;

        // Linear interpolation between the two samples bracketing the
        // phase position.
        float const pos   = phase * static_cast<float>(wt_size);
        auto const  index = static_cast<std::size_t>(pos);
        float const frac  = pos - static_cast<float>(index);
        float const next  = table[(index + 1u) % wt_size];
        float const value = table[index] + (next - table[index]) * frac;

        float envelope  = t < attack ? t * attack_inv : 1.0f;
        envelope       *= std::exp(-t * tau_inv);

        float sample = value * envelope * gain;
        if (!pressed) {
            sample = release_lp.tick(sample);
        }

        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = sample;
        }

        phase += freq * inv_sr;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }
}
