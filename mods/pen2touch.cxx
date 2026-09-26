// Created by moisrex on 9/17/26.

module;
#include <initializer_list>
#include <linux/input-event-codes.h>
#include <linux/input.h>
module fs8.mods;
import fs8.devices.evdev;

using fs8::basic_pen2touch;
using fs8::evdev;

basic_pen2touch::value_type fs8::basic_pen2touch::next_tracking_id() noexcept {
    if (tracking_id_ >= 65'535) {
        tracking_id_ = 0;
    } else {
        ++tracking_id_;
    }
    return tracking_id_;
}

void fs8::basic_pen2touch::reset() noexcept {
    tracking_id_   = -1;
    x_             = 0;
    y_             = 0;
    pressure_      = 0;
    tool_active_   = false;
    touching_      = false;
    have_x_        = false;
    have_y_        = false;
    have_pressure_ = false;
}

input_absinfo fs8::basic_pen2touch::absinfo_from(evdev const& tmpl, code_type const source, input_absinfo const& fallback) noexcept {
    if (auto const* info = tmpl.abs_info(source); info != nullptr) {
        return *info;
    }
    return fallback;
}

void fs8::basic_pen2touch::profile_template(evdev& tmpl) noexcept {
    // ── Strip the tablet identity ──────────────────────────────
    // Tool proximity + stylus buttons: no touchpad equivalent (the
    // mod renames the tool to BTN_TOOL_FINGER at runtime, so that
    // key must be the one advertised).
    for (code_type const btn :
         std::initializer_list<code_type>{
           BTN_TOOL_PEN,
           BTN_TOOL_RUBBER,
           BTN_TOOL_BRUSH,
           BTN_TOOL_PENCIL,
           BTN_TOOL_AIRBRUSH,
           BTN_TOOL_MOUSE,
           BTN_TOOL_LENS,
           BTN_STYLUS,
           BTN_STYLUS2,
           BTN_STYLUS3})
    {
        if (tmpl.has_event_code(EV_KEY, btn)) {
            tmpl.disable_event_code(EV_KEY, btn);
        }
    }
    // Stylus-only axes.
    for (code_type const code : std::initializer_list<code_type>{ABS_TILT_X, ABS_TILT_Y, ABS_TOOL_WIDTH}) {
        if (tmpl.has_event_code(EV_ABS, code)) {
            tmpl.disable_event_code(EV_ABS, code);
        }
    }
    // The parts of the touch profile we actually emit.
    tmpl.enable_event_code(EV_KEY, BTN_TOUCH);
    tmpl.enable_event_code(EV_KEY, BTN_TOOL_FINGER);

    // ── Multitouch absinfo ─────────────────────────────────────
    static constexpr input_absinfo slot_info{.minimum = 0, .maximum = 0};
    static constexpr input_absinfo tracking_info{.minimum = 0, .maximum = 65'535};
    static constexpr input_absinfo xy_fallback{.minimum = 0, .maximum = 32'767};
    static constexpr input_absinfo pressure_fallback{.minimum = 0, .maximum = 1023};

    // Position/pressure mirror the source axes one-to-one, so copy
    // their ranges when the source is part of the template.
    tmpl.abs_info(ABS_MT_SLOT, slot_info);
    tmpl.abs_info(ABS_MT_TRACKING_ID, tracking_info);
    tmpl.abs_info(ABS_MT_POSITION_X, absinfo_from(tmpl, ABS_X, xy_fallback));
    tmpl.abs_info(ABS_MT_POSITION_Y, absinfo_from(tmpl, ABS_Y, xy_fallback));
    tmpl.abs_info(ABS_MT_PRESSURE, absinfo_from(tmpl, ABS_PRESSURE, pressure_fallback));

    // The mod also forwards the legacy single-touch axes.
    if (!tmpl.has_abs_info(ABS_X)) {
        tmpl.abs_info(ABS_X, xy_fallback);
    }
    if (!tmpl.has_abs_info(ABS_Y)) {
        tmpl.abs_info(ABS_Y, xy_fallback);
    }
    if (!tmpl.has_abs_info(ABS_PRESSURE)) {
        tmpl.abs_info(ABS_PRESSURE, pressure_fallback);
    }

    // ── Input properties ───────────────────────────────────────
    // Inherited properties (notably INPUT_PROP_DIRECT from direct
    // tablets) would make libinput classify the device as a
    // touchscreen; a touchpad is a POINTER.
    for (unsigned prop = 0; prop <= INPUT_PROP_MAX; ++prop) {
        if (prop == INPUT_PROP_POINTER) {
            continue;
        }
        if (tmpl.has_property(prop)) {
            tmpl.disable_property(prop);
        }
    }
    tmpl.enable_property(INPUT_PROP_POINTER);
}
