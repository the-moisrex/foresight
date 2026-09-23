// Created by moisrex on 9/12/26.

module;
#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <type_traits>
#include <utility>
export module fs8.mods:tilt;
import fs8.context;
import fs8.devices.evdev;
import fs8.easings;
import fs8.event;
import fs8.traits;
import :input_manager;

export namespace fs8 {

    /// Default full-tilt speed factor (see `tilt_speed_options::max`).
    inline constexpr float tilt_default_max_speed_factor = 2.0F;

    /// Fallback tilt range (device counts) used when the tablet doesn't expose
    /// `ABS_TILT_X` / `ABS_TILT_Y` info. Keeps `tilt_state` usable and testable
    /// on tilt-less pipelines.
    inline constexpr float tilt_default_tilt_range = 9000.0F;

    /// Numeric knobs for `tilt_speed`. `base` is the factor at (or below)
    /// `start` tilt, `max` the factor at (or above) `end` tilt. `max < base`
    /// decelerates, `max > base` accelerates.
    struct [[nodiscard]] tilt_speed_options {
        float base  = 1.0F;
        float max   = tilt_default_max_speed_factor;
        float start = 0.0F;
        float end   = 1.0F;
    };

    /// Controls `tilt_state`'s base-tilt (neutral) tracking.
    ///
    /// The base is the tilt the user naturally holds the pen at; it is
    /// subtracted from the raw tilt so that the natural hold reads as zero.
    struct [[nodiscard]] tilt_base_options {
        /// Time constant of the continuous recenter, in seconds. Each tilt
        /// frame drags the base toward the current tilt with
        /// `alpha = 1 - exp(-dt / recenter_time)`, which makes the rate
        /// independent of the tablet's report frequency. `<= 0` disables
        /// continuous recentering (proximity capture still applies).
        ///
        /// Smaller values adapt faster but also absorb a deliberately held
        /// tilt sooner; larger values preserve intentional tilts for longer.
        float recenter_time = 2.0F;
    };

    // ── State ──────────────────────────────────────────────────────────────

    /// Tracks the pen's absolute tilt (`ABS_TILT_X` / `ABS_TILT_Y`) and exposes
    /// normalized, cached values for the tilt action mods.
    ///
    /// The natural hold is tracked as a per-axis *base* tilt: it is captured
    /// every time the pen comes into proximity and continuously recentered
    /// toward the current tilt (see `tilt_base_options`). All exposed
    /// normalized values are relative to that base.
    ///
    /// All derived values (normalized axes, magnitude, per-event change) are
    /// computed only when a tilt event arrives, so movement events never pay
    /// for `sqrt`. `version()` is bumped on every change; actions use it to
    /// recompute their (potentially eased) factor only when needed.
    constexpr struct [[nodiscard]] basic_tilt_state : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

      private:
        value_type    tilt_x_    = 0;
        value_type    tilt_y_    = 0;
        value_type    last_x_    = 0;
        value_type    last_y_    = 0;
        float         norm_x_    = 0.0F;
        float         norm_y_    = 0.0F;
        float         magnitude_ = 0.0F; // normalized tilt magnitude, 0..1
        float         change_    = 0.0F; // normalized magnitude of the last tilt delta, 0..1
        float         base_x_    = 0.0F; // normalized neutral tilt (subtracted from raw)
        float         base_y_    = 0.0F; // normalized neutral tilt (subtracted from raw)
        float         range_x_   = tilt_default_tilt_range;
        float         range_y_   = tilt_default_tilt_range;
        std::uint32_t version_   = 1U;

        tilt_base_options         options{};
        std::chrono::microseconds last_update_time_{};
        bool                      pending_base_capture_ = false;

        void update(value_type value, bool is_x, std::chrono::microseconds now) noexcept;

        /// Capture the current tilt as the neutral base (called on proximity).
        void begin_proximity(std::chrono::microseconds now) noexcept;

        /// Read `ABS_TILT_X` / `ABS_TILT_Y` ranges from `dev`. Returns false if
        /// the device has no tilt axes (so the scan can try the next device).
        bool seed_range(evdev const& dev) noexcept;

      public:
        consteval basic_tilt_state operator[](tilt_base_options const& inp_options) const noexcept {
            basic_tilt_state res{*this};
            res.options = inp_options;
            return res;
        }

        context_action operator()(event_type const& event) noexcept;

        template <Context CtxT>
        context_action operator()(CtxT& ctx, control_event const& tag) noexcept {
            using enum context_action;
            if (tag.code == start.code) {
                init(ctx);
                return next;
            }
            if (tag.code == toggle_off.code) {
                reset();
                return next;
            }
            // Anything else (load_event, next_event, ...) is not ours; return
            // drop_event so we don't clobber another mod's result.
            return drop_event;
        }

        /// Seed the tilt range from the tablet's `ABS_TILT_*` info so values
        /// can be normalized. Falls back to the built-in defaults if no
        /// device (or no tilt axis) is present, which keeps it testable and
        /// usable on tilt-less pipelines.
        template <Context CtxT>
        void init(CtxT& ctx) noexcept {
            reset();
            if constexpr (has_mod<basic_input_manager, CtxT>) {
                for (evdev const& dev : ctx.mod(input_manager).devices()) {
                    if (seed_range(dev)) {
                        break;
                    }
                }
            }
        }

        void reset() noexcept;

        [[nodiscard]] constexpr float norm_x() const noexcept {
            return norm_x_;
        }

        [[nodiscard]] constexpr float norm_y() const noexcept {
            return norm_y_;
        }

        [[nodiscard]] constexpr float normalized_magnitude() const noexcept {
            return magnitude_;
        }

        [[nodiscard]] constexpr float change() const noexcept {
            return change_;
        }

        [[nodiscard]] constexpr float base_x() const noexcept {
            return base_x_;
        }

        [[nodiscard]] constexpr float base_y() const noexcept {
            return base_y_;
        }

        [[nodiscard]] constexpr std::uint32_t version() const noexcept {
            return version_;
        }

        [[nodiscard]] constexpr bool is_tilted(float const threshold) const noexcept {
            return magnitude_ >= threshold;
        }

        [[nodiscard]] constexpr bool is_changing(float const threshold) const noexcept {
            return change_ >= threshold;
        }
    } tilt_state;

    // ── Conditions ─────────────────────────────────────────────────────────

    /// True while the pen tilt magnitude is at/above `threshold` (0..1).
    constexpr struct [[nodiscard]] basic_tilted : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        consteval basic_tilted operator[](float const inp_threshold) const noexcept {
            basic_tilted res{*this};
            res.threshold = inp_threshold;
            return res;
        }

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_tilt_state, CtxT>, "We need tilt_state to be in the pipeline.");
            return ctx.mod(tilt_state).is_tilted(threshold);
        }

      private:
        float threshold = 0.5F;
    } tilted;

    /// True while the tilt is changing by at least `threshold` per event
    /// (normalized 0..1). This is the "the hand is stretching/repositioning"
    /// detector.
    constexpr struct [[nodiscard]] basic_tilt_changing : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        consteval basic_tilt_changing operator[](float const inp_threshold) const noexcept {
            basic_tilt_changing res{*this};
            res.threshold = inp_threshold;
            return res;
        }

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_tilt_state, CtxT>, "We need tilt_state to be in the pipeline.");
            return ctx.mod(tilt_state).is_changing(threshold);
        }

      private:
        float threshold = 0.15F;
    } tilt_changing;

    // ── Callables ──────────────────────────────────────────────────────────
    //
    // The tilt actions take real callables instead of tag structs, e.g.
    //
    //   tilt_speed[tilt_rel, tilt_isotropic, easeOutCubic<float>, {...}]
    //
    //   * a *domain* classifies the movement event: `tilt_abs` rewrites
    //     `ABS_X`/`ABS_Y` (place before `abs2rel`), `tilt_rel` rewrites
    //     `REL_X`/`REL_Y` (place after);
    //   * a *mapping* turns the tilt state into per-axis normalized amounts:
    //     `tilt_isotropic` uses the tilt magnitude, `tilt_per_axis` uses each
    //     axis separately;
    //   * a *curve* is any `float(float)` easing (see `fs8.easings`),
    //     e.g. `linear<float>`, `easeOutCubic<float>`.
    //
    // Any of them can be replaced with your own function of the matching
    // signature, which is also why they are passed as ordinary arguments.

    /// Per-axis classification of a movement event, shared by all tilt actions.
    struct [[nodiscard]] tilt_axis {
        bool valid  = false;
        bool is_x   = false;
        bool is_abs = false;
        bool reset  = false;
    };

    /// Scratch space the tilt domains keep between events.
    struct [[nodiscard]] tilt_scale_state {
        event_type::value_type x_last = 0;
        event_type::value_type y_last = 0;
        event_type::value_type x_out  = 0;
        event_type::value_type y_out  = 0;
        float                  x_eps  = 0.0F;
        float                  y_eps  = 0.0F;
        bool                   x_init = false;
        bool                   y_init = false;
    };

    struct [[nodiscard]] tilt_push_options {
        float gain      = 1.0F;
        float dead_zone = 0.0F;
    };

    using tilt_domain_fn  = tilt_axis (*)(event_type const& event) noexcept;
    using tilt_mapping_fn = void (*)(basic_tilt_state const& state, float& t_x, float& t_y) noexcept;
    using tilt_curve_fn   = float (*)(float t) noexcept;

    namespace tilt_detail {

        // Domain classifiers.

        tilt_axis classify_abs(event_type const& event) noexcept;
        tilt_axis classify_rel(event_type const& event) noexcept;

        // Tilt-to-amount mappings.

        void isotropic_mapping(basic_tilt_state const& state, float& t_x, float& t_y) noexcept;
        void per_axis_mapping(basic_tilt_state const& state, float& t_x, float& t_y) noexcept;

        // Domain application. Kept out of line so the actions stay declaration.

        context_action apply_speed(event_type& event, tilt_axis axis, tilt_scale_state& scale, float x_factor, float y_factor) noexcept;
        context_action apply_freeze(event_type& event, tilt_axis axis, tilt_scale_state& scale, bool frozen) noexcept;
        context_action apply_push(
          event_type&              event,
          tilt_axis                axis,
          tilt_scale_state&        scale,
          float                    x_norm,
          float                    y_norm,
          tilt_push_options const& options) noexcept;

    } // namespace tilt_detail

    inline constexpr tilt_domain_fn  tilt_abs       = tilt_detail::classify_abs;
    inline constexpr tilt_domain_fn  tilt_rel       = tilt_detail::classify_rel;
    inline constexpr tilt_mapping_fn tilt_isotropic = tilt_detail::isotropic_mapping;
    inline constexpr tilt_mapping_fn tilt_per_axis  = tilt_detail::per_axis_mapping;

    // ── Actions ────────────────────────────────────────────────────────────

    /// Rescale movement based on the pen's tilt.
    ///
    /// The factor is `base + (max - base) * curve(normalized_tilt)` and is
    /// cached until the tilt actually changes.
    constexpr struct [[nodiscard]] basic_tilt_speed : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        tilt_speed_options options{};
        tilt_domain_fn     domain_  = tilt_rel;
        tilt_mapping_fn    mapping_ = tilt_isotropic;
        tilt_curve_fn      curve_   = easeOutCubic<float>;

        tilt_scale_state scale{};
        float            x_factor_       = 1.0F;
        float            y_factor_       = 1.0F;
        std::uint32_t    cached_version_ = 0U;

        [[nodiscard]] float map(float const t) const noexcept {
            float const span = options.end - options.start;
            float       n    = 0.0F;
            if (span > 0.0F) {
                n = std::clamp((t - options.start) / span, 0.0F, 1.0F);
            } else {
                n = t >= options.start ? 1.0F : 0.0F;
            }
            return options.base + ((options.max - options.base) * curve_(n));
        }

        void refresh(basic_tilt_state const& state) noexcept {
            float t_x = 0.0F;
            float t_y = 0.0F;
            mapping_(state, t_x, t_y);
            x_factor_ = map(t_x);
            y_factor_ = map(t_y);
        }

      public:
        template <typename... Args>
            requires((!detail::is_tag_type<std::decay_t<Args>>) && ...)
        consteval basic_tilt_speed operator[](Args&&... args) const noexcept {
            basic_tilt_speed res{*this};
            auto const       assign = [&res]<typename ArgT>(ArgT&& arg) constexpr noexcept {
                using decayed = std::decay_t<ArgT>;
                if constexpr (std::same_as<decayed, tilt_speed_options>) {
                    res.options = std::forward<ArgT>(arg);
                } else if constexpr (std::same_as<decayed, tilt_domain_fn>) {
                    res.domain_ = std::forward<ArgT>(arg);
                } else if constexpr (std::same_as<decayed, tilt_mapping_fn>) {
                    res.mapping_ = std::forward<ArgT>(arg);
                } else if constexpr (std::same_as<decayed, tilt_curve_fn>) {
                    res.curve_ = std::forward<ArgT>(arg);
                }
            };
            (assign(std::forward<Args>(args)), ...);
            return res;
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_tilt_state, CtxT>, "We need tilt_state to be in the pipeline.");
            auto const& state = ctx.mod(tilt_state);
            if (cached_version_ != state.version()) {
                refresh(state);
                cached_version_ = state.version();
            }
            auto const axis = domain_(ctx.event());
            if (axis.reset) {
                scale = {};
                return next;
            }
            if (!axis.valid) {
                return next;
            }
            return tilt_detail::apply_speed(ctx.event(), axis, scale, x_factor_, y_factor_);
        }
    } tilt_speed;

    /// Freeze movement while the tilt is changing faster than `threshold`
    /// (the "hand is stretching, not moving the cursor" case).
    ///
    /// In `tilt_rel` domain movement events are zeroed; in `tilt_abs` domain
    /// the emitted `ABS_X`/`ABS_Y` is held so `abs2rel` sees a zero delta.
    constexpr struct [[nodiscard]] basic_tilt_freeze : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        tilt_domain_fn   domain_   = tilt_rel;
        float            threshold = 0.15F;
        tilt_scale_state scale{};

      public:
        template <typename... Args>
            requires((!detail::is_tag_type<std::decay_t<Args>>) && ...)
        consteval basic_tilt_freeze operator[](Args&&... args) const noexcept {
            basic_tilt_freeze res{*this};
            auto const        assign = [&res]<typename ArgT>(ArgT&& arg) constexpr noexcept {
                using decayed = std::decay_t<ArgT>;
                if constexpr (std::same_as<decayed, float>) {
                    res.threshold = std::forward<ArgT>(arg);
                } else if constexpr (std::same_as<decayed, tilt_domain_fn>) {
                    res.domain_ = std::forward<ArgT>(arg);
                }
            };
            (assign(std::forward<Args>(args)), ...);
            return res;
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_tilt_state, CtxT>, "We need tilt_state to be in the pipeline.");
            auto const axis = domain_(ctx.event());
            if (axis.reset) {
                scale = {};
                return next;
            }
            if (!axis.valid) {
                return next;
            }
            bool const frozen = ctx.mod(tilt_state).is_changing(threshold);
            return tilt_detail::apply_freeze(ctx.event(), axis, scale, frozen);
        }
    } tilt_freeze;

    /// Nudge movement in the direction the pen is tilted, only on movement
    /// events (so merely tilting does not drift the cursor). `gain` is in the
    /// domain's units: pixels for `tilt_rel`, raw `ABS_*` counts for `tilt_abs`.
    constexpr struct [[nodiscard]] basic_tilt_push : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        tilt_push_options options{};
        tilt_domain_fn    domain_ = tilt_rel;
        tilt_scale_state  scale{};

      public:
        template <typename... Args>
            requires((!detail::is_tag_type<std::decay_t<Args>>) && ...)
        consteval basic_tilt_push operator[](Args&&... args) const noexcept {
            basic_tilt_push res{*this};
            auto const      assign = [&res]<typename ArgT>(ArgT&& arg) constexpr noexcept {
                using decayed = std::decay_t<ArgT>;
                if constexpr (std::same_as<decayed, tilt_push_options>) {
                    res.options = std::forward<ArgT>(arg);
                } else if constexpr (std::same_as<decayed, float>) {
                    res.options.gain = std::forward<ArgT>(arg);
                } else if constexpr (std::same_as<decayed, tilt_domain_fn>) {
                    res.domain_ = std::forward<ArgT>(arg);
                }
            };
            (assign(std::forward<Args>(args)), ...);
            return res;
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            using enum context_action;
            static_assert(has_mod<basic_tilt_state, CtxT>, "We need tilt_state to be in the pipeline.");
            auto const& state = ctx.mod(tilt_state);
            auto const  axis  = domain_(ctx.event());
            if (axis.reset) {
                scale = {};
                return next;
            }
            if (!axis.valid) {
                return next;
            }
            return tilt_detail::apply_push(ctx.event(), axis, scale, state.norm_x(), state.norm_y(), options);
        }
    } tilt_push;

} // namespace fs8
