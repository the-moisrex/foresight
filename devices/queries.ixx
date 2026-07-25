// Created by moisrex on 7/17/26.

module;
#include <cassert>
#include <generator>
#include <string_view>
export module fs8.devices.queries;
export import fs8.devices.classification;
export import fs8.devices.capabilities;
import fs8.devices.evdev;

export namespace fs8 {


    enum struct [[nodiscard]] attribute_type {
        match_subsystem,
        match_sysattr,
        match_property,
        match_tag,
        syspath,
        match_sysname,
        nomatch_subsystem,
        nomatch_sysattr,
        nomatch_property,
    };

    constexpr std::uint8_t globe_search = 101;

    /**
     * Attribute Key/Value
     *
     * You can get attributes:
     *   udevadm info --attribute-walk --name=input/mouse0
     */
    struct [[nodiscard]] attribute_kv {
        attribute_type type;

        std::string_view key{}; // example: device/name
        std::string_view value{};

        /// How much fuzzy search should match the specified "value"?
        ///   100% means exactly
        ///   101% means normal udev matching which can use '*' and '?' and '[...]'
        /// anything less means fuzzy search match
        std::uint8_t percentage = globe_search; // NOLINT(*-magic-numbers)
    };

    /**
     * A Query object that describes what kinda device the user is looking for so we can find it again and don't just go and find the wrong
     * one after it's disconnected or whatever.
     */
    struct [[nodiscard]] device_query {
        /// udev queries
        std::span<attribute_kv> props{};

        /// If we should grab the device's events and not give it to anyone else
        bool grab = false;

        /// Multiple Matches are allowed or not?
        /// Default: 1
        std::uint8_t matches_limit = 1;

        /// Should we fail if we find no device match for the query?
        /// Default: false
        bool fail_on_no_match = false;

        /// Capabilities supported by the device
        dev_caps_view caps = +caps::nothing;

        /// Hard limit on caps support
        /// If any device matched would have less than this number matched capabilities, we remove them.
        std::uint8_t caps_support_percentage = 80; // NOLINT(*-magic-numbers)
    };

    namespace attr {
        constexpr attribute_kv name{.type = attribute_type::match_sysattr, .key = "device/name"};
    } // namespace attr

    [[nodiscard]] constexpr bool operator==(device_query const& lhs, device_query const& rhs) noexcept {
        // Note: Assuming `classification` is stateless or has its own operator==.
        // If Cl doesn't have operator==, omit `lhs.classification == rhs.classification &&`
        return std::ranges::equal(lhs.props, rhs.props)
               && (lhs.grab == rhs.grab)
               && (lhs.matches_limit == rhs.matches_limit)
               && (lhs.fail_on_no_match == rhs.fail_on_no_match)
               && (lhs.caps == rhs.caps)
               && (lhs.caps_support_percentage == rhs.caps_support_percentage);
    }

    struct query_tag {};

    constexpr struct grab_tag : query_tag {
        constexpr void operator()(device_query& query) const noexcept {
            query.grab = true;
        }
    } grab;

    constexpr struct allow_multiple_matches_tag : query_tag {
        constexpr void operator()(device_query& query) const noexcept {
            query.matches_limit = std::numeric_limits<std::uint8_t>::max();
        }
    } allow_multiple_matches;

    constexpr struct [[nodiscard]] matches_limit : query_tag {
        std::uint8_t limit = 1;

        constexpr void operator()(device_query& query) const noexcept {
            query.matches_limit = limit;
        }

        consteval matches_limit operator()(std::uint8_t const inp_limit) const noexcept {
            return matches_limit{.limit = inp_limit};
        }
    } matches_limit;

    constexpr struct [[nodiscard]] matches_percentage : query_tag {
        std::uint8_t percentage = 100;

        constexpr void operator()(device_query& query) const noexcept {
            query.caps_support_percentage = percentage;
        }

        consteval matches_percentage operator()(std::uint8_t const percentage) const noexcept {
            assert(percentage <= 100);
            return matches_percentage{.percentage = percentage};
        }
    } matches_percentage;

    constexpr struct fail_on_no_match_tag : query_tag {
        constexpr void operator()(device_query& query) const noexcept {
            query.fail_on_no_match = true;
        }
    } fail_on_no_match;

    template <typename T>
    concept QueryTag = std::is_base_of_v<query_tag, T>;

    // Pipe: device_query | option  →  device_query
    template <QueryTag Tag>
    [[nodiscard]] constexpr device_query operator|(device_query&& query, Tag tag) noexcept {
        tag(query);
        return std::move(query);
    }

    template <std::size_t N>
    [[nodiscard]] constexpr device_query operator|(device_query&& query, dev_caps<N> const& inp_cap) noexcept {
        query.caps = view(inp_cap);
        return std::move(query);
    }

    template <std::size_t N>
    [[nodiscard]] constexpr device_query operator|(device_query&& query, dev_caps_view const& inp_cap) noexcept {
        query.caps = inp_cap;
        return std::move(query);
    }

    [[nodiscard]] constexpr device_query snapshot(device_query const& query) noexcept {
        return device_query{.grab                    = query.grab,
                            .matches_limit           = query.matches_limit,
                            .fail_on_no_match        = query.fail_on_no_match,
                            .caps                    = query.caps,
                            .caps_support_percentage = query.caps_support_percentage};
    }

    [[nodiscard]] std::string to_string(device_query const& query);

    struct [[nodiscard]] udev_device_pick {
        udev_device device{};

        // User query to re-get this device
        device_query query{};

        // The index of the query
        std::uint8_t query_index = 0;
    };

    [[nodiscard]] bool matches(evdev const& dev, device_query const& query) noexcept;
    [[nodiscard]] bool matches(udev_device const& dev, device_query const& query) noexcept;
    [[nodiscard]] bool matches(udev_enumerate const& dev, device_query const& query) noexcept;
    [[nodiscard]] bool matches(udev_monitor const& dev, device_query const& query) noexcept;

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
              && (co_yield udev_device_pick{.device = std::move(dev), .query = snapshot(queries), .query_index = index}, true)),
             ...);
        }
    }


} // namespace fs8
