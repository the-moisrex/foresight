// Created by moisrex on 9/24/26.

module;
#include <cstddef>
#include <cstdint>
#include <span>

module fs8.mods;

import :modelf;
import :bucklespring; // shared click engine (module-internal fs8::detail)
import fs8.sound;

using fs8::click_params;
using fs8::modelf_synth;
using fs8::detail::click_attack;
using fs8::detail::click_duration_frames;
using fs8::detail::click_engine_opts;
using fs8::detail::render_click;

namespace {

    /// Model F engine tuning: brighter and more metallic than the Model M —
    /// higher, tighter modal cluster with a longer metallic ring and a
    /// sharper snap; less bottom-out thump (capacitive leaf, no hard
    /// membrane bottom-out).
    constexpr click_engine_opts modelf_opts{
      .ratio_a        = 1.5f,  // upper cluster sits above the Model M's 1.3
      .ratio_b        = 1.9f,  // ...and further apart (metallic shimmer)
      .q1             = 14.0f, // longer ring than the Model M
      .q2             = 12.0f,
      .q3             = 16.0f,
      .q4             = 12.0f,
      .w1             = 0.45f,
      .w2             = 0.30f,
      .w3             = 0.15f,
      .w4             = 0.10f,
      .snap_gain      = 0.7f,   // sharper upstroke snap
      .impulse_gain   = 1.8f,
      .click_gain     = 0.55f,  // slightly more transient hiss
      .ring_mod_depth = 0.35f,
      .ring_mod_floor = 0.05f,
      .bright_decay   = 320.0f, // brightness dies faster -> crisper click
      .body_gain      = 0.15f,  // lighter thump than the Model M's 0.25
      .body_base      = 340.0f, // small keys start higher; stiff spring
      .body_step      = 18.0f,
      .hp_hz          = 250.0f, // brighter overall
      .lp_hz          = 16000.0f,
    };

} // namespace

click_params const& modelf_synth::params(uint8_t const keycode, bool const pressed) const noexcept {
    auto const& table = pressed ? fs8::modelf_press_params : fs8::modelf_release_params;
    return table[keycode];
}

std::size_t modelf_synth::duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
    return click_duration_frames(params(keycode, pressed), sample_rate);
}

void modelf_synth::render(
  uint8_t const          keycode,
  bool const             pressed,
  uint32_t const         sample_rate,
  uint16_t const         channels,
  std::span<float> const dest) const noexcept {
    // Pure synthesis: no recorded attack transient.
    render_click(params(keycode, pressed), modelf_opts, click_attack{}, keycode, pressed, sample_rate, channels, dest);
}
