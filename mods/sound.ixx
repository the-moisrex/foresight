// Created by moisrex on 9/18/26.

module;
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

export module fs8.mods:sound;
import fs8.context;
import fs8.event;
import fs8.log;
import fs8.traits;
import fs8.pimpl;
import :io_manager;

export namespace fs8 {

    /// Logical sound identifiers.
    enum struct [[nodiscard]] sound_id : uint8_t {
        release = 0, ///< key/button up
        press   = 1, ///< key/button down
        tick,        ///< soft click
        confirm,     ///< success / acknowledge
        error,       ///< failure / deny
        toggle_on,   ///< mode enabled
        toggle_off,  ///< mode disabled
    };

    [[nodiscard]] constexpr uint8_t operator+(sound_id const id) noexcept {
        return std::to_underlying(id);
    }

    /// Audio format supplied to a sound generator.
    struct [[nodiscard]] sound_format {
        uint32_t sample_rate = 48'000;
        uint16_t channels    = 2;
    };

    /// A sound generator produces audio for a given logical sound id.
    ///
    /// This is the extension point: implement this concept to provide a
    /// different palette (FM synth, wavetable, sampled assets, ...) without
    /// touching the player.  `render` must be `noexcept` and write
    /// `duration_frames * format.channels` interleaved float samples into
    /// the destination span.
    template <typename T>
    concept sound_generator = requires(T const& gen, sound_id const id, sound_format const fmt, std::span<float> dest) {
        { gen.duration_frames(id, fmt) } noexcept -> std::convertible_to<std::size_t>;
        { gen.render(id, fmt, dest) } noexcept;
    };

    /// A tiny self-contained synthesizer: sine carrier with exponential
    /// decay, per-sound-id pitch.  No assets, no dependencies.
    struct [[nodiscard]] basic_synth {
        constexpr basic_synth() noexcept = default;

        [[nodiscard]] constexpr std::size_t duration_frames(sound_id const id, sound_format const fmt) const noexcept {
            static constexpr std::array<uint8_t, 7u> ids{45, 45, 20, 90, 140, 70, 70};
            return static_cast<std::size_t>(fmt.sample_rate) * ids[+id] / 1000;
        }

        void render(sound_id id, sound_format fmt, std::span<float> dest) const noexcept;
    };

    static_assert(sound_generator<basic_synth>);

    // Forward declaration for the sink.
    template <sound_generator>
    struct basic_sound_sink;

    /// Play synthesized audio through the system audio backend (PipeWire,
    /// loaded at runtime via dlsym, with graceful fallback).
    ///
    /// As a *transformer* it sits in the pipeline and automatically plays
    /// press/release sounds for key events:
    /// @code
    ///   | fs8::sound_player
    /// @endcode
    ///
    /// As a *factory* it creates explicit sinks for use inside `on[...]`:
    /// @code
    ///   | fs8::on[fs8::keydown[KEY_ENTER], fs8::sound_player.play(fs8::sound_id::confirm)]
    /// @endcode
    ///
    /// To use a custom generator:
    /// @code
    ///   | fs8::sound_player.with(my_synth{})
    /// @endcode
    template <sound_generator Gen = basic_synth>
    struct [[nodiscard]] basic_sound_player : pimpl_idiom<basic_sound_player<Gen>> {
        using pimpl_idiom<basic_sound_player>::pimpl_idiom;

      private:
        Gen gen_{};

      public:
        constexpr basic_sound_player() noexcept = default;

        constexpr explicit basic_sound_player(Gen const& g) noexcept : gen_{g} {}

        /// Create a sink that plays the given sound when invoked.
        [[nodiscard]] consteval basic_sound_sink<Gen> play(sound_id const id) const noexcept {
            return basic_sound_sink<Gen>{id};
        }

        /// Lifecycle: initialise the audio backend on start.
        template <Context CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_io_manager, CtxT>,
                          "sound_player requires io_manager in the pipeline. "
                          "Place it in the main pipeline, not in a router sub-pipeline.");
            if (tag.code == start.code) {
                return do_start(ctx.mod(io_manager));
            }
            return drop_event;
        }

        /// play press/release
        context_action operator()(event_type const& event) noexcept {
            using enum context_action;
            if (event.type() != EV_KEY) {
                return next;
            }
            if (event.value() <= 1) {
                // key press and key up
                play_sound(static_cast<sound_id>(event.value()));
            }
            return next;
        }

        /// Play a sound (called by the sink or transformer).
        void play_sound(sound_id id) noexcept;

      private:
        context_action do_start(basic_io_manager& io) noexcept;
    };

    /// Default player instance using basic_synth.
    constexpr basic_sound_player sound_player;

    static_assert(Modifier<basic_sound_player<basic_synth>>);

    /// A lightweight sink mod that plays a specific sound when invoked by
    /// `on[...]`.  Finds the player via `ctx.mod(...)` at runtime.
    template <sound_generator Gen>
    struct [[nodiscard]] basic_sound_sink : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        sound_id id_ = sound_id::tick;

      public:
        constexpr basic_sound_sink() noexcept = default;

        constexpr explicit basic_sound_sink(sound_id const id) noexcept : id_{id} {}

        template <Context CtxT>
            requires has_mod<basic_sound_player<Gen>, CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            ctx.mod(basic_sound_player<Gen>{}).play_sound(id_);
            return context_action::next;
        }
    };

} // namespace fs8
