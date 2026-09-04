// Shared test utilities for device-dependent tests.
// Extracted from device_test.cxx, intercept_test.cxx, input_manager_test.cxx.
//
// This header contains only system-header utilities with no module dependencies.
// Include it BEFORE any `import` declarations.

#ifndef FORESIGHT_TESTS_COMMON_TEST_HELPERS_HPP
#define FORESIGHT_TESTS_COMMON_TEST_HELPERS_HPP

#include <chrono>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

namespace fs8::test {

    /// Wait until `node` can be opened or `timeout_ms` elapses.
    [[nodiscard]] inline bool wait_for_openable(std::string_view const node, int timeout_ms) noexcept {
        while (timeout_ms > 0) {
            int const fd = ::open(node.data(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0) {
                ::close(fd);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(50, timeout_ms)));
            timeout_ms -= 50;
        }
        return false;
    }

    /// Poll `fd` until it becomes readable or `timeout_ms` elapses.
    [[nodiscard]] inline bool wait_for_event(int const fd, int timeout_ms) noexcept {
        pollfd pfd{.fd = fd, .events = POLLIN, .revents = 0};
        while (timeout_ms > 0) {
            auto const res = ::poll(&pfd, 1, std::min(timeout_ms, 50));
            if (res > 0) {
                return true;
            }
            timeout_ms -= 50;
        }
        return false;
    }

    /// Inject a KEY_A down + SYN_REPORT into the given device node.
    inline void inject_key_down(std::string_view const devnode) {
        int const fd = ::open(devnode.data(), O_WRONLY | O_NONBLOCK);
        ASSERT_GE(fd, 0);
        input_event ev{};
        ev.type  = EV_KEY;
        ev.code  = KEY_A;
        ev.value = 1;
        ASSERT_EQ(::write(fd, &ev, sizeof(ev)), static_cast<ssize_t>(sizeof(ev)));
        ev.type  = EV_SYN;
        ev.code  = SYN_REPORT;
        ev.value = 0;
        ASSERT_EQ(::write(fd, &ev, sizeof(ev)), static_cast<ssize_t>(sizeof(ev)));
        ::close(fd);
    }

    /// The last path component of a device node (e.g. "event9").
    [[nodiscard]] inline std::string sysname_of(std::string_view const devnode) {
        auto const pos = devnode.find_last_of('/');
        return std::string{pos == std::string_view::npos ? devnode : devnode.substr(pos + 1)};
    }

} // namespace fs8::test

#endif // FORESIGHT_TESTS_COMMON_TEST_HELPERS_HPP
