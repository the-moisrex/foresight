module;
#include <cassert>
#include <cmath>
#include <cstdint>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <utility>
export module foresight.mods.abs2rel;
import foresight.mods.context;
import foresight.evdev;

namespace foresight {

    export constexpr struct [[nodiscard]] basic_abs2rel {
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        value_type last_abs_x = 0;
        value_type last_abs_y = 0;

        // pixel per millimeter
        double x_scale_factor = 10.0;
        double y_scale_factor = 10.0;

        // events sent between each syn
        std::uint8_t events_sent  = 0;
        code_type    active_tool  = BTN_TOOL_PEN;
        bool         is_left_down = false;

        value_type pressure_to_click = 500;

      public:
        constexpr basic_abs2rel() noexcept                                = default;
        consteval basic_abs2rel(basic_abs2rel const&) noexcept            = default;
        constexpr basic_abs2rel(basic_abs2rel&&) noexcept                 = default;
        consteval basic_abs2rel& operator=(basic_abs2rel const&) noexcept = default;
        constexpr basic_abs2rel& operator=(basic_abs2rel&&) noexcept      = default;
        constexpr ~basic_abs2rel() noexcept                               = default;

        void init(evdev const& dev, double const scale = 18.0) noexcept {
            auto const* x_absinfo = dev.abs_info(ABS_X);
            auto const* y_absinfo = dev.abs_info(ABS_Y);
            if (x_absinfo == nullptr || y_absinfo == nullptr) {
                return;
            }
            x_scale_factor = static_cast<double>(x_absinfo->resolution) / scale;
            y_scale_factor = static_cast<double>(y_absinfo->resolution) / scale;
        }

        constexpr context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            auto&      event = ctx.event();
            auto const type  = event.type();
            auto const code  = event.code();
            auto const value = event.value();

            if (is_syn(event) && std::exchange(events_sent, 0) == 0) {
                return ignore_event;
            }

            if (EV_ABS == type) {
                // Absolute position event from tablet
                switch (code) {
                    case ABS_X: {
                        auto const delta = value - last_abs_x;
                        auto const pixels =
                          static_cast<value_type>(std::round(static_cast<double>(delta) / x_scale_factor));
                        event.type(EV_REL);
                        event.code(REL_X);
                        event.value(pixels);
                        if (pixels != 0) {
                            last_abs_x = value;
                        }
                        break;
                    }
                    case ABS_Y: {
                        auto const delta = value - last_abs_y;
                        auto const pixels =
                          static_cast<value_type>(static_cast<double>(delta) / y_scale_factor);
                        event.type(EV_REL);
                        event.code(REL_Y);
                        event.value(pixels);
                        if (pixels != 0) {
                            last_abs_y = value;
                        }
                        break;
                    }
                    case ABS_TILT_X:
                    case ABS_TILT_Y:
                        return ignore_event;
                    case ABS_PRESSURE:
                        // use pressure as the left button click
                        if (value >= pressure_to_click && !is_left_down) {
                            event.type(EV_KEY);
                            event.code(BTN_LEFT);
                            event.value(1);
                            is_left_down = true;
                            break;
                        }
                        if (value < pressure_to_click && is_left_down) {
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
                    case BTN_TOOL_RUBBER:
                        event.code(BTN_MIDDLE);
                        break;
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
                        active_tool = code;
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

    } abs2rel;

    // evtest /dev/input/event28
    // Supported events:
    //   Event type 0 (EV_SYN)
    //   Event type 1 (EV_KEY)
    //     Event code 177 (KEY_SCROLLUP)
    //     Event code 178 (KEY_SCROLLDOWN)
    //     Event code 272 (BTN_LEFT)
    //     Event code 273 (BTN_RIGHT)
    //     Event code 274 (BTN_MIDDLE)
    //     Event code 320 (BTN_TOOL_PEN)
    //     Event code 321 (BTN_TOOL_RUBBER)
    //     Event code 322 (BTN_TOOL_BRUSH)
    //     Event code 330 (BTN_TOUCH)
    //     Event code 331 (BTN_STYLUS)
    //   Event type 3 (EV_ABS)
    //     Event code 0 (ABS_X)
    //       Value  27247
    //       Min        0
    //       Max    52324
    //       Resolution     200
    //     Event code 1 (ABS_Y)
    //       Value  15241
    //       Min        0
    //       Max    29600
    //       Resolution     200
    //     Event code 8 (ABS_WHEEL)
    //       Value      0
    //       Min        0
    //       Max       71
    //     Event code 24 (ABS_PRESSURE)
    //       Value      0
    //       Min        0
    //       Max    16383
    //     Event code 26 (ABS_TILT_X)
    //       Value      0
    //       Min      -63
    //       Max       64
    //     Event code 27 (ABS_TILT_Y)
    //       Value      0
    //       Min      -63
    //       Max       64

    //    Supported events:
    //  Event type 0 (EV_SYN)
    //  Event type 1 (EV_KEY)
    //    Event code 320 (BTN_TOOL_PEN)
    //    Event code 321 (BTN_TOOL_RUBBER)
    //    Event code 330 (BTN_TOUCH)
    //    Event code 331 (BTN_STYLUS)
    //  Event type 3 (EV_ABS)
    //    Event code 0 (ABS_X)
    //      Value  18090
    //      Min        0
    //      Max    32767
    //      Resolution     205
    //    Event code 1 (ABS_Y)
    //      Value  14269
    //      Min        0
    //      Max    32767
    //      Resolution     328
    //    Event code 24 (ABS_PRESSURE)
    //      Value      0
    //      Min        0
    //      Max     8191
    //    Event code 26 (ABS_TILT_X)
    //      Value      0
    //      Min     -127
    //      Max      127
    //    Event code 27 (ABS_TILT_Y)
    //      Value      0
    //      Min     -127
    //      Max      127
    //  Event type 4 (EV_MSC)
    //    Event code 4 (MSC_SCAN)


    // Example input:
    // Event: type 1 (EV_KEY), code 320 (BTN_TOOL_PEN), value 1
    // Event: type 3 (EV_ABS), code 0 (ABS_X), value 30694
    // Event: type 3 (EV_ABS), code 1 (ABS_Y), value 17780
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 0 (ABS_X), value 30776
    // Event: type 3 (EV_ABS), code 1 (ABS_Y), value 17732
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 0 (ABS_X), value 30886
    // Event: type 3 (EV_ABS), code 1 (ABS_Y), value 17707
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 1
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 0 (ABS_X), value 30997
    // Event: type 3 (EV_ABS), code 1 (ABS_Y), value 17688
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 2
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 3
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 4
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 5
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 7
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 9
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 10
    // Event: -------------- SYN_REPORT ------------
    // Event: type 1 (EV_KEY), code 320 (BTN_TOOL_PEN), value 0
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 0
    // Event: -------------- SYN_REPORT ------------

    // Event: type 3 (EV_ABS), code 24 (ABS_PRESSURE), value 857
    // Event: -------------- SYN_REPORT ------------
    // Event: type 4 (EV_MSC), code 4 (MSC_SCAN), value d0042
    // Event: type 1 (EV_KEY), code 330 (BTN_TOUCH), value 0
    // Event: type 3 (EV_ABS), code 24 (ABS_PRESSURE), value 0
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 15
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 14
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 13
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 12
    // Event: -------------- SYN_REPORT ------------
    // Event: type 3 (EV_ABS), code 26 (ABS_TILT_X), value 10


    // AI Generated info about Linux events:
    /****************************************************************************
     *
     * Linux Input Event Explanations
     *
     * The following is a breakdown of the input events supported by a device,
     * typically discovered using a tool like `evtest /dev/input/eventX`.
     * Each input from a device (like a key press, mouse movement, or stylus
     * touch) is reported to the kernel as a series of events. Each event has a
     * type, a code, and a value.
     *
     ****************************************************************************/

    //===========================================================================
    // Event Type 0: EV_SYN (Synchronization Events)
    //===========================================================================
    // EV_SYN events are used to group related event data together. When a
    // device sends multiple pieces of information at once (e.g., changes in
    // X, Y, and pressure), it will send all of them and then send an EV_SYN
    // event to signal that a complete "packet" of data is ready for processing.
    // This prevents the system from acting on incomplete reports.
    //
    // Common Codes (value for EV_SYN):
    //   - SYN_REPORT (0):   Signals the end of a multi-event report.
    //   - SYN_DROPPED (3):  Indicates the system has lost events from the device.
    //
    //---------------------------------------------------------------------------


    //===========================================================================
    // Event Type 1: EV_KEY (Key and Button Events)
    //===========================================================================
    // EV_KEY events describe the state of keys and buttons, such as those on a
    // keyboard, mouse, or stylus.
    //
    // The `value` for these events indicates the state:
    //   - 0: Key/Button is released.
    //   - 1: Key/Button is pressed.
    //   - 2: Key/Button is auto-repeating (held down).
    //
    // --- Supported Event Codes for this Device ---
    //
    //   Event code 177 (KEY_SCROLLUP)
    //   Event code 178 (KEY_SCROLLDOWN)
    //     - Represents the rotation of a scroll wheel or a similar control.
    //
    //   Event code 272 (BTN_LEFT)
    //   Event code 273 (BTN_RIGHT)
    //   Event code 274 (BTN_MIDDLE)
    //     - Standard mouse button clicks.
    //
    //   Event code 320 (BTN_TOOL_PEN)
    //   Event code 321 (BTN_TOOL_RUBBER)
    //   Event code 322 (BTN_TOOL_BRUSH)
    //     - Indicates which tool is currently active and in proximity to the
    //       tablet surface. For example, one end of a stylus can be a 'pen'
    //       and the other an 'eraser' (rubber).
    //
    //   Event code 330 (BTN_TOUCH)
    //     - Indicates that the tool (e.g., stylus tip, finger) is making
    //       physical contact with the device surface.
    //
    //   Event code 331 (BTN_STYLUS)
    //     - Represents the press of a button located on the barrel of the stylus.
    //       (BTN_STYLUS2 would be a second button).
    //
    //---------------------------------------------------------------------------


    //===========================================================================
    // Event Type 3: EV_ABS (Absolute Axis Events)
    //===========================================================================
    // EV_ABS events are used for devices that report their position on a fixed
    // coordinate system, such as a graphics tablet, joystick, or touchscreen.
    // Each axis has the following properties:
    //
    //   - Value: The current reported value on this axis.
    //   - Min:   The minimum possible value (e.g., top or left edge).
    //   - Max:   The maximum possible value (e.g., bottom or right edge).
    //   - Resolution: The density of points, often in units per millimeter.
    //                 This allows calculating the physical size of the input area.
    //
    // --- Supported Event Codes for this Device ---
    //
    //   Event code 0 (ABS_X)
    //   Event code 1 (ABS_Y)
    //     - The absolute X and Y coordinates of the tool on the surface.
    //       This device's active area is 52324x29600 units.
    //
    //   Event code 8 (ABS_WHEEL)
    //     - Reports the position of a wheel or slider control.
    //
    //   Event code 24 (ABS_PRESSURE)
    //     - The amount of pressure being applied by the tool (e.g., stylus).
    //       The value ranges from 0 (no pressure) to 16383 (maximum pressure).
    //       Used for effects like variable line thickness in drawing apps.
    //
    //   Event code 26 (ABS_TILT_X)
    //   Event code 27 (ABS_TILT_Y)
    //     - The tilt of the stylus along the X and Y axes. A value of 0 on
    //       both axes means the stylus is perpendicular to the surface.
    //       The values range from -63 to 64 degrees (approx).
    //       Used to alter the shape of the brush for a more realistic feel.
    //
    //---------------------------------------------------------------------------

} // namespace foresight
