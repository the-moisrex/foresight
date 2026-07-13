// Created by moisrex on 7/10/26.

module;
#include <array>
#include <concepts>
#include <ranges>
#include <span>
#include <string_view>
export module foresight.devices.classification;
export import foresight.devices.udev;

namespace fs8::classify {

    using properties_pair_type   = std::pair<std::string_view, std::string_view>;
    export using properties_type = std::span<properties_pair_type const>;

    export template <std::size_t N>
    using properties_storage = std::array<properties_pair_type const, N>;

    template <typename T>
    constexpr properties_storage<1> default_properties_k{{{T::property_key, "1"}}};

    template <typename T>
    constexpr properties_storage<1> default_properties_kv{{{T::property_key, T::property_value}}};
} // namespace fs8::classify

export namespace fs8::classify {


    template <typename T>
    [[nodiscard]] constexpr std::string_view subsystem(T const&) noexcept {
        if constexpr (requires { T::subsystem; }) {
            return T::subsystem;
        } else {
            static_assert(false, "T don't have properties.");
            return {};
        }
    }

    template <typename T>
    [[nodiscard]] constexpr properties_type properties(T const&) noexcept {
        if constexpr (requires {
                          { T::properties } -> std::convertible_to<properties_type>;
                      })
        {
            return T::properties;
        } else if constexpr (requires {
                                 { T::property_key } -> std::convertible_to<std::string_view>;
                                 { T::property_value } -> std::convertible_to<std::string_view>;
                             })
        {
            return {default_properties_kv<T>};
        } else if constexpr (requires {
                                 { T::property_key } -> std::convertible_to<std::string_view>;
                             })
        {
            return {default_properties_k<T>};
        } else {
            static_assert(false, "T don't have properties.");
            return {};
        }
    }

    /**
     * A classification is a class of devices and their properties used to find and classify devices.
     * They can also be considered as "filters" or "queries".
     */
    template <typename T>
    concept Classification = requires(T cls) {
        { subsystem(cls) } noexcept -> std::convertible_to<std::string_view>;
        { properties(cls) } noexcept -> std::convertible_to<properties_type>;
    };

    // 1. Check if an existing device belongs to this classification
    [[nodiscard]] bool matches(udev_device const& dev, Classification auto const& cls) noexcept {
        bool res = dev.subsystem() == subsystem(cls);
        for (auto const& [key, value] : properties(cls)) {
            res &= dev.property(key.data()) == value;
        }
        return res;
    }

    // 2. Apply rules to find these devices
    void match(udev_enumerate& e, Classification auto const& cls) noexcept {
        e.match_subsystem(subsystem(cls).data());
        for (auto const& [key, value] : properties(cls)) {
            e.match_property(key.data(), value.data());
        }
    }

    // 3. Apply rules to monitor these devices
    // Note: udev_monitor cannot filter by property directly, only by subsystem/devtype/tag.
    // We filter by subsystem here, and use `matches()` on the received event later.
    void match(udev_monitor& m, Classification auto const& cls) noexcept {
        m.match_device(subsystem(cls).data());
    }

    constexpr struct [[nodiscard]] basic_keyboard {
        static constexpr std::string_view subsystem    = "input";
        static constexpr std::string_view property_key = "ID_INPUT_KEYBOARD";
    } keyboard;

    constexpr struct [[nodiscard]] basic_mouse {
        static constexpr std::string_view subsystem    = "input";
        static constexpr std::string_view property_key = "ID_INPUT_MOUSE";
    } mouse;

    constexpr struct [[nodiscard]] basic_drawing_tablet {
        static constexpr std::string_view subsystem    = "input";
        static constexpr std::string_view property_key = "ID_INPUT_TABLET";
    } drawing_tablet;

    /// A generic property matcher
    struct [[nodiscard]] property_matcher {
        static constexpr std::string_view subsystem{"input"};

        std::string_view key;
        std::string_view value;
    };

    [[nodiscard]] bool matches(udev_device const& dev, property_matcher const& matcher) noexcept {
        // todo: check if it's null terminated.
        return dev.property(matcher.key.data()) == matcher.value;
    }

    void match(udev_enumerate& e, property_matcher const& matcher) noexcept {
        // todo: check if it's null terminated.
        e.match_property(matcher.key.data(), matcher.value.data());
    }

    void match(udev_monitor&, property_matcher const&) noexcept {
        // Monitors can't filter by property pre-event, handled post-event via matches()
    }

    /// Connected via USB
    constexpr property_matcher via_usb{.key = "ID_BUS", .value = "usb"};

    consteval property_matcher with_name(std::string_view const name) noexcept {
        return {.key = "NAME", .value = name}; // Or ID_MODEL depending on udev specifics
    }

    // Returns a lazy range of ALL devices in the system
    [[nodiscard]] auto all_devices() {
        udev_enumerate enumerator;
        enumerator.scan_devices();

        // Transform the udev_list_entry into a udev_device lazily
        return enumerator.list_entries() | std::views::transform([](auto entry) {
                   return udev_device(&*udev::instance().native(), entry.name().data());
               });
    }

} // namespace fs8::classify
