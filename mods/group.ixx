// Created by moisrex on 8/28/26.

module;
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>
export module fs8.mods:group;
import fs8.context;
import fs8.traits;

export namespace fs8 {

    /**
     * Generic mod composition: runs a tuple of mods as a single mod.
     *
     * For each event, every mod in the group is evaluated in order.  If any
     * mod returns `drop_event`, the event is dropped.  Tags (`start`,
     * `toggle_on`, `toggle_off`) are forwarded to every mod.
     *
     * Usage:
     *   constexpr auto my_mod = basic_group_mod{
     *       std::tuple{on_fail[drop_adjacent_syns, drop_action],
     *                  on_fail[drop_late_syn, drop_action]}};
     *   auto pipeline = context | ... | my_mod | output;
     */
    template <typename... Mods>
    struct [[nodiscard]] basic_group_mod : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::tuple<Mods...> mods_;

      public:
        constexpr basic_group_mod() noexcept = default;

        constexpr explicit basic_group_mod(std::tuple<Mods...> mods) noexcept : mods_{std::move(mods)} {}

        template <typename... T>
            requires((!std::same_as<T, get_variables_tag> && ...))
        consteval auto operator[](T... mods) const noexcept {
            return basic_group_mod<std::remove_cvref_t<T>...>{std::tuple{std::move(mods)...}};
        }

        // --- Tag forwarding (start, toggle_on, toggle_off) ---

        template <Context CtxT>
            requires((invokable_mod<Mods, CtxT, special_event> || ...))
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            using enum context_action;
            // clang-format off
#if __cpp_expansion_statements < 202506L
            // clang-format on
            context_action result = next;
            std::apply(
              [&](auto&... mods) {
                  std::ignore = ((result = invoke_mod(mods, ctx, tag), result == next) && ...);
              },
              mods_);
            return result;
#else
            template for (auto& mod : mods_) {
                if (auto const res = invoke_mod(mod, ctx, tag); res != next) [[unlikely]] {
                    return res;
                }
            }
            return next;
#endif
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            // clang-format off
#if __cpp_expansion_statements < 202506L
            // clang-format on
            context_action result = next;
            std::apply(
              [&](auto&... mods) {
                  std::ignore = ((result = invoke_mod(mods, ctx), result == next) && ...);
              },
              mods_);
            return result;
#else
            template for (auto& mod : mods_) {
                if (auto const res = invoke_mod(mod, ctx); res != next) [[unlikely]] {
                    return res;
                }
            }
            return next;
#endif
        }
    };

    /// Deduction guide.
    template <typename... Mods>
    basic_group_mod(std::tuple<Mods...>) -> basic_group_mod<Mods...>;

    /// Factory: `group_mod[mod1, mod2, ...]` creates a `basic_group_mod` from the given mods.
    constexpr basic_group_mod<> group_mod;

} // namespace fs8
