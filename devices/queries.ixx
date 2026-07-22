// Created by moisrex on 7/17/26.

module;
#include <cassert>
#include <generator>
#include <string_view>
export module fs8.devices.queries;
export import fs8.devices.classification;
export import fs8.mods.caps;

export namespace fs8 {


    template <classify::Classification Cl>
    struct [[nodiscard]] device_query {
        /// The classification
        Cl classification{};

        /// Rank device names in a fuzzy manner based on this query for device name
        std::string name{};

        /// How much fuzzy search should match the specified "name"?
        /// 100% means exactly
        /// anything less means fuzzy search match
        std::uint8_t name_match_percentage = 100; // NOLINT(*-magic-numbers)

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

        /// The percentage of the support of the caps
        std::uint8_t caps_support_percentage = 100; // NOLINT(*-magic-numbers)
    };

    template <classify::Classification Cl>
    [[nodiscard]] constexpr bool operator==(device_query<Cl> const& lhs, device_query<Cl> const& rhs) noexcept {
        // Note: Assuming `classification` is stateless or has its own operator==.
        // If Cl doesn't have operator==, omit `lhs.classification == rhs.classification &&`
        return (lhs.classification == rhs.classification)
               && (lhs.name == rhs.name)
               && (lhs.name_match_percentage == rhs.name_match_percentage)
               && (lhs.grab == rhs.grab)
               && (lhs.matches_limit == rhs.matches_limit)
               && (lhs.fail_on_no_match == rhs.fail_on_no_match)
               && (lhs.caps == rhs.caps)
               && (lhs.caps_support_percentage == rhs.caps_support_percentage);
    }

    struct query_tag {};

    constexpr struct grab_tag : query_tag {
        template <classify::Classification Cl>
        constexpr void operator()(device_query<Cl>& query) const noexcept {
            query.grab = true;
        }
    } grab;

    constexpr struct allow_multiple_matches_tag : query_tag {
        template <classify::Classification Cl>
        constexpr void operator()(device_query<Cl>& query) const noexcept {
            query.matches_limit = std::numeric_limits<std::uint8_t>::max();
        }
    } allow_multiple_matches;

    constexpr struct [[nodiscard]] matches_limit : query_tag {
        std::uint8_t limit = 1;

        template <classify::Classification Cl>
        constexpr void operator()(device_query<Cl>& query) const noexcept {
            query.matches_limit = limit;
        }

        consteval matches_limit operator()(std::uint8_t const inp_limit) const noexcept {
            return matches_limit{.limit = inp_limit};
        }
    } matches_limit;

    constexpr struct [[nodiscard]] matches_percentage : query_tag {
        std::uint8_t percentage = 100;

        template <classify::Classification Cl>
        constexpr void operator()(device_query<Cl>& query) const noexcept {
            query.caps_support_percentage = percentage;
        }

        consteval matches_percentage operator()(std::uint8_t const percentage) const noexcept {
            assert(percentage <= 100);
            return matches_percentage{.percentage = percentage};
        }
    } matches_percentage;

    constexpr struct fail_on_no_match_tag : query_tag {
        template <classify::Classification Cl>
        constexpr void operator()(device_query<Cl>& query) const noexcept {
            query.fail_on_no_match = true;
        }
    } fail_on_no_match;

    constexpr struct [[nodiscard]] name_tag : query_tag {
      private:
        std::string name_query;

      public:
        constexpr name_tag()                                   = default;
        constexpr name_tag(name_tag const& tag)                = default;
        constexpr name_tag(name_tag&& tag) noexcept            = default;
        constexpr name_tag& operator=(name_tag const& tag)     = default;
        constexpr name_tag& operator=(name_tag&& tag) noexcept = default;
        constexpr ~name_tag()                                  = default;

        constexpr name_tag operator()(std::string_view const inp_query) const noexcept {
            name_tag tag;
            tag.name_query = inp_query;
            return tag;
        }

        template <classify::Classification Cl>
        constexpr void operator()(device_query<Cl>& query) noexcept {
            query.name = std::move(name_query);
        }
    } name;

    template <typename T>
    concept QueryTag = std::is_base_of_v<query_tag, T>;

    // Pipe: Classification | option  →  device_query
    template <classify::Classification C, QueryTag Tag>
    [[nodiscard]] constexpr auto operator|(C const& cls, Tag tag) noexcept {
        device_query<C> query{.classification = cls};
        tag(query);
        return query;
    }

    // Pipe: device_query | option  →  device_query
    template <classify::Classification C, QueryTag Tag>
    [[nodiscard]] constexpr device_query<C> operator|(device_query<C>&& query, Tag tag) noexcept {
        tag(query);
        return std::move(query);
    }

    template <classify::Classification C, std::size_t N>
    [[nodiscard]] constexpr device_query<C> operator|(device_query<C>&& query, dev_caps<N> const& inp_cap) noexcept {
        query.caps = view(inp_cap);
        return std::move(query);
    }

    template <classify::Classification C, std::size_t N>
    [[nodiscard]] constexpr device_query<C> operator|(device_query<C>&& query, dev_caps_view const& inp_cap) noexcept {
        query.caps = inp_cap;
        return std::move(query);
    }

    using device_query_snapshot = device_query<classify::classification_snapshot>;

    template <classify::Classification Cl>
    [[nodiscard]] constexpr device_query_snapshot snapshot(device_query<Cl> const& query) noexcept {
        return device_query_snapshot{
          .classification          = snapshot(query.classification),
          .name                    = query.name,
          .grab                    = query.grab,
          .matches_limit           = query.matches_limit,
          .fail_on_no_match        = query.fail_on_no_match,
          .caps                    = query.caps,
          .caps_support_percentage = query.caps_support_percentage};
    }

    [[nodiscard]] std::string to_string(device_query_snapshot const& query);

    struct [[nodiscard]] udev_device_pick {
        udev_device device{};

        // User query to re-get this device
        device_query_snapshot query{};

        // The index of the query
        std::uint8_t query_index = 0;
    };

    template <classify::Classification Cl>
    [[nodiscard]] bool matches(udev_device const& dev, device_query<Cl> const& query) noexcept {
        bool result = matches(dev, query.classification);
        // todo
        return result;
    }

    template <classify::Classification Cl>
    [[nodiscard]] bool matches(udev_enumerate const& dev, device_query<Cl> const& query) noexcept {
        return matches(dev, query.classification);
    }

    template <classify::Classification Cl>
    [[nodiscard]] bool matches(udev_monitor const& dev, device_query<Cl> const& query) noexcept {
        return matches(dev, query.classification);
    }


    template <classify::Classification... Cl>
    [[nodiscard]] std::generator<udev_device_pick> all_devices(device_query<Cl> const&... queries) noexcept {
        static_assert(sizeof...(Cl) < std::numeric_limits<std::uint8_t>::max(), "Too many queries.");

        udev_enumerate enumerator{};
        (match(enumerator, queries.classification), ...);
        enumerator.scan_devices();

        std::array<std::uint8_t, sizeof...(Cl)> limits{queries.matches_limit...};

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
