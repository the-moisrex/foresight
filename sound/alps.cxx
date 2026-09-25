// Created by moisrex on 9/24/26.

module;
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :alps;
import :sound;        // sound_generator concept (static_assert below)
import :bucklespring; // shared click engine (module-internal fs8::detail)
import fs8.sound;

using fs8::alps_synth;
using fs8::click_params;
using fs8::detail::click_attack;
using fs8::detail::click_duration_frames;
using fs8::detail::click_engine_opts;
using fs8::detail::render_click;

namespace {

    /// Alps engine tuning: leaf click — crisp and bright with a hollow
    /// housing shimmer, sharp fast snap, and a light, high-pitched cup
    /// bottom-out (the SKCM "cup" is higher than an MX thock).
    constexpr click_engine_opts alps_opts{
      .ratio_a        = 1.4f,  // brighter cluster than the Model M's 1.3
      .ratio_b        = 1.75f,
      .q1             = 16.0f, // crisp, medium-long ring
      .q2             = 18.0f,
      .q3             = 14.0f,
      .q4             = 16.0f,
      .q_from_table   = true, // measured spectral widths (res1_q / res2_q)
      .w1             = 0.42f,
      .w2             = 0.32f,
      .w3             = 0.15f,
      .w4             = 0.11f,
      .snap_gain      = 0.75f,  // sharp but lighter than a click jacket
      .impulse_gain   = 1.75f,
      .click_gain     = 0.6f,   // leaf tick
      .ring_mod_depth = 0.35f,  // hollow alps shimmer
      .ring_mod_floor = 0.07f,
      .bright_decay   = 220.0f,
      .body_gain      = 0.25f,  // light cup bottom-out
      .body_base      = 320.0f, // higher pitched than the MX thock
      .body_step      = 16.0f,
      .hp_hz          = 240.0f,
      .lp_hz          = 15500.0f,
    };

} // namespace

click_params const& alps_synth::params(uint8_t const keycode, bool const pressed) const noexcept {
    auto const& table = pressed ? fs8::alps_press_params : fs8::alps_release_params;
    return table[keycode];
}

std::size_t alps_synth::duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
    return click_duration_frames(params(keycode, pressed), sample_rate);
}

void alps_synth::render(
  uint8_t const          keycode,
  bool const             pressed,
  uint32_t const         sample_rate,
  uint16_t const         channels,
  std::span<float> const dest) const noexcept {
    // Pure synthesis: no recorded attack transient.
    render_click(params(keycode, pressed), alps_opts, click_attack{}, keycode, pressed, sample_rate, channels, dest);
}

static_assert(fs8::sound_generator<fs8::alps_synth>);
