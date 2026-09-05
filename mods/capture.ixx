// Created by moisrex on 9/4/26.

module;
#include <span>
#include <string>
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

    namespace detail {
        struct capture_impl;
    } // namespace detail

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
        FormatT                                format_{};
        NamingT                                naming_{};
        nullable_indirect<detail::capture_impl> impl_{};

      public:
        consteval basic_capture(FormatT format, NamingT naming) noexcept
          : format_{format}, naming_{std::move(naming)}, impl_{} {}

        void init_impl() noexcept;

        // ── Pipeline interface ───────────────────────────────────────────────

        /// Handle lifecycle tags: start, toggle_on, toggle_off, idle.
        context_action operator()(special_event const& tag) noexcept;

        /// Buffer regular events.
        context_action operator()(event_type const& event) noexcept;

        // ── Accessors (for tests) ───────────────────────────────────────────

        [[nodiscard]] std::span<event_type const> buffered() const noexcept;
        [[nodiscard]] std::size_t                  buffer_size() const noexcept;
        [[nodiscard]] bool                         is_open() const noexcept;

      private:
        bool open_file() noexcept;
        void close_file() noexcept;
        void flush_buffer() noexcept;

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
