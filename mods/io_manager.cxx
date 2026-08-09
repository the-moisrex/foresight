// Created by moisrex on 8/8/26.

module;
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <functional>
#include <iterator>
#include <sys/poll.h>
#include <utility>
#include <vector>
module fs8.mods.io_manager;
import fs8.log;

using fs8::basic_io_manager;
using fs8::context_action;
using fs8::io_event;
using fs8::io_fd;

template <>
struct fs8::pimpl_idiom<basic_io_manager>::impl {
    std::vector<pollfd>                                          fds;
    std::vector<std::function_ref<context_action(io_fd const&)>> callbacks;
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

void basic_io_manager::unwatch(int const fd) noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    for (std::size_t i = 0; i < pimpl->fds.size(); ++i) {
        if (pimpl->fds[i].fd == fd) {
            pimpl->fds.erase(pimpl->fds.begin() + static_cast<std::ptrdiff_t>(i));
            pimpl->callbacks.erase(pimpl->callbacks.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
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

    auto const old_size = pimpl->fds.size();
    try {
        pimpl->fds.emplace_back(pollfd{fd.fd, std::to_underlying(fd.events), 0});
        pimpl->callbacks.push_back(cb);
    } catch (...) {
        pimpl->fds.resize(old_size); // noexcept; no-op if fds didn't grow
        return false;
    }
    return true;
} catch (...) {
    return false;
}

context_action basic_io_manager::operator()(start_tag) noexcept try {
    using enum context_action;
    // Drop stale registrations from a previous pipeline run before the mods
    // re-register their handlers.
    clear();
    if (pimpl.get() == nullptr) {
        init_impl();
    }
    return next;
} catch (...) {
    return context_action::exit;
}

context_action basic_io_manager::operator()(load_event_tag) noexcept {
    using enum context_action;
    if (pimpl.get() == nullptr || pimpl->fds.empty()) [[unlikely]] {
        log("io_manager: no file descriptors are being watched");
        return exit;
    }

    int ready = 0;
    do {
        ready = ::poll(pimpl->fds.data(), static_cast<nfds_t>(pimpl->fds.size()), -1);
    } while (ready < 0 && errno == EINTR);

    if (ready < 0) [[unlikely]] {
        log("io_manager: poll failed: {}", std::strerror(errno));
        return exit;
    }
    if (ready == 0) {
        return next;
    }

    // Dispatch the ready fds one at a time, re-scanning from the front on each
    // iteration. There's no snapshot to allocate, and handlers are free to
    // watch/unwatch: the fd this round is cleared before its handler runs, and
    // `it` is never used again afterwards.
    auto action = next;
    while (true) {
        auto const it = std::ranges::find_if(pimpl->fds, [](pollfd const& pfd) noexcept {
            return pfd.revents != 0;
        });
        if (it == pimpl->fds.end()) [[unlikely]] {
            break;
        }
        auto const fd      = it->fd;
        auto const revents = static_cast<io_event>(it->revents);
        it->revents        = 0;
        auto const index   = static_cast<std::size_t>(std::distance(pimpl->fds.begin(), it));
        auto const result  = pimpl->callbacks[index](io_fd{.fd = fd, .events = static_cast<io_event>(it->events), .revents = revents});
        if (result == exit) [[unlikely]] {
            return exit;
        }
        if (result == idle) [[unlikely]] {
            action = idle;
        }
    }
    return action;
}
