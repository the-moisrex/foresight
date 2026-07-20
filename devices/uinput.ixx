// Created by moisrex on 6/29/24.

module;
#include <filesystem>
#include <libevdev/libevdev-uinput.h>
#include <string_view>
#include <system_error>
export module fs8.devices.uinput;
export import fs8.devices.evdev;
export import fs8.event;
import fs8.log;
import fs8.context;
import fs8.mods.caps;
import fs8.mods.intercept;

export namespace fs8 {

    constexpr std::string_view invalid_syspath   = "/dev/null";
    constexpr std::string_view invalid_devnode   = "/dev/null";
    constexpr std::string_view empty_uinput_name = "Empty-Device";


    enum struct [[nodiscard]] uinput_access_result {
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
    uinput_access_result verify_access_to_uinput() noexcept;

    [[nodiscard]] std::string_view to_string(uinput_access_result) noexcept;

    /**
     * A virtual device
     *
     * If uinput_fd is LIBEVDEV_UINPUT_OPEN_MANAGED, we will open /dev/uinput in read/write mode and manage
     * the file descriptor. Otherwise, uinput_fd must be opened by the caller and opened with the appropriate
     * permissions.
     */
    constexpr struct [[nodiscard]] basic_uinput {
        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

        constexpr basic_uinput() noexcept = default;
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

        bool emit(ev_type type, code_type code, value_type value) noexcept;
        bool emit(input_event const& event) noexcept;
        bool emit(event_type const& event) noexcept;
        bool emit_syn() noexcept;

        bool init(dev_caps_view caps_view) noexcept;
        bool set_device_from(dev_caps_view caps_view) noexcept;

        /// Set the start on start
        bool operator()(dev_caps_view caps_view, start_tag) noexcept;

        /// Set the device on start
        bool operator()([[maybe_unused]] Context auto&, dev_caps_view const caps_view, start_tag) noexcept {
            return operator()(caps_view, start);
        }

        /// Find the device if possible on start
        /// The first device in the interceptor, we automatically find it, and use that one
        bool operator()(std::span<evdev const> devs, start_tag) noexcept;

        /// Find the device if possible on start
        /// The first device in the interceptor, we automatically find it, and use that one
        template <ContextWith<basic_interceptor> CtxT>
        context_action operator()(CtxT& ctx, start_tag) noexcept {
            using enum context_action;
            if (is_ok()) {
                return next;
            }
            if (auto const res = verify_access_to_uinput(); res != uinput_access_result::available) [[unlikely]] {
                log("Uinput init error: {}", to_string(res));
                return idle;
            }
            return operator()(ctx.mod(intercept).devices(), start) ? next : idle;
        }

        context_action operator()(event_type const& event) noexcept;

      private:
        libevdev_uinput* dev      = nullptr;
        int              err_code = 0;
    } uinput;

    static_assert(OutputModifier<basic_uinput>, "Must be an output modifier.");
} // namespace fs8
