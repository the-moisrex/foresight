// Created by moisrex on 6/8/25.

module;
#include <cstdint>
#include <linux/uinput.h>
export module foresight.mods.event;

export namespace foresight {

    struct event_type {
        using type_type  = decltype(input_event::type);
        using code_type  = decltype(input_event::code);
        using value_type = decltype(input_event::value);

        constexpr event_type() noexcept = default;

        constexpr explicit event_type(input_event const inp_ev) noexcept : ev{inp_ev} {}

        constexpr event_type(event_type&&) noexcept            = default;
        constexpr event_type(event_type const&)                = default;
        constexpr event_type& operator=(event_type&&) noexcept = default;
        constexpr event_type& operator=(event_type const&)     = default;
        constexpr ~event_type()                                = default;

        constexpr void type(uint16_t const inp_type) noexcept {
            ev.type = inp_type;
        }

        constexpr void code(uint16_t const inp_code) noexcept {
            ev.code = inp_code;
        }

        constexpr void value(int32_t const inp_value) noexcept {
            ev.value = inp_value;
        }

        [[nodiscard]] constexpr uint16_t type() const noexcept {
            return ev.type;
        }

        [[nodiscard]] constexpr uint16_t code() const noexcept {
            return ev.code;
        }

        [[nodiscard]] constexpr int32_t value() const noexcept {
            return ev.value;
        }

        [[nodiscard]] constexpr input_event& native() noexcept {
            return ev;
        }

        [[nodiscard]] constexpr input_event const& native() const noexcept {
            return ev;
        }

      private:
        input_event ev{};
    };

    [[nodiscard]] constexpr bool is_mouse_movement(event_type const& event) noexcept {
        return event.type() == EV_REL && (event.code() == REL_X || event.code() == REL_Y);
    }


} // namespace foresight
