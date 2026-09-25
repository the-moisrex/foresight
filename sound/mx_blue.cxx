// Created by moisrex on 9/24/26.

module;
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :mx_blue;
import :sound;        // sound_generator concept (static_assert below)
import :bucklespring; // shared click engine (module-internal fs8::detail)
import fs8.sound;

using fs8::click_params;
using fs8::mx_blue_synth;
using fs8::detail::click_attack;
using fs8::detail::click_duration_frames;
using fs8::detail::click_engine_opts;
using fs8::detail::render_click;

namespace {

    /// MX Blue engine tuning: click jacket — heavy snap and crisp high
    /// transient for the jacket tick, moderate shimmer (steel leaf), firm
    /// bottom-out thump on a stiff, high-pitched spring.
    constexpr click_engine_opts mx_blue_opts{
      .ratio_a        = 1.35f,
      .ratio_b        = 1.7f,
      .q1             = 13.0f, // bright, crisp
      .q2             = 15.0f,
      .q3             = 12.0f,
      .q4             = 14.0f,
      .q_from_table   = true, // measured spectral widths (res1_q / res2_q)
      .w1             = 0.42f,
      .w2             = 0.33f,
      .w3             = 0.15f,
      .w4             = 0.10f,
      .snap_gain      = 0.85f, // the jacket tick is the event
      .impulse_gain   = 1.9f,
      .click_gain     = 0.75f, // crisp high transient
      .ring_mod_depth = 0.30f,
      .ring_mod_floor = 0.06f,
      .bright_decay   = 200.0f,
      .body_gain      = 0.22f,  // bottom-out present but secondary to the click
      .body_base      = 300.0f, // stiffer spring: higher thump
      .body_step      = 16.0f,
      .hp_hz          = 250.0f,
      .lp_hz          = 15000.0f,
    };

} // namespace

click_params const& mx_blue_synth::params(uint8_t const keycode, bool const pressed) const noexcept {
    auto const& table = pressed ? fs8::mx_blue_press_params : fs8::mx_blue_release_params;
    return table[keycode];
}

std::size_t mx_blue_synth::duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
    return click_duration_frames(params(keycode, pressed), sample_rate);
}

void mx_blue_synth::render(
  uint8_t const          keycode,
  bool const             pressed,
  uint32_t const         sample_rate,
  uint16_t const         channels,
  std::span<float> const dest) const noexcept {
    // Pure synthesis: no recorded attack transient.
    render_click(params(keycode, pressed), mx_blue_opts, click_attack{}, keycode, pressed, sample_rate, channels, dest);
}

static_assert(fs8::sound_generator<fs8::mx_blue_synth>);
