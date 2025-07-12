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
import foresight.mods.intercept;

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
        basic_uinput(evdev const& evdev_dev, std::filesystem::path const& file) noexcept;
        basic_uinput(libevdev const* evdev_dev, std::filesystem::path const& file) noexcept;
        explicit basic_uinput(libevdev const* evdev_dev,
                              int             file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;
        explicit basic_uinput(evdev const& evdev_dev,
                              int          file_descriptor = LIBEVDEV_UINPUT_OPEN_MANAGED) noexcept;
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

    /**
     * This struct helps to pick which virtual device should be chosen as output based on the even type.
     */
    template <std::size_t N = std::numeric_limits<std::uint8_t>::max()>
        requires(N <= std::numeric_limits<std::uint8_t>::max())
    struct [[nodiscard]] basic_uinput_picker {
        using ev_type    = event_type::type_type;
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

        static constexpr bool is_dynamic = N == std::numeric_limits<std::uint8_t>::max();

      private:
        using container_type =
          std::conditional_t<is_dynamic, std::vector<basic_uinput>, std::array<basic_uinput, N>>;

        // equals to 9
        static constexpr std::uint16_t shift = std::bit_width<std::uint16_t>(KEY_MAX) - 1U;

        [[nodiscard]] static constexpr std::uint16_t hash(event_code const event) noexcept {
            return static_cast<std::uint16_t>(event.type << shift) | static_cast<std::uint16_t>(event.code);
        }

        // returns 16127 or 0x3EFF
        static constexpr std::uint16_t max_hash = hash({.type = EV_MAX, .code = KEY_MAX});


        // the size is ~15KiB
        std::array<std::int8_t, max_hash> hashes{};

        // the uinput devices
        container_type uinputs{};

        bool inherit = false;

      public:
        template <typename... C>
            requires(std::convertible_to<C, dev_caps_view> && ...)
        consteval explicit basic_uinput_picker(C const&... caps_views) noexcept {
            set_caps(caps_views...);
        }

        template <typename... C>
            requires(std::convertible_to<C, dev_caps_view> && ...)
        consteval explicit basic_uinput_picker(bool const inp_inherit, C const&... caps_views) noexcept
          : inherit{inp_inherit} {
            set_caps(caps_views...);
        }

        basic_uinput_picker() noexcept                                      = default;
        basic_uinput_picker(basic_uinput_picker const&) noexcept            = default;
        basic_uinput_picker(basic_uinput_picker&&) noexcept                 = default;
        basic_uinput_picker& operator=(basic_uinput_picker const&) noexcept = default;
        basic_uinput_picker& operator=(basic_uinput_picker&&) noexcept      = default;
        ~basic_uinput_picker() noexcept                                     = default;

        // void init() {
        //     if constexpr (is_dynamic) {
        //         // todo
        //     } else {
        //         // regenerate dev_caps_view:
        //         std::array<dev_cap_view, EV_MAX> views{};
        //
        //         // todo: this includes some invalid types as well
        //         for (ev_type type = 0; type != EV_MAX; ++type) {
        //             views.at(type) = dev_cap_view{
        //               .type = type,
        //               .codes =
        //                 std::span<event_type::code_type const>{
        //                                                        std::next(hashes.begin(), hash({.type =
        //                                                        type, .code = 0})),
        //                                                        std::next(hashes.begin(), hash({.type =
        //                                                        type, .code = KEY_MAX}))},
        //             };
        //         }
        //
        //         // replace uinputs with the input devices with the highest percentage of matching:
        //         for (auto& cur_uinput : uinputs) {
        //             evdev_rank best{};
        //             for (evdev_rank cur : foresight::devices(views)) {
        //                 if (cur.match >= best.match) {
        //                     best = std::move(cur);
        //                 }
        //             }
        //             if (best.match != 0) {
        //                 cur_uinput = std::move(best);
        //             }
        //         }
        //     }
        // }

        /// Auto Initialize
        template <Context CtxT>
        void init(CtxT& ctx) {
            if constexpr (has_mod<basic_interceptor, CtxT>) {
                if (!inherit) {
                    return;
                }

                init_uinputs_from(ctx.mod(intercept).devices());
            } else {
                // init();
            }
        }

        template <typename... C>
            requires(std::convertible_to<C, dev_caps_view> && ...)
        constexpr void set_caps(C const&... caps_views) noexcept {
            hashes.fill(0);

            // Declaring which hash belongs to which uinput device
            (
              [this, input_pick = static_cast<std::int8_t>(0)](dev_caps_view const caps_view) mutable {
                  for (auto const [type, codes, addition] : caps_view) {
                      for (auto const code : codes) {
                          auto const index = hash({.type = type, .code = code});
                          hashes.at(index) = addition ? input_pick : -1;
                      }
                  }
                  ++input_pick;
              }(caps_views),
              ...);
        }

        template <std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_value_t<R>, evdev>
        void init_uinputs_from(R&& devs) {
            if constexpr (is_dynamic) {
                uinputs.clear();
                if constexpr (std::ranges::sized_range<R>) {
                    uinputs.reserve(devs.size());
                }
                for (evdev const& dev : devs) {
                    uinputs.emplace_back(dev);
                }
            } else {
                std::uint8_t index = 0;
                for (evdev const& dev : devs) {
                    if (index >= uinputs.size()) {
                        throw std::invalid_argument("Too many devices has been given to us.");
                    }
                    uinputs.at(index++) = basic_uinput{dev};
                }
            }
        }

        [[nodiscard]] std::span<basic_uinput const> devices() const noexcept {
            return std::span{uinputs};
        }

        [[nodiscard]] std::span<basic_uinput> devices() noexcept {
            return std::span{uinputs};
        }

        consteval basic_uinput_picker operator()(bool const inp_inherit) const noexcept {
            basic_uinput_picker picker;
            picker.inherit = inp_inherit;
            return picker;
        }

        template <typename... C>
            requires(std::convertible_to<C, dev_caps_view> && ...)
        consteval auto operator()(C const&... caps_views) const noexcept {
            return basic_uinput_picker<sizeof...(C)>{caps_views...};
        }

        template <typename... C>
            requires(std::convertible_to<C, dev_caps_view> && ...)
        consteval auto operator()(bool const inp_inherits, C const&... caps_views) const noexcept {
            return basic_uinput_picker<sizeof...(C)>{inp_inherits, caps_views...};
        }

        bool emit(ev_type const type, code_type const code, value_type const value) noexcept {
            return emit(event_type{type, code, value});
        }
        bool emit(input_event const& event) noexcept {
            return emit(event_type{event});
        }
        bool emit(event_type const& event) noexcept {
            return operator()(event) == context_action::next;
        }
        bool emit_syn() noexcept {
            return emit(syn());
        }

        context_action operator()(event_type const& event) noexcept {
            auto const index = hashes.at(hash(static_cast<event_code>(event)));
            if (index < 0) [[unlikely]] {
                return context_action::ignore_event;
            }
            return uinputs.at(index)(event);
        }
    };

    constexpr basic_uinput_picker<> uinput_picker;

    static_assert(output_modifier<basic_uinput>, "Must be an output modifier.");
    static_assert(output_modifier<basic_uinput_picker<>>, "Must be an output modifier.");
} // namespace foresight
