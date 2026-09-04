// Created by moisrex on 6/29/24.

module;
#include <filesystem>
#include <libevdev/libevdev-uinput.h>
#include <ranges>
#include <string_view>
#include <system_error>
export module fs8.mods:uinput;
export import fs8.devices.evdev;
export import fs8.event;
import fs8.log;
import fs8.context;
import fs8.devices.capabilities;
import fs8.devices.queries;
import fs8.devices.udev;
import :input_manager;
import fs8.pimpl;
import fs8.traits;

export namespace fs8 {

    constexpr std::string_view invalid_syspath   = "/dev/null";
    constexpr std::string_view invalid_devnode   = "/dev/null";
    constexpr std::string_view empty_uinput_name = "Empty-Device";


    enum struct [[nodiscard]] uinput_access_result : std::uint8_t {
        available,

        device_not_found,       // /dev/uinput does not exist
        permission_denied,      // Exists, but current process cannot open it
        not_a_character_device, // Exists but is not a device node
        open_failed,            // Other open() failure
    };



    /**
     * Check and verify we have access to /dev/uinput in Linux and essentially know if the kernel module is loaded and we have access or
     * not.
     *
     * Checks whether /dev/uinput is present and can be opened for use.
     *
     * This verifies practical availability for the current process:
     * - /dev/uinput exists
     * - it is a character device
     * - it can be opened read/write
     *
     * A successful result generally means the uinput kernel module/driver is
     * available and the process has sufficient permissions.
     */
    [[nodiscard]] uinput_access_result verify_access_to_uinput() noexcept;

    [[nodiscard]] std::string_view to_string(uinput_access_result) noexcept;

    struct basic_uinput;

    /// Copy a matching device into a virtual (uinput) device, applying caps.
    /// If `best` is not valid, falls back to an empty device and applies caps.
    /// The source device is deep-cloned; it is never modified or freed.
    [[nodiscard]] bool finalize_device(basic_uinput& self, evdev const& best, dev_caps_view caps_view) noexcept;

    /**
     * A virtual device
     *
     * If uinput_fd is LIBEVDEV_UINPUT_OPEN_MANAGED, we will open /dev/uinput in read/write mode and manage
     * the file descriptor. Otherwise, uinput_fd must be opened by the caller and opened with the appropriate
     * permissions.
     */
    constexpr struct [[nodiscard]] basic_uinput : pimpl_idiom<basic_uinput> {
        using pimpl_idiom::pimpl_idiom;

        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

        basic_uinput(evdev const& evdev_dev, std::filesystem::path const& file) noexcept;
        basic_uinput(libevdev const* evdev_dev, std::filesystem::path const& file) noexcept;
        explicit basic_uinput(libevdev const* evdev_dev, int file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;
        explicit basic_uinput(evdev const& evdev_dev, int file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;
        consteval basic_uinput(basic_uinput const&)                     = default;
        constexpr basic_uinput(basic_uinput&&) noexcept                 = default;
        consteval basic_uinput& operator=(basic_uinput const&) noexcept = default;
        constexpr basic_uinput& operator=(basic_uinput&&) noexcept      = default;

        constexpr ~basic_uinput() noexcept {
            if !consteval {
                close();
            }
        }

        void close() noexcept;

        [[nodiscard]] std::error_code error() const noexcept;
        [[nodiscard]] bool            is_ok() const noexcept;

        [[nodiscard]] explicit operator bool() const noexcept {
            return is_ok();
        }

        /**
         * Configure the virtual device
         * @param evdev_dev libevdev device to get the device info from
         * @param file_descriptor file descriptor of the output virtual device
         */
        void set_device(libevdev const* evdev_dev, int file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;

        void set_device(evdev const& inp_dev, int file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;
        void set_device(int file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED, std::string_view name = empty_uinput_name) noexcept;


        /**
         * Return the file descriptor used to create this uinput device. This is the
         * fd pointing to /dev/uinput. This file descriptor may be used to write
         * events that are emitted by the uinput device.
         * Closing this file descriptor will destroy the uinput device, you should
         * call libevdev_uinput_destroy() first to free allocated resources.
         *
         * @return The file descriptor used to create this device
         */
        [[nodiscard]] int native_handle() const noexcept;

        /**
         * Return the syspath representing this uinput device. If the UI_GET_SYSNAME
         * ioctl is not available, libevdev makes an educated guess.
         * The UI_GET_SYSNAME ioctl is available since Linux 3.15.
         *
         * The syspath returned is the one of the input node itself
         * (e.g. /sys/devices/virtual/input/input123), not the syspath of the device
         * node returned with libevdev_uinput_get_devnode().
         *
         * @note This function may return empty string if UI_GET_SYSNAME is not available.
         * In that case, libevdev uses ctime and the device name to guess devices.
         * To avoid false positives, wait at least 1.5s between creating devices that
         * have the same name.
         *
         * @note FreeBSD does not have sysfs, on FreeBSD this function always returns
         * empty string.
         *
         * @return The syspath for this device, including the preceding /sys
         *
         * @see devnode()
         */
        [[nodiscard]] std::string_view syspath() const noexcept;

        /**
         * Return the device node representing this uinput device.
         *
         * @note This function may return empty string. libevdev may have to guess the
         * syspath and the device node.
         *
         * @note On FreeBSD, this function can not return empty string. libudev uses the
         * UI_GET_SYSNAME ioctl to get the device node on this platform and if that
         * fails, the call to libevdev_uinput_create_from_device() fails.
         *
         * @return The device node for this device, in the form of /dev/input/eventN
         */
        [[nodiscard]] std::string_view devnode() const noexcept;

        /// The devnode reported to the `input_manager` so it can skip watching
        /// this device. Empty when the device is not created yet, or when it is
        /// explicitly marked as foreign (`self_created == false`).
        [[nodiscard]] std::string_view self_devnode() const noexcept {
            if (!is_ok() || !self_created_) [[unlikely]] {
                return {};
            }
            return devnode();
        }

        void enable_event_type(ev_type) noexcept;
        void enable_event_code(ev_type, code_type) noexcept;
        void enable_caps(dev_caps_view) noexcept;

        void set_abs(code_type code, input_absinfo const& abs_info) noexcept;

        template <typename... T>
            requires(std::convertible_to<T, code_type> && ...)
        void enable_event_codes(ev_type const type, T const... codes) noexcept {
            (enable_event_code(type, static_cast<code_type>(codes)), ...);
        }

        /// Enable/Disable the caps for this device
        void apply_caps(dev_caps_view) noexcept;

        bool emit(event_type const& event) noexcept;

        bool init(dev_caps_view caps_view) noexcept;
        bool set_device_from(dev_caps_view caps_view) noexcept;

        bool init(device_query const& inp_query) noexcept;
        bool set_device_from(device_query const& inp_query) noexcept;

        /// Set the caps on start
        bool operator()(dev_caps_view caps_view, special_event const& tag) noexcept;

        /// Set the device on start
        bool operator()([[maybe_unused]] Context auto&, dev_caps_view const caps_view, [[maybe_unused]] special_event const& tag) noexcept {
            return operator()(caps_view, start);
        }

        /// Set the device based on the query on start
        bool operator()(device_query const& inp_query, special_event const& tag) noexcept;

        /// Set the device based on the query on start, preferring the devices
        /// known to the input_manager when it's available in the pipeline.
        template <typename CtxT>
            requires requires(CtxT& ctx) { ctx.mod(fs8::input_manager).devices(); }
        bool operator()(CtxT& ctx, device_query const& inp_query, special_event const& tag) noexcept {
            if (tag.code != start.code) {
                return true;
            }
            if (is_ok()) {
                log("uinput: already initialized");
                return true;
            }
            // Prefer the devices the input_manager already knows about (they're
            // already open and matched against queries); fall back to a fresh
            // udev enumeration otherwise.
            for (auto& cur_dev : ctx.mod(fs8::input_manager).devices()) {
                if (!fs8::matches(cur_dev, inp_query) || !fs8::is_usable(cur_dev)) {
                    continue;
                }
                log("uinput: matched device '{}', finalizing...", cur_dev.device_name());
                auto const ok = fs8::finalize_device(*this, cur_dev, inp_query.caps);
                if (ok) {
                    ctx.mod(fs8::input_manager).own_device(devnode());
                }
                return ok;
            }
            log("uinput: no matching device in input_manager, trying set_device_from");
            if (set_device_from(inp_query)) {
                ctx.mod(fs8::input_manager).own_device(devnode());
                return true;
            }
            log("uinput: set_device_from failed");
            return false;
        }

        /// Find the device if possible on start
        /// The first device in the input_manager, we automatically find it, and use that one
        template <std::ranges::range R>
            requires std::convertible_to<std::ranges::range_value_t<R>, evdev>
        bool operator()(R&& devs, special_event const& tag) noexcept {
            if (tag.code != start.code) {
                return true;
            }
            if (is_ok()) {
                return true;
            }
            for (auto const& cur_dev : devs) {
                // Don't intercept the one that's being grabbed.
                // if (cur_dev.grab() == grab_state::grabbing_by_others) {
                //     continue;
                // }

                set_device(cur_dev);
                if (!is_ok()) [[unlikely]] {
                    log("  Failed to set device: {}", cur_dev.device_name());
                }
                break;
            }
            return is_ok();
        }

        /// Find the device if possible on start
        /// The first device in the input_manager, we automatically find it, and use that one
        template <ContextWith<basic_input_manager> CtxT>
        context_action operator()(CtxT& ctx, special_event const& tag) noexcept {
            using enum context_action;
            if (tag.code != start.code) {
                return drop_event;
            }
            if (is_ok()) {
                return next;
            }
            if (auto const res = verify_access_to_uinput(); res != uinput_access_result::available) [[unlikely]] {
                log("Uinput init error: {}", to_string(res));
                return recovery;
            }
            if (operator()(ctx.mod(input_manager).devices(), start)) {
                ctx.mod(input_manager).own_device(devnode());
                return next;
            }
            return recovery;
        }

        context_action operator()(event_type const& event) noexcept;

        /// Whether this device was created by the current pipeline (as opposed
        /// to a foreign/chained process). Only self-created devices are skipped
        /// by `input_manager` to avoid feedback loops.
        [[nodiscard]] constexpr bool is_self_created() const noexcept {
            return self_created_;
        }

        constexpr void set_self_created(bool const value) noexcept {
            self_created_ = value;
        }

        friend bool finalize_device(basic_uinput& self, evdev const& best, dev_caps_view caps_view) noexcept;

      private:
        bool self_created_ = true;
    } uinput;

    static_assert(OutputModifier<basic_uinput>, "Must be an output modifier.");
} // namespace fs8
