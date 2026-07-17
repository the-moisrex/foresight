// Created by moisrex on 7/17/26.

module;
#include <string_view>
export module foresight.devices.device_query;
export import foresight.devices.classification;

export namespace fs8 {


    template <classify::Classification Cl>
    struct [[nodiscard]] device_query {
        /// The classification
        Cl classification{};

        // Rank device names in a fuzzy manner based on this query for device name
        std::string name;

        // If we should grab the device's events and not give it to anyone else
        bool grab = false;

        /// Multiple Matches are allowed or not?
        /// Default: false
        bool allow_multiple_matches = false;

        /// Should we fail if we find no device match for the query?
        /// Default: false
        bool fail_on_no_match = false;
    };

    struct query_tag {};

    constexpr struct grab_tag : query_tag {
        template <classify::Classification Cl>
        constexpr void operator()(device_query<Cl>& query) noexcept {
            query.grab = true;
        }
    } grab;

    constexpr struct allow_multiple_matches_tag : query_tag {
        template <classify::Classification Cl>
        constexpr void operator()(device_query<Cl>& query) noexcept {
            query.allow_multiple_matches = true;
        }
    } allow_multiple_matches;

    constexpr struct fail_on_no_match_tag : query_tag {
        template <classify::Classification Cl>
        constexpr void operator()(device_query<Cl>& query) noexcept {
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

        constexpr name_tag operator()(std::string_view inp_query) const noexcept {
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


} // namespace fs8
