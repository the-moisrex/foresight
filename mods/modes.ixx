module;
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <tuple>
export module fs8.mods:modes;
import fs8.utils;
import fs8.context;
import fs8.traits;

namespace fs8 {

    namespace detail {
        struct modes_caller_info {
            void* instance                         = nullptr;
            void (*switch_fn)(void*, std::uint8_t) = nullptr;
        };

        modes_caller_info& current_modes_caller() noexcept {
            static thread_local modes_caller_info info{};
            return info;
        }
    } // namespace detail

    export template <typename CondT = basic_noop, typename... Mods>
    struct [[nodiscard]] basic_modes : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using mods_type = std::tuple<Mods...>;

      private:
        // todo: add support for condition functions to return the mode directly
        [[no_unique_address]] CondT     cond;
        [[no_unique_address]] mods_type mods;
        std::uint8_t                    mode = 0;

        static_assert(sizeof...(Mods) <= std::numeric_limits<std::uint8_t>::max(), "Too many mods.");

      public:
        explicit consteval basic_modes(CondT inp_cond, Mods... inp_mods) noexcept : cond{inp_cond}, mods{inp_mods...} {}

        template <typename InpCondT, typename... InpMods>
            requires(sizeof...(InpMods) >= 1)
        consteval auto operator[](InpCondT inp_cond, InpMods... inp_mods) const noexcept {
            return basic_modes<std::remove_cvref_t<InpCondT>, std::remove_cvref_t<InpMods>...>{inp_cond, inp_mods...};
        }

        constexpr void switch_mode(std::uint8_t const in_mode) noexcept {
            mode = std::clamp<std::uint8_t>(0, in_mode, sizeof...(Mods) - 1);
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            if (invoke_cond(cond, ctx)) {
                // go to the next mode
                mode = mode >= sizeof...(Mods) - 1 ? 0 : mode + 1;
                // log("Mode Changed to {}", mode);
            }
            assert(mode < sizeof...(Mods));
            auto prev                    = detail::current_modes_caller();
            detail::current_modes_caller() = {this, [](void* p, std::uint8_t m) {
                                                  static_cast<basic_modes*>(p)->switch_mode(m);
                                              }};
            auto const result            = invoke_mod_at(ctx, mods, mode);
            detail::current_modes_caller() = prev;
            return result;
        }
    };

    export constexpr basic_modes<> modes{};

    export struct [[nodiscard]] basic_switch_mode : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        std::uint8_t mode = 0;

      public:
        explicit constexpr basic_switch_mode(std::uint8_t const in_mode) noexcept : mode{in_mode} {}

        consteval basic_switch_mode operator[](std::uint8_t const in_mode) const noexcept {
            return basic_switch_mode{in_mode};
        }

        void operator()(Context auto& /*ctx*/) const noexcept {
            auto& caller = detail::current_modes_caller();
            if (caller.instance && caller.switch_fn) {
                caller.switch_fn(caller.instance, mode);
            }
        }

    } switch_mode;
} // namespace fs8
