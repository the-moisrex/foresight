// Created by moisrex on 9/20/26.

module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :bucklespring;
import :bucklespring_data;

using fs8::bucklespring_synth;
using fs8::bucklespring_voice;

// ---------------------------------------------------------------------------
// bucklespring_synth::voice / duration_frames
// ---------------------------------------------------------------------------

bucklespring_voice const& bucklespring_synth::voice(uint8_t const keycode, bool const pressed) const noexcept {
    auto const& table = pressed ? bucklespring_press_params : bucklespring_release_params;
    return table[keycode];
}

std::size_t bucklespring_synth::duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
    auto const& v = voice(keycode, pressed);
    float const total_ms = 1.5f + v.ring_ms;
    float const capped_ms = total_ms < 150.0f ? total_ms : 150.0f;
    return static_cast<std::size_t>(static_cast<float>(sample_rate) * capped_ms / 1000.0f);
}

// ---------------------------------------------------------------------------
// Internal DSP helpers
// ---------------------------------------------------------------------------

namespace {

    struct [[nodiscard]] xorshift32 {
        uint32_t state;
        constexpr explicit xorshift32(uint32_t s) noexcept : state{s ? s : 1u} {}
        constexpr uint32_t next() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
        [[nodiscard]] constexpr float uniform() noexcept {
            return static_cast<float>(next() & 0x00FFFFFFu) / 8388608.0f - 1.0f;
        }
    };

    struct [[nodiscard]] pink_noise {
        float rows[6] = {};
        float running = 0.0f;
        uint32_t counter = 0;

        [[nodiscard]] constexpr float tick(xorshift32& rng) noexcept {
            ++counter;
            uint32_t idx = 0;
            uint32_t c = counter;
            while ((c & 1u) == 0u && idx < 5u) {
                c >>= 1u;
                ++idx;
            }
            running -= rows[idx];
            rows[idx] = rng.uniform();
            running += rows[idx];
            return (running + rows[0] + rng.uniform() * 0.5f) * 0.16f;
        }
    };

    [[nodiscard]] constexpr float db_to_linear(float db) noexcept {
        return std::pow(10.0f, db / 20.0f);
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// bucklespring_synth::render
//
// Additive sine synthesis: 3 inharmonic partials with independent exponential
// decay + broadband noise click. No bandpass filter (avoids pitched output).
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

    auto const& v = voice(keycode, pressed);
    float const inv_sr = 1.0f / static_cast<float>(sample_rate);

    // Gain from per-key peak amplitude
    float const gain = db_to_linear(v.peak_dbfs);

    // Per-key slight frequency variation (different keycap sizes resonate differently)
    uint32_t const hash = keycode * 2654435761u;
    float const kv = static_cast<float>(hash & 0x3FFu) / 1024.0f;
    float const freq_shift = 1.0f + (kv - 0.5f) * 0.06f;

    // Time boundaries (seconds)
    float const transient_end = 1.5f * 0.001f;

    // Three transient sub-events (contact, buckle, settle)
    float const t_contact = 0.16f * 0.001f;
    float const t_buckle  = 0.60f * 0.001f;
    float const t_settle  = 1.06f * 0.001f;
    float const tc_contact_inv = 1.0f / (0.08f * 0.001f);
    float const tc_buckle_inv  = 1.0f / (0.15f * 0.001f);
    float const tc_settle_inv  = 1.0f / (0.20f * 0.001f);

    // Ring exponential decay
    float const ring_tau = v.ring_ms * 0.001f / 4.0f;
    float const ring_tau_inv = ring_tau > 0.0f ? 1.0f / ring_tau : 0.0f;

    // --- Additive sine oscillators (3 partials) ---
    // Frequencies: primary, secondary, and a detuned third at ~1.4x primary
    // Each gets a random initial phase for inharmonic character
    float const f1 = v.primary_freq * freq_shift;
    float const f2 = v.secondary_freq * freq_shift;
    float const f3 = f1 * 1.414f;  // sqrt(2) ratio — inharmonic

    float phase1 = kv * 6.283185307179586f;               // random initial phase
    float phase2 = (1.0f - kv) * 6.283185307179586f;
    float phase3 = (kv * 0.7f + 0.3f) * 6.283185307179586f;

    float const phase_inc1 = 6.283185307179586f * f1 * inv_sr;
    float const phase_inc2 = 6.283185307179586f * f2 * inv_sr;
    float const phase_inc3 = 6.283185307179586f * f3 * inv_sr;

    // Relative amplitudes of the 3 partials (primary strongest)
    float const a1 = 0.4f;
    float const a2 = 0.25f;
    float const a3 = 0.15f;

    // Noise component for the mechanical click
    float const noise_level = pressed ? 0.20f : 0.25f;

    // PRNG and noise
    xorshift32 rng{static_cast<uint32_t>(keycode) * 2654435761u + (pressed ? 0x9E3779B9u : 0u)};
    pink_noise pink;

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;
        float const white = rng.uniform();
        float const pn = pink.tick(rng);

        float sample = 0.0f;

        if (t < transient_end) {
            // --- Phase 1: Transient (3 sub-events) ---
            // Mostly broadband noise — the sharp mechanical click
            float env = 0.0f;
            if (t >= t_contact) {
                float const d = t - t_contact;
                env += 0.6f * std::exp(-d * tc_contact_inv);
            }
            if (t >= t_buckle) {
                float const d = t - t_buckle;
                env += 1.0f * std::exp(-d * tc_buckle_inv);
            }
            if (t >= t_settle) {
                float const d = t - t_settle;
                env += 0.45f * std::exp(-d * tc_settle_inv);
            }
            sample = (0.7f * white + 0.3f * pn) * env;

        } else {
            // --- Phase 2: Ring (additive sines + noise) ---
            float const ring_t = t - transient_end;
            float const env = std::exp(-ring_t * ring_tau_inv);

            // Update phases
            phase1 += phase_inc1;
            phase2 += phase_inc2;
            phase3 += phase_inc3;
            if (phase1 > 6.283185307179586f) phase1 -= 6.283185307179586f;
            if (phase2 > 6.283185307179586f) phase2 -= 6.283185307179586f;
            if (phase3 > 6.283185307179586f) phase3 -= 6.283185307179586f;

            // Additive partials — inharmonic frequencies = metallic character
            float const tonal = a1 * std::sin(phase1)
                              + a2 * std::sin(phase2)
                              + a3 * std::sin(phase3);

            // Mix: sines for metallic ring, noise for mechanical texture
            sample = (tonal + noise_level * pn) * env;
        }

        float const out = sample * gain;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = out;
        }
    }
}
