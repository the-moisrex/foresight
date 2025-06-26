// Created by moisrex on 6/25/25.

module;
#include <linux/uinput.h>
export module foresight.mods.ignore;
import foresight.mods.context;

namespace foresight {

    export constexpr struct [[nodiscard]] basic_ignore_abs {
        constexpr context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            auto&      event = ctx.event();
            auto const type  = event.type();
            auto const code  = event.code();

            if (EV_ABS == type) {
                return ignore_event;
            }
            if (EV_KEY == type) {
                switch (code) {
                    case BTN_TOOL_RUBBER:
                    case BTN_TOOL_BRUSH:
                    case BTN_TOOL_PEN: return ignore_event;
                    default: break;
                }
            }
            return next;
        }
    } ignore_abs;

    // todo: ignore_types(EV_ABS)
    // todo: ignore_codes(EV_BTN_TOOL_RUBBER)

} // namespace foresight
