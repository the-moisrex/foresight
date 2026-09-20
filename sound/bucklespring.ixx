// Created by moisrex on 9/20/26.

module;
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>

export module fs8.mods:bucklespring;

import :bucklespring_data;
import :sound;
import fs8.event;

export namespace fs8 {

    /// Parameterized synthesizer for IBM Model M bucklespring keystroke sounds.
    ///
    /// Each key has its own voice parameters (center frequency, bandwidth,
    /// envelope shape, ...) extracted from WAV analysis of a real Model M.
    /// The synthesis is noise-driven through a biquad bandpass resonator
    /// with a dual-exponential amplitude envelope.
    struct [[nodiscard]] bucklespring_synth {
        constexpr bucklespring_synth() noexcept = default;

        // -- raw interface (keycode + pressed) ------------------------------

        /// Look up the voice parameters for a given keycode and press state.
        [[nodiscard]] constexpr bucklespring_voice const& voice(uint8_t const keycode, bool const pressed) const noexcept {
            auto const& table = pressed ? bucklespring_press_params : bucklespring_release_params;
            return table[keycode];
        }

        /// Duration in audio frames for the given keycode and press state.
        ///
        /// Computes the audible duration from the envelope parameters rather
        /// than using the raw duration_ms (which includes the full WAV tail).
        [[nodiscard]] constexpr std::size_t duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
            auto const& v = voice(keycode, pressed);
            // The sound is inaudible after ~4 decay time constants (~-35 dB).
            float const audible_ms = v.attack_ms + v.decay_slow_ms * 4.0f;
            // Cap at 150 ms (the player's max_frames limit).
            float const capped_ms = audible_ms < 150.0f ? audible_ms : 150.0f;
            return static_cast<std::size_t>(static_cast<float>(sample_rate) * capped_ms / 1000.0f);
        }

        /// Render `duration_frames` interleaved float samples into `dest`.
        void render(uint8_t keycode, bool pressed, uint32_t sample_rate, uint16_t channels, std::span<float> dest) const noexcept;

        // -- event-based interface (satisfies sound_generator) ---------------

        /// Duration in frames for a given input event.  Returns 0 for
        /// non-EV_KEY events or key-repeat (value > 1).
        [[nodiscard]] constexpr std::size_t duration_frames(event_type const& event, sound_format const fmt) const noexcept {
            if (event.type() != EV_KEY || event.value() > 1) [[unlikely]] {
                return 0;
            }
            return duration_frames(static_cast<uint8_t>(event.code()), event.value() == 1, fmt.sample_rate);
        }

        /// Render audio for a given input event.
        void render(event_type const& event, sound_format const fmt, std::span<float> dest) const noexcept {
            if (event.type() != EV_KEY || event.value() > 1) [[unlikely]] {
                return;
            }
            render(static_cast<uint8_t>(event.code()), event.value() == 1, fmt.sample_rate, fmt.channels, dest);
        }
    };

} // namespace fs8
