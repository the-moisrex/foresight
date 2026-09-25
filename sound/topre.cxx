// Created by moisrex on 9/24/26.

module;
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :topre;
import :sound;        // sound_generator concept (static_assert below)
import :bucklespring; // shared click engine (module-internal fs8::detail)
import fs8.sound;

using fs8::click_params;
using fs8::topre_synth;
using fs8::detail::click_attack;
using fs8::detail::click_duration_frames;
using fs8::detail::click_engine_opts;
using fs8::detail::render_click;

namespace {

    /// Topre engine tuning: rubber dome over a capacitive spring — the dome
    /// bottoming is the whole event.  Soft snap (dome collapse, not a click),
    /// strong deep body thump, damped resonances with barely any ring
    /// modulation, and a gentle low-pass keeping it round.
    constexpr click_engine_opts topre_opts{
      .ratio_a        = 1.3f,
      .ratio_b        = 1.55f,
      .q1             = 8.0f, // dome + plate resonance, moderately damped
      .q2             = 9.0f,
      .q3             = 7.0f,
      .q4             = 8.0f,
      .q_from_table   = true, // measured spectral widths (res1_q / res2_q)
      .w1             = 0.45f,
      .w2             = 0.35f,
      .w3             = 0.12f,
      .w4             = 0.08f,
      .snap_gain      = 0.4f,     // rubber-dome collapse: soft bump
      .impulse_gain   = 1.6f,
      .click_gain     = 0.3f,     // capacitive leaf tick, subtle
      .ring_mod_depth = 0.10f,
      .ring_mod_floor = 0.03f,    // no metallic shimmer
      .bright_decay   = 420.0f,
      .body_gain      = 0.40f,    // the signature topre thock
      .body_base      = 260.0f,   // between the Model M and the linear's 240
      .body_step      = 13.0f,
      .hp_hz          = 150.0f,
      .lp_hz          = 11000.0f, // soft but not muffled
    };

} // namespace

click_params const& topre_synth::params(uint8_t const keycode, bool const pressed) const noexcept {
    auto const& table = pressed ? fs8::topre_press_params : fs8::topre_release_params;
    return table[keycode];
}

std::size_t topre_synth::duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
    return click_duration_frames(params(keycode, pressed), sample_rate);
}

void topre_synth::render(
  uint8_t const          keycode,
  bool const             pressed,
  uint32_t const         sample_rate,
  uint16_t const         channels,
  std::span<float> const dest) const noexcept {
    // Pure synthesis: no recorded attack transient.
    render_click(params(keycode, pressed), topre_opts, click_attack{}, keycode, pressed, sample_rate, channels, dest);
}

static_assert(fs8::sound_generator<fs8::topre_synth>);
