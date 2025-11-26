// Created by moisrex on 6/27/25.

module;
#include <array>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <string_view>
export module foresight.mods.caps;
import foresight.mods.event;

namespace fs8 {
    using ev_type   = event_type::type_type;
    using code_type = event_type::code_type;

    export template <std::size_t N>
    struct [[nodiscard]] dev_cap {
        ev_type                  type;
        std::array<code_type, N> codes;
    };

    export enum struct [[nodiscard]] caps_action : std::uint8_t {
        append,
        remove_codes,
        remove_type,
    };

    export struct [[nodiscard]] dev_cap_view {
        ev_type                    type;
        std::span<code_type const> codes;
        caps_action                action = caps_action::append;
    };

    export template <std::size_t N>
    dev_cap(ev_type, std::array<code_type, N> const&) -> dev_cap<N>;

    export template <std::size_t N>
    using dev_caps = std::array<dev_cap_view, N>;

    export using dev_caps_view = std::span<dev_cap_view const>;

    export template <ev_type Type, code_type Start, code_type End>
    [[nodiscard]] consteval dev_cap<End - Start> caps_range() noexcept {
        dev_cap<End - Start> res{};
        res.type = Type;
        for (code_type code = Start; code < End; ++code) {
            auto const index    = static_cast<std::size_t>(code - Start);
            res.codes.at(index) = code;
        }
        return res;
    }

    export template <typename... T>
    [[nodiscard]] consteval auto cap(ev_type const type, T... codes) noexcept {
        return dev_cap<sizeof...(codes)>{.type = type, .codes = {static_cast<code_type>(codes)...}};
    }

    export template <std::size_t N1, std::size_t N2>
    [[nodiscard]] consteval auto operator+(dev_cap<N1> const& lhs, dev_cap<N2> const& rhs) noexcept {
        return dev_caps<2>{
          dev_cap_view{lhs.type, lhs.codes},
          dev_cap_view{rhs.type, rhs.codes}
        };
    }

    export template <std::size_t N1, std::size_t N2>
    [[nodiscard]] consteval auto operator+(dev_caps<N1> const& lhs, dev_cap<N2> const& rhs) noexcept {
        dev_caps<N1 + 1> res;
        std::copy(lhs.begin(), lhs.end(), res.begin());
        res[N1] = dev_cap_view{rhs.type, rhs.codes};
        return res;
    }

    export template <std::size_t N1, std::size_t N2>
    [[nodiscard]] consteval auto operator+(dev_caps<N1> const& lhs, dev_caps<N2> const& rhs) noexcept {
        dev_caps<N1 + N2> res;
        std::copy(lhs.begin(), lhs.end(), res.begin());
        std::copy(rhs.begin(), rhs.end(), std::next(res.begin(), N1));
        return res;
    }

    /// Remove codes
    export template <std::size_t N1, std::size_t N2>
    [[nodiscard]] consteval auto operator-(dev_cap<N1> const& lhs, dev_cap<N2> const& rhs) noexcept {
        return dev_caps<2>{
          dev_cap_view{.type = lhs.type, .codes = lhs.codes,       .action = caps_action::append},
          dev_cap_view{.type = rhs.type, .codes = rhs.codes, .action = caps_action::remove_codes}
        };
    }

    export template <std::size_t N1>
    [[nodiscard]] consteval auto operator-(dev_cap<N1> const& lhs, ev_type const type) noexcept {
        return dev_caps<2>{
          dev_cap_view{.type = lhs.type, .codes = lhs.codes,      .action = caps_action::append},
          dev_cap_view{    .type = type,        .codes = {}, .action = caps_action::remove_type}
        };
    }

    /// Remove codes
    export template <std::size_t N1, std::size_t N2>
    [[nodiscard]] consteval auto operator-(dev_caps<N1> const& lhs, dev_cap<N2> const& rhs) noexcept {
        dev_caps<N1 + 1> res;
        std::copy(lhs.begin(), lhs.end(), res.begin());
        res[N1] = dev_cap_view{.type = rhs.type, .codes = rhs.codes, .action = caps_action::remove_codes};
        return res;
    }

    /// Remove codes
    export template <std::size_t N1, std::size_t N2>
    [[nodiscard]] consteval auto operator-(dev_caps<N1> const& lhs, dev_caps<N2> const& rhs) noexcept {
        dev_caps<N1 + N2> res;
        std::copy(lhs.begin(), lhs.end(), res.begin());
        std::copy(rhs.begin(), rhs.end(), std::next(res.begin(), N1));
        for (auto cur = std::next(res.begin(), N1); cur != res.end(); ++cur) {
            cur->action = caps_action::remove_codes;
        }
        return res;
    }

    /// Remove a type
    export template <std::size_t N1>
    [[nodiscard]] consteval auto operator-(dev_caps<N1> const& lhs, ev_type const type) noexcept {
        dev_caps<N1 + 1> res;
        std::copy(lhs.begin(), lhs.end(), res.begin());
        res[N1] = dev_cap_view{.type = type, .codes = {}, .action = caps_action::remove_type};
        return res;
    }

    export template <std::size_t N>
    [[nodiscard]] constexpr dev_cap_view view(dev_cap<N> const& inp_cap) noexcept {
        return dev_cap_view{.type = inp_cap.type, .codes = inp_cap.codes};
    }

    /// The return type is convertible to dev_caps_view
    export template <std::size_t N>
    [[nodiscard]] constexpr auto views(dev_cap<N> const& inp_cap) noexcept {
        return std::array<dev_cap_view, 1>{view(inp_cap)};
    }

    export namespace caps {
        // Synchronization
        constexpr auto syn = cap(EV_SYN, SYN_REPORT);

        // Keyboard LEDs
        constexpr auto leds          = caps_range<EV_LED, LED_NUML, LED_MAX + 1>();
        constexpr auto keyboard_leds = cap(EV_LED, LED_CAPSL, LED_NUML, LED_SCROLLL);

        // Key Groups
        constexpr auto keys_main      = caps_range<EV_KEY, KEY_ESC, KEY_SCROLLLOCK + 1>();
        constexpr auto keys_modifiers = cap(
          EV_KEY,
          KEY_LEFTCTRL,
          KEY_LEFTSHIFT,
          KEY_LEFTALT,
          KEY_LEFTMETA,
          KEY_RIGHTCTRL,
          KEY_RIGHTSHIFT,
          KEY_RIGHTALT,
          KEY_RIGHTMETA,
          KEY_COMPOSE);

        constexpr auto keys_locks    = cap(EV_KEY, KEY_CAPSLOCK, KEY_NUMLOCK, KEY_SCROLLLOCK, KEY_SCREENLOCK);
        constexpr auto keys_nav      = caps_range<EV_KEY, KEY_HOME, KEY_DELETE + 1>();
        constexpr auto keys_keypad   = caps_range<EV_KEY, KEY_KP7, KEY_KPDOT + 1>();
        constexpr auto keys_function = caps_range<EV_KEY, KEY_F1, KEY_F12 + 1>() // this includes more things
          /* + caps_range<EV_KEY, KEY_F13, KEY_F24 + 1>() */;
        constexpr auto keys_system = cap(EV_KEY, KEY_POWER, KEY_SLEEP, KEY_WAKEUP);
        constexpr auto keys_media  = cap(
          EV_KEY,
          KEY_MUTE,
          KEY_VOLUMEDOWN,
          KEY_VOLUMEUP,
          KEY_PLAYPAUSE,
          KEY_STOPCD,
          KEY_PREVIOUSSONG,
          KEY_NEXTSONG,
          KEY_MEDIA,
          KEY_MICMUTE);

        // Pointer (Mouse/Trackball)
        constexpr auto pointer_wheels =
          cap(EV_REL, REL_WHEEL, REL_HWHEEL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES);
        constexpr auto pointer_rel_all  = caps_range<EV_REL, REL_X, REL_MAX + 1>();
        constexpr auto pointer_rel_axes = cap(EV_REL, REL_X, REL_Y);
        constexpr auto pointer_btns =
          cap(EV_KEY, BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA, BTN_FORWARD, BTN_BACK, BTN_TASK);

        // Touch Devices (Touchpad/Touchscreen)
        constexpr auto touch_abs_axes = cap(EV_ABS, ABS_X, ABS_Y);
        constexpr auto touch_btns     = cap(EV_KEY, BTN_TOUCH, BTN_TOOL_FINGER);
        constexpr auto mt_abs_axes    = caps_range<EV_ABS, ABS_MT_SLOT, ABS_MT_TOOL_Y + 1>();

        // Gamepad/Joystick
        constexpr auto gamepad_btns      = caps_range<EV_KEY, BTN_SOUTH, BTN_THUMBR + 1>();
        constexpr auto joystick_abs_axes = cap(
          EV_ABS,
          ABS_X,
          ABS_Y,
          ABS_Z,
          ABS_RX,
          ABS_RY,
          ABS_RZ,
          ABS_THROTTLE,
          ABS_RUDDER,
          ABS_WHEEL,
          ABS_GAS,
          ABS_BRAKE,
          ABS_HAT0X,
          ABS_HAT0Y);

        // Tablet (Stylus)
        constexpr auto tablet_tool_btns = cap(
          EV_KEY,
          BTN_TOOL_PEN,
          BTN_TOOL_RUBBER,
          BTN_STYLUS,
          BTN_STYLUS2,
          BTN_TOOL_BRUSH,
          BTN_TOOL_PENCIL,
          BTN_TOOL_AIRBRUSH);
        constexpr auto tablet_abs_axes   = cap(EV_ABS, ABS_PRESSURE, ABS_DISTANCE, ABS_TILT_X, ABS_TILT_Y);
        constexpr auto tablet_abs_common = cap(EV_ABS, ABS_X, ABS_Y, ABS_PRESSURE, ABS_TILT_X, ABS_TILT_Y);

        constexpr auto abs_all = caps_range<EV_ABS, ABS_X, ABS_MAX + 1>();

        // Switches
        constexpr auto switches = caps_range<EV_SW, SW_LID, SW_MAX + 1>();

        constexpr auto misc      = caps_range<EV_MSC, MSC_SERIAL, MSC_MAX + 1>();
        constexpr auto misc_scan = cap(EV_MSC, MSC_SCAN);

        constexpr auto keyboard_keys = cap(
          EV_KEY,
          KEY_ESC,
          KEY_1,
          KEY_2,
          KEY_3,
          KEY_4,
          KEY_5,
          KEY_6,
          KEY_7,
          KEY_8,
          KEY_9,
          KEY_0,
          KEY_MINUS,
          KEY_EQUAL,
          KEY_BACKSPACE,
          KEY_TAB,
          KEY_Q,
          KEY_W,
          KEY_E,
          KEY_R,
          KEY_T,
          KEY_Y,
          KEY_U,
          KEY_I,
          KEY_O,
          KEY_P,
          KEY_LEFTBRACE,
          KEY_RIGHTBRACE,
          KEY_ENTER,
          KEY_LEFTCTRL,
          KEY_A,
          KEY_S,
          KEY_D,
          KEY_F,
          KEY_G,
          KEY_H,
          KEY_J,
          KEY_K,
          KEY_L,
          KEY_SEMICOLON,
          KEY_APOSTROPHE,
          KEY_GRAVE,
          KEY_LEFTSHIFT,
          KEY_BACKSLASH,
          KEY_Z,
          KEY_X,
          KEY_C,
          KEY_V,
          KEY_B,
          KEY_N,
          KEY_M,
          KEY_COMMA,
          KEY_DOT,
          KEY_SLASH,
          KEY_RIGHTSHIFT,
          KEY_KPASTERISK,
          KEY_LEFTALT,
          KEY_SPACE,
          KEY_CAPSLOCK,
          KEY_F1,
          KEY_F2,
          KEY_F3,
          KEY_F4,
          KEY_F5,
          KEY_F6,
          KEY_F7,
          KEY_F8,
          KEY_F9,
          KEY_F10,
          KEY_NUMLOCK,
          KEY_SCROLLLOCK,
          KEY_KP7,
          KEY_KP8,
          KEY_KP9,
          KEY_KPMINUS,
          KEY_KP4,
          KEY_KP5,
          KEY_KP6,
          KEY_KPPLUS,
          KEY_KP1,
          KEY_KP2,
          KEY_KP3,
          KEY_KP0,
          KEY_KPDOT,
          KEY_102ND,
          KEY_F11,
          KEY_F12,
          KEY_RO,
          KEY_HENKAN,
          KEY_KATAKANAHIRAGANA,
          KEY_MUHENKAN,
          KEY_KPJPCOMMA,
          KEY_KPENTER,
          KEY_RIGHTCTRL,
          KEY_KPSLASH,
          KEY_SYSRQ,
          KEY_RIGHTALT,
          KEY_HOME,
          KEY_UP,
          KEY_PAGEUP,
          KEY_LEFT,
          KEY_RIGHT,
          KEY_END,
          KEY_DOWN,
          KEY_PAGEDOWN,
          KEY_INSERT,
          KEY_DELETE,
          KEY_MUTE,
          KEY_VOLUMEDOWN,
          KEY_VOLUMEUP,
          KEY_POWER,
          KEY_KPEQUAL,
          KEY_PAUSE,
          KEY_KPCOMMA,
          KEY_HANGUEL,
          KEY_HANJA,
          KEY_YEN,
          KEY_LEFTMETA,
          KEY_RIGHTMETA,
          KEY_COMPOSE,
          KEY_STOP,
          KEY_AGAIN,
          KEY_PROPS,
          KEY_UNDO,
          KEY_FRONT,
          KEY_COPY,
          KEY_OPEN,
          KEY_PASTE,
          KEY_FIND,
          KEY_CUT,
          KEY_HELP,
          KEY_F13,
          KEY_F14,
          KEY_F15,
          KEY_F16,
          KEY_F17,
          KEY_F18,
          KEY_F19,
          KEY_F20,
          KEY_F21,
          KEY_F22,
          KEY_F23,
          KEY_F24,
          KEY_UNKNOWN);

        constexpr auto numpad = cap(
          EV_KEY,
          KEY_KP7,
          KEY_KP8,
          KEY_KP9,
          KEY_KPMINUS,
          KEY_KP4,
          KEY_KP5,
          KEY_KP6,
          KEY_KPPLUS,
          KEY_KP1,
          KEY_KP2,
          KEY_KP3,
          KEY_KP0,
          KEY_KPDOT,
          KEY_KPENTER,
          KEY_KPASTERISK,
          KEY_KPSLASH);

        constexpr auto alphabets = cap(
          EV_KEY,
          KEY_Q,
          KEY_W,
          KEY_E,
          KEY_R,
          KEY_T,
          KEY_Y,
          KEY_U,
          KEY_I,
          KEY_O,
          KEY_P,
          KEY_A,
          KEY_S,
          KEY_D,
          KEY_F,
          KEY_G,
          KEY_H,
          KEY_J,
          KEY_K,
          KEY_L,
          KEY_Z,
          KEY_X,
          KEY_C,
          KEY_V,
          KEY_B,
          KEY_N,
          KEY_M);

        // -- Composite Device Capabilities --

        // A standard 104-key Keyboard
        constexpr auto keyboard = syn + keyboard_leds + keyboard_keys;

        constexpr auto keyboard_numpad    = views(numpad);
        constexpr auto keyboard_alphabets = views(alphabets);
        constexpr auto all_misc           = views(misc);

        // A keyboard with additional media controls
        constexpr auto multimedia_keyboard = keyboard + keys_media;

        // A standard 3-button mouse with a scroll wheel
        constexpr auto pointer = syn + pointer_rel_axes + pointer_btns + pointer_wheels - abs_all - EV_ABS;

        // Alias for pointer
        constexpr auto mouse = pointer;

        // A simple touchpad with single-touch absolute positioning and a physical button area
        constexpr auto touchpad = touch_abs_axes + touch_btns - pointer_rel_all - EV_REL;

        // A modern multi-touch touchpad (like on most laptops)
        constexpr auto multitouch_touchpad = touchpad + mt_abs_axes;

        // A simple single-touch touchscreen
        constexpr auto touchscreen = syn + touch_abs_axes + touch_btns;

        // A multi-touch capable touchscreen
        constexpr auto multitouch_screen = touchscreen + mt_abs_axes;

        // A standard Gamepad (e.g., Xbox-style controller)
        constexpr auto gamepad = syn + gamepad_btns + joystick_abs_axes;

        // A graphics tablet for drawing
        constexpr auto tablet =
          syn + touch_abs_axes + tablet_abs_common + tablet_tool_btns + touch_btns - pointer_rel_all - EV_REL;

        constexpr std::array<std::pair<std::string_view, dev_caps_view>, 6U> cap_maps{
          {
           {{"mouse"}, mouse},
           {{"keyboard"}, keyboard},
           {{"touchpad"}, touchpad},
           {{"tablet"}, tablet},
           {{"pen"}, tablet},
           {{"drawing-tablet"}, tablet},
           }
        };
    } // namespace caps

    export [[nodiscard]] constexpr dev_caps_view caps_of(std::string_view const query) noexcept {
        for (auto const& [name, cap_view] : caps::cap_maps) {
            if (query == name) {
                return cap_view;
            }
        }
        return dev_caps_view{};
    }

} // namespace fs8
