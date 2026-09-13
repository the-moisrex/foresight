// Created by moisrex on 9/12/26.

module;
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <type_traits>
export module fs8.mods:tilt;
import fs8.context;
import fs8.devices.evdev;
import fs8.easings;
import fs8.event;
import fs8.traits;
import :input_manager;

export namespace fs8 {

    // ── Compile-time selection tags ────────────────────────────────────────
    //
    // These are passed to the action mods through `operator[]`; because they
    // are types, the selection is resolved at compile time and disappears from
    // the generated code (no runtime flag/branch).
    //
    //   tilt_speed[tilt_abs, tilt_per_axis, tilt_curve_out_cubic, {...}]
    //
    // Domains: which axis-space to rewrite (ABS before abs2rel, REL after).
    // Mappings: how tilt maps to a scale factor (one factor vs per-axis).
    // Curves: how the normalized tilt is eased before becoming a factor.

    struct [[nodiscard]] tilt_abs_domain_t {};

    struct [[nodiscard]] tilt_rel_domain_t {};

    inline constexpr tilt_abs_domain_t tilt_abs{};
    inline constexpr tilt_rel_domain_t tilt_rel{};

    struct [[nodiscard]] tilt_isotropic_t {};

    struct [[nodiscard]] tilt_per_axis_t {};

    inline constexpr tilt_isotropic_t tilt_isotropic{};
    inline constexpr tilt_per_axis_t  tilt_per_axis{};

    struct [[nodiscard]] tilt_linear_t {};

    struct [[nodiscard]] tilt_out_quad_t {};

    struct [[nodiscard]] tilt_out_cubic_t {};

    struct [[nodiscard]] tilt_out_sine_t {};

    inline constexpr tilt_linear_t    tilt_curve_linear{};
    inline constexpr tilt_out_quad_t  tilt_curve_out_quad{};
    inline constexpr tilt_out_cubic_t tilt_curve_out_cubic{};
    inline constexpr tilt_out_sine_t  tilt_curve_out_sine{};

    /// Default full-tilt speed factor (see `tilt_speed_options::max`).
    inline constexpr float tilt_default_max_speed_factor = 2.0F;

    /// Numeric knobs for `tilt_speed`. `base` is the factor at (or below)
    /// `start` tilt, `max` the factor at (or above) `end` tilt. `max < base`
    /// decelerates, `max > base` accelerates.
    struct [[nodiscard]] tilt_speed_options {
        float base  = 1.0F;
        float max   = tilt_default_max_speed_factor;
        float start = 0.0F;
        float end   = 1.0F;
    };

    // ── State ──────────────────────────────────────────────────────────────

    /// Tracks the pen's absolute tilt (`ABS_TILT_X` / `ABS_TILT_Y`) and exposes
    /// normalized, cached values for the tilt action mods.
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
        float         range_x_   = 9000.0F;
        float         range_y_   = 9000.0F;
        std::uint32_t version_   = 1U;

        void update(value_type value, bool is_x) noexcept;

      public:
        context_action operator()(event_type const& event) noexcept;

        template <Context CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
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
            if constexpr (has_mod<basic_input_manager, CtxT>) {
                for (evdev const& dev : ctx.mod(input_manager).devices()) {
                    auto const* x_info = dev.abs_info(ABS_TILT_X);
                    auto const* y_info = dev.abs_info(ABS_TILT_Y);
                    if (x_info != nullptr && y_info != nullptr) {
                        range_x_ = static_cast<float>(std::max(std::abs(x_info->minimum), std::abs(x_info->maximum)));
                        range_y_ = static_cast<float>(std::max(std::abs(y_info->minimum), std::abs(y_info->maximum)));
                        if (range_x_ <= 0.0F) {
                            range_x_ = 9000.0F;
                        }
                        if (range_y_ <= 0.0F) {
                            range_y_ = 9000.0F;
                        }
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

    // ── Compile-time argument plumbing ─────────────────────────────────────

    namespace tilt_detail {

        template <typename T>
        struct domain_of {
            static constexpr bool value = false;
        };

        template <>
        struct domain_of<tilt_abs_domain_t> {
            static constexpr bool value = true;
        };

        template <>
        struct domain_of<tilt_rel_domain_t> {
            static constexpr bool value = true;
        };

        template <typename T>
        struct mapping_of {
            static constexpr bool value = false;
        };

        template <>
        struct mapping_of<tilt_isotropic_t> {
            static constexpr bool value = true;
        };

        template <>
        struct mapping_of<tilt_per_axis_t> {
            static constexpr bool value = true;
        };

        template <typename T>
        struct curve_of {
            static constexpr bool value = false;
        };

        template <>
        struct curve_of<tilt_linear_t> {
            static constexpr bool value = true;
        };

        template <>
        struct curve_of<tilt_out_quad_t> {
            static constexpr bool value = true;
        };

        template <>
        struct curve_of<tilt_out_cubic_t> {
            static constexpr bool value = true;
        };

        template <>
        struct curve_of<tilt_out_sine_t> {
            static constexpr bool value = true;
        };

        template <template <typename> typename Pred, typename Default, typename... Ts>
        struct first_match {
            using type = Default;
        };

        template <template <typename> typename Pred, typename Default, typename T, typename... Ts>
        struct first_match<Pred, Default, T, Ts...> {
            using type = std::
              conditional_t<Pred<std::remove_cvref_t<T>>::value, std::remove_cvref_t<T>, typename first_match<Pred, Default, Ts...>::type>;
        };

        /// Pull the (optional) `tilt_speed_options` out of an argument pack,
        /// falling back to the defaults.
        template <typename... Args>
        [[nodiscard]] constexpr tilt_speed_options find_options(Args&&... args) noexcept {
            tilt_speed_options result{};
            auto const         assign = [&]<typename ArgT>(ArgT&& arg) noexcept {
                if constexpr (std::same_as<std::remove_cvref_t<ArgT>, tilt_speed_options>) {
                    result = arg;
                }
            };
            (assign(std::forward<Args>(args)), ...);
            return result;
        }

    } // namespace tilt_detail

    // ── Actions ────────────────────────────────────────────────────────────

    /// Rescale movement based on the pen's tilt.
    ///
    /// The factor is `base + (max - base) * curve(normalized_tilt)` and is
    /// cached until the tilt actually changes. `tilt_per_axis` maps
    /// `ABS_TILT_X -> X` and `ABS_TILT_Y -> Y`; `tilt_isotropic` uses the tilt
    /// magnitude for both. `tilt_abs` rewrites `ABS_X`/`ABS_Y` (place before
    /// `abs2rel`); `tilt_rel` rewrites `REL_X`/`REL_Y` (place after).
    template <typename DomainT = tilt_rel_domain_t, typename MappingT = tilt_isotropic_t, typename CurveT = tilt_out_cubic_t>
    struct [[nodiscard]] basic_tilt_speed : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

      private:
        tilt_speed_options options{};

        float         x_factor_       = 1.0F;
        float         y_factor_       = 1.0F;
        std::uint32_t cached_version_ = 0U;
        float         x_eps_          = 0.0F;
        float         y_eps_          = 0.0F;
        value_type    x_last_         = 0;
        value_type    y_last_         = 0;
        value_type    x_out_          = 0;
        value_type    y_out_          = 0;
        bool          x_init_         = false;
        bool          y_init_         = false;

        [[nodiscard]] static constexpr float curve(float const t) noexcept {
            if constexpr (std::same_as<CurveT, tilt_out_quad_t>) {
                return easeOutQuad(t);
            } else if constexpr (std::same_as<CurveT, tilt_out_cubic_t>) {
                return easeOutCubic(t);
            } else if constexpr (std::same_as<CurveT, tilt_out_sine_t>) {
                return easeOutSine(t);
            } else {
                return linear(t);
            }
        }

        [[nodiscard]] float map(float const t) const noexcept {
            float const span = options.end - options.start;
            float       n    = 0.0F;
            if (span > 0.0F) {
                n = std::clamp((t - options.start) / span, 0.0F, 1.0F);
            } else {
                n = t >= options.start ? 1.0F : 0.0F;
            }
            return options.base + ((options.max - options.base) * curve(n));
        }

        void refresh(basic_tilt_state const& state) noexcept {
            if constexpr (std::same_as<MappingT, tilt_per_axis_t>) {
                x_factor_ = map(std::abs(state.norm_x()));
                y_factor_ = map(std::abs(state.norm_y()));
            } else {
                float const factor = map(state.normalized_magnitude());
                x_factor_          = factor;
                y_factor_          = factor;
            }
        }

        void reset_scale_state() noexcept {
            x_eps_  = 0.0F;
            y_eps_  = 0.0F;
            x_init_ = false;
            y_init_ = false;
        }

        context_action apply_abs(event_type& event) noexcept {
            using enum context_action;
            switch (event.hash()) {
                case hashed(EV_ABS, ABS_X): {
                    auto const value = event.value();
                    if (!x_init_) {
                        x_last_ = value;
                        x_out_  = value;
                        x_init_ = true;
                        return next;
                    }
                    auto const delta    = static_cast<float>(value - x_last_);
                    x_last_             = value;
                    float const scaled  = (delta * x_factor_) + x_eps_;
                    auto const  pixels  = static_cast<value_type>(scaled);
                    x_eps_              = scaled - static_cast<float>(pixels);
                    x_out_             += pixels;
                    event.value(x_out_);
                    return next;
                }
                case hashed(EV_ABS, ABS_Y): {
                    auto const value = event.value();
                    if (!y_init_) {
                        y_last_ = value;
                        y_out_  = value;
                        y_init_ = true;
                        return next;
                    }
                    auto const delta    = static_cast<float>(value - y_last_);
                    y_last_             = value;
                    float const scaled  = (delta * y_factor_) + y_eps_;
                    auto const  pixels  = static_cast<value_type>(scaled);
                    y_eps_              = scaled - static_cast<float>(pixels);
                    y_out_             += pixels;
                    event.value(y_out_);
                    return next;
                }
                case hashed(EV_KEY, BTN_TOOL_PEN):
                case hashed(EV_KEY, BTN_TOOL_RUBBER):
                case hashed(EV_KEY, BTN_TOOL_BRUSH):
                case hashed(EV_KEY, BTN_TOOL_PENCIL):
                case hashed(EV_KEY, BTN_TOOL_AIRBRUSH):
                case hashed(EV_KEY, BTN_TOOL_FINGER):
                case hashed(EV_KEY, BTN_TOOL_MOUSE):
                case hashed(EV_KEY, BTN_TOOL_LENS): reset_scale_state(); return next;
                default: return next;
            }
        }

        context_action apply_rel(event_type& event) noexcept {
            using enum context_action;
            switch (event.hash()) {
                case hashed(EV_REL, REL_X): {
                    float const scaled = (static_cast<float>(event.value()) * x_factor_) + x_eps_;
                    auto const  out    = static_cast<value_type>(scaled);
                    x_eps_             = scaled - static_cast<float>(out);
                    event.value(out);
                    return next;
                }
                case hashed(EV_REL, REL_Y): {
                    float const scaled = (static_cast<float>(event.value()) * y_factor_) + y_eps_;
                    auto const  out    = static_cast<value_type>(scaled);
                    y_eps_             = scaled - static_cast<float>(out);
                    event.value(out);
                    return next;
                }
                case hashed(EV_KEY, BTN_TOOL_PEN):
                case hashed(EV_KEY, BTN_TOOL_RUBBER):
                case hashed(EV_KEY, BTN_TOOL_BRUSH):
                case hashed(EV_KEY, BTN_TOOL_PENCIL):
                case hashed(EV_KEY, BTN_TOOL_AIRBRUSH):
                case hashed(EV_KEY, BTN_TOOL_FINGER):
                case hashed(EV_KEY, BTN_TOOL_MOUSE):
                case hashed(EV_KEY, BTN_TOOL_LENS): reset_scale_state(); return next;
                default: return next;
            }
        }

      public:
        consteval basic_tilt_speed operator[](tilt_speed_options const& inp_options) const noexcept {
            basic_tilt_speed res{*this};
            res.options = inp_options;
            return res;
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_tilt_state, CtxT>, "We need tilt_state to be in the pipeline.");
            auto const& state = ctx.mod(tilt_state);
            if (cached_version_ != state.version()) {
                refresh(state);
                cached_version_ = state.version();
            }
            if constexpr (std::same_as<DomainT, tilt_abs_domain_t>) {
                return apply_abs(ctx.event());
            } else {
                return apply_rel(ctx.event());
            }
        }
    };

    struct [[nodiscard]] tilt_speed_builder {
        template <typename... Args>
        [[nodiscard]] consteval auto operator[](Args&&... args) const noexcept {
            using domain_t  = tilt_detail::first_match<tilt_detail::domain_of, tilt_rel_domain_t, Args...>::type;
            using mapping_t = tilt_detail::first_match<tilt_detail::mapping_of, tilt_isotropic_t, Args...>::type;
            using curve_t   = tilt_detail::first_match<tilt_detail::curve_of, tilt_out_cubic_t, Args...>::type;

            return basic_tilt_speed<domain_t, mapping_t, curve_t>{}.operator[](tilt_detail::find_options(std::forward<Args>(args)...));
        }
    };

    /// `tilt_speed[tilt_rel, tilt_isotropic, tilt_curve_out_cubic, {.base=1, .max=3}]`
    inline constexpr tilt_speed_builder tilt_speed{};

    /// Freeze movement while the tilt is changing faster than `threshold`
    /// (the "hand is stretching, not moving the cursor" case).
    ///
    /// In `tilt_rel` domain movement events are zeroed; in `tilt_abs` domain
    /// the emitted `ABS_X`/`ABS_Y` is held so `abs2rel` sees a zero delta.
    template <typename DomainT = tilt_rel_domain_t>
    struct [[nodiscard]] basic_tilt_freeze : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

      private:
        float      threshold = 0.15F;
        value_type x_last_   = 0;
        value_type y_last_   = 0;
        value_type x_out_    = 0;
        value_type y_out_    = 0;
        bool       x_init_   = false;
        bool       y_init_   = false;

        void reset_hold() noexcept {
            x_init_ = false;
            y_init_ = false;
        }

        context_action apply_abs(event_type& event, bool const frozen) noexcept {
            using enum context_action;
            switch (event.hash()) {
                case hashed(EV_ABS, ABS_X): {
                    auto const value = event.value();
                    if (!x_init_) {
                        x_last_ = value;
                        x_out_  = value;
                        x_init_ = true;
                        return next;
                    }
                    x_last_ = value;
                    if (frozen) {
                        event.value(x_out_); // hold: abs2rel sees a zero delta
                    } else {
                        x_out_ = value;
                    }
                    return next;
                }
                case hashed(EV_ABS, ABS_Y): {
                    auto const value = event.value();
                    if (!y_init_) {
                        y_last_ = value;
                        y_out_  = value;
                        y_init_ = true;
                        return next;
                    }
                    y_last_ = value;
                    if (frozen) {
                        event.value(y_out_);
                    } else {
                        y_out_ = value;
                    }
                    return next;
                }
                case hashed(EV_KEY, BTN_TOOL_PEN):
                case hashed(EV_KEY, BTN_TOOL_RUBBER):
                case hashed(EV_KEY, BTN_TOOL_BRUSH):
                case hashed(EV_KEY, BTN_TOOL_PENCIL):
                case hashed(EV_KEY, BTN_TOOL_AIRBRUSH):
                case hashed(EV_KEY, BTN_TOOL_FINGER):
                case hashed(EV_KEY, BTN_TOOL_MOUSE):
                case hashed(EV_KEY, BTN_TOOL_LENS): reset_hold(); return next;
                default: return next;
            }
        }

        static context_action apply_rel(event_type& event, bool const frozen) noexcept {
            using enum context_action;
            switch (event.hash()) {
                case hashed(EV_REL, REL_X):
                case hashed(EV_REL, REL_Y):
                    if (frozen) {
                        event.value(0);
                    }
                    return next;
                default: return next;
            }
        }

      public:
        consteval basic_tilt_freeze operator[](float const inp_threshold) const noexcept {
            basic_tilt_freeze res{*this};
            res.threshold = inp_threshold;
            return res;
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_tilt_state, CtxT>, "We need tilt_state to be in the pipeline.");
            bool const frozen = ctx.mod(tilt_state).is_changing(threshold);
            if constexpr (std::same_as<DomainT, tilt_abs_domain_t>) {
                return apply_abs(ctx.event(), frozen);
            } else {
                return apply_rel(ctx.event(), frozen);
            }
        }
    };

    inline constexpr basic_tilt_freeze<> tilt_freeze{};

    struct [[nodiscard]] tilt_push_options {
        float gain      = 1.0F;
        float dead_zone = 0.0F;
    };

    /// Nudge movement in the direction the pen is tilted, only on movement
    /// events (so merely tilting does not drift the cursor). `gain` is in the
    /// domain's units: pixels for `tilt_rel`, raw `ABS_*` counts for `tilt_abs`.
    template <typename DomainT = tilt_rel_domain_t>
    struct [[nodiscard]] basic_tilt_push : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using value_type = event_type::value_type;

      private:
        tilt_push_options options{};

        float      x_eps_  = 0.0F;
        float      y_eps_  = 0.0F;
        value_type x_last_ = 0;
        value_type y_last_ = 0;
        bool       x_init_ = false;
        bool       y_init_ = false;

        [[nodiscard]] static float push_for(float const norm, tilt_push_options const& opts) noexcept {
            if (std::abs(norm) <= opts.dead_zone) {
                return 0.0F;
            }
            return opts.gain * norm;
        }

        context_action apply_rel(event_type& event, basic_tilt_state const& state) noexcept {
            using enum context_action;
            switch (event.hash()) {
                case hashed(EV_REL, REL_X): {
                    float const scaled = static_cast<float>(event.value()) + push_for(state.norm_x(), options) + x_eps_;
                    auto const  out    = static_cast<value_type>(scaled);
                    x_eps_             = scaled - static_cast<float>(out);
                    event.value(out);
                    return next;
                }
                case hashed(EV_REL, REL_Y): {
                    float const scaled = static_cast<float>(event.value()) + push_for(state.norm_y(), options) + y_eps_;
                    auto const  out    = static_cast<value_type>(scaled);
                    y_eps_             = scaled - static_cast<float>(out);
                    event.value(out);
                    return next;
                }
                default: return next;
            }
        }

        context_action apply_abs(event_type& event, basic_tilt_state const& state) noexcept {
            using enum context_action;
            switch (event.hash()) {
                case hashed(EV_ABS, ABS_X): {
                    auto const value = event.value();
                    if (!x_init_) {
                        x_last_ = value;
                        x_init_ = true;
                        return next;
                    }
                    if (value == x_last_) {
                        return next; // no movement: don't drift
                    }
                    x_last_            = value;
                    float const scaled = static_cast<float>(value) + push_for(state.norm_x(), options) + x_eps_;
                    auto const  out    = static_cast<value_type>(scaled);
                    x_eps_             = scaled - static_cast<float>(out);
                    event.value(out);
                    return next;
                }
                case hashed(EV_ABS, ABS_Y): {
                    auto const value = event.value();
                    if (!y_init_) {
                        y_last_ = value;
                        y_init_ = true;
                        return next;
                    }
                    if (value == y_last_) {
                        return next;
                    }
                    y_last_            = value;
                    float const scaled = static_cast<float>(value) + push_for(state.norm_y(), options) + y_eps_;
                    auto const  out    = static_cast<value_type>(scaled);
                    y_eps_             = scaled - static_cast<float>(out);
                    event.value(out);
                    return next;
                }
                default: return next;
            }
        }

      public:
        consteval basic_tilt_push operator[](tilt_push_options const& inp_options) const noexcept {
            basic_tilt_push res{*this};
            res.options = inp_options;
            return res;
        }

        consteval basic_tilt_push operator[](float const gain) const noexcept {
            basic_tilt_push res{*this};
            res.options.gain = gain;
            return res;
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_tilt_state, CtxT>, "We need tilt_state to be in the pipeline.");
            auto const& state = ctx.mod(tilt_state);
            if constexpr (std::same_as<DomainT, tilt_abs_domain_t>) {
                return apply_abs(ctx.event(), state);
            } else {
                return apply_rel(ctx.event(), state);
            }
        }
    };

    inline constexpr basic_tilt_push<> tilt_push{};

} // namespace fs8
