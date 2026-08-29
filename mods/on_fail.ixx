// Created by moisrex on 8/28/26.

module;
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>
export module fs8.mods:on_fail;

import fs8.context;
import fs8.event;
import fs8.traits;
import fs8.utils;

export namespace fs8 {

    /**
     * Composition mod: when a condition check fails, invoke a side-effect action.
     *
     * `on_fail[condition, action, extra...]` sits in the pipeline.  For each
     * event it evaluates `condition` via `invoke_cond`; if it fails the action
     * is invoked via `invoke_mod`.  Any extra arguments stored in the mod are
     * forwarded to both `invoke_cond` and `invoke_mod`.
     *
     * Tags (`start`, `toggle_on`, `toggle_off`) are forwarded to both the
     * condition and the action so they can initialise state.
     *
     * Usage:
     *   on_fail[drop_orphan_abs, log_diagnostics]
     *   on_fail[drop_big_jumps[50], my_logger]
     *   on_fail[drop_late_syn, log_diagnostics, sanitizer_issue::late_syn]
     */
    template <typename CondT, typename ActionT, typename... Extra>
    struct [[nodiscard]] basic_on_fail : consteval_copyable {
        template <typename, typename, typename...>
        friend struct basic_on_fail;

        using consteval_copyable::consteval_copyable;

      private:
        [[no_unique_address]] CondT                cond;
        [[no_unique_address]] ActionT              action;
        [[no_unique_address]] std::tuple<Extra...> extra_;

      public:
        constexpr basic_on_fail() noexcept = default;

        explicit consteval basic_on_fail(CondT c, ActionT a, Extra... e) noexcept : cond{c}, action{a}, extra_{e...} {}

        template <typename C, typename A, typename... E>
        consteval auto operator[](C c, A a, E... e) const noexcept {
            return basic_on_fail<C, A, E...>{
              c,
              a,
              e...,
            };
        }

        ///  Tag forwarding (start, toggle_on, toggle_off only)
        template <Context CtxT, Tag T>
            requires(invokable_mod<CondT, CtxT, T> || invokable_mod<ActionT, CtxT, T>)
        context_action operator()(CtxT& ctx, T tag) noexcept {
            using enum context_action;
            if constexpr (invokable_mod<CondT, CtxT, T> && invokable_mod<ActionT, CtxT, T>) {
                if (auto const res = invoke_mod(cond, ctx, tag); res != next) [[unlikely]] {
                    return res;
                }
                return invoke_mod(action, ctx, tag);
            } else if constexpr (invokable_mod<CondT, CtxT, T>) {
                return invoke_mod(cond, ctx, tag);
            } else if constexpr (invokable_mod<ActionT, CtxT, T>) {
                return invoke_mod(action, ctx, tag);
            } else {
                static_assert(false, "This path should be unreachable.");
                return next;
            }
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            if constexpr (sizeof...(Extra) >= 1) {
                return std::apply(
                  [&](auto&... args) noexcept {
                      if (!invoke_cond(cond, ctx, args...)) [[unlikely]] {
                          return invoke_mod(action, ctx, args...);
                      }
                      return context_action::next;
                  },
                  extra_);
            } else {
                if (!invoke_cond(cond, ctx)) [[unlikely]] {
                    return invoke_mod(action, ctx);
                }
                return context_action::next;
            }
        }
    };

    /// Deduction guide.
    template <typename C, typename A, typename... E>
    basic_on_fail(C, A, E...) -> basic_on_fail<C, A, E...>;

    constexpr basic_on_fail on_fail{noop, noop};

} // namespace fs8
