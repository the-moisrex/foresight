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

        // --- Tag forwarding (start, toggle_on, toggle_off) ---

        template <Tag T>
            requires(!std::same_as<T, load_event_tag> && !std::same_as<T, next_event_tag>)
        void operator()(auto&, T tag) noexcept {
            forward_tag(tag, std::make_index_sequence<sizeof...(Mods)>{});
        }

        // --- Main handler: receives event directly from pipeline ---

        context_action operator()(event_type const& event) noexcept {
            return evaluate(event, std::make_index_sequence<sizeof...(Mods)>{});
        }

      private:
        template <typename T, std::size_t... Is>
        void forward_tag(T tag, std::index_sequence<Is...>) noexcept {
            (invoke_tag(std::get<Is>(mods_), tag), ...);
        }

        template <std::size_t... Is>
        context_action evaluate(event_type const& event, std::index_sequence<Is...>) noexcept {
            bool should_drop = false;
            ((should_drop |= run_mod(std::get<Is>(mods_), event)), ...);
            return should_drop ? context_action::drop_event : context_action::next;
        }

        template <typename Mod>
        static bool run_mod(Mod& m, event_type const& event) noexcept {
            using result_t = std::invoke_result_t<Mod&, event_type const&>;
            if constexpr (std::same_as<result_t, context_action>) {
                return m(event) == context_action::drop_event;
            } else if constexpr (std::same_as<result_t, bool>) {
                return !m(event); // false = drop
            } else {
                static_cast<void>(m(event));
                return false;
            }
        }

        template <typename Mod, typename T>
        static void invoke_tag(Mod& m, T tag) noexcept {
            if constexpr (std::invocable<Mod&, T>) {
                m(tag);
            }
        }
    };

    /// Deduction guide.
    template <typename... Mods>
    basic_group_mod(std::tuple<Mods...>) -> basic_group_mod<Mods...>;

    /// Factory: `group_mod[mod1, mod2, ...]` creates a `basic_group_mod` from the given mods.
    constexpr struct [[nodiscard]] basic_group_mod_factory {
        template <typename... Mods>
        consteval auto operator[](Mods... mods) const noexcept {
            return basic_group_mod<std::remove_cvref_t<Mods>...>{std::tuple{std::move(mods)...}};
        }
    } group_mod;

} // namespace fs8
