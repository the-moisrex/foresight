// Created by moisrex on 6/8/25.

module;
#include <cmath>
export module foresight.mods.rel_jumps;
import foresight.mods.event;
import foresight.mods.context;

export namespace foresight::mods {

    struct ignore_big_jumps {
        event_type::value_type threshold = 50; // pixels to resistence to move

        void operator()(context auto& ctx) noexcept {
            if (is_mouse_movement(ctx) && std::abs(value(ctx)) > threshold) {
                return;
            }
            ctx.next();
        }
    };

} // namespace foresight::mods
