// Created by moisrex on 9/4/26.

module;
#include <cassert>
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
import :idle_detector;

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
        FormatT format{};
        NamingT naming{};

        struct state_type {
            std::vector<event_type> buffer;
            int                     current_fd = -1;
            std::string             current_path;
            std::int64_t            last_rotation = 0;
        };

        nullable_indirect<state_type> state{};

      public:
        consteval basic_capture(FormatT inp_format, NamingT inp_naming) noexcept : format{inp_format}, naming{std::move(inp_naming)} {}

        constexpr ~basic_capture() noexcept {
            if (static_cast<bool>(state)) {
                flush_buffer();
                close_file();
            }
        }

        /// Set a custom output filename (for naming strategies that support it, e.g. capture_manual).
        void set_name(std::string_view const name) noexcept
            requires requires { naming.set_name(name); }
        {
            naming.set_name(name);
        }

        // ── Pipeline interface ───────────────────────────────────────────────

        /// receives special events from the context.
        template <Context CtxT>
        context_action operator()(CtxT&, special_event const& tag) noexcept {
            static_assert(has_mod<basic_idle_detector<>, CtxT>, "Mod required");

            using enum context_action;
            switch (tag.code) {
                case start.code:
                    ensure_state();
                    if (!open_file()) [[unlikely]] {
                        return recovery;
                    }
                    return next;
                case idle.code: {
                    assert(static_cast<bool>(state));
                    if (state->buffer.empty()) {
                        return next;
                    }
                    if (state->current_fd < 0 || naming.should_rotate(state->last_rotation)) [[unlikely]] {
                        if (!open_file()) [[unlikely]] {
                            return recovery;
                        }
                    }
                    flush_buffer();
                    return next;
                }
                default: return drop_event;
            }
        }

        context_action operator()(event_type const& event) noexcept {
            assert(static_cast<bool>(state));
            state->buffer.push_back(event);
            return context_action::next;
        }

        // ── Accessors (for tests) ───────────────────────────────────────────

        [[nodiscard]] std::span<event_type const> buffered() const noexcept {
            if (!static_cast<bool>(state)) {
                return {};
            }
            return state->buffer;
        }

        [[nodiscard]] std::size_t buffer_size() const noexcept {
            if (!static_cast<bool>(state)) {
                return 0;
            }
            return state->buffer.size();
        }

        [[nodiscard]] bool is_open() const noexcept {
            return static_cast<bool>(state) && state->current_fd >= 0;
        }

      private:
        void ensure_state() noexcept {
            if (!static_cast<bool>(state)) {
                state = nullable_indirect<state_type>::make();
            }
        }

        bool open_file() noexcept {
            if (is_open()) [[unlikely]] {
                close_file();
            }
            auto const path = naming.filename(FormatT::extension);
            auto const fd   = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (fd < 0) {
                log("capture: failed to open {}", path);
                return false;
            }
            if (!format.write_header(fd)) {
                log("capture: failed to write header to {}", path);
                ::close(fd);
                return false;
            }
            state->current_fd    = fd;
            state->current_path  = std::move(path);
            state->last_rotation = detail::now_epoch_seconds();
            log("Capture started on {}", state->current_path);
            return true;
        }

        void close_file() noexcept {
            if (state->current_fd < 0) {
                return;
            }
            std::ignore = format.write_footer(state->current_fd);
            ::close(state->current_fd);
            state->current_fd = -1;
            log("Closing: {}", state->current_path);
        }

        void flush_buffer() noexcept {
            if (state->buffer.empty() || state->current_fd < 0) [[unlikely]] {
                return;
            }
            std::ignore = format.emit(state->current_fd, state->buffer);
            state->buffer.clear();
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
