module;
#include <cassert>
#include <linux/input-event-codes.h>
#include <utility>
export module fs8.mods.abs2rel;
import fs8.context;
import fs8.devices.evdev;
import fs8.mods.input_manager;
import fs8.devices.capabilities;
import fs8.mods.ignore;
import fs8.mods.keys_status;
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

        void           operator()(auto&&, Tag auto) = delete;
        void           operator()(Tag auto)         = delete;
        context_action operator()(event_type& event) noexcept;

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_ignore_adjacent_repeats, CtxT>, "You need to ignore syn repeats.");
            return operator()(ctx.event());
        }
    } pressure2mouse_clicks;

    constexpr struct [[nodiscard]] basic_pen2touch {
        using code_type = event_type::code_type;

        void operator()(auto&&, Tag auto) = delete;
        void operator()(Tag auto)         = delete;

        template <Context CtxT>
        void operator()(CtxT& ctx, start_tag) const noexcept {
            if constexpr (has_mod<basic_keys_status, CtxT>) {
                auto const& keys = ctx.mod(keys_status);
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

      private:
        value_type last_abs_x = 0;
        value_type last_abs_y = 0;

        // pixel per millimeter
        float x_scale_factor = 10.0F;
        float y_scale_factor = 10.0F;

        float x_epsilon = 0.0F;
        float y_epsilon = 0.0F;

        // code_type   active_tool  = BTN_TOOL_PEN;

        bool inherit = true;

      public:
        explicit constexpr basic_abs2rel(bool const inp_inherit) noexcept : inherit(inp_inherit) {}

        void init(evdev const& dev, float scale = 20.0F) noexcept;

        /// Auto Initialize
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

        // template <Context CtxT>
        // void operator()(CtxT& ctx, start_tag) noexcept {
        //     init(ctx);
        // }

        consteval basic_abs2rel operator[](bool const inp_inherit) const noexcept {
            return basic_abs2rel{inp_inherit};
        }

        void operator()(auto&&, Tag auto) = delete;
        void operator()(Tag auto)         = delete;
        void operator()(start_tag) noexcept;

        /// this fixes flickering of the pen after we switched while the pen (in mouse mode) is still active.
        template <Context CtxT>
        void operator()(CtxT& ctx, toggle_off_tag) noexcept {
            if constexpr (has_mod<basic_keys_status, CtxT>) {
                auto const& keys = ctx.mod(keys_status);
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
                        // re-submit the events, in case they were ignored previously:
                        std::ignore = ctx.fork_emit(event_type{EV_KEY, tool, 0});
                        std::ignore = ctx.fork_emit(syn());
                        std::ignore = ctx.fork_emit(event_type{EV_KEY, tool, 1});
                        std::ignore = ctx.fork_emit(syn());
                    }
                }
            }
        }

        context_action operator()(event_type& event) noexcept;

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_ignore_adjacent_repeats, CtxT>, "You need to ignore syn repeats.");
            return operator()(ctx.event());
        }

    } abs2rel;

} // namespace fs8
