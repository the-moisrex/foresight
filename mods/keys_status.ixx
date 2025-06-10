// Created by moisrex on 6/9/25.

module;
#include <array>
#include <linux/uinput.h>
#include <span>
export module foresight.mods.keys_status;
import foresight.mods.event;
import foresight.mods.context;

export namespace foresight {

    /**
     * If you need to check if a key is pressed or not, this is what you need to use.
     */
    constexpr struct [[nodiscard]] basic_keys_status {
        using code_type = event_type::code_type;

      private:
        // we know this is wasteful, but we don't care :)
        std::array<event_type::value_type, KEY_MAX> btns{};

      public:
        // the copy ctor/assignment-op are marked consteval to stop from copying at run time.
        constexpr basic_keys_status() noexcept                               = default;
        consteval basic_keys_status(basic_keys_status const&)                = default;
        constexpr basic_keys_status(basic_keys_status&&) noexcept            = default;
        consteval basic_keys_status& operator=(basic_keys_status const&)     = default;
        constexpr basic_keys_status& operator=(basic_keys_status&&) noexcept = default;
        constexpr ~basic_keys_status() noexcept                              = default;

        constexpr void process(event_type const& event) noexcept {
            if (event.type() != EV_KEY) {
                return;
            }
            if (event.code() >= KEY_MAX) [[unlikely]] {
                // Just in case
                return;
            }
            this->btns.at(event.code()) = event.value();
        }

        [[nodiscard]] constexpr bool is_pressed(std::span<code_type const> const key_codes) const noexcept {
            for (auto const code : key_codes) {
                if (code >= KEY_MAX || this->btns.at(code) == 0) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] constexpr bool is_released(std::span<code_type const> const key_codes) const noexcept {
            for (auto const code : key_codes) {
                if (code >= KEY_MAX || this->btns.at(code) != 0) {
                    return false;
                }
            }
            return true;
        }

        template <std::integral... T>
        [[nodiscard]] constexpr bool is_pressed(T const... key_codes) const noexcept {
            return ((key_codes < KEY_MAX && btns.at(key_codes) != 0) && ...);
        }

        template <std::integral... T>
        [[nodiscard]] constexpr bool is_released(T const... key_codes) const noexcept {
            return ((key_codes < KEY_MAX && btns.at(key_codes) == 0) && ...);
        }

        constexpr void operator()(Context auto& ctx) noexcept {
            process(ctx.event());
        }
    } keys_status;

} // namespace foresight
