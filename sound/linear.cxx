// Created by moisrex on 9/24/26.

module;
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :linear;
import :sound;        // sound_generator concept (static_assert below)
import :bucklespring; // shared click engine (module-internal fs8::detail)
import fs8.sound;

using fs8::click_params;
using fs8::linear_synth;
using fs8::detail::click_attack;
using fs8::detail::click_duration_frames;
using fs8::detail::click_engine_opts;
using fs8::detail::render_click;

namespace {

    /// Linear engine tuning: muted plastic thock — the bottom-out is the
    /// whole event.  Deep body, dark noise (fast bright decay, soft low-pass),
    /// almost no ring modulation (no metal leaf to shimmer), and a gentle
    /// snap for the housing knock rather than a click jacket tick.
    constexpr click_engine_opts linear_opts{
      .ratio_a        = 1.3f, // plain modal cluster
      .ratio_b        = 1.6f,
      .q1             = 7.0f, // damped plastic: shorter resonance
      .q2             = 8.0f,
      .q3             = 6.0f,
      .q4             = 7.0f,
      .q_from_table   = true, // measured spectral widths (res1_q / res2_q)
      .w1             = 0.45f,
      .w2             = 0.35f,
      .w3             = 0.12f,
      .w4             = 0.08f,
      .snap_gain      = 0.5f,    // housing knock, not a click
      .impulse_gain   = 1.5f,
      .click_gain     = 0.25f,   // smooth travel: little transient hiss
      .ring_mod_depth = 0.12f,
      .ring_mod_floor = 0.03f,   // no metallic shimmer
      .bright_decay   = 500.0f,  // darkness wins quickly
      .body_gain      = 0.35f,   // bottom-out IS the sound of a linear
      .body_base      = 240.0f,  // deeper than the Model M's 280
      .body_step      = 12.0f,
      .hp_hz          = 140.0f,  // keep the low thock
      .lp_hz          = 9000.0f, // muffled plastic
    };

} // namespace

click_params const& linear_synth::params(uint8_t const keycode, bool const pressed) const noexcept {
    auto const& table = pressed ? fs8::linear_press_params : fs8::linear_release_params;
    return table[keycode];
}

std::size_t linear_synth::duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
    return click_duration_frames(params(keycode, pressed), sample_rate);
}

void linear_synth::render(
  uint8_t const          keycode,
  bool const             pressed,
  uint32_t const         sample_rate,
  uint16_t const         channels,
  std::span<float> const dest) const noexcept {
    // Pure synthesis: no recorded attack transient.
    render_click(params(keycode, pressed), linear_opts, click_attack{}, keycode, pressed, sample_rate, channels, dest);
}

static_assert(fs8::sound_generator<fs8::linear_synth>);
