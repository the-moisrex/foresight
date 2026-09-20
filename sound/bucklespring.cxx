// Created by moisrex on 9/20/26.

module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :bucklespring;

using fs8::bucklespring_synth;

// ---------------------------------------------------------------------------
// Biquad bandpass (constant-gain form) — the spring resonator
// ---------------------------------------------------------------------------

namespace {

    struct [[nodiscard]] biquad_bp {
        float b0 = 0.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;

        float z1 = 0.0f;
        float z2 = 0.0f;

        constexpr void configure(float center, float q, float sample_rate) noexcept {
            float const w0    = 6.283185307179586f * center / sample_rate;
            float const cos_w = std::cos(w0);
            float const sin_w = std::sin(w0);
            float const alpha = sin_w / (2.0f * q);

            b0 = alpha;
            b1 = 0.0f;
            b2 = -alpha;
            float const norm = 1.0f / (1.0f + alpha);
            a1 = -2.0f * cos_w * norm;
            a2 = (1.0f - alpha) * norm;
        }

        [[nodiscard]] constexpr float tick(float input) noexcept {
            float const y = b0 * input + z1;
            z1 = b1 * input - a1 * y + z2;
            z2 = b2 * input - a2 * y;
            return y;
        }
    };

    struct [[nodiscard]] xorshift32 {
        uint32_t state;

        constexpr explicit xorshift32(uint32_t seed) noexcept : state{seed ? seed : 1} {}

        constexpr uint32_t next() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        [[nodiscard]] constexpr float uniform() noexcept {
            uint32_t const bits = next() & 0x00FFFFFFu;
            return static_cast<float>(bits) / 8388608.0f - 1.0f;
        }
    };

} // anonymous namespace

// ---------------------------------------------------------------------------
// bucklespring_synth::render — IBM Model M bucklespring synthesis
//
// Three-phase model based on WAV analysis:
//
//   Phase 1 — Pre-delay (0 to pre_delay_ms):
//     Very quiet noise. This is the initial key contact before the spring
//     buckles. ~-25 dBFS in the real recordings.
//
//   Phase 2 — Click (pre_delay_ms to pre_delay_ms + click_ms):
//     Sharp spike: linear ramp up to peak, then fast exponential decay.
//     This is the spring buckling event. Mostly broadband noise.
//
//   Phase 3 — Ring (pre_delay_ms + click_ms onward):
//     Tonal resonance at the spring's natural frequency (700-4000 Hz).
//     Sine through a biquad bandpass. Exponential decay over ring_ms.
//     Presses are 80% tonal (flatness ~0.45), releases 30% tonal (~0.61).
// ---------------------------------------------------------------------------

void bucklespring_synth::render(
    uint8_t const keycode,
    bool const pressed,
    uint32_t const sample_rate,
    uint16_t const channels,
    std::span<float> const dest
) const noexcept {
    if (sample_rate == 0 || channels == 0) [[unlikely]] {
        return;
    }

    auto const frames = dest.size() / static_cast<std::size_t>(channels);
    if (frames == 0) [[unlikely]] {
        return;
    }

    auto const v = get_voice(keycode, pressed);
    float const inv_sr = 1.0f / static_cast<float>(sample_rate);

    // Time boundaries
    float const pre_delay_sec = v.pre_delay_ms * 0.001f;
    float const click_start_sec = pre_delay_sec;
    float const click_end_sec = pre_delay_sec + v.click_ms * 0.001f;
    float const ring_start_sec = click_end_sec;

    // Click envelope: ramp to peak in first half, fast decay in second half
    float const click_peak_sec = click_start_sec + v.click_ms * 0.0005f; // first 0.05ms
    float const click_decay_sec = v.click_ms * 0.001f * 0.5f;           // 50% of click_ms
    float const click_decay_inv = click_decay_sec > 0.0f ? 1.0f / click_decay_sec : 0.0f;

    // Ring envelope
    float const ring_tau = v.ring_ms * 0.001f / 4.0f; // 4 time constants
    float const ring_tau_inv = ring_tau > 0.0f ? 1.0f / ring_tau : 0.0f;

    // Bandpass filter for spring resonance
    biquad_bp filter;
    filter.configure(v.ring_freq, v.ring_q, static_cast<float>(sample_rate));

    // PRNG
    xorshift32 rng{static_cast<uint32_t>(keycode) * 2654435761u + (pressed ? 0x9E3779B9u : 0u)};

    // Sine phase for tonal ring
    float phase = 0.0f;

    float const gain = pressed ? v.press_gain : v.release_gain;

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;
        float const noise = rng.uniform();

        float sample = 0.0f;

        if (t < click_start_sec) {
            // --- Phase 1: Pre-delay — quiet initial contact ---
            sample = noise * 0.02f;

        } else if (t < click_end_sec) {
            // --- Phase 2: Click — sharp spike with fast decay ---
            float click_env;
            if (t < click_peak_sec) {
                // Ramp to peak
                float const ramp = (t - click_start_sec) / (click_peak_sec - click_start_sec);
                click_env = ramp;
            } else {
                // Fast exponential decay
                float const d = t - click_peak_sec;
                click_env = std::exp(-d * click_decay_inv);
            }
            // Click is mostly noise
            sample = noise * click_env * 0.8f;

        } else {
            // --- Phase 3: Ring — tonal spring resonance ---
            float const ring_t = t - ring_start_sec;

            // Sine at spring frequency
            phase += 6.283185307179586f * v.ring_freq * inv_sr;
            if (phase > 6.283185307179586f) {
                phase -= 6.283185307179586f;
            }
            float const tonal = std::sin(phase);

            // Bandpass-filtered noise
            float const filtered_noise = filter.tick(noise);

            // Mix tonal and noise based on press/release
            float const excitation = v.tonal_mix * tonal + (1.0f - v.tonal_mix) * filtered_noise;

            // Exponential decay envelope
            float const ring_env = std::exp(-ring_t * ring_tau_inv);

            sample = excitation * ring_env;
        }

        // Apply gain and write to all channels
        float const final_sample = sample * gain;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = final_sample;
        }
    }
}
