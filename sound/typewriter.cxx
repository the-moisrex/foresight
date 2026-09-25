// Created by moisrex on 9/24/26.

module;
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :typewriter;
import :sound;        // sound_generator concept (static_assert below)
import :bucklespring; // shared click engine (module-internal fs8::detail)
import fs8.sound;

using fs8::click_params;
using fs8::typewriter_synth;
using fs8::detail::click_attack;
using fs8::detail::click_duration_frames;
using fs8::detail::click_engine_opts;
using fs8::detail::render_click;

namespace {

    /// Typewriter engine tuning: typebar metal — inharmonic cluster, narrow
    /// high-Q pings, bright noise that stays bright, heavy snap for the
    /// instant strike, and mechanical clatter in the transient.
    constexpr click_engine_opts typewriter_opts{
      .ratio_a        = 1.45f, // inharmonic typebar metal
      .ratio_b        = 1.8f,
      .q1             = 24.0f, // narrow metallic pings
      .q2             = 26.0f,
      .q3             = 20.0f,
      .q4             = 22.0f,
      .q_from_table   = true, // measured spectral widths (res1_q / res2_q)
      .w1             = 0.40f,
      .w2             = 0.30f,
      .w3             = 0.18f,
      .w4             = 0.12f,
      .snap_gain      = 0.9f,   // instant sharp strike (measured snap 0.6 ms)
      .impulse_gain   = 1.9f,
      .click_gain     = 0.7f,   // mechanical clatter
      .ring_mod_depth = 0.40f,  // metallic shimmer
      .ring_mod_floor = 0.08f,
      .bright_decay   = 150.0f, // metal stays bright
      .body_gain      = 0.30f,  // platen/carriage thump
      .body_base      = 300.0f,
      .body_step      = 15.0f,
      .hp_hz          = 260.0f, // bright, cut the rumble
      .lp_hz          = 16000.0f,
    };

} // namespace

click_params const& typewriter_synth::params(uint8_t const keycode, bool const pressed) const noexcept {
    auto const& table = pressed ? fs8::typewriter_press_params : fs8::typewriter_release_params;
    return table[keycode];
}

std::size_t typewriter_synth::duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
    return click_duration_frames(params(keycode, pressed), sample_rate);
}

void typewriter_synth::render(
  uint8_t const          keycode,
  bool const             pressed,
  uint32_t const         sample_rate,
  uint16_t const         channels,
  std::span<float> const dest) const noexcept {
    // Pure synthesis: no recorded attack transient.
    render_click(params(keycode, pressed), typewriter_opts, click_attack{}, keycode, pressed, sample_rate, channels, dest);
}

static_assert(fs8::sound_generator<fs8::typewriter_synth>);
