// Created by moisrex on 9/20/26.

module;
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>

export module fs8.mods:bucklespring;

import :sound;
import fs8.event;

export namespace fs8 {

    /// Hand-tuned synthesizer for IBM Model M bucklespring keystroke sounds.
    ///
    /// The Model M produces a distinctive two-stage sound:
    ///   1. Click (0-3ms): Broadband noise transient — the spring buckling.
    ///      Energy concentrated above 2 kHz (centroid ~5-6 kHz).
    ///   2. Ping (3-30ms): Tonal resonant body — the spring vibrating.
    ///      Energy concentrated at 700-1500 Hz (spectral flatness ~0.2).
    ///
    /// Per-key variation: alpha keys ring at ~900-1400 Hz, spacebar at ~3500 Hz,
    /// function keys at ~2700 Hz.  Presses are more tonal; releases are noisier.
    struct [[nodiscard]] bucklespring_synth {
        constexpr bucklespring_synth() noexcept = default;

        /// Per-voice parameters for a single key.
        struct [[nodiscard]] voice {
            float click_ms;       ///< Click transient duration (0.5-2ms)
            float pre_delay_ms;   ///< Quiet phase before buckle spike (1-3ms)
            float ring_freq;      ///< Spring resonance frequency (700-4000 Hz)
            float ring_q;         ///< Resonator Q factor (4-8)
            float ring_ms;        ///< Ring decay time (15-40ms)
            float tonal_mix;      ///< Ring tonal/noise blend (0=noise, 1=tonal)
            float press_gain;     ///< Press amplitude scaling
            float release_gain;   ///< Release amplitude scaling
        };

        /// Key frequency mapping from WAV analysis.
        ///
        /// Alpha keys: centroid 900-1400 Hz (spring resonance)
        /// Spacebar: dominant freq ~4100 Hz (bar resonance)
        /// F-keys: centroid ~2700 Hz
        /// Modifiers: centroid ~1500 Hz
        [[nodiscard]] constexpr voice get_voice(uint8_t const keycode, bool const pressed) const noexcept {
            float ring_freq;
            float ring_ms;
            float ring_q;
            float press_gain;
            float release_gain;

            if (keycode == KEY_SPACE) {
                // Spacebar: bright, loud, long resonance
                ring_freq = 3500.0f;
                ring_ms = 35.0f;
                ring_q = 6.0f;
                press_gain = 0.60f;
                release_gain = 0.30f;
            } else if (keycode == KEY_ENTER || keycode == KEY_KPENTER) {
                // Enter: medium depth, moderate resonance
                ring_freq = 2000.0f;
                ring_ms = 28.0f;
                ring_q = 5.0f;
                press_gain = 0.50f;
                release_gain = 0.25f;
            } else if (keycode == KEY_LEFTSHIFT || keycode == KEY_RIGHTSHIFT
                    || keycode == KEY_LEFTCTRL || keycode == KEY_RIGHTCTRL
                    || keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT
                    || keycode == KEY_CAPSLOCK) {
                // Modifiers: medium-low, slightly longer
                ring_freq = 1500.0f;
                ring_ms = 25.0f;
                ring_q = 5.0f;
                press_gain = 0.40f;
                release_gain = 0.20f;
            } else if (keycode >= KEY_F1 && keycode <= KEY_F12) {
                // Function keys: higher, quieter, shorter
                ring_freq = 2700.0f;
                ring_ms = 14.0f;
                ring_q = 6.0f;
                press_gain = 0.30f;
                release_gain = 0.15f;
            } else if (keycode >= KEY_LEFT && keycode <= KEY_DOWN) {
                // Arrow keys: bright, short
                ring_freq = 2000.0f;
                ring_ms = 16.0f;
                ring_q = 5.0f;
                press_gain = 0.35f;
                release_gain = 0.18f;
            } else if (keycode == KEY_BACKSPACE || keycode == KEY_TAB
                    || keycode == KEY_ESC) {
                // Control keys: medium
                ring_freq = 1500.0f;
                ring_ms = 22.0f;
                ring_q = 5.0f;
                press_gain = 0.45f;
                release_gain = 0.22f;
            } else {
                // Default alphanumeric: per-key variation around spring resonance
                uint32_t const hash = keycode * 2654435761u;
                float const v = static_cast<float>(hash & 0xFFFFu) / 65535.0f; // [0, 1]
                ring_freq = 900.0f + v * 600.0f;   // 900-1500 Hz (spring resonance)
                ring_ms = 18.0f + v * 14.0f;        // 18-32ms
                ring_q = 5.0f + v * 3.0f;           // 5-8
                press_gain = 0.42f + v * 0.08f;     // 0.42-0.50
                release_gain = 0.20f + v * 0.05f;   // 0.20-0.25
            }

            return voice{
                .click_ms     = pressed ? 1.5f : 1.0f,
                .pre_delay_ms = pressed ? 2.0f : 0.5f,
                .ring_freq    = ring_freq,
                .ring_q       = ring_q,
                .ring_ms      = ring_ms,
                .tonal_mix    = pressed ? 0.80f : 0.30f,
                .press_gain   = press_gain,
                .release_gain = release_gain,
            };
        }

        /// Duration in audio frames for the given keycode and press state.
        [[nodiscard]] constexpr std::size_t duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
            auto const v = get_voice(keycode, pressed);
            float const total_ms = v.pre_delay_ms + v.click_ms + v.ring_ms;
            float const capped_ms = total_ms < 150.0f ? total_ms : 150.0f;
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
