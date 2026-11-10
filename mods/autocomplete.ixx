// Created by moisrex on 8/16/26.

module;
#include <functional>
#include <string_view>
export module fs8.mods:autocomplete;
import fs8.context;
import fs8.event;
import fs8.lib.xkb;
import fs8.lib.mod_parser;
import :typer;
import fs8.pimpl;

namespace fs8 {

    /**
     * Watches the user's typing and auto-completes a pattern of the form
     * `PREFIX<TAG>COMPLETION`, e.g. `"test@<tab>gmail.com"`.
     *
     * In trigger mode (default) the completion is emitted when the trigger key
     * (e.g. Tab) is pressed after PREFIX has been typed. In auto mode (see
     * `auto_mode`) the tag is only a separator and the completion is emitted as
     * soon as PREFIX is fully typed.
     *
     * The current word being typed is tracked through `unicode_encoded_event`
     * over an internal xkb state, so no `search_engine` is required in the pipeline.
     */
    export struct [[nodiscard]] basic_autocomplete : pimpl_idiom<basic_autocomplete> {
        using pimpl_idiom::pimpl_idiom;

      private:
        std::string_view pattern;        // pattern string
        xkb::basic_state keyboard_state; // the state of the modifier keys and what not
        bool             m_auto_mode    = false;
        bool             m_pass_trigger = false;

        /// Parse the pattern and initialize the search state.
        context_action on_start() noexcept;

        /// Handle a single event; `emit` is called with the completion text when it fires.
        context_action on_event(event_type const& event, std::function_ref<void(std::string_view)> inp_emit) noexcept;

      public:
        explicit consteval basic_autocomplete(std::string_view const inp_pattern) noexcept : pattern{inp_pattern} {}

        /// Return a new autocomplete that matches the specified pattern.
        consteval basic_autocomplete operator[](std::string_view const inp_pattern) const noexcept {
            return basic_autocomplete{inp_pattern};
        }

        consteval basic_autocomplete operator()(std::string_view const inp_pattern) const noexcept {
            return basic_autocomplete{inp_pattern};
        }

        /// Emit the completion automatically once the prefix is fully typed.
        consteval basic_autocomplete auto_mode(bool const value = true) const noexcept {
            auto new_mod       = basic_autocomplete{pattern};
            new_mod.m_auto_mode = value;
            return new_mod;
        }

        /// Don't swallow the trigger keypress when a completion fires.
        consteval basic_autocomplete pass_trigger(bool const value = true) const noexcept {
            auto new_mod          = basic_autocomplete{pattern};
            new_mod.m_pass_trigger = value;
            return new_mod;
        }

        /// Initialize the keyboard state and parse the pattern.
        context_action operator()([[maybe_unused]] Context auto& ctx, control_event const& tag) noexcept {
            if (tag.code != start.code) {
                return context_action::drop_event;
            }
            keyboard_state.initialize(xkb::get_default_keymap());
            return on_start();
        }

        /// Handle events
        context_action operator()(Context auto& ctx) noexcept {
            return on_event(ctx.event(), [&](std::string_view const completion) noexcept {
                // reuse the public type_string machinery: walks modifier tags and
                // fork_emits each event to the downstream mods only.
                basic_type_string<std::string_view>{completion}(ctx);
            });
        }
    };

    export constexpr basic_autocomplete autocomplete;

} // namespace fs8
