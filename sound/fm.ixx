// Created by moisrex on 9/25/26.

module;
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>

export module fs8.mods:fm;

import :sound;
import fs8.sound;
import fs8.event;

export namespace fs8 {

    /// Two-operator FM synthesizer: sine carrier modulated by a sine
    /// modulator, electric-piano / bell flavored (the DX7 "tine" bite).
    ///
    /// Pure synthesis, no parameter tables: pitch comes from the keycode,
    /// the modulation index decays fast on press (bright attack settling
    /// into a clean tone) and release is a shorter, muted variant with a
    /// lower index.  The index is capped against Carson's rule so high
    /// pitches cannot alias.
    struct [[nodiscard]] fm_synth {
        constexpr fm_synth() noexcept = default;

        // -- raw interface (keycode + pressed) ------------------------------

        /// Duration in audio frames for the given keycode and press state
        /// (~7 carrier decay constants; press 140 ms, release 70 ms).
        [[nodiscard]] constexpr std::size_t duration_frames(uint8_t const, bool const pressed, uint32_t const sample_rate) const noexcept {
            float const duration_ms = pressed ? 140.0f : 70.0f;
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
