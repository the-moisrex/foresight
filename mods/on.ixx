// Created by moisrex on 6/18/25.

module;
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <linux/input-event-codes.h>
#include <string_view>
#include <tuple>
#include <utility>
export module fs8.mods:on;

export import fs8.utils;
import fs8.lib.mod_parser;
import fs8.context;
import fs8.traits;
import :keys_state;

namespace fs8 {

    using ev_type    = event_type::type_type;
    using code_type  = event_type::code_type;
    using value_type = event_type::value_type;

    export constexpr struct [[nodiscard]] basic_always_enable {
        constexpr bool operator()() const noexcept {
            return true;
        }
    } always_enable;

    export constexpr struct [[nodiscard]] basic_always_disable {
        constexpr bool operator()() const noexcept {
            return false;
        }
    } always_disable;

    export template <typename... Funcs>
        requires(std::is_nothrow_copy_constructible_v<Funcs> && ...)
    struct [[nodiscard]] or_op;

    export template <typename... Funcs>
        requires(std::is_nothrow_copy_constructible_v<Funcs> && ...)
    struct [[nodiscard]] and_op : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        constexpr and_op() noexcept = default;

      private:
        std::tuple<std::remove_cvref_t<Funcs>...> funcs;

      public:
        template <typename... InpFuncs>
            requires((std::convertible_to<InpFuncs, Funcs> && ...))
        explicit(false) constexpr and_op(InpFuncs&&... inp_funcs) noexcept : funcs{std::forward<InpFuncs>(inp_funcs)...} {}

        template <typename Func>
        [[nodiscard]] consteval auto operator&(Func func) const noexcept {
            return std::apply(
              [func](auto const&... conds) {
                  return and_op<std::remove_cvref_t<Funcs>..., std::remove_cvref_t<Func>>{conds..., func};
              },
              funcs);
        }

        template <typename Func>
        [[nodiscard]] consteval auto operator|(Func func) const noexcept {
            if constexpr (sizeof...(Funcs) == 0) {
                return or_op<std::remove_cvref_t<Func>>{func};
            } else {
                return or_op<and_op, std::remove_cvref_t<Func>>{*this, func};
            }
        }

        [[nodiscard]] constexpr bool operator()(Context auto& ctx) noexcept {
            return std::apply(
              [&ctx](auto&... cond) constexpr noexcept {
                  return (invoke_cond(cond, ctx) && ...);
              },
              funcs);
        }
    };

    export template <typename... Funcs>
        requires(std::is_nothrow_copy_constructible_v<Funcs> && ...)
    struct [[nodiscard]] or_op : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        constexpr or_op() noexcept = default;

      private:
        std::tuple<std::remove_cvref_t<Funcs>...> funcs{};

      public:
        template <typename... InpFuncs>
            requires((std::convertible_to<InpFuncs, Funcs> && ...))
        explicit(false) constexpr or_op(InpFuncs&&... inp_funcs) noexcept : funcs{std::forward<InpFuncs>(inp_funcs)...} {}

        template <typename Func>
        [[nodiscard]] consteval auto operator&(Func func) const noexcept {
            if constexpr (sizeof...(Funcs) == 0) {
                return and_op<std::remove_cvref_t<Func>>{func};
            } else {
                return and_op<or_op, std::remove_cvref_t<Func>>{*this, func};
            }
        }

        template <typename Func>
        [[nodiscard]] consteval auto operator|(Func func) const noexcept {
            return std::apply(
              [func](auto const&... conds) {
                  return or_op<std::remove_cvref_t<Funcs>..., std::remove_cvref_t<Func>>{conds..., func};
              },
              funcs);
        }

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) noexcept {
            return std::apply(
              [&ctx](auto&... cond) constexpr noexcept {
                  return (invoke_cond(cond, ctx) || ...);
              },
              funcs);
        }
    };

    export template <typename T>
    struct [[nodiscard]] operator_adaptor {
        template <typename Func>
        consteval auto operator&(Func func) const noexcept {
            return and_op<T, std::remove_cvref_t<Func>>{static_cast<T const&>(*this), func};
        }

        template <typename Func>
        consteval auto operator|(Func func) const noexcept {
            return or_op<T, std::remove_cvref_t<Func>>{static_cast<T const&>(*this), func};
        }
    };

    /**
     * Conditionally run some functions on each context/events.
     * @tparam CondT Condition Function
     * @tparam Funcs What to run when the condition is true
     */
    export template <typename CondT = basic_always_enable, typename... Funcs>
    struct [[nodiscard]] basic_on : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        template <typename CtxT>
        static constexpr bool can_generate_events = (invokable_mod<Funcs, CtxT, special_event> || ...);

      private:
        [[no_unique_address]] CondT                cond;
        [[no_unique_address]] std::tuple<Funcs...> funcs;
        bool                                       was_active = false;

      public:
        template <typename InpCond, typename... InpFunc>
            requires(std::convertible_to<InpCond, CondT> && (sizeof...(InpFunc) >= 1) && !Context<InpCond> && (!Context<InpFunc> && ...))
        explicit constexpr basic_on(InpCond&& inp_cond, InpFunc&&... inp_funcs) noexcept
          : cond{std::forward<InpCond>(inp_cond)},
            funcs{std::forward<InpFunc>(inp_funcs)...} {}

        template <typename InpCond>
            requires(std::convertible_to<InpCond, CondT>)
        explicit constexpr basic_on(InpCond&& inp_cond, std::tuple<Funcs...> const& inp_funcs) noexcept
          : cond{std::forward<InpCond>(inp_cond)},
            funcs{inp_funcs} {}

        template <typename NCondT, typename... NFuncs>
            requires(sizeof...(NFuncs)
                     >= 1
                     && !Context<NCondT>
                     && !detail::is_tag_type<NCondT>
                     && ((!Context<NFuncs> && !detail::is_tag_type<NFuncs>) && ...))
        consteval auto operator[](NCondT&& n_cond, NFuncs&&... n_funcs) const noexcept {
            return basic_on<std::remove_cvref_t<NCondT>, std::remove_cvref_t<NFuncs>...>{std::forward<NCondT>(n_cond),
                                                                                         std::forward<NFuncs>(n_funcs)...};
        }

        template <typename NCondT, Context CtxT>
            requires(!Context<NCondT> && !detail::is_tag_type<NCondT>)
        consteval auto operator[](NCondT&& n_cond, CtxT&& ctx) const noexcept {
            return std::apply(
              [&]<typename... ModT>(ModT&... mods) constexpr noexcept {
                  return basic_on<std::remove_cvref_t<NCondT>, std::remove_cvref_t<ModT>...>{std::forward<NCondT>(n_cond), mods...};
              },
              ctx.get_mods());
        }

        /// Handle special events (start, next_event, etc.)
        context_action operator()(Context auto& ctx, special_event const& tag) noexcept {
            using enum context_action;
            switch (tag.code) {
                case start.code: { // start
                    if (auto const action = invoke_mod(cond, ctx, start); !action) [[unlikely]] {
                        return action;
                    }
                    return invoke_sub_pipeline(ctx, funcs, start);
                }
                case next_event.code: { // next_event
                    if constexpr (can_generate_events<std::remove_cvref_t<decltype(ctx)>>) {
                        return invoke_first_mod_of_sub_pipeline(ctx, funcs, next_event);
                    }
                    return drop_event;
                }
                case toggle_on.code: { // toggle_on / toggle_off
                    return invoke_sub_pipeline(ctx, funcs, tag);
                }
                default: return drop_event;
            }
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            bool const is_active   = invoke_cond(cond, ctx);
            bool const is_switched = is_active != std::exchange(was_active, is_active);
            if (!is_active) {
                if (is_switched) {
                    if (auto const action = invoke_sub_pipeline(ctx, funcs, toggle_off); !action) [[unlikely]] {
                        return action;
                    }
                }
                return next;
            }
            if (is_switched) {
                if (auto const action = invoke_sub_pipeline(ctx, funcs, toggle_on); !action) [[unlikely]] {
                    return action;
                }
            }
            return invoke_sub_pipeline(ctx, funcs);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) sub_mods(this Self&& self) noexcept {
            return std::forward_like<Self>(self.funcs);
        }
    };

    /**
     * Conditionally run some functions on each context/events only once
     * This is the same as basic_on's toggle_on
     * @tparam CondT Condition Function
     * @tparam Funcs What to run when the condition is true
     */
    export template <typename CondT = basic_always_enable, typename... Funcs>
    struct [[nodiscard]] basic_once : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        template <typename CtxT>
        static constexpr bool can_generate_events = (invokable_mod<Funcs, CtxT, special_event> || ...);

      private:
        [[no_unique_address]] CondT                cond;
        [[no_unique_address]] std::tuple<Funcs...> funcs;
        bool                                       was_active = false;

      public:
        template <typename InpCond, typename... InpFunc>
            requires(std::convertible_to<InpCond, CondT> && (sizeof...(InpFunc) >= 1) && !Context<InpCond> && (!Context<InpFunc> && ...))
        explicit constexpr basic_once(InpCond&& inp_cond, InpFunc&&... inp_funcs) noexcept
          : cond{std::forward<InpCond>(inp_cond)},
            funcs{std::forward<InpFunc>(inp_funcs)...} {}

        template <typename InpCond>
            requires(std::convertible_to<InpCond, CondT>)
        explicit constexpr basic_once(InpCond&& inp_cond, std::tuple<Funcs...> const& inp_funcs) noexcept
          : cond{std::forward<InpCond>(inp_cond)},
            funcs{inp_funcs} {}

        template <typename NCondT, typename... NFuncs>
            requires(sizeof...(NFuncs) >= 1 && !Context<NCondT> && (!Context<NFuncs> && ...))
        consteval auto operator[](NCondT&& n_cond, NFuncs&&... n_funcs) const noexcept {
            return basic_once<std::remove_cvref_t<NCondT>, std::remove_cvref_t<NFuncs>...>{
              std::forward<NCondT>(n_cond),
              std::forward<NFuncs>(n_funcs)...};
        }

        template <typename NCondT, Context CtxT>
            requires(!Context<NCondT>)
        consteval auto operator[](NCondT&& n_cond, CtxT&& ctx) const noexcept {
            return std::apply(
              [&]<typename... ModT>(ModT&... mods) constexpr noexcept {
                  return basic_once<std::remove_cvref_t<NCondT>, std::remove_cvref_t<ModT>...>{std::forward<NCondT>(n_cond), mods...};
              },
              ctx.get_mods());
        }

        /// Handle special events (start, next_event, etc.)
        context_action operator()(Context auto& ctx, special_event const& tag) noexcept {
            using enum context_action;
            switch (tag.code) {
                case start.code: { // start
                    if (auto const action = invoke_mod(cond, ctx, start); !action) [[unlikely]] {
                        return action;
                    }
                    return invoke_sub_pipeline(ctx, funcs, start);
                }
                case next_event.code: { // next_event
                    if constexpr (can_generate_events<std::remove_cvref_t<decltype(ctx)>>) {
                        return invoke_first_mod_of_sub_pipeline(ctx, funcs, next_event);
                    }
                    return drop_event;
                }
                case toggle_on.code: { // toggle_on / toggle_off
                    return invoke_sub_pipeline(ctx, funcs, tag);
                }
                default: return drop_event;
            }
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            bool const is_active   = invoke_cond(cond, ctx);
            bool const is_switched = is_active != std::exchange(was_active, is_active);
            if (!is_active) {
                if (is_switched) {
                    if (auto const action = invoke_sub_pipeline(ctx, funcs, toggle_off); !action) [[unlikely]] {
                        return action;
                    }
                }
                return next;
            }
            if (is_switched) {
                if (auto const action = invoke_sub_pipeline(ctx, funcs); !action) {
                    return action;
                }
            }
            return next;
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) sub_mods(this Self&& self) noexcept {
            return std::forward_like<Self>(self.funcs);
        }
    };

    export template <template <std::size_t> typename A, std::size_t N>
    struct [[nodiscard]] basic_code_adaptor : consteval_copyable, operator_adaptor<A<N>> {
        using consteval_copyable::consteval_copyable;

      protected:
        std::array<code_type, N> codes{};

      public:
        explicit constexpr basic_code_adaptor(code_type const code) noexcept
            requires(N == 1)
          : codes{{code}} {}

        explicit constexpr basic_code_adaptor(std::array<code_type, N> const& inp_codes) noexcept : codes{inp_codes} {}

        explicit constexpr basic_code_adaptor(std::array<code_type, N>&& inp_codes) noexcept : codes{std::move(inp_codes)} {}

        template <typename... T>
            requires((std::convertible_to<T, code_type> && ...))
        consteval auto operator[](T... inp_codes) const noexcept {
            return A<sizeof...(T)>{std::array<code_type, sizeof...(T)>{static_cast<code_type>(inp_codes)...}};
        }

        template <std::size_t NN>
        consteval auto operator[](std::array<code_type, NN> const& inp_codes) const noexcept {
            return A<NN>{inp_codes};
        }
    };

    export template <std::size_t N>
    struct [[nodiscard]] basic_pressed : basic_code_adaptor<basic_pressed, N> {
        using basic_code_adaptor<basic_pressed, N>::basic_code_adaptor;
        using basic_code_adaptor<basic_pressed, N>::operator[];

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_keys_state, CtxT>, "We need keys_state to be in the pipeline.");
            return ctx.mod(keys_state).is_pressed(this->codes);
        }
    };

    export constexpr basic_pressed<0> pressed;

    export template <std::size_t N>
    struct [[nodiscard]] basic_pressed_any : basic_code_adaptor<basic_pressed_any, N> {
        using basic_code_adaptor<basic_pressed_any, N>::basic_code_adaptor;
        using basic_code_adaptor<basic_pressed_any, N>::operator[];

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_keys_state, CtxT>, "We need keys_state to be in the pipeline.");
            return ctx.mod(keys_state).is_pressed_any(this->codes);
        }
    };

    export constexpr basic_pressed_any<0> pressed_any;

    export constexpr struct [[nodiscard]] basic_keydown : consteval_copyable, operator_adaptor<basic_keydown> {
        using consteval_copyable::consteval_copyable;

      private:
        code_type code = KEY_MAX;

      public:
        consteval basic_keydown operator[](code_type const inp_code) const noexcept {
            basic_keydown res;
            res.code = inp_code;
            return res;
        }

        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.is(EV_KEY, code, 1);
        }
    } keydown;

    export constexpr struct [[nodiscard]] basic_keyup : consteval_copyable, operator_adaptor<basic_keyup> {
        using consteval_copyable::consteval_copyable;

      private:
        code_type code = KEY_MAX;

      public:
        consteval basic_keyup operator[](code_type const inp_code) const noexcept {
            basic_keyup res;
            res.code = inp_code;
            return res;
        }

        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.is(EV_KEY, code, 0);
        }
    } keyup;

    export constexpr struct [[nodiscard]] basic_key : consteval_copyable, operator_adaptor<basic_keyup> {
        using consteval_copyable::consteval_copyable;

      private:
        code_type code = KEY_MAX;

      public:
        consteval basic_key operator[](code_type const inp_code) const noexcept {
            basic_key res;
            res.code = inp_code;
            return res;
        }

        [[nodiscard]] constexpr bool operator()(event_type const& event) const noexcept {
            return event.is(EV_KEY, code);
        }
    } key;

    export template <typename CondT>
    struct [[nodiscard]] basic_held_gate;

    /// True while every tracked key is down, except on the tracked keys' own auto-repeat
    /// events (EV_KEY with value 2). Works without keys_state in the pipeline.
    export struct [[nodiscard]] basic_held : consteval_copyable, operator_adaptor<basic_held> {
        using consteval_copyable::consteval_copyable;

        static constexpr std::size_t max_codes = 32;

      private:
        std::string_view                  pattern;
        std::array<code_type, max_codes>  codes{};
        std::array<value_type, max_codes> states{};
        std::size_t                       count = 0;

      public:
        /// String form: e.g. "<f1>", "[F1][Alt]", "<alt-f1>" (parsed when the pipeline starts).
        explicit consteval basic_held(std::string_view const inp_pattern) noexcept : pattern{inp_pattern} {}

        /// Code form: a fixed set of key codes.
        template <std::size_t N>
            requires(N <= max_codes)
        explicit consteval basic_held(std::array<code_type, N> const& inp_codes) noexcept {
            for (std::size_t i = 0; i < N; ++i) {
                codes[i] = inp_codes[i];
            }
            count = N;
        }

        consteval basic_held operator[](std::string_view const inp_pattern) const noexcept {
            return basic_held{inp_pattern};
        }

        template <typename... T>
            requires((std::convertible_to<T, code_type> && ...))
        consteval basic_held operator[](T... inp_codes) const noexcept {
            return basic_held{std::array<code_type, sizeof...(T)>{static_cast<code_type>(inp_codes)...}};
        }

        /// Gate form: a decider decides whether the tracked key(s) are emitted or dropped.
        template <typename CondT>
            requires(!std::convertible_to<CondT, code_type> && !detail::is_tag_type<CondT> && !Context<CondT>)
        consteval auto operator[](std::string_view const inp_pattern, CondT const& inp_cond) const noexcept {
            return basic_held_gate<std::remove_cvref_t<CondT>>{inp_pattern, inp_cond};
        }

        /// Gate form (code form): last argument is the decider, the rest are key codes.
        template <typename... Args>
            requires(sizeof...(Args)
                     >= 2
                     && std::convertible_to<std::tuple_element_t<0, std::tuple<Args...>>, code_type>
                     && !std::convertible_to<std::tuple_element_t<sizeof...(Args) - 1, std::tuple<Args...>>, code_type>)
        consteval auto operator[](Args&&... args) const noexcept {
            constexpr std::size_t N    = sizeof...(Args);
            auto const&           cond = std::get<N - 1>(std::tuple<Args&...>{args...});
            using CondT                = std::remove_cvref_t<decltype(cond)>;
            return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
                static_assert((std::convertible_to<std::tuple_element_t<I, std::tuple<Args...>>, code_type> && ...),
                              "All but the last argument must be key codes.");
                return basic_held_gate<CondT>{
                  std::array<code_type, N - 1>{static_cast<code_type>(std::get<I>(std::tuple<Args&...>{args...}))...},
                  cond};
            }(std::make_index_sequence<N - 1>{});
        }

        /// Resolve the pattern string into key codes when the pipeline starts.
        context_action operator()(special_event const& tag) noexcept {
            if (tag.code != start.code) {
                return context_action::drop_event;
            }
            if (!pattern.empty()) {
                count = fs8::parse_key_tags(pattern, codes);
            }
            return context_action::next;
        }

        [[nodiscard]] bool operator()(event_type const& event) noexcept;
    };

    export constexpr basic_held held;

    /**
     * Gate the tracked key(s): while they're held, every event is passed to a decider.
     * If the decider says "drop", the buffered key events are dropped (the keys are
     * consumed and never emitted). Otherwise they stay buffered until the keys are
     * fully released, at which point they're emitted normally (repeats are always
     * suppressed). Usable directly in a pipeline: `held["<f1>", decider]`.
     */
    export template <typename CondT>
    struct [[nodiscard]] basic_held_gate : consteval_copyable, operator_adaptor<basic_held_gate<CondT>> {
        using consteval_copyable::consteval_copyable;

        static constexpr std::size_t max_codes  = 32;
        static constexpr std::size_t max_buffer = 64;

      private:
        std::string_view                 pattern;
        std::array<code_type, max_codes> codes{};
        std::size_t                      count = 0;

        std::array<event_type, max_buffer> buffer{};
        std::size_t                        buffer_size = 0;

        std::size_t down_count = 0;
        bool        pending    = false;
        bool        consumed   = false;

        [[no_unique_address]] CondT cond{};

      public:
        explicit consteval basic_held_gate(std::string_view const inp_pattern, CondT const& inp_cond) noexcept
          : pattern{inp_pattern},
            cond{inp_cond} {}

        template <std::size_t N>
            requires(N <= max_codes)
        explicit consteval basic_held_gate(std::array<code_type, N> const& inp_codes, CondT const& inp_cond) noexcept : cond{inp_cond} {
            for (std::size_t i = 0; i < N; ++i) {
                codes[i] = inp_codes[i];
            }
            count = N;
        }

        /// Resolve the pattern string into key codes when the pipeline starts.
        context_action operator()(special_event const& tag) noexcept {
            if (tag.code != start.code) {
                return context_action::drop_event;
            }
            if (!pattern.empty()) {
                count = fs8::parse_key_tags(pattern, codes);
            }
            return context_action::next;
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            auto const& event   = ctx.event();
            bool const  is_gate = event.type() == EV_KEY && is_tracked(event.code());

            if (pending) {
                if (invoke_cond(cond, ctx)) {
                    // decider said "drop": drop everything buffered so far
                    pending     = false;
                    consumed    = true;
                    buffer_size = 0;
                    if (is_gate) {
                        update_down_count(event);
                        if (down_count == 0) {
                            consumed = false;
                        }
                        return drop_event;
                    }
                    return next;
                }
                if (is_gate) {
                    if (event.value() == 2) {
                        return drop_event; // suppress repeats
                    }
                    if (buffer_size < max_buffer) {
                        buffer[buffer_size++] = event;
                    }
                    update_down_count(event);
                    if (down_count == 0) {
                        // fully released without being dropped: emit the chord normally
                        pending = false;
                        flush(ctx);
                        return drop_event; // the release is already in the buffer
                    }
                    return drop_event;
                }
                return next;
            }

            if (consumed) {
                if (is_gate) {
                    update_down_count(event);
                    if (down_count == 0) {
                        consumed = false;
                    }
                    return drop_event;
                }
                return next;
            }

            if (is_gate && event.value() == 1) {
                buffer_size           = 0;
                buffer[buffer_size++] = event;
                down_count            = 1;
                pending               = true;
                return drop_event;
            }
            return next;
        }

      private:
        [[nodiscard]] bool is_tracked(code_type const code) const noexcept {
            for (std::size_t i = 0; i < count; ++i) {
                if (codes[i] == code) {
                    return true;
                }
            }
            return false;
        }

        void update_down_count(event_type const& event) noexcept {
            if (event.value() == 1) {
                ++down_count;
            } else if (event.value() == 0 && down_count != 0) {
                --down_count;
            }
        }

        void flush(Context auto& ctx) noexcept {
            for (std::size_t i = 0; i < buffer_size; ++i) {
                std::ignore = ctx.fork_emit(buffer[i]);
            }
            buffer_size = 0;
        }
    };

    /// While any of the given keys is held, run the passed mod on every event
    /// (e.g. `on_held[KEY_CAPSLOCK, BTN_MIDDLE, mouse_to_scroll]` to turn mouse
    /// movement into scroll while a scroll modifier key is held).
    ///
    /// The modifier keys themselves are swallowed: the initial press is buffered
    /// and only released on a quick tap (a real press+release is re-emitted so the
    /// OS still sees a caps-lock toggle / middle click); on a hold past
    /// `hold_threshold` or when the gated mod consumed an event, the key is
    /// treated as a modifier and its release is swallowed too. If the desktop
    /// flipped the toggle-mode when it saw the physical press we swallowed, a
    /// synthetic press+release is emitted to restore the mode to what it was
    /// before the press (so a caps-hold that scrolled doesn't leave you in a
    /// different mode).
    ///
    /// Place it after `keys_state`/`update_mod` so other `pressed[]` conditions
    /// still observe the physical presses.
    export template <std::size_t N, typename ModT = basic_noop>
    struct [[nodiscard]] basic_on_held : consteval_copyable, operator_adaptor<basic_on_held<N, ModT>> {
        using consteval_copyable::consteval_copyable;

        static constexpr std::size_t max_codes = 32;

        using code_type     = event_type::code_type;
        using duration_type = std::chrono::microseconds;

      private:
        std::array<code_type, N>               codes{};
        std::array<event_type, N>              pending{};
        std::array<bool, N>                    held{};
        std::array<bool, N>                    used{};
        std::array<bool, N>                    led_on{};
        std::array<bool, N>                    led_before{};
        std::array<duration_type, N>           press_time{};
        duration_type                          hold_threshold = std::chrono::milliseconds(200);
        [[no_unique_address]] std::tuple<ModT> mod{};

        /// The LED that mirrors a toggle-mode key (CapsLock/NumLock/ScrollLock),
        /// or -1 for keys that don't have an associated mode.
        static constexpr code_type led_code_for(code_type const code) noexcept {
            switch (code) {
                case KEY_CAPSLOCK: return LED_CAPSL;
                case KEY_NUMLOCK: return LED_NUML;
                case KEY_SCROLLLOCK: return LED_SCROLLL;
                default: return static_cast<code_type>(-1);
            }
        }

      public:
        template <std::size_t M>
            requires(M == N)
        explicit consteval basic_on_held(std::array<code_type, M> const& inp_codes, ModT const& inp_mod = {}) noexcept
          : codes{inp_codes},
            mod{inp_mod} {}

        /// Code form: last argument is the mod, the rest are key codes.
        template <typename... Args>
            requires(sizeof...(Args)
                     >= 2
                     && std::convertible_to<std::tuple_element_t<0, std::tuple<Args...>>, code_type>
                     && !std::convertible_to<std::tuple_element_t<sizeof...(Args) - 1, std::tuple<Args...>>, code_type>)
        consteval auto operator[](Args&&... args) const noexcept {
            constexpr std::size_t M       = sizeof...(Args);
            auto const&           the_mod = std::get<M - 1>(std::tuple<Args&...>{args...});
            using NewModT                 = std::remove_cvref_t<decltype(the_mod)>;
            return [&]<std::size_t... I>(std::index_sequence<I...>) constexpr noexcept {
                static_assert((std::convertible_to<std::tuple_element_t<I, std::tuple<Args...>>, code_type> && ...),
                              "All but the last argument must be key codes.");
                return basic_on_held<M - 1, NewModT>{
                  std::array<code_type, M - 1>{static_cast<code_type>(std::get<I>(std::tuple<Args&...>{args...}))...},
                  the_mod};
            }(std::make_index_sequence<M - 1>{});
        }

        /// Set how long a key must be held before it counts as a modifier
        /// (default 200ms). A quicker press is re-emitted as a real tap.
        template <typename DurT>
        consteval auto hold(DurT const& dur) const noexcept {
            basic_on_held result{*this};
            result.hold_threshold = dur;
            return result;
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            auto const& event = ctx.event();

            // Track the desktop's LED state for toggle-mode keys. The desktop
            // writes EV_LED when it toggles the mode; this is the ground truth
            // we use to undo an unwanted toggle on release.
            if (event.type() == EV_LED) {
                for (std::size_t i = 0; i < N; ++i) {
                    if (led_code_for(codes[i]) == event.code()) {
                        led_on[i] = event.value() != 0;
                    }
                }
                return next;
            }

            // Tracked modifier keys: buffer the initial press, swallow repeats,
            // and decide tap-vs-hold on release.
            if (event.type() == EV_KEY) {
                for (std::size_t i = 0; i < N; ++i) {
                    if (event.code() != codes[i]) {
                        continue;
                    }
                    if (event.value() == 1) {
                        if (!held[i]) {
                            pending[i]    = event;
                            held[i]       = true;
                            used[i]       = false;
                            press_time[i] = event.micro_time();
                            // The mode state before the desktop reacts to the
                            // press we're about to swallow (its EV_LED toggle
                            // arrives after this event).
                            led_before[i] = led_on[i];
                        }
                        return drop_event;
                    }
                    if (event.value() == 2) {
                        return drop_event;
                    }
                    if (event.value() == 0 && held[i]) {
                        held[i] = false;
                        if (used[i] || event.micro_time() - press_time[i] >= hold_threshold) {
                            // used as a modifier: swallow the release too, and if
                            // the desktop flipped the toggle-mode (it saw the
                            // physical press we swallowed), restore the mode.
                            if (auto const led = led_code_for(codes[i]); led != static_cast<code_type>(-1) && led_on[i] != led_before[i]) {
                                led_on[i]   = led_before[i];
                                std::ignore = ctx.fork_emit(EV_KEY, codes[i], 1);
                                std::ignore = ctx.fork_emit(EV_KEY, codes[i], 0);
                                std::ignore = ctx.fork_emit(EV_SYN, SYN_REPORT, 0);
                            }
                            return drop_event;
                        }
                        // quick tap: re-emit the buffered press + this release
                        // so the OS sees a real press+release (caps toggle / click)
                        std::ignore = ctx.fork_emit(pending[i]);
                        std::ignore = ctx.fork_emit(event);
                        return drop_event;
                    }
                    return next;
                }
            }

            bool is_held = false;
            for (std::size_t i = 0; i < N; ++i) {
                is_held = is_held || held[i];
            }
            if (!is_held) {
                return next;
            }

            // While a modifier key is held, run the gated mod on every event.
            // Use a sub-pipeline so that fork_emit() calls from the gated mod
            // (e.g. mouse_to_scroll) see the sub-pipeline frame and
            // correctly continue into the parent pipeline.
            auto const action = invoke_sub_pipeline(ctx, mod);
            if (action != next) {
                for (std::size_t i = 0; i < N; ++i) {
                    if (held[i]) {
                        used[i] = true;
                    }
                }
                return action;
            }
            return next;
        }
    };

    export constexpr basic_on_held<0> on_held;

    export template <typename Func>
    struct [[nodiscard]] op_not {
        [[no_unique_address]] Func func;

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) noexcept {
            return !invoke_cond(func, ctx);
        }
    };

    export template <typename FuncT>
    struct [[nodiscard]] basic_longtime_released : consteval_copyable, operator_adaptor<basic_longtime_released<FuncT>> {
        using consteval_copyable::consteval_copyable;

        static constexpr std::chrono::milliseconds default_delay{100};

      private:
        [[no_unique_address]] FuncT func{};
        std::chrono::microseconds   dur = default_delay;
        std::chrono::microseconds   last_time{};

      public:
        constexpr explicit basic_longtime_released(FuncT const& inp_func, std::chrono::microseconds const inp_dur) noexcept
          : func{inp_func},
            dur{inp_dur} {}

        // todo: initialize the dur with repetition delay of the keyboard

        template <typename InpFuncT>
            requires(!Context<InpFuncT> && !detail::is_tag_type<InpFuncT>)
        consteval auto operator[](InpFuncT&& inp_func, std::chrono::microseconds const inp_dur = default_delay) const noexcept {
            return basic_longtime_released<std::remove_cvref_t<InpFuncT>>{std::forward<InpFuncT>(inp_func), inp_dur};
        }

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) noexcept {
            if (invoke_cond(func, ctx)) {
                if (last_time == std::chrono::microseconds(0)) {
                    last_time = ctx.event().micro_time();
                }
            } else {
                if (last_time == std::chrono::microseconds(0)) {
                    return false;
                }
                return (ctx.event().micro_time() - std::exchange(last_time, std::chrono::microseconds(0))) >= dur;
            }
            return false;
        }
    };

    export constexpr basic_longtime_released<basic_noop> longtime_released;

    export template <typename CondT = basic_noop>
    struct [[nodiscard]] basic_limit_mouse_travel : consteval_copyable, operator_adaptor<basic_limit_mouse_travel<CondT>> {
        using consteval_copyable::consteval_copyable;

      private:
        value_type x_amount = 50;
        value_type y_amount = 50;

        value_type x_cur = 0;
        value_type y_cur = 0;

        [[no_unique_address]] CondT cond{};

      public:
        constexpr explicit basic_limit_mouse_travel(CondT const& inp_cond, value_type const x, value_type const y) noexcept
          : x_amount{x},
            y_amount{y},
            cond{inp_cond} {}

        constexpr explicit basic_limit_mouse_travel(value_type const x, value_type const y) noexcept : x_amount{x}, y_amount{y} {}

        constexpr explicit basic_limit_mouse_travel(value_type const both) noexcept : x_amount{both}, y_amount{both} {}

        consteval basic_limit_mouse_travel operator[](value_type const x, value_type const y) const noexcept {
            return basic_limit_mouse_travel{x, y};
        }

        consteval basic_limit_mouse_travel operator[](value_type const both) const noexcept {
            return basic_limit_mouse_travel{both};
        }

        template <typename InpCondT>
        consteval auto operator[](InpCondT&& inp_cond, value_type const x, value_type const y) const noexcept {
            return basic_limit_mouse_travel<std::remove_cvref_t<InpCondT>>{std::forward<InpCondT>(inp_cond), x, y};
        }

        template <typename InpCondT>
        consteval auto operator[](InpCondT&& inp_cond, value_type const both) const noexcept {
            return basic_limit_mouse_travel<std::remove_cvref_t<InpCondT>>{std::forward<InpCondT>(inp_cond), both, both};
        }

        [[nodiscard]] constexpr bool operator()(Context auto& ctx) noexcept {
            auto const& event       = ctx.event();
            bool const  is_it_mouse = is_mouse_movement(event);
            if (invoke_cond(cond, ctx)) {
                if (is_it_mouse && (x_cur == -1 || y_cur == -1)) {
                    switch (event.code()) {
                        case REL_X: x_cur = event.value(); break;
                        case REL_Y: y_cur = event.value(); break;
                        default: break;
                    }
                    return true;
                }
            } else {
                x_cur = -1;
                y_cur = -1;
                return false;
            }
            if (is_it_mouse) {
                switch (event.code()) {
                    case REL_X: x_cur += std::abs(event.value()); break;
                    case REL_Y: y_cur += std::abs(event.value()); break;
                    default: break;
                }
            }
            // log("{} {} {}", x_cur, y_cur, std::abs(x_cur) <= x_amount && std::abs(y_cur) <= y_amount);
            return (x_cur <= x_amount) && (y_cur <= y_amount);
        }
    };

    export constexpr basic_limit_mouse_travel<> limit_mouse_travel;

    export constexpr struct [[nodiscard]] basic_led_on : consteval_copyable, operator_adaptor<basic_led_on> {
        using consteval_copyable::consteval_copyable;

      private:
        code_type code = LED_MAX;

      public:
        consteval basic_led_on operator[](code_type const inp_code) const noexcept {
            basic_led_on obj;
            obj.code = inp_code;
            return obj;
        }

        template <Context CtxT>
        [[nodiscard]] constexpr bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_led_state, CtxT>, "We need led_state to be in the pipeline.");
            return ctx.mod(led_state).is_on(code);
        }
    } led_on;

    export constexpr struct [[nodiscard]] basic_led_off : consteval_copyable, operator_adaptor<basic_led_off> {
        using consteval_copyable::consteval_copyable;

      private:
        code_type code = LED_MAX;

      public:
        consteval basic_led_off operator[](code_type const inp_code) const noexcept {
            basic_led_off obj;
            obj.code = inp_code;
            return obj;
        }

        template <Context CtxT>
        [[nodiscard]] bool operator()(CtxT& ctx) const noexcept {
            static_assert(has_mod<basic_led_state, CtxT>, "We need led_state to be in the pipeline.");
            return ctx.mod(led_state).is_off(code);
        }
    } led_off;

    export constexpr struct [[nodiscard]] basic_swipe_detector : consteval_copyable, operator_adaptor<basic_swipe_detector> {
        using consteval_copyable::consteval_copyable;

      private:
        value_type cur_x = 0;
        value_type cur_y = 0;

      public:
        void reset() noexcept;

        [[nodiscard]] constexpr value_type x() const noexcept {
            return cur_x;
        }

        [[nodiscard]] constexpr value_type y() const noexcept {
            return cur_y;
        }

        [[nodiscard]] bool is_active(value_type x_axis, value_type y_axis) const noexcept;

        /// Returns the number of times X and Y have passed
        /// multiples of their respective thresholds.
        /// Returns a std::pair where .first is x_multiples_passed and .second is y_multiples_passed.
        [[nodiscard]] std::pair<std::uint16_t, std::uint16_t> passed_threshold_count(value_type x_axis, value_type y_axis) const noexcept;

        void operator()(event_type const& event) noexcept;
    } swipe_detector;

    export struct [[nodiscard]] basic_swipe : consteval_copyable, operator_adaptor<basic_swipe> {
        using consteval_copyable::consteval_copyable;

      private:
        value_type x_axis = 0;
        value_type y_axis = 0;

        std::uint16_t count = 0;

      public:
        constexpr basic_swipe(value_type const inp_x_axis, value_type const inp_y_axis) noexcept : x_axis{inp_x_axis}, y_axis{inp_y_axis} {}

        template <Context CtxT>
        [[nodiscard]] bool operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_swipe_detector, CtxT>, "You need to enable swipe detector");

            if (!is_mouse_movement(ctx.event())) {
                return false;
            }

            auto const [cur_x_count, cur_y_count] = ctx.mod(swipe_detector).passed_threshold_count(x_axis, y_axis);
            auto const cur_count                  = cur_x_count + cur_y_count;
            return cur_count > std::exchange(count, cur_count);
        }
    };

    export constexpr struct [[nodiscard]] basic_multi_click : consteval_copyable, operator_adaptor<basic_multi_click> {
        using consteval_copyable::consteval_copyable;

        using duration_type = std::chrono::microseconds;
        using msec          = std::chrono::milliseconds;
        using code_type     = user_event::code_type;

        static constexpr auto default_threshold = msec{200};

      private:
        user_event    usr{};
        std::uint8_t  count              = 2;
        duration_type duration_threshold = default_threshold;
        std::uint8_t  cur_count          = 0;
        duration_type last_click{};

      public:
        constexpr explicit basic_multi_click(
          user_event const&   inp_usr_event,
          std::uint8_t const  inp_count     = 2,
          duration_type const dur_threshold = default_threshold) noexcept
          : usr{inp_usr_event},
            count{inp_count},
            duration_threshold{dur_threshold} {}

        constexpr explicit basic_multi_click(
          event_code const&   inp_usr_event,
          std::uint8_t const  inp_count     = 2,
          duration_type const dur_threshold = default_threshold) noexcept
          : basic_multi_click{
              user_event{.type = inp_usr_event.type, .code = inp_usr_event.code, .value = 0},
              inp_count,
              dur_threshold
        } {}

        constexpr explicit basic_multi_click(
          code_type const&    code,
          std::uint8_t const  inp_count = 2,
          duration_type const dur_threshold =
            std::chrono::milliseconds{
              200
        }) noexcept
          : basic_multi_click{user_event{.type = EV_KEY, .code = code, .value = 0}, inp_count, dur_threshold} {}

        consteval auto operator[](user_event const&   inp_usr_event,
                                  std::uint8_t const  inp_count     = 2,
                                  duration_type const dur_threshold = default_threshold) const noexcept {
            return basic_multi_click{inp_usr_event, inp_count, dur_threshold};
        }

        consteval auto operator[](event_code const&   inp_usr_event,
                                  std::uint8_t const  inp_count     = 2,
                                  duration_type const dur_threshold = default_threshold) const noexcept {
            return basic_multi_click{
              user_event{.type = inp_usr_event.type, .code = inp_usr_event.code, .value = 0},
              inp_count,
              dur_threshold
            };
        }

        consteval auto operator[](code_type const&    code,
                                  std::uint8_t const  inp_count     = 2,
                                  duration_type const dur_threshold = std::chrono::milliseconds{200}) const noexcept {
            return basic_multi_click{
              user_event{.type = EV_KEY, .code = code, .value = 0},
              inp_count,
              dur_threshold
            };
        }

        [[nodiscard]] bool operator()(event_type const& event) noexcept;
    } multi_click;

    constexpr basic_on<> enable_only;

    /// usage: op & pressed{...} | ...
    export constexpr and_op<> op;

    /// usage: on[released{...}, [] { ... }]
    export constexpr basic_on<> on;

    /// usage: once[pressed{}, [] { ... }]
    export constexpr basic_once<> once;

    constexpr auto               no_axis           = std::numeric_limits<value_type>::max();
    constexpr value_type         default_sipe_step = 120;
    export constexpr basic_swipe swipe_left{-default_sipe_step, no_axis};
    export constexpr basic_swipe swipe_right{default_sipe_step, no_axis};
    export constexpr basic_swipe swipe_up{no_axis, -default_sipe_step};
    export constexpr basic_swipe swipe_down{no_axis, default_sipe_step};

    constexpr user_event               left_click{EV_KEY, BTN_LEFT, 0};
    export constexpr basic_multi_click double_click{left_click};
    export constexpr basic_multi_click triple_click{left_click, 3};
} // namespace fs8
