// Created by moisrex on 8/23/26.

module;
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/timerfd.h>
#include <unistd.h>
module fs8.mods;
import fs8.log;

using fs8::basic_io_manager;
using fs8::basic_scheduler;
using fs8::context_action;
using fs8::event_type;
using fs8::io_event;
using fs8::io_fd;

template <>
struct fs8::pimpl_idiom<basic_scheduler>::impl {
    static constexpr std::size_t capacity = 128;

    int                              timer_fd = -1;
    std::array<event_type, capacity> pending{};
    std::size_t                      head             = 0;
    std::size_t                      size             = 0;
    std::uint64_t                    expiration_count = 0;
    basic_io_manager*                io               = nullptr;

    [[nodiscard]] bool empty() const noexcept {
        return size == 0;
    }

    void clear() noexcept {
        size = 0;
        head = 0;
    }
};

context_action basic_scheduler::do_start(basic_io_manager& io) noexcept try {
    using enum context_action;
    if (pimpl.get() == nullptr) {
        init_impl();
    }
    pimpl->io = &io;

    if (pimpl->timer_fd < 0) {
        pimpl->timer_fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (pimpl->timer_fd < 0) [[unlikely]] {
            log("scheduler: timerfd_create failed: {}", std::strerror(errno));
            return exit;
        }
    }

    if (!io.is_watched(pimpl->timer_fd)) {
        if (!io.watch(io_fd{.fd = pimpl->timer_fd, .events = io_event::in}, *this)) [[unlikely]] {
            log("scheduler: failed to register timer fd with io_manager");
            return exit;
        }
    }
    // If events were scheduled before start(), arm the timer now.
    if (!pimpl->empty() && pimpl->expiration_count == 0) {
        itimerspec its{};
        its.it_interval.tv_sec  = 16 / 1000;
        its.it_interval.tv_nsec = static_cast<long>(16 % 1000) * 1'000'000L;
        its.it_value            = its.it_interval;
        if (::timerfd_settime(pimpl->timer_fd, 0, &its, nullptr) < 0) [[unlikely]] {
            log("scheduler: timerfd_settime failed: {}", std::strerror(errno));
        }
    }
    return next;
} catch (...) {
    return context_action::exit;
}

context_action basic_scheduler::operator()(io_fd const& fd) noexcept try {
    using enum context_action;
    if (pimpl.get() == nullptr || fd.fd != pimpl->timer_fd) [[unlikely]] {
        return next;
    }
    std::uint64_t expirations = 0;
    auto const    n           = ::read(pimpl->timer_fd, &expirations, sizeof(expirations));
    if (n < 0 && errno != EAGAIN) [[unlikely]] {
        log("scheduler: timerfd read failed: {}", std::strerror(errno));
        return exit;
    }
    pimpl->expiration_count += expirations;
    return next;
} catch (...) {
    return context_action::next;
}

context_action basic_scheduler::operator()(event_type& event, next_event_tag) noexcept {
    using enum context_action;
    if (pimpl.get() == nullptr || pimpl->expiration_count == 0 || pimpl->empty()) [[likely]] {
        return ignore_event;
    }
    event       = pimpl->pending[pimpl->head];
    pimpl->head = (pimpl->head + 1) % decltype(pimpl)::element_type::capacity;
    --pimpl->size;
    --pimpl->expiration_count;

    // disarm when done
    if (pimpl->empty()) [[unlikely]] {
        cancel();
    }
    return next;
}

void basic_scheduler::schedule(std::span<event_type const> const events, int const interval_ms) noexcept {
    if (pimpl.get() == nullptr) {
        init_impl();
    }
    pimpl->clear();
    pimpl->expiration_count = 0;
    auto const count        = std::min(events.size(), decltype(pimpl)::element_type::capacity);
    std::copy_n(events.begin(), count, pimpl->pending.begin());
    pimpl->size = count;

    if (pimpl->timer_fd >= 0) {
        itimerspec its{};
        its.it_interval.tv_sec  = interval_ms / 1000;
        its.it_interval.tv_nsec = static_cast<long>(interval_ms % 1000) * 1'000'000L;
        its.it_value            = its.it_interval;
        if (::timerfd_settime(pimpl->timer_fd, 0, &its, nullptr) < 0) [[unlikely]] {
            log("scheduler: timerfd_settime failed: {}", std::strerror(errno));
        }
    }
}

void basic_scheduler::cancel() noexcept {
    if (pimpl.get() == nullptr) [[unlikely]] {
        return;
    }
    pimpl->clear();
    pimpl->expiration_count = 0;

    if (pimpl->timer_fd >= 0) {
        itimerspec const zero{};
        if (::timerfd_settime(pimpl->timer_fd, 0, &zero, nullptr) < 0) [[unlikely]] {
            log("scheduler: timerfd disarm failed: {}", std::strerror(errno));
        }
    }
}

bool basic_scheduler::has_pending() const noexcept {
    return pimpl.get() != nullptr && !pimpl->empty();
}
