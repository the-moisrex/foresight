// Created by moisrex on 9/20/26.

module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
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
    auto const& v         = voice(keycode, pressed);
    float const total_ms  = 1.5f + v.ring_ms * 8.0f;
    float const capped_ms = total_ms < 150.0f ? total_ms : 150.0f;
    return static_cast<std::size_t>(static_cast<float>(sample_rate) * capped_ms / 1000.0f);
}

// ---------------------------------------------------------------------------
// Internal DSP helpers
// ---------------------------------------------------------------------------

namespace {

    constexpr float two_pi        = std::numbers::pi_v<float> * 2.0f;
    constexpr float ring_ms_scale = 8.0f;
    constexpr float ms_to_sec     = 0.001f;

    struct [[nodiscard]] biquad_bp {
        float b0 = 0.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;

        float z1 = 0.0f;
        float z2 = 0.0f;

        constexpr void configure(float center, float q, float sr) noexcept {
            float const w0    = two_pi * center / sr;
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

        constexpr explicit xorshift32(uint32_t s) noexcept : state{s ? s : 1u} {}

        constexpr uint32_t next() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        [[nodiscard]] constexpr float uniform() noexcept {
            return static_cast<float>(next() & 0x00FF'FFFFu) / 8'388'608.0f - 1.0f;
        }
    };

    struct [[nodiscard]] pink_noise {
        float    rows[6] = {};
        float    running = 0.0f;
        uint32_t counter = 0;

        [[nodiscard]] constexpr float tick(xorshift32& rng) noexcept {
            ++counter;
            uint32_t idx = 0;
            uint32_t c   = counter;
            while ((c & 1u) == 0u && idx < 5u) {
                c >>= 1u;
                ++idx;
            }
            running   -= rows[idx];
            rows[idx]  = rng.uniform();
            running   += rows[idx];
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
// Noise-dominant synthesis: pink noise colored by two per-key biquad resonances
// matching the measured spring barrel modes. Bright transient (white noise) +
// dark ring (pink noise). HP at 300 Hz + LP at 10 kHz shape the spectrum.
// ---------------------------------------------------------------------------

void bucklespring_synth::render(
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

    auto const& v      = voice(keycode, pressed);
    float const inv_sr = 1.0f / static_cast<float>(sample_rate);
    float const sr     = static_cast<float>(sample_rate);

    float const gain = db_to_linear(v.peak_dbfs);

    float const transient_end = 2.0f * ms_to_sec;

    float const t_contact      = 0.16f * ms_to_sec;
    float const t_buckle       = 0.60f * ms_to_sec;
    float const t_settle       = 1.06f * ms_to_sec;
    float const tc_contact_inv = 1.0f / (0.10f * ms_to_sec);
    float const tc_buckle_inv  = 1.0f / (0.20f * ms_to_sec);
    float const tc_settle_inv  = 1.0f / (0.25f * ms_to_sec);

    float const ring_tau     = v.ring_ms * ring_ms_scale * ms_to_sec / 2.0f;
    float const ring_tau_inv = ring_tau > 0.0f ? 1.0f / ring_tau : 0.0f;

    biquad_bp res1;
    biquad_bp res2;
    float const freq_scale = 0.65f; // shift resonances lower for heavier sound
    res1.configure(v.primary_freq * freq_scale, v.primary_q, sr);
    res2.configure(v.secondary_freq * freq_scale, v.secondary_q, sr);

    xorshift32 rng{static_cast<uint32_t>(keycode) * 2'654'435'761u + (pressed ? 0x9E37'79B9u : 0u)};
    pink_noise pink;

    float const lp_alpha = 62'832.0f / (62'832.0f + sr);
    float       lp_state = 0.0f;

    float const hp_alpha     = 0.9859f; // ~100 Hz cutoff — keeps keyboard body/weight
    float       hp_x_prev    = 0.0f;
    float       hp_y_prev    = 0.0f;

    static constexpr int delay_max            = 794;
    int const            delay_len            = std::min(static_cast<int>(sr * 0.018f), delay_max);
    float                delay_buf[delay_max] = {};
    int                  delay_idx            = 0;
    float const delay_feedback       = 0.40f;

    for (std::size_t i = 0; i < frames; ++i) {
        float const t     = static_cast<float>(i) * inv_sr;
        float const white = rng.uniform();
        float const pn    = pink.tick(rng);

        // Noise through resonances — resonances color the noise, not replace it
        float const raw = 0.05f * white + 0.45f * pn;
        float const r1 = res1.tick(raw);
        float const r2 = res2.tick(raw);
        // Blend: mostly filtered (metallic character) + some raw (warmth/body)
        float const colored = 0.2f * raw + 0.4f * (r1 + r2);

        float sample = 0.0f;

        if (t < transient_end) {
            float env = 0.0f;
            if (t >= t_contact) {
                float const d  = t - t_contact;
                env           += 0.7f * std::exp(-d * tc_contact_inv);
            }
            if (t >= t_buckle) {
                float const d  = t - t_buckle;
                env           += 1.3f * std::exp(-d * tc_buckle_inv);
            }
            if (t >= t_settle) {
                float const d  = t - t_settle;
                env           += 0.55f * std::exp(-d * tc_settle_inv);
            }
            // Transient: bright click + low thump for weight
            float const thump = std::max(0.0f, std::sin(t * 37.699f)) * 0.6f; // ~60 Hz
            sample = (0.4f * white + 0.05f * pn + thump) * env;

        } else {
            float const ring_t = t - transient_end;
            float const env    = std::exp(-ring_t * ring_tau_inv);

            // Ring: colored noise (resonances shape the spectrum)
            float const tonal_env = colored * env;

            int const   read_idx = (delay_idx - delay_len + delay_max) % delay_max;
            float const delayed  = delay_buf[read_idx];

            float const mixed    = tonal_env + delayed * delay_feedback;
            delay_buf[delay_idx] = mixed;
            delay_idx            = (delay_idx + 1) % delay_max;

            lp_state = mixed + (1.0f - lp_alpha) * (lp_state - mixed);

            float const hp_y = lp_state - hp_x_prev + hp_alpha * hp_y_prev;
            hp_x_prev        = lp_state;
            hp_y_prev        = hp_y;
            sample           = hp_y;
        }

        float const out = sample * gain;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = out;
        }
    }
}
