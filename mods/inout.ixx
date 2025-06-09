// Created by moisrex on 6/9/25.

module;
#include <unistd.h>
#include <utility>
export module foresight.mods.inout;
import foresight.mods.context;
import foresight.mods.event;

export namespace foresight::mods {

    constexpr struct out_type {
        void operator()(Context auto& ctx) const noexcept {
            std::ignore = write(STDOUT_FILENO, &ctx.event(), sizeof(event_type));
        }
    } out;

    constexpr struct inp_type {
        [[nodiscard]] context_action operator()(Context auto& ctx) const noexcept {
            using enum context_action;
            auto const res = read(STDIN_FILENO, &ctx.event(), sizeof(event_type));
            if (res == 0) [[unlikely]] {
                return exit;
            }
            if (res != sizeof(event_type)) [[unlikely]] {
                return ignore_event;
            }
            return next;
        }
    } inp;

    static_assert(modifier<inp_type>, "It's input");
} // namespace foresight::mods
