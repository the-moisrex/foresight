// Created by moisrex on 6/9/25.

module;
#include <array>
#include <linux/uinput.h>
export module foresight.mods.keys_status;
import foresight.mods.event;
import foresight.mods.context;

export namespace foresight {

    /**
     * If you need to check if a key is pressed or not, this is what you need to use.
     */
    constexpr struct [[nodiscard]] keys_status_type {
      private:
        // we know this is wasteful, but we don't care :)
        std::array<event_type::value_type, KEY_MAX> btns{};

      public:
        // the copy ctor/assignment-op are marked consteval to stop from copying at run time.
        constexpr keys_status_type() noexcept                              = default;
        consteval keys_status_type(keys_status_type const&)                = default;
        constexpr keys_status_type(keys_status_type&&) noexcept            = default;
        consteval keys_status_type& operator=(keys_status_type const&)     = default;
        constexpr keys_status_type& operator=(keys_status_type&&) noexcept = default;
        constexpr ~keys_status_type() noexcept                             = default;

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
