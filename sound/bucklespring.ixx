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
    /// The Model M sound has three distinct phases captured from WAV analysis:
    ///
    ///   1. Pre-delay (0 to ~2ms): Very quiet initial key contact.
    ///   2. Transient (~0.5ms): Three sub-events — contact, buckle, settle —
    ///      with 46% of energy above 8 kHz.  This is the "snap."
    ///   3. Ring (~15-35ms): Dual tonal resonance at ~850 Hz and ~1400 Hz
    ///      (spectral flatness drops from 0.60 to 0.18).  This is the "ping."
    ///
    /// Presses are tonal (flatness ~0.45), releases are noisier (~0.61).
    /// Noise is pink (-1 dB/octave), not white.
    struct [[nodiscard]] bucklespring_synth {
        constexpr bucklespring_synth() noexcept = default;

        /// Per-voice parameters for a single key.
        struct [[nodiscard]] voice {
            float pre_delay_ms;     ///< Quiet phase before transient (0.5-5ms)
            float transient_ms;     ///< Duration of the 3-event transient (0.5-1.5ms)
            float primary_freq;     ///< Primary spring resonance (700-1500 Hz for alpha)
            float primary_q;        ///< Primary resonator Q (5-10)
            float secondary_freq;   ///< Secondary resonance (1200-2000 Hz)
            float secondary_q;      ///< Secondary resonator Q (8-15)
            float secondary_level;  ///< Secondary resonance amplitude relative to primary
            float ring_ms;          ///< Ring decay time (15-40ms)
            float press_gain;       ///< Press amplitude scaling
            float release_gain;     ///< Release amplitude scaling
        };

        /// Key frequency mapping from WAV spectral analysis.
        ///
        /// Alpha keys: dual resonance at ~850 Hz + ~1400 Hz
        /// Spacebar: dominant at ~3500-4100 Hz (bar resonance), longer pre-delay
        /// F-keys: centroid ~2700 Hz, quieter, shorter
        /// Modifiers: centroid ~1500 Hz, longer pre-delay (stabilizer bars)
        [[nodiscard]] constexpr voice get_voice(uint8_t const keycode, bool const pressed) const noexcept {
            float primary_freq;
            float secondary_freq;
            float secondary_q;
            float secondary_level;
            float ring_ms;
            float pre_delay_ms;
            float press_gain;
            float release_gain;

            if (keycode == KEY_SPACE) {
                // Spacebar: loud, bright, long resonance, slow build (stabilizer bars)
                primary_freq = 3500.0f;
                secondary_freq = 4100.0f;
                secondary_q = 10.0f;
                secondary_level = 0.7f;
                ring_ms = 35.0f;
                pre_delay_ms = 4.0f;
                press_gain = 0.65f;
                release_gain = 0.32f;
            } else if (keycode == KEY_ENTER || keycode == KEY_KPENTER) {
                // Enter: medium, dual resonance
                primary_freq = 1500.0f;
                secondary_freq = 2200.0f;
                secondary_q = 10.0f;
                secondary_level = 0.5f;
                ring_ms = 28.0f;
                pre_delay_ms = 2.5f;
                press_gain = 0.50f;
                release_gain = 0.25f;
            } else if (keycode == KEY_LEFTSHIFT || keycode == KEY_RIGHTSHIFT
                    || keycode == KEY_LEFTCTRL || keycode == KEY_RIGHTCTRL
                    || keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT
                    || keycode == KEY_CAPSLOCK) {
                // Modifiers: lower, slower (stabilizer bars)
                primary_freq = 1200.0f;
                secondary_freq = 1800.0f;
                secondary_q = 8.0f;
                secondary_level = 0.4f;
                ring_ms = 25.0f;
                pre_delay_ms = 3.5f;
                press_gain = 0.42f;
                release_gain = 0.21f;
            } else if (keycode >= KEY_F1 && keycode <= KEY_F12) {
                // Function keys: higher, quieter, shorter
                primary_freq = 2200.0f;
                secondary_freq = 2800.0f;
                secondary_q = 10.0f;
                secondary_level = 0.5f;
                ring_ms = 14.0f;
                pre_delay_ms = 1.5f;
                press_gain = 0.30f;
                release_gain = 0.15f;
            } else if (keycode >= KEY_LEFT && keycode <= KEY_DOWN) {
                // Arrow keys: bright, short
                primary_freq = 1800.0f;
                secondary_freq = 2400.0f;
                secondary_q = 9.0f;
                secondary_level = 0.5f;
                ring_ms = 16.0f;
                pre_delay_ms = 1.5f;
                press_gain = 0.35f;
                release_gain = 0.18f;
            } else if (keycode == KEY_BACKSPACE || keycode == KEY_TAB
                    || keycode == KEY_ESC) {
                // Control keys: medium
                primary_freq = 1200.0f;
                secondary_freq = 1500.0f;
                secondary_q = 8.0f;
                secondary_level = 0.5f;
                ring_ms = 22.0f;
                pre_delay_ms = 2.0f;
                press_gain = 0.45f;
                release_gain = 0.22f;
            } else {
                // Default alphanumeric: per-key variation around spring resonance
                uint32_t const hash = keycode * 2654435761u;
                float const v = static_cast<float>(hash & 0xFFFFu) / 65535.0f; // [0, 1]
                primary_freq = 750.0f + v * 300.0f;       // 750-1050 Hz
                secondary_freq = 1200.0f + v * 400.0f;    // 1200-1600 Hz
                secondary_q = 8.0f + v * 5.0f;            // 8-13
                secondary_level = 0.4f + v * 0.3f;        // 0.4-0.7
                ring_ms = 18.0f + v * 14.0f;              // 18-32ms
                pre_delay_ms = 1.5f + v * 1.5f;           // 1.5-3ms
                press_gain = 0.42f + v * 0.08f;           // 0.42-0.50
                release_gain = 0.20f + v * 0.05f;         // 0.20-0.25
            }

            return voice{
                .pre_delay_ms    = pressed ? pre_delay_ms : pre_delay_ms * 0.4f,
                .transient_ms    = pressed ? 1.0f : 0.6f,
                .primary_freq    = primary_freq,
                .primary_q       = pressed ? 7.0f : 4.0f,
                .secondary_freq  = secondary_freq,
                .secondary_q     = secondary_q,
                .secondary_level = secondary_level,
                .ring_ms         = pressed ? ring_ms : ring_ms * 0.6f,
                .press_gain      = press_gain,
                .release_gain    = release_gain,
            };
        }

        /// Duration in audio frames for the given keycode and press state.
        [[nodiscard]] constexpr std::size_t duration_frames(uint8_t const keycode, bool const pressed, uint32_t const sample_rate) const noexcept {
            auto const v = get_voice(keycode, pressed);
            float const total_ms = v.pre_delay_ms + v.transient_ms + v.ring_ms;
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
