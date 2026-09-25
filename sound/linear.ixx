// Created by moisrex on 9/24/26.

module;
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>

export module fs8.mods:linear;

import :sound;
import fs8.sound;
import fs8.event;

export namespace fs8 {

    /// Synthesizer for linear-switch (MX Red-class) keystroke sounds.
    ///
    /// Linear switches have no click jacket: the sound is the bottom-out
    /// thud plus the plastic housing resonance — muted, bassy, with a soft
    /// attack and little transient hiss.  Pure synthesis through the shared
    /// click engine (`fs8::detail::render_click`) — no recorded attack
    /// samples.
    struct [[nodiscard]] linear_synth {
        constexpr linear_synth() noexcept = default;

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
