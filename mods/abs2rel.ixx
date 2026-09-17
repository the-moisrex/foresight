module;
#include <cassert>
#include <climits>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <utility>
export module fs8.mods:abs2rel;
import :drop;
import :input_manager;
import :keys_state;
import :sanitizer;
import fs8.context;
import fs8.devices.capabilities;
import fs8.devices.evdev;
import fs8.traits;

export namespace fs8 {

    constexpr struct [[nodiscard]] basic_pressure2mouse_clicks : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        value_type pressure_threshold = 1;
        bool       is_left_down       = false;

      public:
        explicit constexpr basic_pressure2mouse_clicks(value_type const inp_pressure_threshold) noexcept
          : pressure_threshold{inp_pressure_threshold} {}

        consteval auto operator[](value_type const inp_pressure_threshold) const noexcept {
            auto res{*this};
            res.pressure_threshold = inp_pressure_threshold;
            assert(inp_pressure_threshold > 0);
            return res;
        }

        context_action operator()(event_type& event) noexcept;

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_drop_adjacent_repeats, CtxT>, "You need to drop syn repeats.");
            return operator()(ctx.event());
        }
    } pressure2mouse_clicks;

    constexpr struct [[nodiscard]] basic_pen2touch {
        using code_type = event_type::code_type;

        template <Context CtxT>
        void operator()(CtxT& ctx, special_event const& tag) const noexcept {
            if (tag.code != start.code) {
                return;
            }
            if constexpr (has_mod<basic_keys_state, CtxT>) {
                auto const& keys = ctx.mod(keys_state);
                for (code_type const tool :
                     std::initializer_list<code_type>{
                       BTN_TOOL_PEN,
                       BTN_TOOL_RUBBER,
                       BTN_TOOL_BRUSH,
                       BTN_TOOL_PENCIL,
                       BTN_TOOL_AIRBRUSH,
                       // BTN_TOOL_FINGER,
                       BTN_TOOL_MOUSE,
                       BTN_TOOL_LENS})
                {
                    if (keys.is_pressed(tool)) {
                        // Release the tools
                        std::ignore = ctx.fork_emit(event_type{EV_KEY, tool, 0});
                        std::ignore = ctx.fork_emit(syn());
                        std::ignore = ctx.fork_emit(event_type{EV_KEY, BTN_TOOL_FINGER, 0});
                        std::ignore = ctx.fork_emit(syn());
                    }
                }
            }
        }

        context_action operator()(event_type& event) const noexcept;
    } pen2touch;

    constexpr struct [[nodiscard]] basic_pen2mice : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        code_type active_tool = KEY_MAX;

      public:
        void operator()(event_type& event) noexcept;
    } pen2mice;

    constexpr struct [[nodiscard]] basic_abs2rel : consteval_copyable {
        using consteval_copyable::consteval_copyable;

        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

        static constexpr value_type states_loc   = (sizeof(value_type) * CHAR_BIT) - 3;
        static constexpr value_type x_bit_loc    = states_loc;
        static constexpr value_type y_bit_loc    = states_loc + 1;
        static constexpr value_type x_init_state = 0b1U << static_cast<std::uint32_t>(x_bit_loc);
        static constexpr value_type y_init_state = 0b1U << static_cast<std::uint32_t>(y_bit_loc);

      private:
        value_type last_abs_x = 0;
        value_type last_abs_y = 0;

        float x_scale_factor = 10.0F;
        float y_scale_factor = 10.0F;

        float x_epsilon = 0.0F;
        float y_epsilon = 0.0F;

        bool inherit = true;

      public:
        constexpr basic_abs2rel() noexcept = default;

        explicit constexpr basic_abs2rel(bool const inp_inherit) noexcept : inherit(inp_inherit) {}

        consteval basic_abs2rel operator[](bool const inp_inherit) const noexcept {
            return basic_abs2rel{inp_inherit};
        }

        void init(evdev const& dev, float scale = 20.0F) noexcept;

        template <Context CtxT>
            requires has_mod<basic_input_manager, CtxT>
        void init(CtxT& ctx) noexcept {
            if (!inherit) {
                return;
            }
            for (evdev const& dev : ctx.mod(input_manager).devices()) {
                if (dev.has_abs_info()) {
                    init(dev);
                    break;
                }
            }
        }

        void operator()(special_event const& tag) noexcept;

        template <Context CtxT>
        void operator()(CtxT& ctx, special_event const& tag) noexcept {
            if (tag.code != toggle_off.code) {
                return;
            }
            if constexpr (has_mod<basic_keys_state, CtxT>) {
                auto const& keys = ctx.mod(keys_state);
                for (code_type const tool :
                     std::initializer_list<code_type>{
                       BTN_TOOL_PEN,
                       BTN_TOOL_RUBBER,
                       BTN_TOOL_BRUSH,
                       BTN_TOOL_PENCIL,
                       BTN_TOOL_AIRBRUSH,
                       BTN_TOOL_FINGER,
                       BTN_TOOL_MOUSE,
                       BTN_TOOL_LENS})
                {
                    if (keys.is_pressed(tool)) {
                        std::ignore = ctx.fork_emit(event_type{EV_KEY, tool, 0});
                        std::ignore = ctx.fork_emit(syn());
                        std::ignore = ctx.fork_emit(event_type{EV_KEY, tool, 1});
                        std::ignore = ctx.fork_emit(syn());
                    }
                }
            }
        }

        context_action operator()(event_type& event) noexcept;

      private:
        void init_state() noexcept {
            last_abs_x |= x_init_state;
            last_abs_y |= y_init_state;
            x_epsilon   = 0.0F;
            y_epsilon   = 0.0F;
        }

        struct tablet_ranges {
            float x = 0.0F;
            float y = 0.0F;
        };

        static tablet_ranges read_tablet_ranges(basic_input_manager& im) noexcept {
            for (auto const& dev : im.devices()) {
                if (dev.has_abs_info()) {
                    if (auto const* x = dev.abs_info(ABS_X); x != nullptr) {
                        if (auto const* y = dev.abs_info(ABS_Y); y != nullptr) {
                            return {
                              .x = static_cast<float>(x->maximum - x->minimum),
                              .y = static_cast<float>(y->maximum - y->minimum),
                            };
                        }
                    }
                }
            }
            return {};
        }
    } abs2rel;

} // namespace fs8
