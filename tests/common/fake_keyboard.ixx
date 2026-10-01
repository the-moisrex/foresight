// A pipe-backed fake keyboard for tests that need to inject and read events
// without creating a real uinput device (which triggers Wayland compositor
// reconfiguration and causes multi-second input pauses).
//
// This is a module, not a header: a header can neither `#include` after an
// `import` nor hold an `import` of its own without forcing an include order
// on every TU.  Tests `import fs8.test.fake_keyboard;` instead.

module;

#include <linux/input.h>
#include <unistd.h>
#include <utility>

export module fs8.test.fake_keyboard;

import fs8.devices.evdev;

export namespace fs8::test {

    /// A pipe-backed keyboard device: events written to inject_fd are readable
    /// via dev.next().  The evdev holds the read end; the caller owns write_fd.
    struct fake_keyboard {
        evdev dev;
        int   inject_fd = -1;

        fake_keyboard() noexcept = default;

        fake_keyboard(evdev d, int fd) noexcept : dev{std::move(d)}, inject_fd{fd} {}

        fake_keyboard(fake_keyboard&& other) noexcept : dev{std::move(other.dev)}, inject_fd{std::exchange(other.inject_fd, -1)} {}

        fake_keyboard& operator=(fake_keyboard&& other) noexcept {
            if (this != &other) {
                close_inject_fd();
                dev       = std::move(other.dev);
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
    [[nodiscard]] fake_keyboard make_fake_keyboard() noexcept {
        int   write_fd = -1;
        evdev dev      = evdev::make_pipe_device(write_fd);
        return fake_keyboard{std::move(dev), write_fd};
    }

} // namespace fs8::test
