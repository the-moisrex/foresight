// Created by moisrex on 7/17/26.

module;
#include <cassert>
#include <generator>
#include <span>
#include <string_view>
#include <vector>
export module fs8.devices.queries;
export import fs8.devices.capabilities;
import fs8.devices.udev;
import fs8.log;
import fs8.devices.evdev;

export namespace fs8 {

    template <typename T, std::size_t N>
    struct value_or_view {
        using type = std::array<T, N>;
    };

    template <typename T>
    struct value_or_view<T, std::dynamic_extent> {
        using type = std::span<T const>;
    };

    template <typename T, std::size_t N>
    using value_or_view_t = value_or_view<T, N>::type;

    // The factory function
    template <typename T, std::size_t N = std::dynamic_extent>
    [[nodiscard]] constexpr value_or_view_t<T, N> capture(T const* data, std::size_t size) noexcept {
        if constexpr (N == std::dynamic_extent) {
            return std::span<T const>{data, size}; // Non-owning view
        } else {
            std::array<T, N> arr{};                // Owning copy
            for (std::size_t i = 0; i < N; ++i) {
                arr[i] = data[i];                  // NOLINT(*-pointer-arithmetic)
            }
            return arr;
        }
    }


    /**
     * Matching Action Type
     */
    enum struct [[nodiscard]] query_target : std::uint8_t {
        match_subsystem = 0U, // Match the device's kernel subsystem, e.g. "block", "net", "usb"
        match_sysattr   = 1U, // Match a sysfs attribute exposed under /sys for the device
        match_property  = 2U, // Match a udev property / environment value, e.g. ENV{ID_FS_TYPE}

        // stored in value; "key" must be empty
        tag     = 3U,                                       // Match a udev tag attached to the device
        syspath = 4U,                                       // The device's sysfs path, e.g. /sys/... for this device
        sysname = 5U,                                       // Match the device's sysfs name / kernel device name

        nomatch_flag      = 1U << 7U,                       // Bitmask flag for inverted match
        nomatch_subsystem = match_subsystem | nomatch_flag, // Exclude devices from a subsystem
        nomatch_sysattr   = match_sysattr | nomatch_flag,   // Exclude devices by a sysfs attribute value
        nomatch_property  = match_property | nomatch_flag,  // Exclude devices by a udev property / environment value
    };

    [[nodiscard]] constexpr auto operator+(query_target const target) noexcept {
        return std::to_underlying(target);
    }

    constexpr std::uint8_t globe_search = 101;

    /**
     * Attribute Key/Value
     *
     * You can get attributes:
     *   udevadm info --attribute-walk --name=input/mouse0
     */
    struct [[nodiscard]] query_term {
        // NOLINTBEGIN(*-non-private-member-variables-in-classes)

        // in case of "subsystem", key stores the subsystem, and value stores the devtype
        // if action == tag then key must be empty
        // if action == syspath then key must be empty
        // if action == sysname then key must be empty
        std::string_view key; // example: device/name

        // in case of subsystem, this stores the devtype
        // tags are stored in "value"
        // syspath are stored in "value"
        // sysname are stored in "value"
        std::string_view value;

        /// Matching action
        query_target target;

        /// How much fuzzy search should match the specified "value"?
        ///   100% means exactly
        ///   101% means normal udev matching which can use '*' and '?' and '[...]'
        /// anything less means fuzzy search match
        std::uint8_t percentage = globe_search; // NOLINT(*-magic-numbers)

        // NOLINTEND(*-non-private-member-variables-in-classes)
        [[nodiscard]] constexpr bool operator==(query_term const&) const noexcept = default;

        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return !key.empty();
        }

        [[nodiscard]] std::string_view operator()(udev_device const& dev) const noexcept {
            using enum query_target;
            switch (static_cast<query_target>(+target & ~+nomatch_flag)) {
                case match_subsystem: return dev.subsystem();
                case match_sysattr: return dev.sysattr(key.data());
                case match_property: return dev.property(key.data());
                case sysname: return dev.sysname();
                case syspath: return dev.syspath();
                default: break;
            }
            return {};
        }

        /// Usage: attr::name["USB Keyboard"]
        consteval query_term operator[](std::string_view const new_value) const noexcept {
            query_term copy = *this;
            copy.value = new_value;
            return copy;
        }
    };

    /// Official invalid field
    constexpr query_term invalid_field{.key = {}, .value = {}, .target = query_target::match_subsystem};

    [[nodiscard]] bool is_matched(query_term const& field, std::string_view query_term::* key_val, std::string_view const val) noexcept;

    /**
     * A Query object that describes what kinda device the user is looking for so we can find it again and don't just go
     * and find the wrong one after it's disconnected or whatever.
     */
    template <std::size_t N = std::dynamic_extent>
    struct [[nodiscard]] basic_device_query {
        // NOLINTBEGIN(*-non-private-member-variables-in-classes)

        /// udev fields
        value_or_view_t<query_term, N> fields{};

        /// Capabilities supported by the device
        dev_caps_view caps = +caps::nothing;

        /// Hard limit on caps support
        /// If any device matched would have less than this number matched capabilities, we remove them.
        std::uint8_t caps_support_percentage = 80; // NOLINT(*-magic-numbers)

        /// Multiple Matches are allowed or not?
        /// Default: 1
        std::uint8_t matches_limit = 1;

        /// If we should grab the device's events and not give it to anyone else
        bool grab = false;

        /// Should we fail if we find no device match for the query?
        /// Default: false
        bool fail_on_no_match = false;

        // NOLINTEND(*-non-private-member-variables-in-classes)
        [[nodiscard]] explicit(false) constexpr operator basic_device_query<std::dynamic_extent>() const noexcept {
            return basic_device_query<std::dynamic_extent>{
              .fields                  = std::span<query_term const>{fields},
              .caps                    = caps,
              .caps_support_percentage = caps_support_percentage,
              .matches_limit           = matches_limit,
              .grab                    = grab,
              .fail_on_no_match        = fail_on_no_match};
        }
    };

    using device_query = basic_device_query<>;
    constexpr basic_device_query<0> query{};

    template <std::size_t N>
    [[nodiscard]] constexpr bool operator==(basic_device_query<N> const& lhs, basic_device_query<N> const& rhs) noexcept {
        return std::ranges::equal(lhs.fields, rhs.fields)
               && (lhs.grab == rhs.grab)
               && (lhs.matches_limit == rhs.matches_limit)
               && (lhs.fail_on_no_match == rhs.fail_on_no_match)
               && (lhs.caps == rhs.caps)
               && (lhs.caps_support_percentage == rhs.caps_support_percentage);
    }

    constexpr struct [[nodiscard]] grab_tag {
        template <std::size_t N>
        constexpr void operator()(basic_device_query<N>& out_query) const noexcept {
            out_query.grab = true;
        }
    } grab;

    constexpr struct [[nodiscard]] allow_multiple_matches_tag {
        template <std::size_t N>
        constexpr void operator()(basic_device_query<N>& out_query) const noexcept {
            out_query.matches_limit = std::numeric_limits<std::uint8_t>::max();
        }
    } allow_multiple_matches;

    constexpr struct [[nodiscard]] matches_limit {
        std::uint8_t limit = 1;

        template <std::size_t N>
        constexpr void operator()(basic_device_query<N>& out_query) const noexcept {
            out_query.matches_limit = limit;
        }

        consteval matches_limit operator()(std::uint8_t const inp_limit) const noexcept {
            return matches_limit{.limit = inp_limit};
        }
    } matches_limit;

    constexpr struct [[nodiscard]] matches_percentage {
        std::uint8_t percentage = 100;

        template <std::size_t N>
        constexpr void operator()(basic_device_query<N>& out_query) const noexcept {
            out_query.caps_support_percentage = percentage;
        }

        consteval matches_percentage operator()(std::uint8_t const inp_percentage) const noexcept {
            assert(inp_percentage <= 100);
            return matches_percentage{.percentage = inp_percentage};
        }
    } matches_percentage;

    constexpr struct [[nodiscard]] fail_on_no_match_tag {
        template <std::size_t N>
        constexpr void operator()(basic_device_query<N>& out_query) const noexcept {
            out_query.fail_on_no_match = true;
        }
    } fail_on_no_match;

    template <typename T>
    concept QueryTag = std::invocable<T, device_query&>;

    template <std::size_t N>
    [[nodiscard]] consteval auto operator+(std::array<query_term, N> const& lhs, query_term rhs) noexcept {
        std::array<query_term, N + 1U> arr;
        std::size_t                    index = 0;
        for (auto const& field : lhs) {
            arr[index++] = field;
        }
        arr[index] = rhs;
        return arr;
    }

    consteval query_target unmatch(query_target const action) {
        using enum query_target;
        auto const val         = std::to_underlying(action);
        auto const base_action = static_cast<std::uint8_t>(val & ~std::to_underlying(nomatch_flag));

        if (base_action > std::to_underlying(match_property)) {
            throw std::invalid_argument("Matching action cannot be unmatched!");
        }

        return static_cast<query_target>(val ^ std::to_underlying(nomatch_flag));
    }

    consteval query_target operator-(query_target const action) noexcept {
        return unmatch(action);
    }

    [[nodiscard]] consteval query_term unmatch(query_term const& field) noexcept {
        query_term result = field;
        result.target     = unmatch(field.target);
        return result;
    }

    [[nodiscard]] consteval query_term operator-(query_term const& field) noexcept {
        return unmatch(field);
    }

    template <std::size_t N>
    [[nodiscard]] consteval auto operator-(std::array<query_term, N> const& lhs, query_term const rhs) noexcept {
        return operator+(lhs, unmatch(rhs));
    }

    [[nodiscard]] consteval std::array<query_term, 2U> operator+(query_term lhs, query_term rhs) noexcept {
        return std::array{std::move(lhs), std::move(rhs)};
    }

    // Pipe: device_query | option  →  device_query
    template <std::size_t N, QueryTag Tag>
    [[nodiscard]] consteval auto operator|(basic_device_query<N> inp_query, Tag tag) noexcept {
        tag(inp_query);
        return inp_query;
    }

    template <std::size_t N1, std::size_t N2>
    [[nodiscard]] consteval auto operator|(basic_device_query<N1> inp_query, dev_caps<N2> const& inp_cap) noexcept {
        inp_query.caps = view(inp_cap);
        return inp_query;
    }

    template <std::size_t N>
    [[nodiscard]] consteval auto operator|(basic_device_query<N> inp_query, dev_caps_view const& inp_cap) noexcept {
        inp_query.caps = inp_cap;
        return inp_query;
    }

    template <std::size_t N>
        requires(N != std::dynamic_extent)
    consteval basic_device_query<N + 1> operator+(basic_device_query<N> inp_query, query_term const new_field) noexcept {
        basic_device_query<N + 1> res;
        std::size_t               index = 0;
        for (auto const& field : inp_query.fields) {
            res.fields[index++] = field;
        }
        res.fields[index]           = new_field;
        res.matches_limit           = inp_query.matches_limit;
        res.caps                    = inp_query.caps;
        res.caps_support_percentage = inp_query.caps_support_percentage;
        res.fail_on_no_match        = inp_query.fail_on_no_match;
        res.grab                    = inp_query.grab;
        return res;
    }

    template <std::size_t N>
        requires(N != std::dynamic_extent)
    consteval basic_device_query<N + 1> operator-(basic_device_query<N> inp_query, query_term const new_field) noexcept {
        return operator+(inp_query, unmatch(new_field));
    }

    [[nodiscard]] std::string_view to_string(query_target) noexcept;
    [[nodiscard]] std::string      to_string(device_query const& inp_query);

    [[nodiscard]] bool matches(evdev const& dev, device_query const& inp_query) noexcept;
    [[nodiscard]] bool matches(udev_device const& dev, device_query const& inp_query) noexcept;

    void match(udev_enumerate& enumerate, device_query const& inp_query) noexcept;
    void match(udev_monitor& monitor, device_query const& inp_query) noexcept;


    template <typename T>
    concept field_range = std::ranges::forward_range<T> && std::same_as<std::ranges::range_value_t<T>, query_term>;

    [[nodiscard]] constexpr bool is_subsystem(query_term const& field) noexcept {
        return field.target == query_target::match_subsystem;
    }

    [[nodiscard]] constexpr bool is_property(query_term const& field) noexcept {
        return field.target == query_target::match_property;
    }

    [[nodiscard]] constexpr bool is_sysattr(query_term const& field) noexcept {
        return field.target == query_target::match_sysattr;
    }

    template <std::size_t N>
    [[nodiscard]] constexpr auto subsystems(basic_device_query<N> const& inp_query) noexcept {
        return inp_query.fields | std::views::filter(is_subsystem);
    }

    [[nodiscard]] constexpr query_term subsystem(std::string_view const sub, std::string_view const devtype = {}) noexcept {
        return query_term{.key = sub, .value = devtype, .target = query_target::match_subsystem};
    }

    template <std::size_t N>
    [[nodiscard]] constexpr auto properties(basic_device_query<N> const& inp_query) noexcept {
        return inp_query.fields | std::views::filter(is_property);
    }

    template <std::size_t N>
    [[nodiscard]] constexpr auto sysattrs(basic_device_query<N> const& inp_query) noexcept {
        return inp_query.fields | std::views::filter(is_sysattr);
    }

    query_term       property(device_query const& inp_query, std::string_view key) noexcept;
    query_term       sysattr(device_query const& inp_query, std::string_view key) noexcept;
    std::string_view name(device_query const& inp_query) noexcept;

    [[nodiscard]] bool has_subsystem(device_query const& inp_query) noexcept;
    [[nodiscard]] bool has_subsystem(device_query const& inp_query, std::string_view subsystem) noexcept;
    [[nodiscard]] bool has_property(device_query const& inp_query, std::string_view key) noexcept;

    struct [[nodiscard]] udev_device_pick {
        udev_device device{};

        // User query to re-get this device
        device_query query{};

        // The index of the query
        std::uint8_t query_index = 0;
    };

    constexpr query_term match_subsystem(std::string_view const sub, std::string_view const devtype = {}) noexcept {
        return {.key = sub, .value = devtype, .target = query_target::match_subsystem};
    }

    constexpr query_term match_sysname(std::string_view const name) noexcept {
        return {.key = {}, .value = name, .target = query_target::sysname};
    }

    constexpr query_term match_property(std::string_view const key, std::string_view const value) noexcept {
        return {.key = key, .value = value, .target = query_target::match_property};
    }

    constexpr query_term match_sysattr(std::string_view const key, std::string_view const value) noexcept {
        return {.key = key, .value = value, .target = query_target::match_sysattr};
    }

    [[nodiscard]] std::generator<udev_device> filter_devices(udev_enumerate const& enumerate, device_query const& query) noexcept;

    template <typename... T>
        requires(std::convertible_to<T, device_query> && ...)
    [[nodiscard]] std::generator<udev_device_pick> filter_devices(T const&... queries) noexcept {
        static_assert(sizeof...(T) >= 1 && sizeof...(T) < 255, "Too many or too little queries specified.");
        udev_enumerate enumerator{};
        (match(enumerator, queries), ...);
        enumerator.scan_devices();

        std::uint8_t index = 0;
        template for (auto const& cur_query : {queries...}) {
            for (auto device : filter_devices(enumerator, cur_query)) {
                co_yield udev_device_pick{.device = std::move(device), .query = cur_query, .query_index = index};
            }
            ++index;
        }
    }

    /// Parse a single string_view into a query_term
    query_term parse_query_term(std::string_view str) noexcept;

    /// Parse argc and argv into a dynamic device_query
    device_query parse_device_query(int argc, char const* const* argv, std::vector<query_term>& fields);

    namespace attr {
        constexpr query_term name     = match_sysattr("device/name", "");
        constexpr query_term keyboard = match_property("ID_INPUT_KEYBOARD", "1");
        constexpr query_term mouse    = match_property("ID_INPUT_MOUSE", "1");
        constexpr query_term tablet   = match_property("ID_INPUT_TABLET", "1");

        constexpr query_term input           = match_property("ID_INPUT", "1");
        constexpr query_term via_usb         = match_property("ID_BUS", "usb");
        constexpr query_term input_subsystem = match_subsystem("input");
        constexpr query_term event_sysname   = match_sysname("event*");
    } // namespace attr

    constexpr auto input    = query + attr::input + attr::input_subsystem + attr::event_sysname;
    constexpr auto keyboard = (input + attr::keyboard) | caps::keyboard;
    constexpr auto mouse    = (input + attr::mouse) | caps::mouse;
    constexpr auto tablet   = (input + attr::tablet) | caps::tablet;

} // namespace fs8
