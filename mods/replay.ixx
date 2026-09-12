// Created by moisrex on 9/4/26.

module;
#include <string_view>
export module fs8.mods:replay;
import fs8.context;
import fs8.event;
import fs8.pimpl;
import fs8.log;
import fs8.lib.evtest;

export namespace fs8 {

    namespace detail {
        constexpr std::size_t format_header_size = 6;
        constexpr std::size_t input_event_size   = 24; // sizeof(input_event) on 64-bit Linux
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
    /// pipeline.mod(replay).set_file("capture-2026-09-04.fs8");
    /// pipeline();
    /// ```
    struct [[nodiscard]] basic_replay : pimpl_idiom<basic_replay> {
        using pimpl_idiom::pimpl_idiom;

        void set_file(std::string_view path) noexcept;

        // ── Pipeline interface ───────────────────────────────────────────────

        /// Handle start and load_event tags.
        context_action operator()(event_type& event, special_event const& tag) noexcept;
    };

    /// Default replay: auto-detect format.
    constexpr basic_replay replay;

} // namespace fs8
