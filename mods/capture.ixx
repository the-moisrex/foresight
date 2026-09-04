// Created by moisrex on 9/4/26.

module;
#include <cstdint>
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
    /// Lifecycle:
    ///   start       → initialize (no file opened yet)
    ///   toggle_on   → open file, begin buffering
    ///   idle        → flush buffer to file
    ///   toggle_off  → flush, close file, stop buffering
    ///
    /// Use inside `on[key_combo, capture]` for explicit start/stop,
    /// or at pipeline end for always-on capture.
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
        [[nodiscard]] bool                         is_active() const noexcept;

      private:
        context_action on_toggle_on() noexcept;
        void           flush_buffer() noexcept;
        void           flush_and_close() noexcept;
    };

    template <capture_format FormatT, capture_naming NamingT>
    basic_capture(FormatT, NamingT) -> basic_capture<FormatT, NamingT>;

    /// Default capture: binary format, daily rotation.
    constexpr auto capture = basic_capture{capture_binary_format{}, capture_daily{}};

} // namespace fs8
