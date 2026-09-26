// Created by moisrex on 9/18/26.

module;
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <tuple>
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

    /// Type-erased sound generator that dispatches through function pointers.
    ///
    /// Use `dynamic_synth::register_synth(my_synth{})` to wire any
    /// `sound_generator` into a single `basic_sound_player` type, avoiding
    /// template bloat when the generator is chosen at runtime.
    struct [[nodiscard]] dynamic_synth {
        [[nodiscard]] std::size_t duration_frames(event_type const& event, sound_format const fmt) const noexcept {
            return s_duration_fn ? s_duration_fn(event, fmt) : 0;
        }

        void render(event_type const& event, sound_format const fmt, std::span<float> dest) const noexcept {
            if (s_render_fn) {
                s_render_fn(event, fmt, dest);
            }
        }

        /// Register a `sound_generator` as the active synth.
        ///
        /// Calling this multiple times replaces the previous registration.
        /// The generator instance is a function-local `static const`, so it
        /// is initialised once and lives for the duration of the program.
        template <sound_generator Gen>
        static void register_synth(Gen const& = Gen{}) {
            static Gen const gen{};
            s_duration_fn = +[](event_type const& e, sound_format const f) noexcept -> std::size_t {
                return gen.duration_frames(e, f);
            };
            s_render_fn = +[](event_type const& e, sound_format const f, std::span<float> d) noexcept {
                gen.render(e, f, d);
            };
        }

      private:
        using duration_fn_t = std::size_t (*)(event_type const&, sound_format const) noexcept;
        using render_fn_t   = void (*)(event_type const&, sound_format const, std::span<float>) noexcept;

        static inline duration_fn_t s_duration_fn = nullptr;
        static inline render_fn_t   s_render_fn   = nullptr;
    };

    static_assert(sound_generator<dynamic_synth>);

    // Forward declaration for the sink.
    template <sound_generator>
    struct basic_sound_sink;

    /// Sound slot pool constants.
    inline constexpr std::size_t max_slot_frames  = queue_sample_rate * 150 / 1000;   // 7200
    inline constexpr std::size_t max_slot_samples = max_slot_frames * queue_channels; // 14400
    inline constexpr std::size_t slot_pool_size   = 16;
    /// Length of the raised-cosine end fade applied to every committed slot
    /// (5 ms), so playback always reaches digital silence instead of stopping
    /// dead mid-ring (very audible on headphones).
    inline constexpr std::size_t slot_fade_frames = queue_sample_rate * 5 / 1000; // 240

    /// Master click-volume bounds and step, in dBFS.  `volume_muted_db` is
    /// a sentinel below `volume_min_db` meaning "silent": it lets
    /// `volume_down` reach exact silence and `volume_up` unmute back to the
    /// quietest step (a purely multiplicative ramp could never leave 0).
    inline constexpr float volume_min_db   = -60.0f;
    inline constexpr float volume_max_db   = 6.0f;
    inline constexpr float volume_step_db  = 3.0f;
    inline constexpr float volume_muted_db = -200.0f;

    /// Apply the slot end fade in-place to an interleaved (`queue_channels`)
    /// slot buffer: the last `slot_fade_frames` frames are multiplied by a
    /// raised-cosine ramp ending at exactly 0.  Samples before the fade
    /// region are left untouched.
    void apply_slot_fade(std::span<float> samples) noexcept;

    /// Returned by acquire_slot: a span into a slot buffer + its index.
    struct [[nodiscard]] slot_buffer {
        std::span<float> samples{};
        std::size_t      index = 0;
    };

    /// Non-template base that owns the audio backend and pimpl.
    ///
    /// Separates the template-independent backend lifecycle (init,
    /// io_manager registration) from the template-dependent generator.
    struct [[nodiscard]] basic_sound_player_core : pimpl_idiom<basic_sound_player_core> {
        using pimpl_idiom::pimpl_idiom;

        constexpr basic_sound_player_core() noexcept = default;

        /// Initialise the audio backend (called on start).
        context_action ensure_backend(basic_io_manager& io) noexcept;

        /// Acquire a sound slot for rendering.  Returns an empty span if
        /// the pool is exhausted and no slot could be stolen.
        [[nodiscard]] slot_buffer acquire_slot(std::size_t sample_count) noexcept;

        /// Mark a previously acquired slot as active.  Applies the end fade
        /// and computes peak amplitude for slot-stealing heuristics.
        void commit_slot(std::size_t index, std::size_t sample_count) noexcept;

        /// Toggle the paused state. When paused, no sounds are played.
        bool toggle_pause() noexcept;

        /// Set the paused state explicitly.
        void set_paused(bool paused) noexcept;

        /// Returns true if the player is paused.
        [[nodiscard]] bool is_paused() const noexcept;

        /// Set the master click volume from a linear gain (≤ 0 mutes, above
        /// `volume_max_db` clamps).  The gain is applied by the mixer, so it
        /// also affects sounds that are already playing.
        void set_volume(float gain) noexcept;

        /// Current master click volume as a linear gain (0 when muted).
        [[nodiscard]] float get_volume() const noexcept;

        /// Raise the master click volume by `volume_step_db` (+3 dB),
        /// unmuting to `volume_min_db` first.  Clamped to `volume_max_db`.
        /// Returns the new linear gain.
        float volume_up() noexcept;

        /// Lower the master click volume by `volume_step_db` (-3 dB), down
        /// to `volume_min_db` and then to exact silence.  Returns the new
        /// linear gain.
        float volume_down() noexcept;

      private:
        /// Step the stored dB value by `delta_db` with mute/unmute and
        /// bounds handling, store it, log the result, and return the new
        /// linear gain.
        float adjust_volume(float delta_db) noexcept;
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

        /// Render into a sound slot.  The audio backend pulls mixed
        /// audio from active slots on demand (via the on_process callback).
        void play_event(event_type const& event) noexcept {
            sound_format const fmt{
              .sample_rate = queue_sample_rate,
              .channels    = queue_channels,
            };

            auto const frames = gen_.duration_frames(event, fmt);
            if (frames == 0) [[unlikely]] {
                return;
            }

            auto const sample_count = frames * queue_channels;
            if (sample_count > max_slot_samples) [[unlikely]] {
                return;
            }

            auto slot = acquire_slot(sample_count);
            if (slot.samples.empty()) [[unlikely]] {
                return;
            }

            gen_.render(event, fmt, slot.samples);
            commit_slot(slot.index, sample_count);
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

        constexpr basic_sound_sink(uint16_t const code, bool const pressed) noexcept : code_{code}, pressed_{pressed} {}

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
            context_action result = next;
            std::apply(
              [&](auto&... mod) noexcept {
                  (([&]() noexcept {
                       if constexpr (requires { mod.toggle_pause(); }) {
                           result = mod.toggle_pause() ? next : drop_event;
                       }
                   }()),
                   ...);
              },
              ctx.get_mods());
            return result;
        }
    } toggle_sound_pause;

    /// Raise the player's master click volume by 3 dB (clamped).  Returns
    /// `next`, so the trigger key still clicks — at the new volume.
    constexpr struct [[nodiscard]] basic_sound_volume_up {
        template <Context CtxT>
        context_action operator()(CtxT& ctx) const noexcept {
            std::apply(
              [&](auto&... mod) noexcept {
                  (([&]() noexcept {
                       if constexpr (requires { mod.volume_up(); }) {
                           (void) mod.volume_up();
                       }
                   }()),
                   ...);
              },
              ctx.get_mods());
            return context_action::next;
        }
    } sound_volume_up;

    /// Lower the player's master click volume by 3 dB (clamped to
    /// silence).  Returns `next`, so the trigger key still clicks — at the
    /// new volume.
    constexpr struct [[nodiscard]] basic_sound_volume_down {
        template <Context CtxT>
        context_action operator()(CtxT& ctx) const noexcept {
            std::apply(
              [&](auto&... mod) noexcept {
                  (([&]() noexcept {
                       if constexpr (requires { mod.volume_down(); }) {
                           (void) mod.volume_down();
                       }
                   }()),
                   ...);
              },
              ctx.get_mods());
            return context_action::next;
        }
    } sound_volume_down;

} // namespace fs8
