// Created by moisrex on 6/29/25.

module;
#include "../../../../../usr/lib/clang/20/include/limits.h"

#include <cmath>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <print>
#include <utility>
module foresight.mods.abs2rel;
import foresight.main.log;

using foresight::basic_abs2rel;
using foresight::context_action;
using foresight::event_type;

constexpr basic_abs2rel::value_type states_loc   = sizeof(basic_abs2rel::value_type) * CHAR_BIT - 3;
constexpr basic_abs2rel::value_type x_bit_loc    = states_loc;
constexpr basic_abs2rel::value_type y_bit_loc    = states_loc + 1;
constexpr basic_abs2rel::value_type x_init_state = 0b1 << x_bit_loc;
constexpr basic_abs2rel::value_type y_init_state = 0b1 << y_bit_loc;

void basic_abs2rel::init(evdev const& dev, double const scale) noexcept {
    auto const* x_absinfo = dev.abs_info(ABS_X);
    auto const* y_absinfo = dev.abs_info(ABS_Y);
    if (x_absinfo == nullptr || y_absinfo == nullptr) {
        return;
    }
    x_scale_factor = static_cast<double>(x_absinfo->resolution) / scale;
    y_scale_factor = static_cast<double>(y_absinfo->resolution) / scale;

    log("Init abs2rel: ({}, {}) with resolution ({}, {})",
        x_scale_factor,
        y_scale_factor,
        x_absinfo->resolution,
        y_absinfo->resolution);
}

context_action basic_abs2rel::operator()(event_type& event) noexcept {
    using enum context_action;
    auto const type  = event.type();
    auto const code  = event.code();
    auto const value = event.value();

    // AI Poem:
    //   From touch to tool, I steer with the pen,
    //   Sensing the path where the stylus can’t wend—
    //   No need to click, I’m in full swing,
    //   My screen's a stage, I let the hand begin.
    //   No more clicks, just pressure, grace,
    //   A digital dance in every trace.
    //   From tilt to track, I follow through,
    //   No more clicks—I break the cue.
    //   So let the stylus fly, the input's true—
    //   No wires, no keys—just me and my view.

    // -1 is given because the syn itself is an event that's being sent
    if (is_syn(event) && std::exchange(events_sent, -1) <= 0) {
        return ignore_event;
    }

    if (EV_ABS == type) {
        // Absolute position event from a tablet
        switch (code) {
            case ABS_X: {
                auto const delta   = value - (last_abs_x & ~x_init_state);
                auto       pixels  = static_cast<value_type>(static_cast<double>(delta) / x_scale_factor);
                pixels            &= ~(0 - (last_abs_x >> x_bit_loc)); // don't move if we're in init state
                event.type(EV_REL);
                event.code(REL_X);
                event.value(pixels);
                last_abs_x = value;
                break;
            }
            case ABS_Y: {
                auto const delta   = value - (last_abs_y & ~y_init_state);
                auto       pixels  = static_cast<value_type>(static_cast<double>(delta) / y_scale_factor);
                pixels            &= ~(0 - (last_abs_y >> y_bit_loc)); // don't move if we're in init state
                event.type(EV_REL);
                event.code(REL_Y);
                event.value(pixels);
                last_abs_y = value;
                break;
            }
            case ABS_TILT_X:
            case ABS_TILT_Y: return ignore_event;
            case ABS_PRESSURE:
                // use pressure as the left button click
                if (value >= pressure_threshold && !is_left_down) {
                    event.type(EV_KEY);
                    event.code(BTN_LEFT);
                    event.value(1);
                    is_left_down = true;
                    break;
                }
                if (value < pressure_threshold && is_left_down) {
                    event.code(BTN_LEFT);
                    event.type(EV_KEY);
                    event.value(0);
                    is_left_down = false;
                    break;
                }
                return ignore_event;
            default: break;
        }
    } else if (EV_KEY == type) {
        switch (code) {
            case BTN_STYLUS: event.code(BTN_RIGHT); break;
            case BTN_TOUCH: return ignore_event;
            case BTN_STYLUS2:
            case BTN_STYLUS3: return ignore_event;
            case BTN_TOOL_RUBBER: event.code(BTN_MIDDLE); break;
            case BTN_TOOL_PEN:
            case BTN_TOOL_BRUSH:
            case BTN_TOOL_PENCIL:
            case BTN_TOOL_AIRBRUSH:
                // case BTN_TOOL_FINGER:
                // case BTN_TOOL_MOUSE:
                // case BTN_TOOL_LENS:
                // case BTN_TOOL_QUINTTAP:
                // case BTN_TOOL_DOUBLETAP:
                // case BTN_TOOL_TRIPLETAP:
                // case BTN_TOOL_QUADTAP:
                // active_tool = code;
                last_abs_x |= x_init_state;
                last_abs_y |= y_init_state;
                return ignore_event;
            default: break;
        }
    } else if (EV_REL == type) {
        // The EV_REL block for updating last_abs is likely not needed if the primary
        // input is the tablet, but we leave it for mixed-device scenarios.
        switch (code) {
            case REL_X: last_abs_x += value; break;
            case REL_Y: last_abs_y += value; break;
            default: break;
        }
    }


    ++events_sent;
    return next;
}
