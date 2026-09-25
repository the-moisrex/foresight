// Created by moisrex on 9/25/26.

module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>

export module fs8.mods:wavetable;

import :sound;
import fs8.sound;
import fs8.event;

namespace {

    // Portable constexpr sine (libstdc++'s std::sin is not constexpr);
    // wrapped Taylor series, error < 1e-9 over [-pi, pi].
    [[nodiscard]] constexpr float ct_sin(float x) noexcept {
        constexpr float pi  = 3.14159265358979323846f;
        constexpr float tau = 2.0f * pi;
        while (x > pi) {
            x -= tau;
        }
        while (x < -pi) {
            x += tau;
        }
        float const x2 = x * x;
        return x
               * (1.0f
                  - x2
                  / 6.0f
                  + x2
                  * x2
                  / 120.0f
                  - x2
                  * x2
                  * x2
                  / 5040.0f
                  + x2
                  * x2
                  * x2
                  * x2
                  / 362880.0f
                  - x2
                  * x2
                  * x2
                  * x2
                  * x2
                  / 39916800.0f);
    }

    constexpr std::size_t wt_size = 64;
    using wt_t                    = std::array<float, wt_size>;

    /// Build a single-cycle waveform from integer harmonics (keeps the
    /// cycle exactly periodic, hence loop-free at playback).
    template <std::size_t N>
    [[nodiscard]] constexpr wt_t make_harmonic_wt(std::array<float, N> const& harmonics, std::array<float, N> const& amps) noexcept {
        wt_t table{};
        for (std::size_t i = 0; i < wt_size; ++i) {
            float const two_pi_x = 6.28318530717958647692f * static_cast<float>(i) / static_cast<float>(wt_size);
            float       value    = 0.0f;
            for (std::size_t h = 0; h < N; ++h) {
                value += amps[h] * ct_sin(two_pi_x * harmonics[h]);
            }
            table[i] = value;
        }
        return table;
    }

} // namespace

export namespace fs8 {

    // Single-cycle wavetables, 64 samples each.  All integer-harmonic (or
    // piecewise-linear) shapes so one cycle loops without a seam.
    inline constexpr std::array<float, wt_size> wavetable_triangle = [] {
        std::array<float, wt_size> table{};
        for (std::size_t i = 0; i < wt_size; ++i) {
            float const x    = static_cast<float>(i) / static_cast<float>(wt_size);
            float const dist = x < 0.5f ? 0.5f - x : x - 0.5f;
            table[i]         = 4.0f * dist - 1.0f;
        }
        return table;
    }();

    inline constexpr std::array<float, wt_size> wavetable_saw = [] {
        std::array<float, wt_size> table{};
        for (std::size_t i = 0; i < wt_size; ++i) {
            table[i] = 2.0f * (static_cast<float>(i) / static_cast<float>(wt_size)) - 1.0f;
        }
        return table;
    }();

    inline constexpr std::array<float, wt_size> wavetable_square = [] {
        std::array<float, wt_size> table{};
        for (std::size_t i = 0; i < wt_size; ++i) {
            table[i] = static_cast<float>(i) < static_cast<float>(wt_size) / 2.0f ? 1.0f : -1.0f;
        }
        return table;
    }();

    inline constexpr std::array<float, wt_size> wavetable_organ =
      make_harmonic_wt(std::array{1.0f, 2.0f, 4.0f, 5.0f}, std::array{0.50f, 0.30f, 0.20f, 0.15f});

    inline constexpr std::array<float, wt_size> wavetable_voice =
      make_harmonic_wt(std::array{1.0f, 2.0f, 3.0f, 4.0f, 5.0f}, std::array{0.55f, 0.32f, 0.28f, 0.18f, 0.10f});

    inline constexpr std::array<float, wt_size> wavetable_bell =
      make_harmonic_wt(std::array{1.0f, 3.0f, 5.0f, 7.0f}, std::array{0.70f, 0.30f, 0.15f, 0.08f});

    inline constexpr std::size_t wavetable_count = 6;

    /// Wavetable-synthesis sound profile: per-keycode table select plus an
    /// envelope from the existing slot machinery.
    ///
    /// Pure synthesis, no `_data` partition: the tables are the `constexpr`
    /// arrays above.  Press plays the key's table with a full envelope;
    /// release plays the same table darker and shorter (one-pole
    /// low-pass), and the pitch mapping is pentatonic-clamped so random
    /// typing stays consonant.
    struct [[nodiscard]] wavetable_synth {
        constexpr wavetable_synth() noexcept = default;

        // -- raw interface (keycode + pressed) ------------------------------

        /// Duration in audio frames for the given keycode and press state
        /// (~7 envelope decay constants; press 140 ms, release 56 ms).
        [[nodiscard]] constexpr std::size_t duration_frames(uint8_t const, bool const pressed, uint32_t const sample_rate) const noexcept {
            float const duration_ms = pressed ? 140.0f : 56.0f;
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
