module;
#include <cassert>
#include <linux/input-event-codes.h>
#include <utility>
export module foresight.mods.abs2rel;
import foresight.mods.context;
import foresight.evdev;
import foresight.mods.intercept;
import foresight.mods.caps;
import foresight.mods.ignore;
import foresight.mods.keys_status;

export namespace foresight {

    constexpr struct [[nodiscard]] basic_pressure2mouse_clicks {
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        value_type pressure_threshold = 1;
        bool       is_left_down       = false;

      public:
        explicit constexpr basic_pressure2mouse_clicks(value_type const inp_pressure_threshold) noexcept
          : pressure_threshold{inp_pressure_threshold} {}

        constexpr basic_pressure2mouse_clicks() noexcept                                         = default;
        consteval basic_pressure2mouse_clicks(basic_pressure2mouse_clicks const&) noexcept            = default;
        constexpr basic_pressure2mouse_clicks(basic_pressure2mouse_clicks&&) noexcept                 = default;
        consteval basic_pressure2mouse_clicks& operator=(basic_pressure2mouse_clicks const&) noexcept = default;
        constexpr basic_pressure2mouse_clicks& operator=(basic_pressure2mouse_clicks&&) noexcept      = default;
        constexpr ~basic_pressure2mouse_clicks() noexcept                                        = default;

        consteval auto operator()(value_type const inp_pressure_threshold) const noexcept {
            auto res{*this};
            res.pressure_threshold = inp_pressure_threshold;
            assert(inp_pressure_threshold > 0);
            return res;
        }

        context_action operator()(event_type& event) noexcept;

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_ignore_adjacent_repeats, CtxT>, "You need to ignore syn repeats.");
            return operator()(ctx.event());
        }
    } pressure2mouse_clicks;

    constexpr struct [[nodiscard]] basic_pen2touch {
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        value_type pressure_threshold = 1;
        bool       is_left_down       = false;

      public:
        explicit constexpr basic_pen2touch(value_type const inp_pressure_threshold) noexcept
          : pressure_threshold{inp_pressure_threshold} {}

        constexpr basic_pen2touch() noexcept                                  = default;
        consteval basic_pen2touch(basic_pen2touch const&) noexcept            = default;
        constexpr basic_pen2touch(basic_pen2touch&&) noexcept                 = default;
        consteval basic_pen2touch& operator=(basic_pen2touch const&) noexcept = default;
        constexpr basic_pen2touch& operator=(basic_pen2touch&&) noexcept      = default;
        constexpr ~basic_pen2touch() noexcept                                 = default;

        consteval auto operator()(value_type const inp_pressure_threshold) const noexcept {
            auto res{*this};
            res.pressure_threshold = inp_pressure_threshold;
            assert(inp_pressure_threshold > 0);
            return res;
        }

        context_action operator()(event_type& event) noexcept;

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_ignore_adjacent_repeats, CtxT>, "You need to ignore syn repeats.");
            return operator()(ctx.event());
        }
    } pen2touch;

    constexpr struct [[nodiscard]] basic_abs2rel {
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        value_type last_abs_x = 0;
        value_type last_abs_y = 0;

        // pixel per millimeter
        float x_scale_factor = 10.0f;
        float y_scale_factor = 10.0f;

        // code_type   active_tool  = BTN_TOOL_PEN;

        bool inherit = true;

      public:
        explicit constexpr basic_abs2rel(bool const inp_inherit) noexcept : inherit(inp_inherit) {}

        constexpr basic_abs2rel() noexcept                                = default;
        consteval basic_abs2rel(basic_abs2rel const&) noexcept            = default;
        constexpr basic_abs2rel(basic_abs2rel&&) noexcept                 = default;
        consteval basic_abs2rel& operator=(basic_abs2rel const&) noexcept = default;
        constexpr basic_abs2rel& operator=(basic_abs2rel&&) noexcept      = default;
        constexpr ~basic_abs2rel() noexcept                               = default;

        void init(evdev const& dev, float scale = 20.0f) noexcept;

        /// Auto Initialize
        template <Context CtxT>
            requires has_mod<basic_interceptor, CtxT>
        void init(CtxT& ctx) noexcept {
            if (!inherit) {
                return;
            }
            for (evdev const& dev : ctx.mod(intercept).devices()) {
                if (dev.has_abs_info()) {
                    init(dev);
                    break;
                }
            }
        }

        consteval basic_abs2rel operator()(bool const inp_inherit) const noexcept {
            return basic_abs2rel{inp_inherit};
        }

        void operator()(start_type) noexcept;

        /// this fixes flickering of the pen after we switched while the pen (in mouse mode) is still active.
        template <Context CtxT>
        void operator()(CtxT& ctx, done_type) noexcept {
            if constexpr (has_mod<basic_keys_status, CtxT>) {
                auto const& keys = ctx.mod(keys_status);
                for (code_type const tool : std::initializer_list<code_type>{
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

} // namespace foresight
