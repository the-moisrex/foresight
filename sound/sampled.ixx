// Created by moisrex on 9/25/26.

module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>

export module fs8.mods:sampled;

import :sound;
import fs8.sound;
import fs8.event;

export namespace fs8 {

    /// Sampled keystroke player: plays back short recordings cut from CC0
    /// reference captures (provenance in `sound/sampled_data.cxx`, produced
    /// by `tools/gen-sampled.py`).
    ///
    /// Press plays a cut at natural level.  Release plays the same cut
    /// time-compressed to 55% at lower gain through a 2.5 kHz lowpass as a
    /// damper (no release recordings were embedded).  Keys map by keyboard
    /// row; the spacebar and modifier-like keys get their own heavier
    /// typewriter cuts, and every keycode gets a deterministic +/-1%
    /// varispeed so repeated keys don't machine-gun.
    struct [[nodiscard]] sampled_synth {
        constexpr sampled_synth() noexcept = default;

        // -- release shaping ------------------------------------------------

        /// Fraction of a cut played on release, its gain, lowpass corner and
        /// end fade (recordings only capture presses; releases are shaped).
        static constexpr float release_len     = 0.55f;
        static constexpr float release_gain    = 0.45f;
        static constexpr float release_lp_hz   = 2'500.0f;
        static constexpr float release_fade_ms = 4.0f;

        // -- mapping -------------------------------------------------------

        /// Index into `sampled_slices` for a keycode.
        [[nodiscard]] static constexpr std::size_t sample_index(uint8_t const code) noexcept {
            if (code == KEY_SPACE) {
                return sampled_space_index;
            }
            switch (code) {
                case KEY_ESC:
                case KEY_TAB:
                case KEY_CAPSLOCK:
                case KEY_ENTER:
                case KEY_BACKSPACE:
                case KEY_LEFTSHIFT:
                case KEY_RIGHTSHIFT:
                case KEY_LEFTCTRL:
                case KEY_RIGHTCTRL:
                case KEY_LEFTALT:
                case KEY_RIGHTALT:
                    // sampled_type_count >= 1 is enforced by gen-sampled.py.
                    return sampled_type_first + (static_cast<std::size_t>(code) % sampled_type_count);
                default: break;
            }
            return (static_cast<std::size_t>(code) / 12u) % sampled_normal_count;
        }

        /// Deterministic per-keycode varispeed in [0.990, 1.010].
        [[nodiscard]] static constexpr float play_rate(uint8_t const code) noexcept {
            uint64_t const hash   = static_cast<uint64_t>(code) * 0x9E37'79B9'7F4A'7C15ull;
            uint32_t const bucket = static_cast<uint32_t>(hash >> 56) % 21u;
            return 0.990f + static_cast<float>(bucket) * 0.001f;
        }

        // -- raw interface (keycode + pressed) ------------------------------

        /// Duration in audio frames (press: the whole cut; release: 55% of
        /// it, speed-adjusted).  Reads the generated slice table, so this
        /// cannot be `constexpr`.
        [[nodiscard]] std::size_t duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
            if (sample_rate == 0) [[unlikely]] {
                return 0;
            }
            auto const& slice = sampled_slices[sample_index(keycode)];
            float const step  = play_rate(keycode) * static_cast<float>(sample_rate) / static_cast<float>(sampled_rate);
            float const src   = pressed ? static_cast<float>(slice.frames) : static_cast<float>(slice.frames) * release_len;
            return static_cast<std::size_t>(std::ceil(src / step));
        }

        /// Render `duration_frames` interleaved float samples into `dest`.
        void render(uint8_t keycode, bool pressed, uint32_t sample_rate, uint16_t channels, std::span<float> dest) const noexcept;

        // -- event-based interface (satisfies sound_generator) ---------------

        /// Duration in frames for a given input event.  Returns 0 for
        /// non-EV_KEY events or key-repeat (value > 1).
        [[nodiscard]] std::size_t duration_frames(event_type const& event, sound_format const fmt) const noexcept {
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

    static_assert(sound_generator<sampled_synth>);

} // namespace fs8
