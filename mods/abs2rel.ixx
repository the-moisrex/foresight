module;
#include <cassert>
#include <cstdint>
#include <linux/input-event-codes.h>
export module foresight.mods.abs2rel;
import foresight.mods.context;
import foresight.evdev;
import foresight.mods.intercept;
import foresight.mods.caps;

export namespace foresight {

    constexpr struct [[nodiscard]] basic_abs2rel {
        using code_type  = event_type::code_type;
        using value_type = event_type::value_type;

      private:
        value_type last_abs_x = 0;
        value_type last_abs_y = 0;

        // pixel per millimeter
        double x_scale_factor = 10.0;
        double y_scale_factor = 10.0;

        // events sent between each syn
        std::int8_t events_sent = 0;
        // code_type   active_tool  = BTN_TOOL_PEN;

        value_type pressure_threshold = 1;

        bool inherit      = true;
        bool is_left_down = false;

      public:
        explicit constexpr basic_abs2rel(bool const inp_inherit) noexcept : inherit(inp_inherit) {}

        constexpr basic_abs2rel() noexcept                                = default;
        consteval basic_abs2rel(basic_abs2rel const&) noexcept            = default;
        constexpr basic_abs2rel(basic_abs2rel&&) noexcept                 = default;
        consteval basic_abs2rel& operator=(basic_abs2rel const&) noexcept = default;
        constexpr basic_abs2rel& operator=(basic_abs2rel&&) noexcept      = default;
        constexpr ~basic_abs2rel() noexcept                               = default;

        consteval basic_abs2rel operator()(value_type const inp_pressure_threshold) const noexcept {
            auto res{*this};
            res.pressure_threshold = inp_pressure_threshold;
            assert(inp_pressure_threshold > 0);
            return res;
        }

        void init(evdev const& dev, double scale = 20.0) noexcept;

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
        context_action operator()(event_type& event) noexcept;

    } abs2rel;

} // namespace foresight
