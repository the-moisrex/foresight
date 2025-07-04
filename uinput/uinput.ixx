// Created by moisrex on 6/29/24.

module;
#include <bit>
#include <cassert>
#include <filesystem>
#include <libevdev/libevdev-uinput.h>
#include <limits>
#include <string_view>
#include <system_error>
export module foresight.uinput;
export import foresight.evdev;
export import foresight.mods.event;
import foresight.mods.context;
import foresight.mods.caps;

export namespace foresight {
    /**
     * A virtual device
     *
     * If uinput_fd is LIBEVDEV_UINPUT_OPEN_MANAGED, we will open /dev/uinput in read/write mode and manage
     * the file descriptor. Otherwise, uinput_fd must be opened by the caller and opened with the appropriate
     * permissions.
     */
    constexpr struct basic_uinput {
        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

        constexpr basic_uinput() noexcept = default;
        basic_uinput(evdev& evdev_dev, std::filesystem::path const& file) noexcept;
        basic_uinput(libevdev const* evdev_dev, std::filesystem::path const& file) noexcept;
        explicit basic_uinput(libevdev const* evdev_dev,
                              int             file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;
        explicit basic_uinput(evdev& evdev_dev, int file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;
        consteval basic_uinput(basic_uinput const&)                     = default;
        constexpr basic_uinput(basic_uinput&&) noexcept                 = default;
        consteval basic_uinput& operator=(basic_uinput const&) noexcept = default;
        constexpr basic_uinput& operator=(basic_uinput&&) noexcept      = default;

        constexpr ~basic_uinput() noexcept {
            if !consteval {
                if (dev != nullptr) {
                    libevdev_uinput_destroy(dev);
                    dev = nullptr;
                }
            }
        }

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
        void set_device(libevdev const* evdev_dev,
                        int             file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;

        void set_device(evdev const& inp_dev, int file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;


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

        bool emit(ev_type type, code_type code, value_type value) noexcept;
        bool emit(input_event const& event) noexcept;
        bool emit(event_type const& event) noexcept;
        bool emit_syn() noexcept;

        context_action operator()(event_type const& event) noexcept;

      private:
        libevdev_uinput* dev      = nullptr;
        std::errc        err_code = std::errc{};
    } uinput;

    template <std::size_t N>
        requires(N < std::numeric_limits<std::uint8_t>::max())
    struct [[nodiscard]] basic_uinput_picker {
      private:
        // equals to 9
        static constexpr std::uint16_t shift = std::bit_width<std::uint16_t>(KEY_MAX) - 1U;

        [[nodiscard]] static constexpr std::uint16_t hash(event_code const event) noexcept {
            return static_cast<std::uint16_t>(event.type << shift) | static_cast<std::uint16_t>(event.code);
        }

        // returns 16127 or 0x3EFF
        static constexpr std::uint16_t max_hash = hash({.type = EV_MAX, .code = KEY_MAX});


        // the size is ~15KiB
        std::array<std::uint8_t, max_hash> hashes{};

        // the uinput devices
        std::array<basic_uinput, N> uinputs{};

      public:
        consteval explicit basic_uinput_picker(dev_caps_view const caps_view) noexcept {
            assert(caps_view.size() < std::numeric_limits<std::uint8_t>::max());

            // Declaring which hash belongs to which uinput device
            std::uint8_t input_pick = 0;
            for (auto const [type, codes] : caps_view) {
                for (auto const code : codes) {
                    auto const index = hash({.type = type, .code = code});
                    hashes.at(index) = input_pick;
                }
                ++input_pick;
            }
        }

        basic_uinput_picker() noexcept                                      = default;
        basic_uinput_picker(basic_uinput_picker const&) noexcept            = default;
        basic_uinput_picker(basic_uinput_picker&&) noexcept                 = default;
        basic_uinput_picker& operator=(basic_uinput_picker const&) noexcept = default;
        basic_uinput_picker& operator=(basic_uinput_picker&&) noexcept      = default;
        ~basic_uinput_picker() noexcept                                     = default;

        // void init() {
        //     auto devs = devices();
        // }

        context_action operator()(event_type const& event) noexcept {
            auto const index = hash(static_cast<event_code>(event));
            return uinputs.at(index)(event);
        }
    };

    constexpr basic_uinput_picker<1> uinput_picker;

    static_assert(output_modifier<basic_uinput>, "Must be a output modifier.");
} // namespace foresight
