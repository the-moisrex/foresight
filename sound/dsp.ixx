// Shared DSP helpers for the sound profiles.
//
// These were previously duplicated in anonymous namespaces inside
// bucklespring.cxx and chime.cxx; they now live here so every profile
// (and the shared click engine) uses one implementation.

module;
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

export module fs8.sound:dsp;

export namespace fs8::dsp {

    inline constexpr float two_pi    = std::numbers::pi_v<float> * 2.0f;
    inline constexpr float ms_to_sec = 0.001f;

    /// RBJ constant-power bandpass (TDF-II).
    ///
    /// Used by the click engine (bucklespring and friends): peak gain at the
    /// center frequency is unity regardless of Q.
    struct [[nodiscard]] biquad_bp_power {
        constexpr void configure(float const center, float const q, float const sr) noexcept {
            float const w0    = two_pi * center / sr;
            float const sin_w = std::sin(w0);
            float const cos_w = std::cos(w0);
            float const two   = 2.0f;
            float const alpha = sin_w / (two * q);
            float const norm  = 1.0f / (1.0f + alpha);
            b0                = alpha * norm;
            b2                = -(alpha * norm);
            a1                = -((two * cos_w) * norm);
            a2                = (1.0f - alpha) * norm;
        }

        [[nodiscard]] constexpr float tick(float x) noexcept {
            float const y = (b0 * x) + z1;
            z1            = ((b2 * x) - (a1 * y)) + z2;
            z2            = -(a2 * y);
            return y;
        }

      private:
        float b0 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    /// RBJ constant-gain bandpass (LPF + HPF cascade form).
    ///
    /// Used by the chime-style profiles: unity passband gain independent of Q.
    struct [[nodiscard]] biquad_bp_gain {
        constexpr void reset() noexcept {
            z1 = 0.0f;
            z2 = 0.0f;
        }

        /// Compute coefficients from center frequency, Q, and sample rate.
        constexpr void configure(float const center, float const q, float const sample_rate) noexcept {
            float const w0    = two_pi * center / sample_rate;
            float const cos_w = std::cos(w0);
            float const sin_w = std::sin(w0);
            float const two   = 2.0f;
            float const alpha = sin_w / (two * q);

            b0               = alpha;
            b1               = 0.0f;
            b2               = -alpha;
            float const norm = 1.0f / (1.0f + alpha);
            a1               = -((two * cos_w) * norm);
            a2               = (1.0f - alpha) * norm;
        }

        [[nodiscard]] constexpr float tick(float input) noexcept {
            float const y = (b0 * input) + z1;
            z1            = ((b1 * input) - (a1 * y)) + z2;
            z2            = ((b2 * input) - (a2 * y));
            return y;
        }

      private:
        float b0 = 0.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;

        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    /// Deterministic xorshift32 PRNG; seeded per (keycode, pressed) so
    /// renders are reproducible.
    struct [[nodiscard]] xorshift32 {
        constexpr explicit xorshift32(uint32_t const seed) noexcept : state{seed != 0u ? seed : 1u} {}

        constexpr uint32_t next() noexcept {
            state ^= state << shift_a;
            state ^= state >> shift_b;
            state ^= state << shift_c;
            return state;
        }

        /// Return a float in [-1, 1).
        [[nodiscard]] constexpr float uniform() noexcept {
            // Use the 24 bits of mantissa a float can represent exactly.
            uint32_t const bits = next() & mantissa_mask;
            return (static_cast<float>(bits) / mantissa_scale) - 1.0f;
        }

      private:
        static constexpr uint32_t shift_a        = 13u;
        static constexpr uint32_t shift_b        = 17u;
        static constexpr uint32_t shift_c        = 5u;
        static constexpr uint32_t mantissa_mask  = 0x00FF'FFFFu;
        static constexpr float    mantissa_scale = 8'388'608.0f; // 2^23

        uint32_t state;
    };

    /// Voss-McCartney-style pink noise filter driven by a caller-supplied RNG.
    struct [[nodiscard]] pink_noise {
        [[nodiscard]] constexpr float tick(xorshift32& rng) noexcept {
            float const white_mix = 0.5f;
            float const out_gain  = 0.16f;

            ++counter;
            uint32_t idx = 0;
            uint32_t c   = counter;
            while ((c & 1u) == 0u && idx < (rows.size() - 1)) {
                c >>= 1u;
                ++idx;
            }
            running      -= rows.at(idx);
            rows.at(idx)  = rng.uniform();
            running      += rows.at(idx);
            return (running + rows.at(0) + (rng.uniform() * white_mix)) * out_gain;
        }

      private:
        static constexpr std::size_t n_rows = 6;

        std::array<float, n_rows> rows{};
        float                     running = 0.0f;
        uint32_t                  counter = 0;
    };

    /// Convert decibels (relative to full scale) to a linear factor.
    [[nodiscard]] constexpr float db_to_linear(float const db) noexcept {
        float const pow_base  = 10.0f;
        float const db_decade = 20.0f;
        return std::pow(pow_base, db / db_decade);
    }

    /// One-pole DC blocker: `y = x - lowpass(x)`.
    struct [[nodiscard]] dc_blocker {
        constexpr void configure(float const cutoff, float const sr) noexcept {
            float const inv_sr = 1.0f / sr;
            alpha              = 1.0f - std::exp(-two_pi * cutoff * inv_sr);
        }

        [[nodiscard]] constexpr float tick(float x) noexcept {
            state += alpha * (x - state);
            return x - state;
        }

      private:
        float alpha = 0.0f;
        float state = 0.0f;
    };

    /// One-pole low-pass filter.
    struct [[nodiscard]] one_pole_lp {
        constexpr void configure(float const cutoff, float const sr) noexcept {
            float const inv_sr = 1.0f / sr;
            alpha              = 1.0f - std::exp(-two_pi * cutoff * inv_sr);
        }

        [[nodiscard]] constexpr float tick(float x) noexcept {
            state += alpha * (x - state);
            return state;
        }

      private:
        float alpha = 0.0f;
        float state = 0.0f;
    };

} // namespace fs8::dsp
