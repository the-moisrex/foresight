// Created by moisrex on 9/4/26.

module;
#include <cstdint>
#include <fcntl.h>
#include <format>
#include <span>
#include <string>
#include <unistd.h>
#include <vector>
export module fs8.mods:capture;
import fs8.context;
import fs8.event;
import fs8.nullable_indirect;
import fs8.traits;
import fs8.log;
import :capture_format;
import :capture_naming;

export namespace fs8 {

    /// Pipeline mod that buffers events in memory and flushes to a file during
    /// idle periods. File naming and output format are template parameters.
    ///
    /// Events are always buffered. The file is opened on the first flush and
    /// stays open until the pipeline exits or the naming rotates.
    ///
    ///   start       → no-op
    ///   toggle_on   → no-op (events are buffered regardless)
    ///   idle        → open file if needed, flush buffer, rotate if needed
    ///   toggle_off  → flush buffer immediately
    ///
    /// Pipeline form:
    /// ```cpp
    /// auto pipeline = context | io_manager | idle_detector
    ///     | intercept | input_manager | stopper
    ///     | on[pressed[KEY_F1], capture[capture_evtest_format{}, capture_daily{}]];
    /// ```
    template <capture_format FormatT, capture_naming NamingT>
    struct [[nodiscard]] basic_capture : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        FormatT format_{};
        NamingT naming_{};

        struct state {
            std::vector<event_type> buffer;
            int                     current_fd = -1;
            std::string             current_path;
            std::int64_t            last_rotation = 0;
        };

        nullable_indirect<state> st_{};

      public:
        consteval basic_capture(FormatT format, NamingT naming) noexcept : format_{format}, naming_{std::move(naming)} {}

        // ── Pipeline interface ───────────────────────────────────────────────

        context_action operator()(special_event const& tag) noexcept {
            using enum context_action;
            ensure_state();
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
                    if (st_->buffer.empty()) {
                        return next;
                    }
                    if (st_->current_fd < 0) {
                        if (!open_file()) {
                            return next;
                        }
                    } else if (naming_.should_rotate(st_->last_rotation)) {
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

        context_action operator()(event_type const& event) noexcept {
            ensure_state();
            st_->buffer.push_back(event);
            return context_action::next;
        }

        // ── Accessors (for tests) ───────────────────────────────────────────

        [[nodiscard]] std::span<event_type const> buffered() const noexcept {
            if (!static_cast<bool>(st_)) {
                return {};
            }
            return st_->buffer;
        }

        [[nodiscard]] std::size_t buffer_size() const noexcept {
            if (!static_cast<bool>(st_)) {
                return 0;
            }
            return st_->buffer.size();
        }

        [[nodiscard]] bool is_open() const noexcept {
            return static_cast<bool>(st_) && st_->current_fd >= 0;
        }

      private:
        void ensure_state() noexcept {
            if (!static_cast<bool>(st_)) {
                st_ = nullable_indirect<state>::make();
            }
        }

        bool open_file() noexcept {
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
            st_->current_fd    = fd;
            st_->current_path  = std::move(path);
            st_->last_rotation = detail::now_epoch_seconds();
            return true;
        }

        void close_file() noexcept {
            if (st_->current_fd < 0) {
                return;
            }
            std::ignore = format_.write_footer(st_->current_fd);
            ::close(st_->current_fd);
            st_->current_fd = -1;
        }

        void flush_buffer() noexcept {
            if (st_->buffer.empty() || st_->current_fd < 0) {
                return;
            }
            std::ignore = format_.emit(st_->current_fd, st_->buffer);
            st_->buffer.clear();
        }

      public:
        // ── Bracket syntax ──────────────────────────────────────────────────

        /// `capture[daily]` — binary format + custom naming.
        consteval auto operator[](capture_naming auto naming) const noexcept {
            return basic_capture{capture_binary_format{}, naming};
        }

        /// `capture[evtest_format]` — custom format + daily naming.
        consteval auto operator[](capture_format auto fmt) const noexcept {
            return basic_capture{fmt, capture_daily{}};
        }

        /// `capture[evtest_format, daily]` — custom format + custom naming.
        consteval auto operator[](capture_format auto fmt, capture_naming auto naming) const noexcept {
            return basic_capture{fmt, naming};
        }

        /// `capture[daily, evtest_format]` — naming first, format second.
        consteval auto operator[](capture_naming auto naming, capture_format auto fmt) const noexcept {
            return basic_capture{fmt, naming};
        }
    };

    template <capture_format FormatT, capture_naming NamingT>
    basic_capture(FormatT, NamingT) -> basic_capture<FormatT, NamingT>;

    /// Default capture: binary format, daily rotation.
    /// Supports bracket syntax: `capture[daily]`, `capture[evtest_format]`, etc.
    /// Use `capture_name{1h}` for duration-based rotation.
    constexpr basic_capture<capture_binary_format, capture_daily> capture{capture_binary_format{}, capture_daily{}};

} // namespace fs8
