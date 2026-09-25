// Created by moisrex on 9/25/26.

module;
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>

export module fs8.mods:marimba;

import :sound;
import fs8.sound;
import fs8.event;

export namespace fs8 {

    /// Pitched-percussion (mallet) synthesizer: a wooden bar, softer than
    /// `chime_synth`'s bell.
    ///
    /// Pure synthesis, no parameter tables: marimba bar modes at roughly
    /// 1 : 3.9 : 10.8 (fundamental, major third partial, tenth partial)
    /// with per-mode decay (the fundamental rings longest), excited by a
    /// short soft mallet noise burst — much quieter on release.  The pitch
    /// mapping is a pentatonic-clamped `2^(keycode / 12)` formula so random
    /// typing stays consonant.
    struct [[nodiscard]] marimba_synth {
        constexpr marimba_synth() noexcept = default;

        // -- raw interface (keycode + pressed) ------------------------------

        /// Duration in audio frames for the given keycode and press state
        /// (~7 fundamental decay constants; press 140 ms, release 60 ms).
        [[nodiscard]] constexpr std::size_t duration_frames(uint8_t const, bool const pressed, uint32_t const sample_rate) const noexcept {
            float const duration_ms = pressed ? 140.0f : 60.0f;
            return static_cast<std::size_t>(static_cast<float>(sample_rate) * duration_ms / 1000.0f);
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
