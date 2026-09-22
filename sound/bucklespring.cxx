// Created by moisrex on 9/20/26.

module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

module fs8.mods;

import :bucklespring;
import :bucklespring_data;
import :bucklespring_attack;

using fs8::attack_entry;
using fs8::attack_sample_rate;
using fs8::bucklespring_params;
using fs8::bucklespring_synth;

// ---------------------------------------------------------------------------
// bucklespring_synth::params / duration_frames
// ---------------------------------------------------------------------------

bucklespring_params const& bucklespring_synth::params(uint8_t const keycode, bool const pressed) const noexcept {
    auto const& table = pressed ? bucklespring_press_params : bucklespring_release_params;
    return table[keycode];
}

std::size_t bucklespring_synth::duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
    auto const& v         = params(keycode, pressed);
    float const total_ms  = v.snap_ms + v.ring_ms * 1.6f + 10.0f; // snap + ring + tail
    float const capped_ms = total_ms < 150.0f ? total_ms : 150.0f;
    return static_cast<std::size_t>(static_cast<float>(sample_rate) * capped_ms / 1000.0f);
}

// ---------------------------------------------------------------------------
// Internal DSP helpers
// ---------------------------------------------------------------------------

namespace {

    constexpr float two_pi    = std::numbers::pi_v<float> * 2.0f;
    constexpr float ms_to_sec = 0.001f;

    // RBJ constant-power bandpass (TDF-II).
    struct [[nodiscard]] biquad_bp {
        float b0 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
        float z1 = 0.0f;
        float z2 = 0.0f;

        constexpr void configure(float center, float q, float sr) noexcept {
            float const w0    = two_pi * center / sr;
            float const sin_w = std::sin(w0);
            float const cos_w = std::cos(w0);
            float const alpha = sin_w / (2.0f * q);
            float const norm  = 1.0f / (1.0f + alpha);
            b0                = alpha * norm;
            b2                = -alpha * norm;
            a1                = -2.0f * cos_w * norm;
            a2                = (1.0f - alpha) * norm;
        }

        [[nodiscard]] constexpr float tick(float x) noexcept {
            float const y = b0 * x + z1;
            z1            = b2 * x - a1 * y + z2;
            z2            = -a2 * y;
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
// bucklespring_synth::render  —  hybrid attack + synthetic tail
//
// Attack: first few ms of the reference WAV (recorded transient).
// Tail:   4 resonators excited by shaped impulse (existing modal synthesis).
// Crossfade: raised-cosine overlap blend between attack and tail.
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

    auto const& v      = params(keycode, pressed);
    float const inv_sr = 1.0f / static_cast<float>(sample_rate);
    float const sr     = static_cast<float>(sample_rate);

    // ------------------------------------------------------------------
    // Attack sample lookup + resampling constants
    // ------------------------------------------------------------------
    auto const& entry          = bucklespring_attack_table[keycode][pressed ? 1 : 0];
    bool const  has_attack     = entry.frames > 0;
    float const resample_ratio = static_cast<float>(attack_sample_rate) / sr;

    // Crossfade: 5 ms raised cosine (Hann window)
    static constexpr float crossfade_ms       = 5.0f;
    float const            crossfade_frames_f = sr * crossfade_ms / 1000.0f;
    float const            attack_end_f       = static_cast<float>(entry.frames) / resample_ratio;

    // ------------------------------------------------------------------
    // 4 resonators — clustered modes create beating / mechanical complexity.
    // Modes 1–2 from measured data; 3–4 derived at 1.3× and 1.7×.
    // ------------------------------------------------------------------
    biquad_bp res1, res2, res3, res4;
    res1.configure(v.primary_freq, 11.0f, sr);
    res2.configure(v.secondary_freq, 10.0f, sr);
    res3.configure(v.primary_freq * 1.3f, 12.0f, sr);
    res4.configure(v.secondary_freq * 1.7f, 10.0f, sr);

    // ------------------------------------------------------------------
    // Noise generators
    // ------------------------------------------------------------------
    xorshift32 rng{static_cast<uint32_t>(keycode) * 2'654'435'761u + (pressed ? 0x9E37'79B9u : 0x85EB'CA6Bu)};
    pink_noise pink;

    // ------------------------------------------------------------------
    // Timing
    // ------------------------------------------------------------------
    float const contact_t  = v.contact_ms * ms_to_sec;
    float const snap_t     = v.snap_ms * ms_to_sec;
    float const attack_end = contact_t + 0.2f * ms_to_sec;

    // ------------------------------------------------------------------
    // Filtering
    // ------------------------------------------------------------------
    float const hp_alpha = 1.0f - std::exp(-two_pi * 200.0f * inv_sr);
    float       hp_state = 0.0f;
    float const lp_alpha = 1.0f - std::exp(-two_pi * 14000.0f * inv_sr);
    float       lp_state = 0.0f;

    float const gain = db_to_linear(v.peak_dbfs);

    // ------------------------------------------------------------------
    // Per-sample loop — hybrid attack + synthetic tail
    // ------------------------------------------------------------------
    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;

        // -- Noise sources ------------------------------------------------
        float const white = rng.uniform();
        float const pn    = pink.tick(rng);

        float const bright_wt = std::exp(-t * 250.0f);
        float const noise     = (0.6f + 0.4f * bright_wt) * white + (0.4f - 0.2f * bright_wt) * pn;

        // -- Asymmetric envelope ------------------------------------------
        float env = 0.0f;
        if (t < attack_end) {
            float const x = (t - contact_t) / (attack_end - contact_t);
            env           = x * x * (3.0f - 2.0f * x);
        } else if (t >= contact_t) {
            env = std::exp(-(t - attack_end) / (v.ring_ms * ms_to_sec));
        }

        float const snap_gauss =
          (t >= snap_t) ? std::exp(-((t - snap_t) / (v.snap_bw_ms * ms_to_sec)) * ((t - snap_t) / (v.snap_bw_ms * ms_to_sec))) : 0.0f;
        env += snap_gauss * 0.6f;

        // -- Excitation + resonators (synthetic tail) ---------------------
        float const impulse = env * 1.8f;

        float const r1 = res1.tick(impulse);
        float const r2 = res2.tick(impulse);
        float const r3 = res3.tick(impulse);
        float const r4 = res4.tick(impulse);

        float const tonal = 0.40f * r1 + 0.35f * r2 + 0.15f * r3 + 0.10f * r4;

        float const mod_index      = 0.3f * bright_wt + 0.05f;
        float const modulated_ring = tonal * (1.0f + noise * mod_index);

        float const click = noise * env * 0.5f;

        float const body_freq = 280.0f + static_cast<float>(keycode & 0x0F) * 15.0f;
        float       body_env  = 0.0f;
        if (t >= contact_t && t < contact_t + 12.0f * ms_to_sec) {
            body_env = std::exp(-(t - contact_t) / (3.0f * ms_to_sec));
        }
        float const body = std::sin(two_pi * body_freq * t) * body_env * 0.25f;

        float tail = modulated_ring + click + body;

        hp_state += hp_alpha * (tail - hp_state);
        tail      = tail - hp_state;
        lp_state += lp_alpha * (tail - lp_state);
        tail      = lp_state;

        // -- Mix attack sample + tail with crossfade ----------------------
        float sample = 0.0f;

        if (has_attack) {
            // Resample attack: linear interpolation from 44100 → sample_rate
            float const src_idx_f = static_cast<float>(i) * resample_ratio;
            auto const  src_idx   = static_cast<std::size_t>(src_idx_f);
            if (src_idx < entry.frames) {
                float const frac = src_idx_f - static_cast<float>(src_idx);
                float       a0   = static_cast<float>(
                  bucklespring_attack_blob[entry.offset + src_idx * 2] | (bucklespring_attack_blob[entry.offset + src_idx * 2 + 1] << 8));
                // int16 → float: reinterpret bits
                auto const i0 = static_cast<int16_t>(
                  bucklespring_attack_blob[entry.offset + src_idx * 2] | (bucklespring_attack_blob[entry.offset + src_idx * 2 + 1] << 8));
                a0 = static_cast<float>(i0) / 32768.0f;

                float a1 = 0.0f;
                if (src_idx + 1 < entry.frames) {
                    auto const i1 = static_cast<int16_t>(bucklespring_attack_blob[entry.offset + (src_idx + 1) * 2]
                                                         | (bucklespring_attack_blob[entry.offset + (src_idx + 1) * 2 + 1] << 8));
                    a1            = static_cast<float>(i1) / 32768.0f;
                } else {
                    a1 = a0;
                }
                float const attack_sample = a0 + frac * (a1 - a0);

                // Crossfade region: raised cosine over 5 ms
                float const fade_pos = attack_end_f - static_cast<float>(i);
                if (fade_pos > crossfade_frames_f) {
                    // Pure attack (before crossfade region)
                    sample = attack_sample;
                } else if (fade_pos > 0.0f) {
                    // Crossfade: attack fades out, tail fades in
                    float const x        = fade_pos / crossfade_frames_f;
                    float const fade_out = 0.5f * (1.0f + std::cos(3.14159265f * (1.0f - x)));
                    float const fade_in  = 1.0f - fade_out;
                    sample               = attack_sample * fade_out + tail * fade_in;
                } else {
                    // Pure tail (after attack ends)
                    sample = tail;
                }
            } else {
                sample = tail;
            }
        } else {
            sample = tail;
        }

        float const out = sample * gain;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            dest[static_cast<std::size_t>(i) * channels + ch] = out;
        }
    }

    // ------------------------------------------------------------------
    // Peak normalization: scan for the actual peak and scale to peak_dbfs.
    // ------------------------------------------------------------------
    float peak = 0.0f;
    for (std::size_t i = 0; i < dest.size(); ++i) {
        float const a = std::abs(dest[i]);
        if (a > peak) {
            peak = a;
        }
    }
    if (peak > 1.0e-9f) {
        float const target = db_to_linear(v.peak_dbfs);
        float const scale  = target / peak;
        for (std::size_t i = 0; i < dest.size(); ++i) {
            dest[i] *= scale;
        }
    }
}
