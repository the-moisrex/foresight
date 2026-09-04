// Created by moisrex on 9/4/26.

module;
#include <string>
#include <string_view>
export module fs8.mods:replay;
import fs8.context;
import fs8.event;
import fs8.nullable_indirect;
import fs8.traits;
import fs8.log;
import fs8.lib.evtest;
import :capture_format;

export namespace fs8 {

    namespace detail {
        constexpr std::size_t format_header_size = 6;
        struct replay_impl;
    } // namespace detail

    /// Pipeline mod that reads captured events from a file and injects them
    /// into the pipeline on each `load_event` tag. When the file is exhausted,
    /// returns `exit`.
    ///
    /// Auto-detects file format (binary or evtest text) from the file header.
    ///
    /// Pipeline form:
    /// ```cpp
    /// auto pipeline = context | stopper | replay | output;
    /// replay.set_file("capture-2026-09-04.bin");
    /// pipeline();
    /// ```
    template <capture_format FormatT = capture_binary_format>
    struct [[nodiscard]] basic_replay : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        FormatT                               format_{};
        std::string                           file_path_;
        nullable_indirect<detail::replay_impl> impl_{};

      public:
        void set_file(std::string_view path) noexcept;
        void init_impl() noexcept;

        // ── Pipeline interface ───────────────────────────────────────────────

        /// Handle start tag: open file and read header.
        context_action operator()(special_event const& tag) noexcept;

        /// Handle load_event: read the next event from the file.
        context_action operator()(event_type& event, special_event const& tag) noexcept;
    };

    /// Default replay: auto-detect format.
    constexpr basic_replay<> replay;

} // namespace fs8
