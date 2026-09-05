// Created by moisrex on 9/4/26.

module;
#include <cstdint>
#include <fcntl.h>
#include <span>
#include <string>
#include <unistd.h>
#include <vector>
module fs8.mods;

using fs8::context_action;
using fs8::event_type;
using fs8::special_event;

// ── capture_impl (not exported) ──────────────────────────────────────────────

struct fs8::detail::capture_impl {
    std::vector<event_type> buffer;
    int                     current_fd    = -1;
    std::string             current_path;
    std::int64_t            last_rotation = 0;
};

// ── basic_capture members ────────────────────────────────────────────────────

template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
void fs8::basic_capture<FormatT, NamingT>::init_impl() noexcept {
    impl_ = nullable_indirect<detail::capture_impl>::make();
}

template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
context_action fs8::basic_capture<FormatT, NamingT>::operator()(special_event const& tag) noexcept {
    using enum context_action;
    if (!static_cast<bool>(impl_)) {
        init_impl();
    }
    switch (tag.code) {
        case start.code: return next;
        case toggle_on.code: {
            if (tag.value == toggle_on.value) {
                return next; // no-op: events are always buffered
            }
            // toggle_off: flush immediately
            flush_buffer();
            return next;
        }
        case idle.code: {
            if (impl_->buffer.empty()) {
                return next;
            }
            if (impl_->current_fd < 0) {
                if (!open_file()) {
                    return next;
                }
            } else if (naming_.should_rotate(impl_->last_rotation)) {
                close_file();
                if (!open_file()) {
                    return next;
                }
            }
            flush_buffer();
            return next;
        }
        default: return drop_event;
    }
}

template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
context_action fs8::basic_capture<FormatT, NamingT>::operator()(event_type const& event) noexcept {
    if (!static_cast<bool>(impl_)) {
        init_impl();
    }
    impl_->buffer.push_back(event);
    return context_action::next;
}

template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
std::span<event_type const> fs8::basic_capture<FormatT, NamingT>::buffered() const noexcept {
    if (!static_cast<bool>(impl_)) {
        return {};
    }
    return impl_->buffer;
}

template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
std::size_t fs8::basic_capture<FormatT, NamingT>::buffer_size() const noexcept {
    if (!static_cast<bool>(impl_)) {
        return 0;
    }
    return impl_->buffer.size();
}

template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
bool fs8::basic_capture<FormatT, NamingT>::is_open() const noexcept {
    return static_cast<bool>(impl_) && impl_->current_fd >= 0;
}

template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
bool fs8::basic_capture<FormatT, NamingT>::open_file() noexcept {
    auto const path = naming_.filename(FormatT::extension);
    auto const fd   = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        log("capture: failed to open {}", path);
        return false;
    }
    if (!format_.write_header(fd)) {
        log("capture: failed to write header to {}", path);
        ::close(fd);
        return false;
    }
    impl_->current_fd    = fd;
    impl_->current_path  = std::move(path);
    impl_->last_rotation = detail::now_epoch_seconds();
    return true;
}

template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
void fs8::basic_capture<FormatT, NamingT>::close_file() noexcept {
    if (impl_->current_fd < 0) {
        return;
    }
    std::ignore = format_.write_footer(impl_->current_fd);
    ::close(impl_->current_fd);
    impl_->current_fd = -1;
}

template <fs8::capture_format FormatT, fs8::capture_naming NamingT>
void fs8::basic_capture<FormatT, NamingT>::flush_buffer() noexcept {
    if (impl_->buffer.empty() || impl_->current_fd < 0) {
        return;
    }
    std::ignore = format_.emit(impl_->current_fd, impl_->buffer);
    impl_->buffer.clear();
}

// ── Explicit instantiations ──────────────────────────────────────────────────

template struct fs8::basic_capture<fs8::capture_binary_format, fs8::capture_daily>;
template struct fs8::basic_capture<fs8::capture_evtest_format, fs8::capture_daily>;
