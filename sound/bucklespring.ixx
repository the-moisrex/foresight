// Created by moisrex on 9/20/26.

module;
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>

export module fs8.mods:bucklespring;

import :sound;
import fs8.sound;
import fs8.event;

export namespace fs8 {

    /// Synthesizer for IBM Model M bucklespring keystroke sounds.
    ///
    /// A thin wrapper around the shared click engine (`fs8::detail::
    /// render_click`) with Model M parameter tables and the recorded
    /// attack-transient samples.
    struct [[nodiscard]] bucklespring_synth {
        constexpr bucklespring_synth() noexcept = default;

        /// Look up the synthesis parameters for a given keycode and press state.
        [[nodiscard]] click_params const& params(uint8_t keycode, bool pressed) const noexcept;

        /// Duration in audio frames for the given keycode and press state.
        [[nodiscard]] std::size_t duration_frames(uint8_t keycode, bool pressed, uint32_t sample_rate) const noexcept;

        /// Render interleaved float samples into `dest`.
        void render(uint8_t keycode, bool pressed, uint32_t sample_rate, uint16_t channels, std::span<float> dest) const noexcept;

        // -- event-based interface (satisfies sound_generator) ---------------

        [[nodiscard]] std::size_t duration_frames(event_type const& event, sound_format const fmt) const noexcept {
            if (event.type() != EV_KEY || event.value() > 1) [[unlikely]] {
                return 0;
            }
            return duration_frames(static_cast<uint8_t>(event.code()), event.value() == 1, fmt.sample_rate);
        }

        void render(event_type const& event, sound_format const fmt, std::span<float> dest) const noexcept {
            if (event.type() != EV_KEY || event.value() > 1) [[unlikely]] {
                return;
            }
            render(static_cast<uint8_t>(event.code()), event.value() == 1, fmt.sample_rate, fmt.channels, dest);
        }
    };

} // namespace fs8

// ---------------------------------------------------------------------------
// Shared click engine — module-internal (not exported from fs8.mods).
//
// Declared here so every click-style profile's implementation unit can
// reach it with `import :bucklespring;`, without leaking it to importers
// of fs8.mods.
// ---------------------------------------------------------------------------

namespace fs8::detail {

    /// Engine-level knobs for the shared click engine.  The defaults
    /// reproduce the IBM Model M bucklespring sound exactly; other profiles
    /// override only what they need.
    struct click_engine_opts {
        float ratio_a            = 1.3f;  ///< 3rd resonator: primary_freq * ratio_a
        float ratio_b            = 1.7f;  ///< 4th resonator: secondary_freq * ratio_b
        float q1                 = 11.0f; ///< resonator Qs
        float q2                 = 10.0f;
        float q3                 = 12.0f;
        float q4                 = 10.0f;
        float w1                 = 0.40f; ///< tonal mix weights
        float w2                 = 0.35f;
        float w3                 = 0.15f;
        float w4                 = 0.10f;
        float snap_gain          = 0.6f;   ///< gaussian snap burst added to envelope
        float impulse_gain       = 1.8f;   ///< envelope -> resonator excitation scale
        float click_gain         = 0.5f;   ///< noise * envelope transient mix
        float ring_mod_depth     = 0.3f;   ///< ring amplitude modulation: depth at t=0
        float ring_mod_floor     = 0.05f;
        float bright_decay       = 250.0f; ///< noise brightness decay rate (1/s)
        float noise_white_base   = 0.6f;
        float noise_white_bright = 0.4f;
        float noise_pink_base    = 0.4f;
        float noise_pink_bright  = 0.2f;
        float body_gain          = 0.25f;    ///< bottom-out thump mix
        float body_base          = 280.0f;   ///< thump frequency (Hz), keycode low nibble 0
        float body_step          = 15.0f;    ///< Hz per low-nibble step
        float body_window_ms     = 12.0f;
        float body_tau_ms        = 3.0f;
        float hp_hz              = 200.0f;   ///< DC-blocker cutoff
        float lp_hz              = 14000.0f; ///< one-pole low-pass cutoff
        float crossfade_ms       = 5.0f;     ///< attack <-> tail crossfade length
    };

    /// A recorded attack transient (int16 LE mono PCM), crossfaded over the
    /// start of the synthetic tail.  `frames == 0` means pure synthesis.
    struct click_attack {
        uint8_t const* pcm         = nullptr;
        uint32_t       frames      = 0;
        uint32_t       sample_rate = 44'100;
    };

    /// Shared duration formula: contact + snap + ~7 ring time-constants,
    /// capped at the player's 150 ms slot budget.
    [[nodiscard]] std::size_t click_duration_frames(click_params const& v, uint32_t sample_rate) noexcept;

    /// Render one click-style keystroke: shaped noise excitation through a
    /// 4-resonator modal bank, optionally crossfaded with a recorded attack.
    void render_click(
      click_params const&      v,
      click_engine_opts const& opts,
      click_attack const&      attack,
      uint8_t                  keycode,
      bool                     pressed,
      uint32_t                 sample_rate,
      uint16_t                 channels,
      std::span<float>         dest) noexcept;

} // namespace fs8::detail
