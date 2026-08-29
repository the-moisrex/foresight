// Created by moisrex on 8/28/26.

module;
#include <concepts>
#include <type_traits>
#include <utility>
export module fs8.mods:on_fail;

import fs8.context;
import fs8.traits;

export namespace fs8 {

    /**
     * Composition mod: when a condition check fails, invoke a side-effect action.
     *
     * `on_fail[condition, action]` sits in the pipeline.  For each event it
     * evaluates `condition`:
     *   - If the condition returns `drop_event` (or `false`), the event is
     *     dropped **and** `action` is invoked.
     *   - If the condition returns `next` (or `true`), the event passes through.
     *
     * Tags (`start`, `toggle_on`, `toggle_off`) are forwarded to both the
     * condition and the action so they can initialise state.
     *
     * Accepts either `CtxT&` (context) or `event_type const&` (event).
     * The same argument is forwarded to both the condition and the action.
     * The context overload is only enabled when the condition itself accepts
     * `CtxT&`; otherwise the pipeline extracts the event automatically.
     *
     * Usage:
     *   on_fail[drop_orphan_abs, log_diagnostics]
     *   on_fail[drop_big_jumps[50], my_logger]
     *   on_fail[drop_late_syn, log_diagnostics]
     */
    template <typename CondT, typename ActionT>
    struct [[nodiscard]] basic_on_fail : consteval_copyable {
        template <typename, typename>
        friend struct basic_on_fail;

        using consteval_copyable::consteval_copyable;

      private:
        [[no_unique_address]] CondT   cond;
        [[no_unique_address]] ActionT action;

      public:
        constexpr basic_on_fail() noexcept = default;

        explicit consteval basic_on_fail(CondT const& c, ActionT const& a) noexcept : cond{c}, action{a} {}

        template <typename C, typename A>
        consteval auto operator[](C&& c, A&& a) const noexcept {
            return basic_on_fail<std::remove_cvref_t<C>, std::remove_cvref_t<A>>{std::forward<C>(c), std::forward<A>(a)};
        }

        // --- Tag forwarding (start, toggle_on, toggle_off only) ---

        template <Tag T>
            requires(!std::same_as<T, load_event_tag> && !std::same_as<T, next_event_tag>)
        void operator()(auto&, T) noexcept {
            if constexpr (std::invocable<CondT&, T>) {
                cond(T{});
            }
            if constexpr (std::invocable<ActionT&, T>) {
                action(T{});
            }
        }

        // --- Context handler: only enabled when the condition accepts CtxT& ---

        template <typename CtxT>
            requires requires(CtxT& c) {
                c.event();
                { std::declval<CondT&>()(c) };
            }
        context_action operator()(CtxT& ctx) noexcept {
            if (evaluate_condition(ctx)) {
                return context_action::next;
            }
            invoke_action(ctx);
            return context_action::drop_event;
        }

        // --- Event handler: receives event directly from pipeline ---

        context_action operator()(event_type const& event) noexcept {
            if (evaluate_condition(event)) {
                return context_action::next;
            }
            invoke_action(event);
            return context_action::drop_event;
        }

      private:
        template <typename Arg>
        [[nodiscard]] constexpr bool evaluate_condition(Arg& arg) noexcept {
            if constexpr (std::invocable<CondT&, Arg&>) {
                using cond_result = std::invoke_result_t<CondT&, Arg&>;
                if constexpr (std::same_as<cond_result, bool>) {
                    return cond(arg);
                } else if constexpr (std::same_as<cond_result, context_action>) {
                    return cond(arg) == context_action::next;
                } else {
                    static_cast<void>(cond(arg));
                    return true; // void → always passes
                }
            } else {
                return true;     // condition can't evaluate this arg → pass
            }
        }

        template <typename Arg>
        constexpr void invoke_action(Arg& arg) noexcept {
            if constexpr (std::invocable<ActionT&, Arg&>) {
                static_cast<void>(action(arg));
            }
        }
    };

    /// Deduction guide.
    template <typename C, typename A>
    basic_on_fail(C, A) -> basic_on_fail<C, A>;

    constexpr struct [[nodiscard]] basic_on_fail_creater {
        template <typename C, typename A>
        consteval auto operator[](C&& c, A&& a) const noexcept {
            return basic_on_fail<std::remove_cvref_t<C>, std::remove_cvref_t<A>>{
              std::forward<C>(c),
              std::forward<A>(a),
            };
        }
    } on_fail;

} // namespace fs8
