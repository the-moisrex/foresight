// Created by moisrex on 9/18/26.

module;
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <utility>

export module fs8.mods:sound;
import fs8.context;
import fs8.event;
import fs8.log;
import fs8.traits;
import fs8.pimpl;
import fs8.sound;
import :io_manager;

export namespace fs8 {

    /// Audio format supplied to a sound generator.
    struct [[nodiscard]] sound_format {
        uint32_t sample_rate = 48'000;
        uint16_t channels    = 2;
    };

    /// A sound generator produces audio for a given input event.
    ///
    /// This is the extension point: implement this concept to provide a
    /// different palette (FM synth, wavetable, sampled assets, ...) without
    /// touching the player.  `render` must be `noexcept` and write
    /// `duration_frames * format.channels` interleaved float samples into
    /// the destination span.  Return 0 from `duration_frames` for events
    /// this generator does not handle.
    template <typename T>
    concept sound_generator = requires(T const& gen, event_type const& event, sound_format const fmt, std::span<float> dest) {
        { gen.duration_frames(event, fmt) } noexcept -> std::convertible_to<std::size_t>;
        { gen.render(event, fmt, dest) } noexcept;
    };

    /// A tiny self-contained synthesizer: sine carrier with exponential
    /// decay, per-event pitch.  No assets, no dependencies.
    struct [[nodiscard]] basic_synth {
        constexpr basic_synth() noexcept = default;

        [[nodiscard]] constexpr std::size_t duration_frames(event_type const& event, sound_format const fmt) const noexcept {
            if (event.type() != EV_KEY || event.value() > 1) {
                return 0;
            }
            // value 0 = release, value 1 = press
            static constexpr std::array<uint8_t, 2u> ids{45, 45};
            return static_cast<std::size_t>(fmt.sample_rate) * ids[event.value()] / 1000;
        }

        void render(event_type const& event, sound_format fmt, std::span<float> dest) const noexcept;
    };

    static_assert(sound_generator<basic_synth>);

    // Forward declaration for the sink.
    template <sound_generator>
    struct basic_sound_sink;

    /// Non-template base that owns the audio backend and pimpl.
    ///
    /// Separates the template-independent backend lifecycle (init, push,
    /// io_manager registration) from the template-dependent generator.
    struct [[nodiscard]] basic_sound_player_core : pimpl_idiom<basic_sound_player_core> {
        using pimpl_idiom::pimpl_idiom;

        constexpr basic_sound_player_core() noexcept = default;

        /// Initialise the audio backend (called on start).
        context_action ensure_backend(basic_io_manager& io) noexcept;

        /// Push interleaved samples to the audio backend.
        void push_samples(std::span<float const> samples) noexcept;

        /// Toggle the paused state. When paused, no sounds are played.
        bool toggle_pause() noexcept;

        /// Set the paused state explicitly.
        void set_paused(bool paused) noexcept;

        /// Returns true if the player is paused.
        [[nodiscard]] bool is_paused() const noexcept;
    };

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
    ///   | fs8::on[fs8::keydown[KEY_ENTER], fs8::sound_player.play(KEY_ENTER, true)]
    /// @endcode
    ///
    /// To use a custom generator:
    /// @code
    ///   | fs8::basic_sound_player(my_synth{})
    /// @endcode
    template <sound_generator Gen = basic_synth>
    struct [[nodiscard]] basic_sound_player : basic_sound_player_core {
        Gen gen_{};

        constexpr basic_sound_player() noexcept = default;

        constexpr explicit basic_sound_player(Gen const& g) noexcept : gen_{g} {}

        /// Create a sink that plays a key event sound when invoked.
        [[nodiscard]] consteval basic_sound_sink<Gen> play(uint16_t const code, bool const pressed) const noexcept {
            return basic_sound_sink<Gen>{code, pressed};
        }

        /// Lifecycle: initialise the audio backend on start.
        template <Context CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_io_manager, CtxT>,
                          "sound_player requires io_manager in the pipeline. "
                          "Place it in the main pipeline, not in a router sub-pipeline.");
            if (tag.code == start.code) {
                return ensure_backend(ctx.mod(io_manager));
            }
            return drop_event;
        }

        /// Play press/release sounds for key events.
        context_action operator()(event_type const& event) noexcept {
            using enum context_action;
            if (is_paused()) {
                return next;
            }
            if (event.type() != EV_KEY) {
                return next;
            }
            if (event.value() <= 1) {
                play_event(event);
            }
            return next;
        }

        /// Render and push audio for the given event.
        void play_event(event_type const& event) noexcept {
            sound_format const fmt{
              .sample_rate = queue_sample_rate,
              .channels    = queue_channels,
            };

            auto const frames = gen_.duration_frames(event, fmt);
            if (frames == 0) [[unlikely]] {
                return;
            }

            constexpr std::size_t max_frames = queue_sample_rate * 150 / 1000;
            if (frames > max_frames) [[unlikely]] {
                return;
            }

            std::array<float, max_frames * queue_channels> samples{};
            gen_.render(event, fmt, std::span<float>{samples.data(), frames * queue_channels});
            push_samples(std::span<float const>{samples.data(), frames * queue_channels});
        }
    };

    /// Default player instance using basic_synth.
    constexpr basic_sound_player sound_player;

    static_assert(Modifier<basic_sound_player<basic_synth>>);

    /// A lightweight sink mod that plays a key event sound when invoked by
    /// `on[...]`.  Stores the keycode and pressed state; reconstructs a
    /// minimal event at invocation time.  Finds the player via
    /// `ctx.mod(...)` at runtime.
    template <sound_generator Gen>
    struct [[nodiscard]] basic_sound_sink : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        uint16_t code_    = 0;
        bool     pressed_ = false;

      public:
        constexpr basic_sound_sink() noexcept = default;

        constexpr basic_sound_sink(uint16_t const code, bool const pressed) noexcept
            : code_{code}, pressed_{pressed} {}

        template <Context CtxT>
            requires has_mod<basic_sound_player<Gen>, CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            event_type event;
            event.set(EV_KEY, code_, pressed_ ? 1 : 0);
            ctx.mod(basic_sound_player<Gen>{}).play_event(event);
            return context_action::next;
        }
    };

    constexpr struct [[nodiscard]] basic_toggle_sound_pause {
        template <Context CtxT>
        context_action operator()(CtxT& ctx) const noexcept {
            using enum context_action;
            return ctx.mod(sound_player).toggle_pause() ? next : drop_event;
        }
    } toggle_sound_pause;

} // namespace fs8
