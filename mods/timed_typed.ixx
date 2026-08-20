// Created by moisrex on 8/18/26.

module;
#include <chrono>
#include <cstdint>
#include <limits>
#include <string_view>
export module fs8.mods:timed_typed;
import fs8.context;
import fs8.lib.xkb;
import fs8.pimpl;
import :typed;

namespace fs8 {

    /**
     * Like `typed`, but the pattern only matches if it's typed within a time window:
     * if the user pauses longer than `duration` between two characters of the pattern,
     * the partial match is discarded, so a pattern spread over a long period of time
     * will never fire.
     */
    export constexpr struct [[nodiscard]] basic_timed_typed : pimpl_idiom<basic_timed_typed> {
        using pimpl_idiom::pimpl_idiom;

        static constexpr std::uint16_t invalid_trigger_id = std::numeric_limits<std::uint16_t>::max();
        using duration_type                               = std::chrono::microseconds;
        static constexpr duration_type default_duration   = std::chrono::milliseconds(2000);

      private:
        std::string_view pattern;          // pattern string
        duration_type    duration{default_duration};
        xkb::basic_state keyboard_state{}; // the state of the modifier keys and what not

        /// Register the pattern into the search engine
        context_action on_start(basic_search_engine& engine) noexcept;

        /// Process and search with a time window
        [[nodiscard]] bool on_search(event_type const& event, basic_search_engine const& engine) noexcept;

      public:
        explicit consteval basic_timed_typed(std::string_view const inp_pattern) noexcept : pattern{inp_pattern} {}

        explicit consteval basic_timed_typed(std::string_view const inp_pattern, duration_type const inp_duration) noexcept
          : pattern{inp_pattern},
            duration{inp_duration} {}

        /// Return a new timed_typed class that triggers when "str" is typed by the user
        /// within the time window.
        consteval basic_timed_typed operator[](std::string_view const inp_trigger) const noexcept {
            return basic_timed_typed{inp_trigger};
        }

        /// Specify the time window along with the pattern: `timed_typed["test", 2s]`.
        consteval basic_timed_typed operator[](std::string_view const inp_trigger, duration_type const inp_duration) const noexcept {
            return basic_timed_typed{inp_trigger, inp_duration};
        }

        /// Register the pattern into the search engine
        context_action operator()(Context auto& ctx, start_tag) noexcept {
            keyboard_state.initialize(xkb::get_default_keymap());
            return on_start(ctx.mod(search_engine));
        }

        template <Context CtxT>
        [[nodiscard]] bool operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_search_engine, CtxT>, "You need to have 'search_engine' in your pipeline.");
            return on_search(ctx.event(), ctx.mod(search_engine));
        }
    } timed_typed;

} // namespace fs8
