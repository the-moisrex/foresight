module;
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <tuple>
export module foresight.mods.modes;
import foresight.main.utils;
import foresight.mods.context;

namespace fs8 {

    export template <typename CondT = basic_noop, typename... Mods>
    struct [[nodiscard]] basic_modes {
        using mods_type = std::tuple<Mods...>;

      private:
        // todo: add support for condition functions to return the mode directly
        [[no_unique_address]] CondT     cond;
        [[no_unique_address]] mods_type mods;
        std::uint8_t                    mode = 0;

        static_assert(sizeof...(Mods) <= std::numeric_limits<std::uint8_t>::max(), "Too many mods.");

      public:
        consteval basic_modes() noexcept = default;

        explicit consteval basic_modes(CondT inp_cond, Mods... inp_mods) noexcept
          : cond{inp_cond},
            mods{inp_mods...} {}

        consteval basic_modes(basic_modes const&) noexcept            = default;
        constexpr basic_modes(basic_modes&&) noexcept                 = default;
        consteval basic_modes& operator=(basic_modes const&) noexcept = default;
        constexpr basic_modes& operator=(basic_modes&&) noexcept      = default;
        constexpr ~basic_modes() noexcept                             = default;

        void operator()(auto&&, tag auto) = delete;
        void operator()(tag auto)         = delete;

        template <typename InpCondT, typename... InpMods>
            requires(sizeof...(InpMods) >= 1)
        consteval auto operator()(InpCondT inp_cond, InpMods... inp_mods) const noexcept {
            return basic_modes<std::remove_cvref_t<InpCondT>, std::remove_cvref_t<InpMods>...>{
              inp_cond,
              inp_mods...};
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
            return invoke_mod_at(ctx, mods, mode);
        }
    };

    export constexpr basic_modes<> modes{};

    export struct [[nodiscard]] basic_switch_mode {
      private:
        std::uint8_t mode = 0;

      public:
        basic_switch_mode() noexcept = default;

        explicit constexpr basic_switch_mode(std::uint8_t const in_mode) noexcept : mode{in_mode} {}

        basic_switch_mode(basic_switch_mode const&) noexcept            = default;
        basic_switch_mode(basic_switch_mode&&) noexcept                 = default;
        basic_switch_mode& operator=(basic_switch_mode const&) noexcept = default;
        basic_switch_mode& operator=(basic_switch_mode&&) noexcept      = default;
        ~basic_switch_mode() noexcept                                   = default;

        consteval basic_switch_mode operator()(std::uint8_t const in_mode) const noexcept {
            return basic_switch_mode{in_mode};
        }

        void operator()(Context auto& ctx) const noexcept {
            ctx.mod(modes).switch_mode(mode);
        }

    } switch_mode;
} // namespace fs8
