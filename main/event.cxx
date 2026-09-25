// Created by moisrex on 8/17/26.

module;
#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>
module fs8.event;

[[nodiscard]] std::string_view fs8::to_source_string(std::uint32_t const source_id) noexcept {
    if (source_id == source_id_none) {
        return {"none"};
    }

    static thread_local std::array<char, 32> buf{};
    auto const                               first = buf.data();

    // Format: "mod:XXXX,idx:XXXX"
    auto const mid = std::copy(std::begin("mod:"), std::end("mod:") - 1, first);
    auto [ptr, ec] = std::to_chars(mid, buf.data() + buf.size() - 1, sid(source_id), 16);
    if (ec != std::errc{}) [[unlikely]] {
        return {"<unknown>"};
    }
    *ptr++               = ',';
    ptr                  = std::copy(std::begin("idx:"), std::end("idx:") - 1, ptr);
    auto const remaining = static_cast<std::size_t>(buf.data() + buf.size() - ptr);
    if (remaining < 6) [[unlikely]] {
        return {"<unknown>"};
    }
    auto const [ptr2, ec2] = std::to_chars(ptr, buf.data() + buf.size() - 1, source_index(source_id), 16);
    if (ec2 != std::errc{}) [[unlikely]] {
        return {"<unknown>"};
    }
    return std::string_view{first, static_cast<std::size_t>(ptr2 - first)};
}

[[nodiscard]] std::string_view fs8::to_string(control_event const& event) noexcept {
    switch (event.type) {
        case general_control_event:
        case required_control_event:
            break;
        [[unlikely]] default:
            return {"Unknown-Control-Event"};
    }
    switch (event.code) {
        case null_event.code: return {"Null"};
        case start.code: return {"Start"};
        case no_init.code: return {"No-Init"};
        case load_event.code: return {"Load"};
        case next_event.code: return {"Next"};
        case toggle_on.code: return toggle_off.value == event.value ? std::string_view{"Toggle-Off"} : std::string_view{"Toggle-On"};
        case idle.code: return {"Idle"};
        case monitors_updated.code: return {"Monitors-Updated"};
        case we_own_device.code: return {"We-Own-Device"};
        case register_query_provider.code: return {"Register-Query-Provider"};
        case add_evdev_device.code: return {"Add-Manual-Device"};
        case source_registered.code: return {"Source-Registered"};
        case source_unregistered.code: return {"Source-Unregistered"};
        case enumerate_devices.code: return {"Enumerate-Devices"};
        case devices_changed.code:
            switch (event.value) {
                case device_connected.value: return {"Device-Connected"};
                case device_disconnected.value: return {"Device-Disconnected"};
                default: return {"Devices-Changed"};
            }
        default: return {"Unknown-Control-Code"};
    }
}
