// Created by moisrex on 9/12/26.

module;
#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>
module fs8.mods;

using fs8::basic_tilt_state;
using fs8::context_action;
using fs8::event_type;

void basic_tilt_state::update(value_type const value, bool const is_x) noexcept {
    if (is_x) {
        tilt_x_ = value;
    } else {
        tilt_y_ = value;
    }

    float const nx = range_x_ > 0.0F ? static_cast<float>(tilt_x_) / range_x_ : 0.0F;
    float const ny = range_y_ > 0.0F ? static_cast<float>(tilt_y_) / range_y_ : 0.0F;

    float const dx = range_x_ > 0.0F ? static_cast<float>(tilt_x_ - last_x_) / range_x_ : 0.0F;
    float const dy = range_y_ > 0.0F ? static_cast<float>(tilt_y_ - last_y_) / range_y_ : 0.0F;

    last_x_ = tilt_x_;
    last_y_ = tilt_y_;

    norm_x_    = std::clamp(nx, -1.0F, 1.0F);
    norm_y_    = std::clamp(ny, -1.0F, 1.0F);
    magnitude_ = std::clamp(std::sqrt((norm_x_ * norm_x_) + (norm_y_ * norm_y_)), 0.0F, 1.0F);
    change_    = std::clamp(std::sqrt((dx * dx) + (dy * dy)), 0.0F, 1.0F);

    ++version_;
}

void basic_tilt_state::reset() noexcept {
    tilt_x_    = 0;
    tilt_y_    = 0;
    last_x_    = 0;
    last_y_    = 0;
    norm_x_    = 0.0F;
    norm_y_    = 0.0F;
    magnitude_ = 0.0F;
    change_    = 0.0F;
    ++version_;
}

context_action basic_tilt_state::operator()(event_type const& event) noexcept {
    using enum context_action;

    switch (event.hash()) {
        case hashed(EV_ABS, ABS_TILT_X): update(event.value(), true); return next;
        case hashed(EV_ABS, ABS_TILT_Y): update(event.value(), false); return next;

        case hashed(EV_KEY, BTN_TOOL_PEN):
        case hashed(EV_KEY, BTN_TOOL_RUBBER):
        case hashed(EV_KEY, BTN_TOOL_BRUSH):
        case hashed(EV_KEY, BTN_TOOL_PENCIL):
        case hashed(EV_KEY, BTN_TOOL_AIRBRUSH):
        case hashed(EV_KEY, BTN_TOOL_FINGER):
        case hashed(EV_KEY, BTN_TOOL_MOUSE):
        case hashed(EV_KEY, BTN_TOOL_LENS): reset(); return next;

        default: return next;
    }
}
