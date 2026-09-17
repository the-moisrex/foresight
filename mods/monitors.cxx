module;
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>
module fs8.mods;
import fs8.compositor.monitor_detection;
import fs8.devices.udev;
import fs8.log;
import :input_manager;

using fs8::basic_monitors;
using fs8::context_action;
using fs8::io_event;
using fs8::io_fd;
using namespace fs8::compositor;

template <>
struct fs8::pimpl_idiom<basic_monitors>::impl {
    std::vector<monitor_info> monitors;
    desktop_bounds            desktop{};
    bool                      started = false;

    float effective_tablet_range_x = 0.0f;
    float effective_tablet_range_y = 0.0f;

    fs8::udev_monitor drm_monitor{};
    std::atomic<bool> hotplug_pending{false};

    void log_layout() const noexcept {
        log("monitors: {} monitor(s), desktop {}x{} at ({}, {})", monitors.size(), desktop.width, desktop.height, desktop.x, desktop.y);
        for (auto const& m : monitors) {
            log("  {} : {}x{} at ({}, {}){}", m.name, m.width_px, m.height_px, m.x, m.y, m.is_primary ? " primary" : "");
        }
        if (!monitors.empty() && desktop.width > 0 && desktop.height > 0) {
            constexpr int scale = 128;
            auto const    cols  = static_cast<int>((desktop.width + scale - 1) / scale);
            auto const    rows  = static_cast<int>((desktop.height + scale - 1) / scale);
            if (cols > 0 && cols <= 80 && rows > 0 && rows <= 40) {
                std::vector<int> grid(static_cast<std::size_t>(cols * rows), 0);
                for (std::size_t mi = 0; mi < monitors.size(); ++mi) {
                    auto const& m  = monitors[mi];
                    auto const  x0 = m.x / scale;
                    auto const  y0 = m.y / scale;
                    auto const  x1 = (m.x + static_cast<int32_t>(m.width_px)) / scale;
                    auto const  y1 = (m.y + static_cast<int32_t>(m.height_px)) / scale;
                    for (int r = y0; r < y1; ++r) {
                        for (int c = x0; c < x1; ++c) {
                            if (r >= 0 && r < rows && c >= 0 && c < cols) {
                                grid[static_cast<std::size_t>(r * cols + c)] = static_cast<int>(mi + 1);
                            }
                        }
                    }
                }
                log("  Layout (each char ~{}px):", scale);
                for (int r = 0; r < rows; ++r) {
                    std::string line(static_cast<std::size_t>(cols + 2), ' ');
                    for (int c = 0; c < cols; ++c) {
                        auto const v                         = grid[static_cast<std::size_t>(r * cols + c)];
                        line[static_cast<std::size_t>(c)]    = v > 0 ? static_cast<char>('0' + v - 1) : '.';
                        line[static_cast<std::size_t>(cols)] = '|';
                    }
                    line[static_cast<std::size_t>(cols + 1)] = '\0';
                    log("  |{}|", line.c_str());
                }
                for (std::size_t mi = 0; mi < monitors.size(); ++mi) {
                    auto const& m = monitors[mi];
                    log("  {} = {} ({}x{} at {},{})", static_cast<char>('0' + mi), m.name, m.width_px, m.height_px, m.x, m.y);
                }
            }
        }
    }

    static bool same_monitor(monitor_info const& a, monitor_info const& b) noexcept {
        return a.connector == b.connector && a.x == b.x && a.y == b.y && a.width_px == b.width_px
               && a.height_px == b.height_px && a.is_primary == b.is_primary;
    }

    static bool same_layout(std::vector<monitor_info> const& a, std::vector<monitor_info> const& b) noexcept {
        if (a.size() != b.size()) {
            return false;
        }
        for (auto const& m : a) {
            auto it = std::find_if(b.begin(), b.end(), [&m](monitor_info const& n) noexcept {
                return n.connector == m.connector;
            });
            if (it == b.end() || !same_monitor(m, *it)) {
                return false;
            }
        }
        return true;
    }

    bool refresh_internal() noexcept {
        auto       result  = enumerate_monitors();
        bool const changed = !same_layout(monitors, result.monitors);
        monitors = std::move(result.monitors);
        desktop  = compute_desktop_bounds(monitors);
        if (changed) {
            log_layout();
        }
        return changed;
    }
};

context_action basic_monitors::do_start(basic_io_manager& io) noexcept {
    using enum context_action;

    if (pimpl.get() == nullptr) [[unlikely]] {
        init_impl();
    }

    pimpl->refresh_internal();

    if (!pimpl->drm_monitor.is_valid()) {
        pimpl->drm_monitor.match_device("drm");
        pimpl->drm_monitor.enable();
    }

    if (!io.watch(io_fd{.fd = pimpl->drm_monitor.file_descriptor(), .events = io_event::in}, *this)) {
        log("monitors: failed to register DRM monitor fd");
    }

    pimpl->started = true;
    return next;
}

bool basic_monitors::consume_hotplug_pending() noexcept {
    return pimpl && pimpl->hotplug_pending.exchange(false);
}

context_action basic_monitors::operator()(io_fd& fd) noexcept {
    using enum context_action;

    if (fd.fd < 0 || !pimpl) [[unlikely]] {
        return next;
    }

    while (auto dev = pimpl->drm_monitor.next_device()) {
        auto const action = dev.action();
        if (action == "change" || action == "add" || action == "remove" || action == "bind" || action == "unbind") {
            pimpl->hotplug_pending.store(true, std::memory_order_relaxed);
        }
    }

    return next;
}

std::span<monitor_info const> basic_monitors::monitors() const noexcept {
    if (!pimpl) {
        return {};
    }
    return pimpl->monitors;
}

desktop_bounds basic_monitors::desktop() const noexcept {
    if (!pimpl) {
        return {};
    }
    return pimpl->desktop;
}

bool basic_monitors::refresh() noexcept {
    if (!pimpl) {
        return false;
    }
    return pimpl->refresh_internal();
}

void basic_monitors::set_effective_tablet_range(float const x, float const y) noexcept {
    if (pimpl) {
        pimpl->effective_tablet_range_x = x;
        pimpl->effective_tablet_range_y = y;
    }
}

float basic_monitors::effective_tablet_range_x() const noexcept {
    return pimpl ? pimpl->effective_tablet_range_x : 0.0f;
}

float basic_monitors::effective_tablet_range_y() const noexcept {
    return pimpl ? pimpl->effective_tablet_range_y : 0.0f;
}
