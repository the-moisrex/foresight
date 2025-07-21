// Created by moisrex on 6/9/25.

module;
#include <array>
#include <linux/input-event-codes.h>
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
        // We know this is wasteful, but we don't care :)
        // It's about ~3KiB of storage
        std::array<event_type::value_type, KEY_MAX> btns{};

      public:
        // the copy ctor/assignment-op are marked consteval to stop from copying at run time.
        constexpr basic_keys_status() noexcept                               = default;
        consteval basic_keys_status(basic_keys_status const&)                = default;
        constexpr basic_keys_status(basic_keys_status&&) noexcept            = default;
        consteval basic_keys_status& operator=(basic_keys_status const&)     = default;
        constexpr basic_keys_status& operator=(basic_keys_status&&) noexcept = default;
        constexpr ~basic_keys_status() noexcept                              = default;

        [[nodiscard]] bool is_pressed(std::span<code_type const> key_codes) const noexcept;
        [[nodiscard]] bool is_released(std::span<code_type const> key_codes) const noexcept;

        template <std::integral... T>
        [[nodiscard]] bool is_pressed(T const... key_codes) const noexcept {
            return ((key_codes < KEY_MAX && btns.at(key_codes) != 0) && ...);
        }

        template <std::integral... T>
        [[nodiscard]] bool is_released(T const... key_codes) const noexcept {
            return ((key_codes < KEY_MAX && btns.at(key_codes) == 0) && ...);
        }

        void operator()(event_type const& event) noexcept;
    } keys_status;

    /**
     * Keeps the track of LEDs
     */
    constexpr struct [[nodiscard]] basic_led_status {
        using code_type = event_type::code_type;

      private:
        // this is not wasteful, it's only 11 of them:
        std::array<event_type::value_type, LED_MAX> leds{};

      public:
        // the copy ctor/assignment-op are marked consteval to stop from copying at run time.
        constexpr basic_led_status() noexcept                              = default;
        consteval basic_led_status(basic_led_status const&)                = default;
        constexpr basic_led_status(basic_led_status&&) noexcept            = default;
        consteval basic_led_status& operator=(basic_led_status const&)     = default;
        constexpr basic_led_status& operator=(basic_led_status&&) noexcept = default;
        constexpr ~basic_led_status() noexcept                             = default;

        [[nodiscard]] bool is_on(std::span<code_type const> key_codes) const noexcept;
        [[nodiscard]] bool is_off(std::span<code_type const> key_codes) const noexcept;

        template <std::integral... T>
        [[nodiscard]] bool is_on(T const... key_codes) const noexcept {
            return ((key_codes < LED_MAX && leds.at(key_codes) != 0) && ...);
        }

        template <std::integral... T>
        [[nodiscard]] bool is_off(T const... key_codes) const noexcept {
            return ((key_codes < LED_MAX && leds.at(key_codes) == 0) && ...);
        }

        void operator()(event_type const& event) noexcept;
    } led_status;
} // namespace foresight
