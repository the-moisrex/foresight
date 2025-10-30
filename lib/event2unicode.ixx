// Created by moisrex on 10/29/25.

module;
#include <xkbcommon/xkbcommon.h>
export module foresight.lib.xkb.event2unicode;
import foresight.mods.event;
import foresight.lib.xkb;

namespace foresight::xkb {

    export struct [[nodiscard]] basic_event2unicode {
      private:
        keymap::pointer map;

        // pointer to the xkb state for the keyboard
        xkb_state* state = nullptr;

      public:
        explicit basic_event2unicode(keymap::pointer map) noexcept;
        basic_event2unicode() noexcept;
        basic_event2unicode(basic_event2unicode const&)                      = delete;
        basic_event2unicode(basic_event2unicode&& other) noexcept            = default;
        basic_event2unicode& operator=(basic_event2unicode const&)           = delete;
        basic_event2unicode& operator=(basic_event2unicode&& other) noexcept = default;
        ~basic_event2unicode() noexcept;

        /// Process
        [[nodiscard]] char32_t operator()(event_type const& event) noexcept;
    };

} // namespace foresight::xkb
