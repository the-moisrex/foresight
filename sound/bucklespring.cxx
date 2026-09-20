// Created by moisrex on 9/20/26.

module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :bucklespring;

using fs8::bucklespring_synth;

namespace {

    struct [[nodiscard]] biquad_bp {
        float b0 = 0.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        constexpr void configure(float center, float q, float sr) noexcept {
            float const w0 = 6.283185307179586f * center / sr;
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

        [[nodiscard]] constexpr float tick(float x) noexcept {
            float const y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

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

    // Pink noise via Voss-McCartney: 6 octave generators + high-pass residual.
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

} // anonymous namespace

// ---------------------------------------------------------------------------
// bucklespring_synth::render
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
    float const gain = pressed ? v.press_gain : v.release_gain;

    // Per-key variation from keycode hash
    uint32_t const hash = keycode * 2654435761u;
    float const kv = static_cast<float>(hash & 0x3FFu) / 1024.0f; // [0, 1]
    float const freq_shift = 1.0f + (kv - 0.5f) * 0.1f;          // +/-5%

    // Time boundaries (seconds)
    float const pre_end = v.pre_delay_ms * 0.001f;
    float const transient_end = pre_end + v.transient_ms * 0.001f;

    // Three transient sub-events (contact, buckle, settle)
    float const t_contact = pre_end;
    float const t_buckle  = pre_end + 0.25f * 0.001f; // +0.25ms
    float const t_settle  = pre_end + 1.0f * 0.001f;  // +1.0ms
    // Decay time constants for each sub-event (very fast)
    float const tc_contact = 0.08f * 0.001f; // 0.08ms
    float const tc_buckle  = 0.15f * 0.001f; // 0.15ms
    float const tc_settle  = 0.20f * 0.001f; // 0.20ms
    float const tc_contact_inv = 1.0f / tc_contact;
    float const tc_buckle_inv  = 1.0f / tc_buckle;
    float const tc_settle_inv  = 1.0f / tc_settle;

    // Ring exponential decay
    float const ring_tau = v.ring_ms * 0.001f / 4.0f;
    float const ring_tau_inv = ring_tau > 0.0f ? 1.0f / ring_tau : 0.0f;

    // Dual bandpass filters
    biquad_bp primary;
    primary.configure(v.primary_freq * freq_shift, v.primary_q, static_cast<float>(sample_rate));
    biquad_bp secondary;
    secondary.configure(v.secondary_freq * freq_shift, v.secondary_q, static_cast<float>(sample_rate));

    // Sine phases for tonal ring
    float phase1 = 0.0f;
    float phase2 = 0.0f;

    // PRNG and pink noise
    xorshift32 rng{static_cast<uint32_t>(keycode) * 2654435761u + (pressed ? 0x9E3779B9u : 0u)};
    pink_noise pink;

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;
        float const white = rng.uniform();
        float const pn = pink.tick(rng);

        float sample = 0.0f;

        if (t < pre_end) {
            // --- Phase 1: Pre-delay — very quiet initial contact ---
            sample = pn * 0.015f;

        } else if (t < transient_end) {
            // --- Phase 2: Three-event transient ---
            float env = 0.0f;

            // Event 1: Contact (small, 0.6x)
            if (t >= t_contact) {
                float const d = t - t_contact;
                env += 0.6f * std::exp(-d * tc_contact_inv);
            }
            // Event 2: Buckle (LARGE, 1.0x — the main event)
            if (t >= t_buckle) {
                float const d = t - t_buckle;
                env += 1.0f * std::exp(-d * tc_buckle_inv);
            }
            // Event 3: Settle (medium, 0.45x)
            if (t >= t_settle) {
                float const d = t - t_settle;
                env += 0.45f * std::exp(-d * tc_settle_inv);
            }

            // Transient is broadband noise (white + pink mix for ultrasonic content)
            sample = (0.5f * white + 0.5f * pn) * env;

        } else {
            // --- Phase 3: Ring — dual tonal resonance ---
            float const ring_t = t - transient_end;

            // Tonal: dual sine resonances
            phase1 += 6.283185307179586f * v.primary_freq * freq_shift * inv_sr;
            if (phase1 > 6.283185307179586f) phase1 -= 6.283185307179586f;
            phase2 += 6.283185307179586f * v.secondary_freq * freq_shift * inv_sr;
            if (phase2 > 6.283185307179586f) phase2 -= 6.283185307179586f;

            float const tonal1 = std::sin(phase1);
            float const tonal2 = std::sin(phase2) * v.secondary_level;

            // Bandpass-filtered pink noise for secondary resonance
            float const filtered1 = primary.tick(pn);
            float const filtered2 = secondary.tick(pn);

            // Press: mostly tonal (flatness ~0.45). Release: mostly noise (~0.61).
            float const tonal_mix = pressed ? 0.75f : 0.25f;
            float const tonal = tonal1 + tonal2;
            float const resonant_noise = filtered1 + filtered2 * v.secondary_level;
            float const excitation = tonal_mix * tonal + (1.0f - tonal_mix) * resonant_noise;

            // Exponential decay
            float const env = std::exp(-ring_t * ring_tau_inv);
            sample = excitation * env;
        }

        float const out = sample * gain;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = out;
        }
    }
}
