// Created by moisrex on 6/8/25.

module;
#include <linux/uinput.h>
module foresight.mods.event;

bool foresight::is_mouse_movement(event const& ev) noexcept {
    return ev.type() == EV_REL && (ev.code() == REL_X || ev.code() == REL_Y);
}
