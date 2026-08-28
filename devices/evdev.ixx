// Created by moisrex on 6/18/24.

module;
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <libevdev/libevdev.h>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
export module fs8.devices.evdev;
export import fs8.event;
import fs8.devices.capabilities;
import fs8.utils;

namespace fs8 {

    export enum struct [[nodiscard]] evdev_status : std::uint8_t {
        unknown,
        success,
        success_grabbed,
        grab_failure,
        invalid_file_descriptor,
        invalid_device,
        failed_setting_file_descriptor,
        failed_to_open_file,
        failed_to_set_options,

        not_matched, // query doesn't match the device
    };

    export std::string_view to_string(evdev_status) noexcept;

    export constexpr bool is_valid(evdev_status const status) noexcept {
        return status == evdev_status::success || status == evdev_status::success_grabbed;
    }

    export enum struct [[nodiscard]] grab_state : std::uint8_t {
        grabbing,     // this FD currently has the grab
        not_grabbing, // this FD does NOT have the grab
        error         // ENOTTY / EPERM / EACCES / unexpected error (check errno)
    };

    export [[nodiscard]] bool is_grabbed(grab_state const state) noexcept {
        return state == grab_state::grabbing;
    }

    export constexpr std::string_view invalid_device_name       = "[UNKNOWN]";
    export constexpr std::string_view invalid_device_location   = "/dev/null";
    export constexpr std::string_view invalid_unique_identifier = "[NO-UNIQ-ID]";

    /**
     * This is a wrapper for libevdev's related features
     */
    export struct [[nodiscard]] evdev {
        using ev_type   = event_type::type_type;
        using code_type = event_type::code_type;

        explicit evdev(std::filesystem::path const& file) noexcept;

        explicit evdev(libevdev* ptr, evdev_status const inp_status) noexcept : dev{ptr}, status{inp_status} {}

        constexpr evdev() noexcept = default;

        // Copying must only happen at compile time (evdev owns a libevdev
        // handle; a runtime copy would double-free). The runtime branch aborts
        // instead of being `consteval`, so pimpl clones (which type-check a
        // runtime copy path) can still be instantiated.
        constexpr evdev(evdev const& other) noexcept : dev{other.dev}, status{other.status} {
            if !consteval {
                std::abort();
            }
        }

        evdev(evdev&& inp) noexcept;
        evdev& operator=(evdev&& other) noexcept;

        constexpr evdev& operator=(evdev const& other) noexcept {
            if !consteval {
                std::abort();
            }
            dev    = other.dev;
            status = other.status;
            return *this;
        }

        ~evdev() noexcept;

        static constexpr evdev invalid(evdev_status const inp_status = evdev_status::unknown) noexcept {
            return evdev{nullptr, inp_status};
        }

        // evdev(evdev const&)            = delete;
        // evdev& operator=(evdev const&) = delete;

        void close() noexcept;

        /// change the input event file (for example /dev/input/eventX)
        void set_file(std::filesystem::path const& file) noexcept;
        void set_file(int file) noexcept;

        [[nodiscard]] int       native_handle() const noexcept;
        [[nodiscard]] libevdev* device_ptr() const noexcept;

        /// Check if the device has initialized with a file descriptor
        [[nodiscard]] bool is_fd_initialized() const noexcept;

        /// check if everything is okay
        [[nodiscard]] bool is_ok() const noexcept {
            using enum evdev_status;
            return dev != nullptr && (status == success || status == success_grabbed);
        }

        evdev_status get_status() const noexcept {
            return status;
        }

        /// Grab or ungrab the device's output
        /// Grabbing means that we'd be the only one that can access the output of this device
        void grab_input(bool grab = true) noexcept;

        grab_state grab() const noexcept;


        /**
         * Retrieve the device's name, either as set by the caller or as read from
         * the kernel. The string returned is valid until libevdev_free() or until
         * libevdev_set_name(), whichever comes earlier.
         */
        [[nodiscard]] std::string_view device_name() const noexcept;
        void                           device_name(std::string_view) noexcept;


        /**
         * Retrieve the device's physical location, either as set by the caller or
         * as read from the kernel. The string returned is valid until
         * libevdev_free() or until libevdev_set_phys(), whichever comes earlier.
         *
         * Virtual devices such as uinput devices have no phys location.
         */
        [[nodiscard]] std::string_view physical_location() const noexcept;
        void                           physical_location(std::string_view) noexcept;

        [[nodiscard]] std::string_view unique_identifier() const noexcept;
        void                           unique_identifier(std::string_view) noexcept;

        void enable_event_type(ev_type) noexcept;
        void enable_event_code(ev_type, code_type) noexcept;
        void enable_event_code(ev_type, code_type, void const*) noexcept;
        void enable_caps(dev_caps_view) noexcept;

        template <typename... T>
            requires(std::convertible_to<T, code_type> && ...)
        void enable_event_codes(ev_type const type, T const... codes) noexcept {
            (enable_event_code(type, static_cast<code_type>(codes)), ...);
        }

        void disable_event_type(ev_type) noexcept;
        void disable_event_code(ev_type, code_type) noexcept;
        void disable_caps(dev_caps_view) noexcept;

        template <typename... T>
            requires(std::convertible_to<T, code_type> && ...)
        void disable_event_codes(ev_type const type, T const... codes) noexcept {
            (disable_event_code(type, static_cast<code_type>(codes)), ...);
        }

        /// Enable/Disable the caps for this device
        void apply_caps(dev_caps_view) noexcept;

        [[nodiscard]] bool has_event_type(ev_type) const noexcept;
        [[nodiscard]] bool has_event_code(ev_type, code_type) const noexcept;

        template <typename... T>
            requires((std::convertible_to<T, code_type> && ...) && sizeof...(T) >= 1)
        [[nodiscard]] bool has_event_codes(ev_type const type, T const... codes) const noexcept {
            return (has_event_code(type, static_cast<code_type>(codes)) && ...);
        }

        [[nodiscard]] bool has_cap(dev_cap_view const& inp_cap) const noexcept;

        /// returns a percentage of matches
        [[nodiscard]] std::uint8_t match_cap(dev_cap_view const& inp_cap) const noexcept;

        [[nodiscard]] bool has_caps(dev_caps_view inp_caps) const noexcept;

        /// returns a percentage of matches
        [[nodiscard]] std::uint8_t match_caps(dev_caps_view inp_caps) const noexcept;

        /// May return nullptr
        [[nodiscard]] input_absinfo const* abs_info(code_type code) const noexcept;
        [[nodiscard]] bool                 has_abs_info(code_type code = ABS_X) const noexcept;
        void                               abs_info(code_type abs_code, input_absinfo const& abs_info) noexcept;

        [[nodiscard]] bool operator==(evdev const& other) const noexcept;

        /**
         * Get a new input_event from the input device
         */
        [[nodiscard]] std::optional<input_event> next() noexcept;

        /// Write a single input_event back into the device (e.g. an EV_LED to
        /// reflect a mode toggle on the hardware device). Returns false on
        /// failure (including devices that don't accept writes).
        [[nodiscard]] bool send_event(input_event const& event) const noexcept;

        /// Write a single input_event constructed from (type, code, value).
        [[nodiscard]] bool send_event(event_type::type_type type, event_type::code_type code, event_type::value_type value) const noexcept;

      private:
        libevdev*    dev    = nullptr;
        evdev_status status = evdev_status::unknown;
    };

    /// Check if a freshly-opened device can be grabbed without disrupting a grab
    /// this process already holds. Always leaves the device ungrabbed afterwards.
    export [[nodiscard]] bool test_grab(evdev& dev) noexcept;

    /// Check if a device is usable as a source for a virtual device without
    /// disrupting a grab that this process already holds.
    export [[nodiscard]] bool is_usable(evdev& dev) noexcept;

    /// sysname of an open device (e.g. "event10"), derived from its fd.
    export [[nodiscard]] std::string device_sysname(evdev const& dev) noexcept;

    /// Make an independent deep copy of a device so it can be reshaped
    /// (name/uniq/bustype/caps) without mutating the caller's device, which
    /// may still be in use (e.g. an input_manager device that shares this
    /// libevdev handle).
    export [[nodiscard]] evdev clone_device(evdev const& src) noexcept;

    /// Number of bytes in the kernel key state bitmap for EVIOCGKEY.
    export inline constexpr std::size_t key_bitmap_bytes = (KEY_MAX + 7) / 8;

    /// Read the current key state bitmap from the kernel (EVIOCGKEY) into
    /// the provided span.  The span must have at least `key_bitmap_bytes`
    /// elements.  Returns true on success.
    export [[nodiscard]] bool query_key_state(evdev const& dev, std::span<unsigned char, key_bitmap_bytes> out) noexcept;

    /// Release all held keys on a device by sending EV_KEY release events.
    export void release_all_keys(evdev& dev) noexcept;

    /// Returns the highest valid code for a given event type (KEY_MAX for
    /// EV_KEY, REL_MAX for EV_REL, etc.). Unknown types return 0.
    export [[nodiscard]] constexpr unsigned event_type_max_code(unsigned const type) noexcept {
        switch (type) {
            case EV_KEY: return KEY_MAX;
            case EV_REL: return REL_MAX;
            case EV_ABS: return ABS_MAX;
            case EV_MSC: return MSC_MAX;
            case EV_SW: return SW_MAX;
            case EV_LED: return LED_MAX;
            case EV_SND: return SND_MAX;
            case EV_FF: return FF_MAX;
            default: return 0u;
        }
    }

    export constexpr struct [[nodiscard]] basic_only_ok : std::ranges::range_adaptor_closure<basic_only_ok> {
        template <typename R>
        [[nodiscard]] constexpr bool operator()(R const& obj) const noexcept {
            if constexpr (std::same_as<R, evdev>) {
                return obj.is_ok();
            } else {
                return true;
            }
        }

        template <std::ranges::range Range>
        [[nodiscard]] constexpr auto operator()(Range&& rng) const noexcept {
            return std::forward<Range>(rng) | std::views::filter(*this);
        }
    } only_ok;

    export constexpr struct [[nodiscard]] basic_to_evdev : std::ranges::range_adaptor_closure<basic_to_evdev> {
        // udev_device_pick
        template <typename T>
            requires requires(T pick) {
                pick.device.subsystem();
                pick.device.devnode();
            }
        [[nodiscard]] auto operator()(T const& pick) const noexcept {
            return evdev{pick.device.devnode()};
        }

        // udev_device
        template <typename T>
            requires requires(T device) {
                device.subsystem();
                device.devnode();
            }
        [[nodiscard]] auto operator()(T const& device) const noexcept {
            return evdev{device.devnode()};
        }

        template <std::ranges::range Range>
        [[nodiscard]] constexpr auto operator()(Range&& rng) const noexcept {
            return std::forward<Range>(rng) | std::views::transform(*this);
        }
    } to_evdev;

} // namespace fs8
