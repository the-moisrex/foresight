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

        explicit event_type(input_event const inp_ev) noexcept : ev{inp_ev} {}

        event_type(event_type&&) noexcept            = default;
        event_type& operator=(event_type&&) noexcept = default;
        ~event_type()                                = default;

        void type(uint16_t const inp_type) noexcept {
            ev.type = inp_type;
        }

        void code(uint16_t const inp_code) noexcept {
            ev.code = inp_code;
        }

        void value(int32_t const inp_value) noexcept {
            ev.value = inp_value;
        }

        [[nodiscard]] uint16_t type() const noexcept {
            return ev.type;
        }

        [[nodiscard]] uint16_t code() const noexcept {
            return ev.code;
        }

        [[nodiscard]] int32_t value() const noexcept {
            return ev.value;
        }

      private:
        input_event ev;
    };

    [[nodiscard]] bool is_mouse_movement(event_type const&) noexcept;


} // namespace foresight
