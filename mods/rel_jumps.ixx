// Created by moisrex on 6/8/25.

export module foresight.mods.rel_jumps;
import foresight.mods.event;

export namespace foresight::mods {

    struct ignore_big_jumps {
        void operator()(event ev, auto&& next) noexcept;
    };

} // namespace foresight::mods
