// Created by moisrex on 7/10/26.
module;
#include <cstdint>
#include <optional>
#include <vector>
export module foresight.mods.device_registry;
import foresight.devices.udev;
import foresight.mods.context;
import foresight.devices.evdev;
import foresight.devices.device_query;

namespace fs8 {

    static constexpr std::uint8_t no_classification = std::numeric_limits<std::uint8_t>::max();

    export struct [[nodiscard]] query_info {
        bool grabbed = false;
    };

    struct [[nodiscard]] device_pick {
        evdev device;

        /// Classification Index
        std::uint8_t class_index = no_classification;
    };

    /**
     * Monitor and manage input devices.
     */
    export template <classify::Classification... C>
    struct [[nodiscard]] basic_device_registry {
        constexpr basic_device_registry() noexcept                                        = default;
        consteval basic_device_registry(basic_device_registry const&)                     = default;
        constexpr basic_device_registry(basic_device_registry&&) noexcept                 = default;
        consteval basic_device_registry& operator=(basic_device_registry const&) noexcept = default;
        constexpr basic_device_registry& operator=(basic_device_registry&&) noexcept      = default;
        constexpr ~basic_device_registry() noexcept                                       = default;

        /// Add device manually
        void add(evdev&& inp_dev, std::uint8_t class_index = no_classification) {
            devices.emplace_back(std::move(inp_dev), class_index);
        }

        void operator()(start_tag) noexcept {
            monitor = std::make_optional<udev_monitor>();
        }

      private:
        std::optional<udev_monitor> monitor = std::nullopt;
        std::vector<device_pick>    devices;
    };

    export constexpr basic_device_registry device_registry;

} // namespace fs8
