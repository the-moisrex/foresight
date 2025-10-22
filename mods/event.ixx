// Created by moisrex on 6/8/25.

module;
#include <bit>
#include <chrono>
#include <cstdint>
#include <libevdev/libevdev.h>
#include <linux/uinput.h>
export module foresight.mods.event;

// don't import log here.

export namespace foresight {

    struct [[nodiscard]] user_event {
        using type_type  = decltype(input_event::type);
        using code_type  = decltype(input_event::code);
        using value_type = decltype(input_event::value);

        type_type  type  = EV_MAX;
        code_type  code  = KEY_MAX;
        value_type value = 0;
    };

    constexpr user_event invalid_user_event{.type = EV_MAX, .code = KEY_MAX, .value = 0};

    [[nodiscard]] constexpr bool operator==(user_event const& lhs, user_event const& rhs) noexcept {
        return lhs.type == rhs.type && lhs.code == rhs.code && lhs.value == rhs.value;
    }

    [[nodiscard]] constexpr bool is_invalid(user_event const& event) noexcept {
        return event.type == invalid_user_event.type;
    }

    template <std::size_t N>
    struct user_events : std::array<user_event, N> {};

    using user_events_iterator = user_events<0>::iterator;

    struct [[nodiscard]] event_code {
        using type_type  = decltype(input_event::type);
        using code_type  = decltype(input_event::code);
        using value_type = decltype(input_event::value);

        type_type type = EV_MAX;
        code_type code = KEY_MAX;
    };

    template <std::size_t N>
    using event_codes = std::array<event_code, N>;

    [[nodiscard]] constexpr std::uint32_t hashed(event_code const& code) noexcept {
        static constexpr std::uint32_t shift  = std::countr_zero(std::bit_ceil<std::uint32_t>(KEY_MAX));
        std::uint32_t                  hash   = 0;
        hash                                 |= static_cast<std::uint32_t>(code.type) << shift;
        hash                                 |= static_cast<std::uint32_t>(code.code);
        return hash;
    }

    [[nodiscard]] consteval event_code key_code(event_code::code_type code) noexcept {
        return event_code{.type = EV_KEY, .code = code};
    }

    template <typename... T>
    [[nodiscard]] consteval event_codes<sizeof...(T)> key_codes(T... codes) noexcept {
        return event_codes<sizeof...(T)>{
          std::array<event_code, sizeof...(T)>{key_code(static_cast<event_code::code_type>(codes))...}};
    }

    [[nodiscard]] constexpr std::uint32_t hashed(event_code::type_type const type,
                                                 event_code::code_type const code) noexcept {
        return hashed(event_code{.type = type, .code = code});
    }

    struct [[nodiscard]] event_type {
        using type_type  = decltype(input_event::type);
        using code_type  = decltype(input_event::code);
        using value_type = decltype(input_event::value);
        using time_type  = decltype(input_event::time);

        constexpr event_type() noexcept = default;

        constexpr explicit event_type(input_event const& inp_ev) noexcept : ev{inp_ev} {}

        constexpr explicit event_type(user_event const& inp_ev) noexcept
          : event_type{inp_ev.type, inp_ev.code, inp_ev.value} {}

        constexpr event_type(type_type const  inp_type,
                             code_type const  inp_code,
                             value_type const inp_val) noexcept {
            reset_time();
            ev.type  = inp_type;
            ev.code  = inp_code;
            ev.value = inp_val;
        }

        constexpr event_type(event_type&&) noexcept            = default;
        constexpr event_type(event_type const&)                = default;
        constexpr event_type& operator=(event_type&&) noexcept = default;
        constexpr event_type& operator=(event_type const&)     = default;
        constexpr ~event_type()                                = default;

        constexpr event_type& operator=(input_event const& inp_event) noexcept {
            ev = inp_event;
            return *this;
        }

        constexpr event_type& operator=(event_code const& inp_code) noexcept {
            ev.type = inp_code.type;
            ev.code = inp_code.code;
            return *this;
        }

        constexpr event_type& operator=(user_event const& inp_code) noexcept {
            ev.type  = inp_code.type;
            ev.code  = inp_code.code;
            ev.value = inp_code.value;
            return *this;
        }

        constexpr void time(time_type const inp_time) noexcept {
            ev.time = inp_time;
        }

        constexpr void type(type_type const inp_type) noexcept {
            ev.type = inp_type;
        }

        constexpr void code(code_type const inp_code) noexcept {
            ev.code = inp_code;
        }

        constexpr void value(value_type const inp_value) noexcept {
            ev.value = inp_value;
        }

        constexpr void set(type_type const inp_type, code_type const inp_code) noexcept {
            ev.type = inp_type;
            ev.code = inp_code;
        }

        constexpr void set(event_code const& rhs) noexcept {
            ev.type = rhs.type;
            ev.code = rhs.code;
        }

        constexpr void set(user_event const& rhs) noexcept {
            ev.type  = rhs.type;
            ev.code  = rhs.code;
            ev.value = rhs.value;
        }

        constexpr void set(type_type const  inp_type,
                           code_type const  inp_code,
                           value_type const inp_value) noexcept {
            ev.type  = inp_type;
            ev.code  = inp_code;
            ev.value = inp_value;
        }

        [[nodiscard]] constexpr time_type time() const noexcept {
            return ev.time;
        }

        [[nodiscard]] constexpr std::chrono::microseconds micro_time() const noexcept {
            return std::chrono::seconds{ev.time.tv_sec} + std::chrono::microseconds{ev.time.tv_usec};
        }

        [[nodiscard]] constexpr type_type type() const noexcept {
            return ev.type;
        }

        [[nodiscard]] constexpr code_type code() const noexcept {
            return ev.code;
        }

        [[nodiscard]] constexpr value_type value() const noexcept {
            return ev.value;
        }

        [[nodiscard]] constexpr input_event& native() noexcept {
            return ev;
        }

        [[nodiscard]] constexpr input_event const& native() const noexcept {
            return ev;
        }

        [[nodiscard]] explicit constexpr operator user_event() const noexcept {
            return user_event{.type = ev.type, .code = ev.code, .value = ev.value};
        }

        [[nodiscard]] explicit constexpr operator event_code() const noexcept {
            return event_code{.type = ev.type, .code = ev.code};
        }

        constexpr void reset_time() noexcept {
            if !consteval {
                gettimeofday(&ev.time, nullptr);
            }
        }

        [[nodiscard]] std::string_view code_name() const noexcept {
            return libevdev_event_code_get_name(ev.type, ev.code);
        }

        [[nodiscard]] std::string_view type_name() const noexcept {
            return libevdev_event_type_get_name(ev.type);
        }

        [[nodiscard]] std::string_view value_name() const noexcept {
            return libevdev_event_value_get_name(ev.type, ev.code, ev.value);
        }

        [[nodiscard]] constexpr bool is(type_type const inp_type) const noexcept {
            return libevdev_event_is_type(&ev, inp_type) == 1;
        }

        [[nodiscard]] constexpr bool is(type_type const inp_type, code_type const inp_code) const noexcept {
            return ev.type == inp_type && ev.code == inp_code;
        }

        [[nodiscard]] constexpr bool is_of(type_type const inp_type,
                                           code_type const inp_code) const noexcept {
            return libevdev_event_is_code(&ev, inp_type, inp_code) == 1;
        }

        /// Only checks the type and the code, but not the value
        [[nodiscard]] constexpr bool is_of(user_event const& rhs) const noexcept {
            return libevdev_event_is_code(&ev, rhs.type, rhs.code) == 1;
        }

        /// Only checks the type and the code, but not the value
        [[nodiscard]] constexpr bool is_of(event_code const& rhs) const noexcept {
            return libevdev_event_is_code(&ev, rhs.type, rhs.code) == 1;
        }

        [[nodiscard]] constexpr bool
        is(type_type const inp_type, code_type const inp_code, value_type const inp_value) const noexcept {
            return ev.type == inp_type && ev.code == inp_code && ev.value == inp_value;
        }

        [[nodiscard]] constexpr bool is(user_event const& usr) const noexcept {
            return ev.type == usr.type && ev.code == usr.code && ev.value == usr.value;
        }

        [[nodiscard]] constexpr bool is(event_code const& usr) const noexcept {
            return ev.type == usr.type && ev.code == usr.code;
        }

        [[nodiscard]] constexpr bool operator==(user_event const& rhs) const noexcept {
            return is(rhs);
        }

        [[nodiscard]] constexpr bool operator==(event_code const& rhs) const noexcept {
            return is(rhs);
        }

        [[nodiscard]] constexpr auto operator|(event_code const& rhs) const noexcept {
            event_type ret{*this};
            ret.set(rhs);
            ret.reset_time();
            return ret;
        }

        [[nodiscard]] constexpr auto operator|(user_event const& rhs) const noexcept {
            event_type ret{*this};
            ret.set(rhs);
            ret.reset_time();
            return ret;
        }

        [[nodiscard]] constexpr auto operator|(event_type const& rhs) const noexcept {
            event_type ret{rhs};
            ret.reset_time();
            return ret;
        }

        constexpr event_type& operator|=(event_code const& rhs) noexcept {
            set(rhs);
            reset_time();
            return *this;
        }

        constexpr event_type& operator|=(user_event const& rhs) noexcept {
            set(rhs);
            reset_time();
            return *this;
        }

        constexpr event_type& operator|=(event_type const& rhs) noexcept {
            *this = rhs;
            reset_time();
            return *this;
        }

        [[nodiscard]] constexpr std::uint32_t hash() const noexcept {
            return hashed(static_cast<event_code>(*this));
        }

      private:
        input_event ev{};
    };

    [[nodiscard]] constexpr event_type syn() noexcept {
        return {EV_SYN, SYN_REPORT, 0};
    }

    [[nodiscard]] constexpr bool is_mouse_movement(event_type const& event) noexcept {
        return event.type() == EV_REL && (event.code() == REL_X || event.code() == REL_Y);
    }

    [[nodiscard]] constexpr bool is_mouse_event(event_type const& event) noexcept {
        auto const code = event.code();
        auto const type = event.type();
        return (type == EV_REL)
               || (type == EV_KEY && (code == BTN_LEFT || code == BTN_RIGHT || code == BTN_MIDDLE));
    }

    [[nodiscard]] constexpr bool is_syn(event_type const& event) noexcept {
        return event.type() == EV_SYN;
    }

    [[nodiscard]] constexpr bool is_syn(user_event const& event) noexcept {
        return event.type == EV_SYN;
    }
} // namespace foresight
