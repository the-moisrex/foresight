// Created by moisrex on 8/8/26.

module;
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include <iterator>
#include <sys/poll.h>
#include <utility>
#include <vector>
module fs8.mods;
import fs8.log;

using fs8::basic_io_manager;
using fs8::context_action;
using fs8::io_event;
using fs8::io_fd;

using steady_clock = std::chrono::steady_clock;

template <>
struct fs8::pimpl_idiom<basic_io_manager>::impl {
    std::vector<pollfd>                                    fds;
    std::vector<std::function_ref<context_action(io_fd&)>> callbacks;

    /// Idle timeout: when no fd is ready for this duration, set idle_flag.
    std::chrono::microseconds idle_timeout{0};
    steady_clock::time_point  last_event_time{};
    bool                      idle_flag = false;
};

void basic_io_manager::clear() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    pimpl->fds.clear();
    pimpl->callbacks.clear();
}

bool basic_io_manager::is_watched(int const fd) const noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return false;
    }
    return std::ranges::any_of(pimpl->fds, [fd](pollfd const& pfd) noexcept {
        return pfd.fd == fd;
    });
}

bool basic_io_manager::empty() const noexcept {
    return pimpl.get() == nullptr || pimpl->fds.empty();
}

std::size_t basic_io_manager::size() const noexcept {
    return pimpl->fds.size();
}

void basic_io_manager::set_idle_timeout(std::chrono::microseconds const timeout) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }
    pimpl->idle_timeout    = timeout;
    pimpl->last_event_time = steady_clock::now();
    pimpl->idle_flag       = false;
}

void basic_io_manager::clear_idle_timeout() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    pimpl->idle_timeout = std::chrono::microseconds{0};
}

bool basic_io_manager::is_idle() const noexcept {
    return pimpl.get() != nullptr && pimpl->idle_flag;
}

void basic_io_manager::clear_idle() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    pimpl->idle_flag = false;
}

void basic_io_manager::unwatch(int const fd) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    try {
        for (std::size_t i = 0; i < pimpl->fds.size(); ++i) {
            if (pimpl->fds[i].fd == fd) {
                pimpl->fds.erase(pimpl->fds.begin() + static_cast<std::ptrdiff_t>(i));
                pimpl->callbacks.erase(pimpl->callbacks.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    } catch (...) {
        log("Allocation failure during erase.");
        // Allocation failure during erase: leave in a consistent state.
    }
}

bool basic_io_manager::watch(io_fd const& fd, io_callback const& cb) noexcept try {
    if (fd.fd < 0) [[unlikely]] {
        return false;
    }
    if (pimpl.get() == nullptr) {
        init_impl();
    }

    // Re-registering an already-watched fd replaces it in place, so a pipeline
    // restart that re-watches the same fds won't accumulate duplicates.
    for (std::size_t i = 0; i < pimpl->fds.size(); ++i) {
        if (pimpl->fds[i].fd == fd.fd) {
            pimpl->fds[i].events  = std::to_underlying(fd.events);
            pimpl->fds[i].revents = 0;
            pimpl->callbacks[i]   = cb;
            return true;
        }
    }

    // `push_back` only throws on allocation failure; each vector is kept
    // consistent here and the outer function-try-block turns it into `false`.
    pimpl->fds.emplace_back(pollfd{fd.fd, std::to_underlying(fd.events), 0});
    try {
        pimpl->callbacks.push_back(cb);
    } catch (...) {
        pimpl->fds.pop_back();
        return false;
    }
    return true;
} catch (...) {
    return false;
}

context_action basic_io_manager::operator()(special_event const& tag) noexcept {
    using enum context_action;
    switch (tag.code) {
        case 0: // start
            try {
                // Drop stale registrations from a previous pipeline run before the mods
                // re-register their handlers.
                clear();
                if (pimpl.get() == nullptr) {
                    init_impl();
                }
                pimpl->idle_flag       = false;
                pimpl->last_event_time = steady_clock::now();
                return next;
            } catch (...) {
                return context_action::exit;
            }
        case 2:    // load_event
            break; // fall through to load_event logic below
        default: return drop_event;
    }

    // load_event logic
    // Nothing watched is not fatal: the mods may not have re-registered their
    // fds yet after a restart, so fall through and let the pipeline re-poll.
    if (pimpl.get() == nullptr || pimpl->fds.empty()) [[unlikely]] {
        return next;
    }

    // Compute poll timeout from idle_timeout.
    int poll_timeout = -1;
    if (pimpl->idle_timeout.count() > 0) {
        auto const now       = steady_clock::now();
        auto const elapsed   = std::chrono::duration_cast<std::chrono::microseconds>(now - pimpl->last_event_time);
        auto const remaining = std::chrono::duration_cast<std::chrono::microseconds>(pimpl->idle_timeout - elapsed);
        poll_timeout         = static_cast<int>(std::max(remaining, std::chrono::microseconds{0}).count() / 1000);
    }

    int ready = 0;
    do {
        ready = ::poll(pimpl->fds.data(), static_cast<nfds_t>(pimpl->fds.size()), poll_timeout);
    } while (ready < 0 && errno == EINTR);

    if (ready < 0) [[unlikely]] {
        log("io_manager: poll failed: {}", std::strerror(errno));
        return exit;
    }

    // poll timed out with no ready fds — idle threshold reached.
    if (ready == 0 && pimpl->idle_timeout.count() > 0) {
        pimpl->idle_flag = true;
        return next;
    }

    // Collect all ready fds in a single forward scan, then dispatch forward.
    // Each handler is only called if its fd is still watched (a prior handler
    // may have unwatched it), so we re-validate before every dispatch.
    auto action = next;

    struct ready_entry {
        std::size_t index;
        int         fd;
        io_event    revents;
        io_event    events;
    };

    std::array<ready_entry, 64> ready_fds{};
    std::size_t                 ready_count = 0;

    for (std::size_t i = 0; i < pimpl->fds.size(); ++i) {
        if (pimpl->fds[i].revents != 0 && ready_count < ready_fds.size()) {
            ready_fds[ready_count++] = {
              .index   = i,
              .fd      = pimpl->fds[i].fd,
              .revents = static_cast<io_event>(pimpl->fds[i].revents),
              .events  = static_cast<io_event>(pimpl->fds[i].events),
            };
            pimpl->fds[i].revents = 0;
        }
    }

    // Activity happened — update the idle clock.
    pimpl->last_event_time = steady_clock::now();

    for (std::size_t i = 0; i < ready_count; ++i) {
        auto const& entry = ready_fds[i];
        // A prior handler may have unwatched this fd; skip if so.
        if (entry.index >= pimpl->fds.size() || pimpl->fds[entry.index].fd != entry.fd) [[unlikely]] {
            continue;
        }
        auto       io_fd_state = io_fd{.fd = entry.fd, .events = entry.events, .revents = entry.revents};
        auto const result      = pimpl->callbacks[entry.index](io_fd_state);
        if (io_fd_state.unwatch) [[unlikely]] {
            try {
                pimpl->fds.erase(pimpl->fds.begin() + static_cast<std::ptrdiff_t>(entry.index));
                pimpl->callbacks.erase(pimpl->callbacks.begin() + static_cast<std::ptrdiff_t>(entry.index));
            } catch (...) {
                // Allocation failure during erase: leave in a consistent state.
                log("Allocation failure during erase.");
            }
            continue;
        }
        if (result == exit) [[unlikely]] {
            return exit;
        }
        if (result == recovery) [[unlikely]] {
            action = recovery;
        }
    }
    return action;
}
