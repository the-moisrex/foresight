// Created by moisrex on 6/18/24.

module;
#include <concepts>
#include <filesystem>
#include <libevdev/libevdev.h>
#include <optional>
#include <ranges>
#include <string_view>
#include <utility>
export module foresight.devices.evdev;
export import foresight.mods.event;
import foresight.mods.caps;

namespace foresight {

    export enum struct evdev_status : std::uint8_t {
        unknown,
        success,
        success_grabbed,
        grab_failure,
        invalid_file_descriptor,
        invalid_device,
        failed_setting_file_descriptor,
        failed_to_open_file,
        failed_to_set_options,
    };

    export std::string_view to_string(evdev_status) noexcept;

    export enum struct [[nodiscard]] grab_state : std::uint8_t {
        grabbing,           // this FD currently has the grab
        grabbing_by_others, // this FD currently has the grab by some other process
        not_grabbing,       // this FD does NOT have the grab
        error               // ENOTTY / EPERM / EACCES / unexpected error (check errno)
    };

    export [[nodiscard]] bool is_grabbed(grab_state const state) noexcept {
        return state == grab_state::grabbing || state == grab_state::grabbing_by_others;
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
        consteval evdev()                      = default;
        consteval evdev(evdev const&) noexcept = default;
        evdev(evdev&& inp) noexcept;
        evdev&           operator=(evdev&& other) noexcept;
        consteval evdev& operator=(evdev const&) noexcept = default;
        ~evdev() noexcept;

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
        [[nodiscard]] bool ok() const noexcept {
            using enum evdev_status;
            return dev != nullptr && (status == success || status == success_grabbed);
        }

        [[nodiscard]] evdev_status get_status() const noexcept {
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
        void abs_info(code_type abs_code, input_absinfo const& abs_info) noexcept;

        /**
         * Get a new input_event from the input device
         */
        [[nodiscard]] std::optional<input_event> next() noexcept;

      private:
        libevdev*    dev    = nullptr;
        evdev_status status = evdev_status::unknown;
    };

    /**
     * @brief Factory function to create a view over all valid input devices.
     *
     * @param dir The path to the input device directory. Defaults to "/dev/input".
     * @return A lazy view object that can be used in a range-based for loop.
     */
    export [[nodiscard]] auto all_input_devices(std::filesystem::path const& dir = "/dev/input") {
        using std::filesystem::directory_entry;
        using std::filesystem::directory_iterator;
        using std::ranges::views::filter;
        using std::ranges::views::transform;

        // Create a view over the directory entries. This may throw if the path is invalid.
        // We wrap it to return an empty view on error, making it safer for the caller.
        auto dir_view = is_directory(dir) ? directory_iterator{dir} : directory_iterator{};

        // Define the pipeline of operations:
        return dir_view
               // 1. Filter the directory entries to find potential device files.
               | filter([](directory_entry const& entry) {
                     std::string const file = entry.path().filename().string();
                     return entry.is_character_file() && file.starts_with("event");
                 })
               // 2. Transform the valid directory entries into evdev objects.
               | transform([](directory_entry const& entry) {
                     return evdev{entry.path()};
                 })
               // 3. Filter again to discard any evdev objects that failed initialization.
               //    (e.g., due to permissions issues when calling `open`).
               | filter([](evdev const& dev) noexcept {
                     return dev.ok();
                 });
    }

    export constexpr struct [[nodiscard]]
    basic_grab_inputs : std::ranges::range_adaptor_closure<basic_grab_inputs> {
        void operator()(evdev& dev, bool const grab = true) const noexcept {
            dev.grab_input(grab);
        }

        evdev&& operator()(evdev&& dev) const noexcept {
            dev.grab_input();
            return std::move(dev);
        }

        template <std::ranges::range R>
        constexpr auto operator()(R&& rng) const noexcept {
            return std::forward<R>(rng) | std::views::transform(*this);
        }
    } grab_inputs;

    export struct [[nodiscard]] evdev_rank {
        std::uint8_t score = 0; // in percentage
        evdev        dev;
    };

    export [[nodiscard]] constexpr auto match_devices(dev_caps_view const       inp_caps,
                                                      std::ranges::range auto&& devs) {
        using std::ranges::views::transform;
        return devs | transform([=](evdev&& dev) {
                   auto const percentage = dev.match_caps(inp_caps);
                   return evdev_rank{.score = percentage, .dev = std::move(dev)};
               });
    }

    /// Get the list of devices and their ranks
    export [[nodiscard]] auto rank_devices(dev_caps_view const inp_caps) {
        return match_devices(inp_caps, all_input_devices());
    }

    /// Get the first device and its rank
    export [[nodiscard]] evdev_rank device(dev_caps_view inp_caps);


    /// Get the first device based on the query
    /// Example: /dev/input/event10
    /// Example: keyboard
    /// Example: tablet
    export [[nodiscard]] evdev_rank device(std::string_view);

    export constexpr struct [[nodiscard]]
    basic_only_matching : std::ranges::range_adaptor_closure<basic_only_matching> {
      private:
        std::uint8_t percentage = 40;

      public:
        constexpr explicit basic_only_matching(std::uint8_t const inp_percentage) noexcept
          : percentage(inp_percentage) {}

        basic_only_matching()                                      = default;
        basic_only_matching(basic_only_matching const&)            = default;
        basic_only_matching(basic_only_matching&&)                 = default;
        basic_only_matching& operator=(basic_only_matching const&) = default;
        basic_only_matching& operator=(basic_only_matching&&)      = default;
        ~basic_only_matching()                                     = default;

        [[nodiscard]] constexpr auto operator()(evdev_rank const& ranker) const noexcept {
            return ranker.score >= percentage;
        }

        template <std::ranges::range Range>
        [[nodiscard]] constexpr auto operator()(Range&& rng) const noexcept {
            return std::forward<Range>(rng) | std::views::filter(*this);
        }

        constexpr basic_only_matching operator()(std::uint8_t const inp_percentage) const noexcept {
            return basic_only_matching{inp_percentage};
        }
    } only_matching;

    export constexpr struct [[nodiscard]] basic_only_ok : std::ranges::range_adaptor_closure<basic_only_ok> {
        template <typename R>
        [[nodiscard]] constexpr auto operator()(R const& obj) const noexcept {
            if constexpr (std::same_as<R, evdev_rank>) {
                return obj.dev.ok();
            } else if constexpr (std::same_as<R, evdev>) {
                return obj.ok();
            } else {
                return true;
            }
        }

        template <std::ranges::range Range>
        [[nodiscard]] constexpr auto operator()(Range&& rng) const noexcept {
            return std::forward<Range>(rng) | std::views::filter(*this);
        }
    } only_ok;

    export constexpr struct [[nodiscard]]
    basic_find_devices : std::ranges::range_adaptor_closure<basic_find_devices> {
        template <std::ranges::sized_range Range>
            requires(std::same_as<std::ranges::range_value_t<Range>, std::string_view>)
        constexpr auto operator()(Range&& rng) const noexcept {
            return std::forward<Range>(rng)
                   // exclude bad inputs
                   | std::views::filter([](std::string_view const query) {
                         return !query.empty();
                     })
                   // Get a device
                   | std::views::transform([](std::string_view const query) {
                         return device(query).dev;
                     });
        }

        [[nodiscard]] constexpr auto operator()(std::string_view const query) const noexcept {
            return std::span<std::string_view const>{query} | *this;
        }

    } find_devices;

    export constexpr struct [[nodiscard]]
    basic_to_evdev : std::ranges::range_adaptor_closure<basic_to_evdev> {
        [[nodiscard]] constexpr auto operator()(evdev_rank&& ranker) const noexcept {
            return std::move(std::move(ranker).dev);
        }

        template <std::ranges::range Range>
        [[nodiscard]] constexpr auto operator()(Range&& rng) const noexcept {
            return std::forward<Range>(rng) | std::views::transform(*this);
        }
    } to_evdev;

} // namespace foresight
