// Created by moisrex on 6/8/25.

module;
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <libevdev/libevdev.h>
#include <linux/uinput.h>
export module fs8.event;
import fs8.hash;

// don't import log here.

export namespace fs8 {

    struct [[nodiscard]] user_event {
        using type_type  = decltype(input_event::type);
        using code_type  = decltype(input_event::code);
        using value_type = decltype(input_event::value);

        type_type  type  = EV_MAX;
        code_type  code  = KEY_MAX;
        value_type value = 0;
    };

    constexpr user_event invalid_user_event{.type = EV_MAX, .code = KEY_MAX, .value = 0};
    constexpr user_event syn_user_event{.type = EV_SYN, .code = SYN_REPORT, .value = 0};

    using user_event_callback = std::function_ref<void(user_event const&)>;

    [[nodiscard]] constexpr bool operator==(user_event const& lhs, user_event const& rhs) noexcept {
        return lhs.type == rhs.type && lhs.code == rhs.code && lhs.value == rhs.value;
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

    struct [[nodiscard]] key_event {
        using code_type  = decltype(input_event::code);
        using value_type = decltype(input_event::value);

        // type == EV_KEY
        code_type  code  = KEY_MAX;
        value_type value = 1;
    };

    template <std::size_t N>
    using event_codes = std::array<event_code, N>;

    [[nodiscard]] constexpr bool is_invalid(user_event const& event) noexcept {
        return event.type == invalid_user_event.type;
    }

    [[nodiscard]] constexpr bool is_invalid(key_event const& event) noexcept {
        return event.code == invalid_user_event.code;
    }

    [[nodiscard]] constexpr std::uint32_t hashed(event_code const& code) noexcept {
        static constexpr std::uint32_t shift  = std::countr_zero(std::bit_ceil<std::uint32_t>(KEY_MAX));
        std::uint32_t                  hash   = 0;
        hash                                 |= static_cast<std::uint32_t>(code.type) << shift;
        hash                                 |= static_cast<std::uint32_t>(code.code);
        return hash;
    }

    [[nodiscard]] constexpr std::uint32_t hashed(key_event const& code) noexcept {
        static constexpr std::uint32_t shift  = std::countr_zero(std::bit_ceil<std::uint32_t>(KEY_MAX));
        std::uint32_t                  hash   = 0;
        hash                                 |= static_cast<std::uint32_t>(code.code) << shift;
        hash                                 |= static_cast<std::uint32_t>(code.value);
        return hash;
    }

    [[nodiscard]] constexpr key_event unhashed(std::uint32_t const hash) noexcept {
        static constexpr std::uint32_t shift = std::countr_zero(std::bit_ceil<std::uint32_t>(KEY_MAX));
        return key_event{
          .code  = static_cast<std::uint16_t>(hash >> shift),
          .value = static_cast<std::uint16_t>(hash & ((1u << shift) - 1u)),
        };
    }

    [[nodiscard]] consteval event_code key_code(event_code::code_type code) noexcept {
        return event_code{.type = EV_KEY, .code = code};
    }

    template <typename... T>
    [[nodiscard]] consteval event_codes<sizeof...(T)> key_codes(T... codes) noexcept {
        return event_codes<sizeof...(T)>{std::array<event_code, sizeof...(T)>{key_code(static_cast<event_code::code_type>(codes))...}};
    }

    [[nodiscard]] constexpr std::uint32_t hashed(event_code::type_type const type, event_code::code_type const code) noexcept {
        return hashed(event_code{.type = type, .code = code});
    }

    // ── source_id ───────────────────────────────────────────────────────────
    //
    // A source_id is a std::uint32_t that encodes the origin of an event:
    //
    //   High 16 bits — mod_id: identifies which pipeline mod generated the
    //                  event (intercept, from_input, scheduler, etc.).
    //   Low  16 bits — source_index: a mod-private identifier (e.g. device
    //                  index for intercept, tick index for scheduler).
    //
    // A value of 0 (source_id_none) means "unknown / unset" — the default
    // for newly constructed events.  Mods that synthesise events (emit,
    // fork_emit) leave it at 0; provider mods set it to their own mod_id
    // combined with a mod-specific source index.

    /// Sentinel value meaning "unknown / unset source".
    constexpr std::uint32_t source_id_none = 0;

    /// Extract the mod_id (high 16 bits) from a source_id.
    [[nodiscard]] constexpr std::uint16_t mod_id(std::uint32_t const src) noexcept {
        return static_cast<std::uint16_t>(src >> 16);
    }

    /// Extract the source_index (low 16 bits) from a source_id.
    [[nodiscard]] constexpr std::uint16_t source_index(std::uint32_t const src) noexcept {
        return static_cast<std::uint16_t>(src & 0xFFFFu);
    }

    /// Pack a mod_id and source_index into a single source_id.
    [[nodiscard]] constexpr std::uint32_t make_source_id(std::uint16_t const m, std::uint16_t const idx) noexcept {
        return (static_cast<std::uint32_t>(m) << 16) | idx;
    }

    /// Derive a compile-time mod_id for a type T.  If T defines a static
    /// constexpr `mod_id` member, use it; otherwise hash __PRETTY_FUNCTION__
    /// (which includes the type name) at compile time via ci_hash.
    ///
    /// Only provider mods (intercept, from_input, scheduler, …) need a mod_id;
    /// the hash fallback gives them a unique value without manual registration.
    template <typename T>
    [[nodiscard]] consteval std::uint16_t mod_id_of() noexcept {
        if constexpr (requires { T::mod_id; }) {
            return T::mod_id;
        } else {
            constexpr std::string_view name = __PRETTY_FUNCTION__;
            return static_cast<std::uint16_t>(ci_hash(name));
        }
    }

    /// Shorthand: make_source_id(mod_id_of<decltype(mod)>(), idx).
    template <typename ModT>
    [[nodiscard]] constexpr std::uint32_t sid(ModT const&, std::uint16_t const idx) noexcept {
        return make_source_id(mod_id_of<ModT>(), idx);
    }

    /// Shorthand: sid(mod, 0).
    [[nodiscard]] constexpr std::uint32_t sid(auto const& mod) noexcept {
        return sid(mod, std::uint16_t{0});
    }

    /// Extract the mod_id (high 16 bits) from a source_id.
    [[nodiscard]] constexpr std::uint16_t sid(std::uint32_t const src) noexcept {
        return static_cast<std::uint16_t>(src >> 16u);
    }

    /// Convert a source_id to a human-readable string (for diagnostics).
    [[nodiscard]] std::string_view to_source_string(std::uint32_t source_id) noexcept;

    // ── end source_id ───────────────────────────────────────────────────────
    struct [[nodiscard]] event_type {
        using type_type  = decltype(input_event::type);
        using code_type  = decltype(input_event::code);
        using value_type = decltype(input_event::value);
        using time_type  = decltype(input_event::time);

        constexpr event_type() noexcept = default;

        constexpr explicit event_type(input_event const& inp_ev) noexcept : ev{inp_ev} {}

        constexpr explicit event_type(user_event const& inp_ev) noexcept : event_type{inp_ev.type, inp_ev.code, inp_ev.value} {}

        constexpr event_type(type_type const inp_type, code_type const inp_code, value_type const inp_val) noexcept : from{source_id_none} {
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
            from    = source_id_none;
            return *this;
        }

        constexpr event_type& operator=(user_event const& inp_code) noexcept {
            ev.type  = inp_code.type;
            ev.code  = inp_code.code;
            ev.value = inp_code.value;
            from     = source_id_none;
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

        constexpr void set(type_type const inp_type, code_type const inp_code, value_type const inp_value) noexcept {
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

        [[nodiscard]] explicit constexpr operator key_event() const noexcept {
            assert(ev.type == EV_KEY);
            return key_event{.code = ev.code, .value = ev.value};
        }

        constexpr void reset_time() noexcept {
            if !consteval {
                gettimeofday(&ev.time, nullptr);
            }
        }

        [[nodiscard]] std::string_view code_name() const noexcept {
            auto const* name = libevdev_event_code_get_name(ev.type, ev.code);
            return name != nullptr ? std::string_view{name} : std::string_view{"<unknown>"};
        }

        [[nodiscard]] std::string_view type_name() const noexcept {
            auto const* name = libevdev_event_type_get_name(ev.type);
            return name != nullptr ? std::string_view{name} : std::string_view{"<unknown>"};
        }

        [[nodiscard]] std::string_view value_name() const noexcept {
            auto const* name = libevdev_event_value_get_name(ev.type, ev.code, ev.value);
            return name != nullptr ? std::string_view{name} : std::string_view{"<unknown>"};
        }

        [[nodiscard]] constexpr bool is(type_type const inp_type) const noexcept {
            return libevdev_event_is_type(&ev, inp_type) == 1;
        }

        [[nodiscard]] constexpr bool is(type_type const inp_type, code_type const inp_code) const noexcept {
            return ev.type == inp_type && ev.code == inp_code;
        }

        [[nodiscard]] constexpr bool is_of(type_type const inp_type, code_type const inp_code) const noexcept {
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

        [[nodiscard]] constexpr bool is(type_type const inp_type, code_type const inp_code, value_type const inp_value) const noexcept {
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

        [[nodiscard]] constexpr std::uint32_t source() const noexcept {
            return from;
        }

        constexpr void source(std::uint32_t const inp_source) noexcept {
            from = inp_source;
        }

        [[nodiscard]] constexpr std::uint32_t hash() const noexcept {
            return hashed(static_cast<event_code>(*this));
        }

      private:
        input_event   ev{};
        std::uint32_t from = source_id_none;
    };

    [[nodiscard]] consteval event_type syn() noexcept {
        return {EV_SYN, SYN_REPORT, 0};
    }

    [[nodiscard]] constexpr bool is_mouse_movement(event_type const& event) noexcept {
        return event.type() == EV_REL && (event.code() == REL_X || event.code() == REL_Y);
    }

    [[nodiscard]] constexpr bool is_high_res_scroll(event_type const& event) noexcept {
        return event.type() == EV_REL && (event.code() == REL_WHEEL_HI_RES || event.code() == REL_HWHEEL_HI_RES);
    }

    [[nodiscard]] constexpr bool is_mouse_clicks(event_type const& event) noexcept {
        auto const code = event.code();
        auto const type = event.type();
        return type == EV_KEY && (code == BTN_LEFT || code == BTN_RIGHT || code == BTN_MIDDLE);
    }

    [[nodiscard]] constexpr bool is_mouse_event(event_type const& event) noexcept {
        auto const code = event.code();
        auto const type = event.type();
        return (type == EV_REL) || (type == EV_KEY && (code == BTN_LEFT || code == BTN_RIGHT || code == BTN_MIDDLE));
    }

    [[nodiscard]] constexpr bool is_syn(event_type const& event) noexcept {
        return event.type() == EV_SYN;
    }

    [[nodiscard]] constexpr bool is_syn(user_event const& event) noexcept {
        return event.type == EV_SYN;
    }

    /// Sentinel type value used by all lifecycle events.
    constexpr auto special_event_type = static_cast<event_type::type_type>(EV_MAX + 1);

    /// A lifecycle event (tag replacement) that shares the same field layout
    /// as `event_type` so mods can handle both regular events and lifecycle
    /// events in a single overload if desired. The `type` field is set to
    /// `EV_MAX + 1` (a value no real kernel event will ever use) so callers
    /// can distinguish lifecycle events from real input events.
    struct [[nodiscard]] special_event {
        using type_type  = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;
        using time_type  = event_type::time_type;

        time_type     time  = {};
        type_type     type  = special_event_type;
        code_type     code  = 0;
        value_type    value = 0;
        std::uint32_t from  = source_id_none;
    };

    /// Lifecycle event constants. Each uses a unique `code` value; toggle
    /// events encode their state in the `value` field (1 = on, 0 = off).
    constexpr special_event start{.type = special_event_type, .code = 0};
    constexpr special_event no_init{.type = special_event_type, .code = 1};
    constexpr special_event load_event{.type = special_event_type, .code = 2};
    constexpr special_event next_event{.type = special_event_type, .code = 3};
    constexpr special_event toggle_on{.type = special_event_type, .code = 4, .value = 1};
    constexpr special_event toggle_off{.type = special_event_type, .code = 4, .value = 0};
    constexpr special_event idle{.type = special_event_type, .code = 5};

    /// Check whether a `special_event` matches a given lifecycle code.
    [[nodiscard]] constexpr bool is_special(special_event const& ev, special_event::code_type const code) noexcept {
        return ev.type == special_event_type && ev.code == code;
    }

    /// Check whether an `event_type` is actually a lifecycle event (shouldn't
    /// happen in practice, but guards against data corruption).
    [[nodiscard]] constexpr bool is_special(event_type const& ev) noexcept {
        return ev.type() == special_event_type;
    }

    /// Hash a `special_event` into a `std::uint32_t` for use in `switch`/`case` and
    /// comparison.  The hash encodes both `code` and `value` so that `toggle_on`
    /// and `toggle_off` (which share the same `code`) produce different hashes.
    [[nodiscard]] constexpr std::uint32_t hashed(special_event const& ev) noexcept {
        static constexpr std::uint32_t shift  = 6;
        std::uint32_t                  hash   = 0;
        hash                                 |= static_cast<std::uint32_t>(ev.code) << shift;
        hash                                 |= static_cast<std::uint32_t>(ev.value) & 0x3Fu;
        return hash;
    }

    /// Unhash: recover the `code` from a hash produced by `hashed(special_event)`.
    [[nodiscard]] constexpr special_event::code_type unhashed_special(std::uint32_t const hash) noexcept {
        static constexpr std::uint32_t shift = 6;
        return static_cast<special_event::code_type>(hash >> shift);
    }

    /// `operator+` returns the hash of a `special_event`, enabling
    /// `switch (tag + start)` patterns.
    [[nodiscard]] constexpr std::uint32_t operator+(special_event const& ev) noexcept {
        return hashed(ev);
    }

    /// Two `special_event`s are equal iff they carry the same `code` and `value`.
    [[nodiscard]] constexpr bool operator==(special_event const& lhs, special_event const& rhs) noexcept {
        return lhs.code == rhs.code && lhs.value == rhs.value;
    }

    /// Check whether an `event_type` matches a `special_event` by code.
    [[nodiscard]] constexpr bool operator==(event_type const& lhs, special_event const& rhs) noexcept {
        return lhs.type() == special_event_type && lhs.code() == rhs.code;
    }

    [[nodiscard]] constexpr bool operator==(special_event const& lhs, event_type const& rhs) noexcept {
        return rhs == lhs;
    }

} // namespace fs8
