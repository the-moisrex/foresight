// A pipe-backed fake keyboard for tests that need to inject and read events
// without creating a real uinput device (which triggers Wayland compositor
// reconfiguration and causes multi-second input pauses).
//
// Include this header AFTER any `import` declarations.

#ifndef FORESIGHT_FAKE_KEYBOARD_HPP
#define FORESIGHT_FAKE_KEYBOARD_HPP

#ifndef NDEBUG

#include <unistd.h>
import fs8.devices.evdev;

namespace fs8::test {

    /// A pipe-backed keyboard device: events written to inject_fd are readable
    /// via dev.next().  The evdev holds the read end; the caller owns write_fd.
    struct fake_keyboard {
        evdev dev;
        int   inject_fd = -1;

        fake_keyboard() noexcept = default;

        fake_keyboard(evdev d, int fd) noexcept : dev{std::move(d)}, inject_fd{fd} {}

        fake_keyboard(fake_keyboard&& other) noexcept
            : dev{std::move(other.dev)}, inject_fd{std::exchange(other.inject_fd, -1)} {}

        fake_keyboard& operator=(fake_keyboard&& other) noexcept {
            if (this != &other) {
                close_inject_fd();
                dev      = std::move(other.dev);
                inject_fd = std::exchange(other.inject_fd, -1);
            }
            return *this;
        }

        ~fake_keyboard() {
            close_inject_fd();
        }

        fake_keyboard(fake_keyboard const&)            = delete;
        fake_keyboard& operator=(fake_keyboard const&) = delete;

        /// Write a raw input_event into the pipe.
        void inject(input_event const& ev) noexcept {
            if (inject_fd >= 0) {
                [[maybe_unused]] auto const n = ::write(inject_fd, &ev, sizeof(ev));
            }
        }

        /// Inject a key press followed by a SYN_REPORT.
        void inject_key_down(unsigned code = KEY_A) noexcept {
            inject({.time = {}, .type = EV_KEY, .code = static_cast<__u16>(code), .value = 1});
            inject({.time = {}, .type = EV_SYN, .code = SYN_REPORT, .value = 0});
        }

        /// Inject a key release followed by a SYN_REPORT.
        void inject_key_up(unsigned code = KEY_A) noexcept {
            inject({.time = {}, .type = EV_KEY, .code = static_cast<__u16>(code), .value = 0});
            inject({.time = {}, .type = EV_SYN, .code = SYN_REPORT, .value = 0});
        }

        /// Inject a full key press+release cycle.
        void inject_key_tap(unsigned code = KEY_A) noexcept {
            inject_key_down(code);
            inject_key_up(code);
        }

      private:
        void close_inject_fd() noexcept {
            if (inject_fd >= 0) {
                ::close(inject_fd);
                inject_fd = -1;
            }
        }
    };

    /// Create a fake keyboard backed by a pipe.  No real devices are opened.
    [[nodiscard]] inline fake_keyboard make_fake_keyboard() noexcept {
        int write_fd = -1;
        fs8::evdev dev = fs8::evdev::make_pipe_device(write_fd);
        return fake_keyboard{std::move(dev), write_fd};
    }

} // namespace fs8::test

#endif // NDEBUG
#endif // FORESIGHT_FAKE_KEYBOARD_HPP
