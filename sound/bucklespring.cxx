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
import fs8.sound;
import :bucklespring_attack;

using fs8::attack_entry;
using fs8::attack_sample_rate;
using fs8::bucklespring_synth;
using fs8::click_params;
using fs8::detail::click_attack;
using fs8::detail::click_duration_frames;
using fs8::detail::click_engine_opts;
using fs8::detail::render_click;
using fs8::dsp::biquad_bp_power;
using fs8::dsp::db_to_linear;
using fs8::dsp::dc_blocker;
using fs8::dsp::ms_to_sec;
using fs8::dsp::one_pole_lp;
using fs8::dsp::pink_noise;
using fs8::dsp::two_pi;
using fs8::dsp::xorshift32;

// ---------------------------------------------------------------------------
// bucklespring_synth::params / duration_frames
// ---------------------------------------------------------------------------

click_params const& bucklespring_synth::params(uint8_t const keycode, bool const pressed) const noexcept {
    auto const& table = pressed ? bucklespring_press_params : bucklespring_release_params;
    return table[keycode];
}

std::size_t bucklespring_synth::duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
    return click_duration_frames(params(keycode, pressed), sample_rate);
}

// ---------------------------------------------------------------------------
// Shared click engine
// ---------------------------------------------------------------------------

std::size_t fs8::detail::click_duration_frames(click_params const& v, uint32_t const sample_rate) noexcept {
    // ~7 decay time-constants after the snap (the envelope starts at
    // contact_ms + 0.2 ms), so the ring dies out at ≈ -60 dB naturally
    // instead of being truncated while still audible.
    float const total_ms  = v.snap_ms + v.ring_ms * 7.0f + v.contact_ms + 1.0f;
    float const capped_ms = total_ms < 150.0f ? total_ms : 150.0f;
    return static_cast<std::size_t>(static_cast<float>(sample_rate) * capped_ms / 1000.0f);
}

// ---------------------------------------------------------------------------
// render_click  —  hybrid attack + synthetic tail
//
// Attack: first few ms of a reference WAV (recorded transient), if supplied.
// Tail:   4 resonators excited by shaped impulse (modal synthesis).
// Crossfade: raised-cosine overlap blend between attack and tail.
// ---------------------------------------------------------------------------

void fs8::detail::render_click(
  click_params const&      v,
  click_engine_opts const& opts,
  click_attack const&      attack,
  uint8_t const            keycode,
  bool const               pressed,
  uint32_t const           sample_rate,
  uint16_t const           channels,
  std::span<float> const   dest) noexcept {
    if (sample_rate == 0 || channels == 0) [[unlikely]] {
        return;
    }
    auto const frames = dest.size() / static_cast<std::size_t>(channels);
    if (frames == 0) [[unlikely]] {
        return;
    }

    float const inv_sr = 1.0f / static_cast<float>(sample_rate);
    float const sr     = static_cast<float>(sample_rate);

    // ------------------------------------------------------------------
    // Attack sample constants
    // ------------------------------------------------------------------
    bool const  has_attack         = attack.frames > 0;
    float const resample_ratio     = static_cast<float>(attack.sample_rate) / sr;
    float const crossfade_frames_f = sr * opts.crossfade_ms / 1000.0f;
    float const attack_end_f       = static_cast<float>(attack.frames) / resample_ratio;

    // ------------------------------------------------------------------
    // 4 resonators — clustered modes create beating / mechanical complexity.
    // Modes 1–2 from the parameter table; 3–4 derived at opts.ratio_a/b.
    // ------------------------------------------------------------------
    biquad_bp_power res1, res2, res3, res4;
    auto const      table_q = [&opts](float const q) {
        return std::clamp(q, 3.0f, opts.q_max);
    };
    res1.configure(v.primary_freq, opts.q_from_table ? table_q(v.primary_q) : opts.q1, sr);
    res2.configure(v.secondary_freq, opts.q_from_table ? table_q(v.secondary_q) : opts.q2, sr);
    res3.configure(v.primary_freq * opts.ratio_a, opts.q3, sr);
    res4.configure(v.secondary_freq * opts.ratio_b, opts.q4, sr);

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
    dc_blocker hp;
    hp.configure(opts.hp_hz, sr);
    one_pole_lp lp;
    lp.configure(opts.lp_hz, sr);

    float const gain = db_to_linear(v.peak_dbfs);

    // ------------------------------------------------------------------
    // Per-sample loop — hybrid attack + synthetic tail
    // ------------------------------------------------------------------
    for (std::size_t i = 0; i < frames; ++i) {
        float const t = static_cast<float>(i) * inv_sr;

        // -- Noise sources ------------------------------------------------
        float const white = rng.uniform();
        float const pn    = pink.tick(rng);

        float const bright_wt = std::exp(-t * opts.bright_decay);
        float const noise =
          (opts.noise_white_base + opts.noise_white_bright * bright_wt)
          * white
          + (opts.noise_pink_base - opts.noise_pink_bright * bright_wt)
          * pn;

        // -- Asymmetric envelope ------------------------------------------
        // contact_ms is a pre-delay: silence until the contact, then a fast
        // smoothstep attack into the ring.  Sub-millisecond contacts (the
        // Model M's measured 0.19 ms) keep the historical smoothstep across
        // the whole window; a genuine pre-delay (> 1 ms) must be silent
        // beforehand — the unclamped smoothstep would evaluate its rising
        // branch far to the left of t = contact and explode.
        float env = 0.0f;
        if (contact_t > 1.0f * ms_to_sec && t < contact_t) {
            env = 0.0f;
        } else if (t < attack_end) {
            float const x = (t - contact_t) / (attack_end - contact_t);
            env           = x * x * (3.0f - 2.0f * x);
        } else {
            env = std::exp(-(t - attack_end) / (v.ring_ms * ms_to_sec));
        }

        float const snap_gauss =
          (t >= snap_t) ? std::exp(-((t - snap_t) / (v.snap_bw_ms * ms_to_sec)) * ((t - snap_t) / (v.snap_bw_ms * ms_to_sec))) : 0.0f;
        env += snap_gauss * opts.snap_gain;

        // -- Excitation + resonators (synthetic tail) ---------------------
        float const impulse = env * opts.impulse_gain;

        float const r1 = res1.tick(impulse);
        float const r2 = res2.tick(impulse);
        float const r3 = res3.tick(impulse);
        float const r4 = res4.tick(impulse);

        float const tonal = opts.w1 * r1 + opts.w2 * r2 + opts.w3 * r3 + opts.w4 * r4;

        float const mod_index      = opts.ring_mod_depth * bright_wt + opts.ring_mod_floor;
        float const modulated_ring = tonal * (1.0f + noise * mod_index);

        float const click = noise * env * opts.click_gain;

        float const body_freq = opts.body_base + static_cast<float>(keycode & 0x0F) * opts.body_step;
        float       body_env  = 0.0f;
        if (t >= contact_t && t < contact_t + opts.body_window_ms * ms_to_sec) {
            body_env = std::exp(-(t - contact_t) / (opts.body_tau_ms * ms_to_sec));
        }
        float const body = std::sin(two_pi * body_freq * t) * body_env * opts.body_gain;

        float tail = modulated_ring + click + body;

        tail = hp.tick(tail);
        tail = lp.tick(tail);

        // -- Mix attack sample + tail with crossfade ----------------------
        float sample = 0.0f;

        if (has_attack) {
            // Resample attack: linear interpolation from attack rate → sample_rate
            float const src_idx_f = static_cast<float>(i) * resample_ratio;
            auto const  src_idx   = static_cast<std::size_t>(src_idx_f);
            if (src_idx < attack.frames) {
                float const frac = src_idx_f - static_cast<float>(src_idx);
                auto const  i0   = static_cast<int16_t>(attack.pcm[src_idx * 2] | (attack.pcm[src_idx * 2 + 1] << 8));
                float const a0   = static_cast<float>(i0) / 32768.0f;

                float a1 = 0.0f;
                if (src_idx + 1 < attack.frames) {
                    auto const i1 = static_cast<int16_t>(attack.pcm[(src_idx + 1) * 2] | (attack.pcm[(src_idx + 1) * 2 + 1] << 8));
                    a1            = static_cast<float>(i1) / 32768.0f;
                } else {
                    a1 = a0;
                }
                float const attack_sample = a0 + frac * (a1 - a0);

                // Crossfade region: raised cosine over crossfade_ms
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

// ---------------------------------------------------------------------------
// bucklespring_synth::render  —  Model M tables + recorded attack samples
// ---------------------------------------------------------------------------

void bucklespring_synth::render(
  uint8_t const          keycode,
  bool const             pressed,
  uint32_t const         sample_rate,
  uint16_t const         channels,
  std::span<float> const dest) const noexcept {
    auto const&        entry = bucklespring_attack_table[keycode][pressed ? 1 : 0];
    click_attack const attack{
      .pcm         = bucklespring_attack_blob + entry.offset,
      .frames      = entry.frames,
      .sample_rate = attack_sample_rate,
    };
    render_click(params(keycode, pressed), click_engine_opts{}, attack, keycode, pressed, sample_rate, channels, dest);
}
