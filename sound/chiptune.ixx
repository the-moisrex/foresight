// Created by moisrex on 9/25/26.

module;
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>

export module fs8.mods:chiptune;

import :sound;
import fs8.sound;
import fs8.event;

export namespace fs8 {

    /// 8-bit console-style synthesizer: a NES-style APU in miniature.
    ///
    /// Pure synthesis, no parameter tables: press plays a duty-cycle square
    /// wave (12.5 / 25 / 50 %) at a pentatonic keycode-derived pitch plus a
    /// short noise-channel tick; release is a quiet square blip an octave up
    /// (Game Boy UI style).  All phase accumulators are integer — the
    /// lo-fi aliasing is part of the aesthetic.
    struct [[nodiscard]] chiptune_synth {
        constexpr chiptune_synth() noexcept = default;

        // -- raw interface (keycode + pressed) ------------------------------

        /// Duration in audio frames for the given keycode and press state
        /// (press 90 ms, release 40 ms).
        [[nodiscard]] constexpr std::size_t duration_frames(uint8_t const, bool const pressed, uint32_t const sample_rate) const noexcept {
            float const duration_ms = pressed ? 90.0f : 40.0f;
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
