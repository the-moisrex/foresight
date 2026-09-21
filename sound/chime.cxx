// Created by moisrex on 9/20/26.

module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

module fs8.mods;

import :chime;
import :chime_data;

using fs8::chime_synth;
using fs8::chime_voice;

// ---------------------------------------------------------------------------
// Biquad bandpass (constant-gain form)
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

        constexpr void reset() noexcept {
            z1 = 0.0f;
            z2 = 0.0f;
        }

        /// Compute coefficients from center frequency, Q, and sample rate.
        constexpr void configure(float center, float q, float sample_rate) noexcept {
            float const w0    = std::numbers::pi_v<float> * 2.0f * center / sample_rate;
            float const cos_w = std::cos(w0);
            float const sin_w = std::sin(w0);
            float const alpha = sin_w / (2.0f * q);

            // Constant-gain bandpass form (LPF + HPF cascade)
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

    // ---------------------------------------------------------------------------
    // Xorshift32 PRNG
    // ---------------------------------------------------------------------------

    struct [[nodiscard]] xorshift32 {
        uint32_t state;

        constexpr explicit xorshift32(uint32_t seed) noexcept : state{seed ? seed : 1} {}

        constexpr uint32_t next() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        /// Return a float in [-1, 1).
        [[nodiscard]] constexpr float uniform() noexcept {
            // Use 24 bits of mantissa for float precision
            uint32_t const bits = next() & 0x00FF'FFFFu;
            return static_cast<float>(bits) / 8'388'608.0f - 1.0f;
        }
    };

} // anonymous namespace

// ---------------------------------------------------------------------------
// chime_synth::render
// ---------------------------------------------------------------------------

void chime_synth::render(
    uint8_t const keycode,
    bool const pressed,
    uint32_t const sample_rate,
    uint16_t const channels,
    std::span<float> const dest
) const noexcept {
    auto const& v = voice(keycode, pressed);

    if (sample_rate == 0 || channels == 0) [[unlikely]] {
        return;
    }

    auto const frames = dest.size() / static_cast<std::size_t>(channels);
    if (frames == 0) [[unlikely]] {
        return;
    }

    // Pre-computed constants
    float const inv_sr = 1.0f / static_cast<float>(sample_rate);

    // Envelope time constants
    float const tau_fast = v.decay_fast_ms * 0.001f;
    float const tau_slow = v.decay_slow_ms * 0.001f;
    float const tau_fast_inv = tau_fast > 0.0f ? 1.0f / tau_fast : 0.0f;
    float const tau_slow_inv = tau_slow > 0.0f ? 1.0f / tau_slow : 0.0f;

    // Attack boundary in frames
    float const attack_sec  = v.attack_ms * 0.001f;
    float const attack_inv  = attack_sec > 0.0f ? 1.0f / attack_sec : 0.0f;

    // Pitch drift: sweep from center+drift to center over first 30% of sound
    float const drift_frames = static_cast<float>(frames) * 0.3f;
    float const drift_inv    = drift_frames > 0.0f ? 1.0f / drift_frames : 0.0f;

    // Initialize biquad at the drift starting frequency
    biquad_bp filter;
    float const start_freq = v.center_freq + v.pitch_drift;
    filter.configure(start_freq, v.q_factor, static_cast<float>(sample_rate));

    // PRNG seeded from keycode for per-key randomness
    xorshift32 rng{static_cast<uint32_t>(keycode) * 2'654'435'761u + (pressed ? 0x9E37'79B9u : 0u)};

    // Sine phase accumulator for tonal component
    float phase = 0.0f;

    // Accumulate biquad coefficient updates only when pitch is drifting
    float current_freq = start_freq;
    biquad_bp drift_filter;
    drift_filter.configure(current_freq, v.q_factor, static_cast<float>(sample_rate));

    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;

        // --- Pitch drift update ---
        if (static_cast<float>(i) < drift_frames) {
            float const blend = static_cast<float>(i) * drift_inv;
            // Ease-in curve (quadratic) for natural sweep
            float const curved = blend * blend;
            current_freq = start_freq + (v.center_freq - start_freq) * curved;
            drift_filter.configure(current_freq, v.q_factor, static_cast<float>(sample_rate));
        } else if (static_cast<float>(i) == static_cast<std::size_t>(drift_frames)) {
            // Lock to center frequency once drift is complete
            drift_filter.configure(v.center_freq, v.q_factor, static_cast<float>(sample_rate));
        }

        // --- Noise excitation ---
        float const noise = rng.uniform();

        // --- Tonal component ---
        phase += std::numbers::pi_v<float> * 2.0f * current_freq * inv_sr;
        if (phase > std::numbers::pi_v<float> * 2.0f) {
            phase -= std::numbers::pi_v<float> * 2.0f;
        }
        float const tonal = std::sin(phase);

        // --- Mix noise and tonal ---
        float const excitation = (1.0f - v.noise_mix) * tonal + v.noise_mix * noise;

        // --- Biquad filter ---
        float const filtered = drift_filter.tick(excitation);

        // --- Dual-exponential envelope ---
        float envelope;
        if (t < attack_sec) {
            // Linear attack ramp
            envelope = t * attack_inv;
        } else {
            float const d = t - attack_sec;
            envelope = v.decay_fast_mix * std::exp(-d * tau_fast_inv)
                     + (1.0f - v.decay_fast_mix) * std::exp(-d * tau_slow_inv);
        }

        // Clamp envelope
        if (envelope < 0.0f) {
            envelope = 0.0f;
        }

        // --- Final sample ---
        float const sample = filtered * envelope * v.gain;

        // --- Write to all channels (mono duplication) ---
        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = sample;
        }
    }
}
