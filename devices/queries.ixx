// Created by moisrex on 7/17/26.

module;
#include <cassert>
#include <generator>
#include <string_view>
export module fs8.devices.queries;
export import fs8.devices.capabilities;
import fs8.devices.udev;
import fs8.devices.evdev;

namespace {

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

} // namespace

export namespace fs8 {


    /**
     * Matching Action Type
     */
    enum struct [[nodiscard]] matching_action_type : std::uint8_t {
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


    constexpr std::uint8_t globe_search = 101;

    /**
     * Attribute Key/Value
     *
     * You can get attributes:
     *   udevadm info --attribute-walk --name=input/mouse0
     */
    struct [[nodiscard]] field_type {
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
        matching_action_type matching_action;

        /// How much fuzzy search should match the specified "value"?
        ///   100% means exactly
        ///   101% means normal udev matching which can use '*' and '?' and '[...]'
        /// anything less means fuzzy search match
        std::uint8_t percentage = globe_search; // NOLINT(*-magic-numbers)

        // NOLINTEND(*-non-private-member-variables-in-classes)
        [[nodiscard]] constexpr bool operator==(field_type const&) const noexcept = default;

        [[nodiscard]] explicit constexpr operator bool() const noexcept {
            return !key.empty();
        }
    };

    [[nodiscard]] constexpr auto operator+(matching_action_type const action) noexcept {
        return std::to_underlying(action);
    }

    /// Official invalid field
    constexpr field_type invalid_field{.key = {}, .value = {}, .matching_action = matching_action_type::match_subsystem};

    [[nodiscard]] constexpr bool
    is_matched(field_type const& field, std::string_view field_type::* key_val, std::string_view const val) noexcept {
        using enum matching_action_type;
        // todo: handle percentage
        if ((+field.matching_action & +nomatch_flag) != 0) {
            return field.*key_val != val;
        }
        return field.*key_val == val;
    }

    /**
     * A Query object that describes what kinda device the user is looking for so we can find it again and don't just go
     * and find the wrong one after it's disconnected or whatever.
     */
    template <std::size_t N = std::dynamic_extent>
    struct [[nodiscard]] basic_device_query {
        /// udev fields
        value_or_view_t<field_type, N> fields{};

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
    [[nodiscard]] consteval auto operator+(std::array<field_type, N> const& lhs, field_type rhs) noexcept {
        std::array<field_type, N + 1U> arr;
        std::size_t                    index = 0;
        for (auto const& field : lhs) {
            arr[index++] = field;
        }
        arr[index] = rhs;
        return arr;
    }

    consteval matching_action_type unmatch(matching_action_type const action) {
        using enum matching_action_type;
        auto const val         = std::to_underlying(action);
        auto const base_action = static_cast<std::uint8_t>(val & ~std::to_underlying(nomatch_flag));

        if (base_action > std::to_underlying(match_property)) {
            throw std::invalid_argument("Matching action cannot be unmatched!");
        }

        return static_cast<matching_action_type>(val ^ std::to_underlying(nomatch_flag));
    }

    consteval matching_action_type operator-(matching_action_type const action) noexcept {
        return unmatch(action);
    }

    [[nodiscard]] consteval field_type unmatch(field_type const& field) noexcept {
        field_type result      = field;
        result.matching_action = unmatch(field.matching_action);
        return result;
    }

    [[nodiscard]] consteval field_type operator-(field_type const& field) noexcept {
        return unmatch(field);
    }

    template <std::size_t N>
    [[nodiscard]] consteval auto operator-(std::array<field_type, N> const& lhs, field_type const rhs) noexcept {
        return operator+(lhs, unmatch(rhs));
    }

    [[nodiscard]] consteval std::array<field_type, 2U> operator+(field_type lhs, field_type rhs) noexcept {
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
    consteval basic_device_query<N + 1> operator+(basic_device_query<N> inp_query, field_type const new_field) noexcept {
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
    consteval basic_device_query<N + 1> operator-(basic_device_query<N> inp_query, field_type const new_field) noexcept {
        return operator+(inp_query, unmatch(new_field));
    }

    [[nodiscard]] std::string_view to_string(matching_action_type) noexcept;
    [[nodiscard]] std::string      to_string(device_query const& inp_query);

    struct [[nodiscard]] udev_device_pick {
        udev_device device{};

        // User query to re-get this device
        device_query query{};

        // The index of the query
        std::uint8_t query_index = 0;
    };

    [[nodiscard]] bool matches(evdev const& dev, device_query const& inp_query) noexcept;
    [[nodiscard]] bool matches(udev_device const& dev, device_query const& inp_query) noexcept;

    void match(udev_enumerate& enumerate, device_query const& inp_query) noexcept;
    void match(udev_monitor& monitor, device_query const& inp_query) noexcept;


    template <typename T>
    concept field_range = std::ranges::forward_range<T> && std::same_as<std::ranges::range_value_t<T>, field_type>;

    [[nodiscard]] constexpr bool is_subsystem(field_type const& field) noexcept {
        return field.matching_action == matching_action_type::match_subsystem;
    }

    [[nodiscard]] constexpr bool is_property(field_type const& field) noexcept {
        return field.matching_action == matching_action_type::match_subsystem;
    }

    [[nodiscard]] constexpr bool is_sysattr(field_type const& field) noexcept {
        return field.matching_action == matching_action_type::match_sysattr;
    }

    template <std::size_t N>
    [[nodiscard]] constexpr auto subsystems(basic_device_query<N> const& inp_query) noexcept {
        return inp_query.fields | std::views::filter(is_subsystem);
    }

    [[nodiscard]] constexpr field_type subsystem(std::string_view const sub, std::string_view const devtype = {}) noexcept {
        return field_type{.key = sub, .value = devtype, .matching_action = matching_action_type::match_subsystem};
    }

    template <std::size_t N>
    [[nodiscard]] constexpr auto properties(basic_device_query<N> const& inp_query) noexcept {
        return inp_query.fields | std::views::filter(is_property);
    }

    template <std::size_t N>
    [[nodiscard]] constexpr auto sysattrs(basic_device_query<N> const& inp_query) noexcept {
        return inp_query.fields | std::views::filter(is_sysattr);
    }

    field_type       property(device_query const& inp_query, std::string_view key) noexcept;
    field_type       sysattr(device_query const& inp_query, std::string_view key) noexcept;
    std::string_view name(device_query const& inp_query) noexcept;

    [[nodiscard]] bool has_subsystem(device_query const& inp_query, std::string_view subsystem) noexcept;
    [[nodiscard]] bool has_property(device_query const& inp_query, std::string_view key) noexcept;

    template <typename... T>
        requires((std::convertible_to<T, device_query> && ...))
    [[nodiscard]] std::generator<udev_device_pick> all_devices(T const&... queries) noexcept {
        static_assert(sizeof...(T) < std::numeric_limits<std::uint8_t>::max(), "Too many queries.");

        udev_enumerate enumerator{};
        (match(enumerator, queries.classification), ...);
        enumerator.scan_devices();

        std::array<std::uint8_t, sizeof...(T)> limits{queries.matches_limit...};

        for (auto const& entry : enumerator.list_entries()) {
            auto dev = udev_device{entry};
            if (!dev) [[unlikely]] {
                continue;
            }
            // todo: use `template for` whenever compilers start to support it
            std::uint8_t index = 0;
            ((matches(dev, queries)
              && (limits[index++]-- != 0)
              && (co_yield udev_device_pick{.device = std::move(dev), .query = queries, .query_index = index}, true)),
             ...);
        }
    }

    namespace attr {
        constexpr field_type name{.key = "device/name", .value = "", .matching_action = matching_action_type::match_sysattr};
        constexpr field_type keyboard{.key = "ID_INPUT_KEYBOARD", .value = "1", .matching_action = matching_action_type::match_property};
        constexpr field_type mouse{.key = "ID_INPUT_MOUSE", .value = "1", .matching_action = matching_action_type::match_property};
        constexpr field_type tablet{.key = "ID_INPUT_TABLET", .value = "1", .matching_action = matching_action_type::match_property};
    } // namespace attr

    constexpr auto keyboard = query + attr::keyboard | caps::keyboard;
    constexpr auto mouse    = query + attr::mouse | caps::mouse;
    constexpr auto tablet   = query + attr::tablet | caps::tablet;

} // namespace fs8
