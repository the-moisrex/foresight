// Created by moisrex on 7/10/26.

module;
#include <array>
#include <concepts>
#include <coroutine>
#include <generator>
#include <ranges>
#include <span>
#include <string_view>
export module fs8.devices.classification;
export import fs8.devices.udev;

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
    [[nodiscard]] constexpr std::string_view subsystem(T const& cls) noexcept {
        if constexpr (requires {
                          { cls.subsystem } -> std::convertible_to<std::string_view>;
                      })
        {
            return cls.subsystem;
        } else if constexpr (requires {
                                 { T::subsystem } -> std::convertible_to<std::string_view>;
                             })
        {
            return T::subsystem;
        } else {
            static_assert(false, "T don't have properties.");
            return {};
        }
    }

    template <typename T>
    [[nodiscard]] constexpr properties_type properties(T const& cls) noexcept {
        if constexpr (requires {
                          { cls.properties } -> std::convertible_to<properties_type>;
                      })
        {
            return cls.properties;
        } else if constexpr (requires {
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

    [[nodiscard]] constexpr std::string to_string(Classification auto const& cls) {
        std::string str;
        str += subsystem(cls);
        for (auto const& [key, value] : properties(cls)) {
            str += " ";
            str += key;
            str += "=";
            str += value;
        }
        return str;
    }

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

    constexpr struct [[nodiscard]] basic_any_device {
    } any_device;

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

    [[nodiscard]] bool matches(udev_device const&, basic_any_device const&) noexcept {
        return true;
    }

    void match(udev_enumerate&, basic_any_device const&) noexcept {}

    void match(udev_monitor&, basic_any_device const&) noexcept {}

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

    template <Classification C>
    struct [[nodiscard]] filter_by : std::ranges::range_adaptor_closure<filter_by<C>> {
        C cls{};

        template <std::ranges::viewable_range R>
        [[nodiscard]] constexpr auto operator()(R&& r) const noexcept {
            return std::forward<R>(r) | std::views::filter([c = cls](udev_device const& d) noexcept {
                       return matches(d, c);
                   });
        }
    };

    // Pipe: range | Classification  →  same as range | filter_by{cls}
    template <std::ranges::viewable_range R, Classification C>
    [[nodiscard]] constexpr auto operator|(R&& r, C const& cls) noexcept {
        return std::forward<R>(r) | filter_by<C>{.cls = cls};
    }

    /**
     * Returns a lazy, owning range of all current udev devices.
     */
    [[nodiscard]] std::generator<udev_device> all_devices(Classification auto... cls) noexcept {
        udev_enumerate enumerator{};
        (match(enumerator, cls), ...);
        enumerator.scan_devices();

        for (auto const& entry : enumerator.list_entries()) {
            auto dev = udev_device{entry};
            if (!dev || (!matches(dev, cls) || ...)) [[unlikely]] {
                continue;
            }
            co_yield std::move(dev);
        }
    }

    // Workaround: coroutine_traits not visible when defining a variadic
    // template coroutine inside a module (even with <coroutine> in global fragment).
    // Non-templated wrapper ensures clean definition context.
    //
    // We shouldn't need this function, but making all_devices a template causes the Clang
    // to not be able to figure out early that we need to export coroutines or whatever.
    [[nodiscard]] decltype(auto) all_devices() noexcept {
        return all_devices(any_device);
    }

    /**
     * This is a snapshot in time of a classification
     */
    struct [[nodiscard]] classification_snapshot {
        std::string_view subsystem = "input";
        properties_type  properties{};
    };

    constexpr classification_snapshot snapshot(Classification auto const& cls) noexcept {
        return {.subsystem = subsystem(cls), .properties = properties(cls)};
    }

    [[nodiscard]] constexpr bool operator==(basic_keyboard const&, basic_keyboard const&) noexcept {
        return true;
    }

    [[nodiscard]] constexpr bool operator==(basic_mouse const&, basic_mouse const&) noexcept {
        return true;
    }

    [[nodiscard]] constexpr bool operator==(basic_any_device const&, basic_any_device const&) noexcept {
        return true;
    }

    [[nodiscard]] constexpr bool operator==(classification_snapshot const& lhs, classification_snapshot const& rhs) {
        return lhs.subsystem == rhs.subsystem && std::ranges::equal(lhs.properties, rhs.properties);
    }

    template <classify::Classification Cl>
    [[nodiscard]] constexpr bool operator==(Cl const& lhs, Cl const& rhs) noexcept {
        return subsystem(lhs) == subsystem(rhs) && std::ranges::equal(properties(lhs), properties(rhs));
    }

} // namespace fs8::classify
