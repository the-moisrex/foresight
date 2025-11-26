// Created by moisrex on 6/9/25.

module;
export module foresight.mods.quantifier;
import foresight.mods.event;
import foresight.mods.context;

export namespace fs8 {


    /**
     * If you need to act upon each N pixels movement, then this is what you need.
     * This works for SINGLE event code.
     */
    constexpr struct [[nodiscard]] basic_quantifier {
        using value_type = event_type::value_type;
        using code_type  = event_type::code_type;

      private:
        value_type step  = 10; // pixels/units
        value_type value = 0;

      public:
        constexpr basic_quantifier() noexcept = default;

        explicit basic_quantifier(value_type inp_step) noexcept;

        consteval basic_quantifier(basic_quantifier const&)                = default;
        constexpr basic_quantifier(basic_quantifier&&) noexcept            = default;
        consteval basic_quantifier& operator=(basic_quantifier const&)     = default;
        constexpr basic_quantifier& operator=(basic_quantifier&&) noexcept = default;
        constexpr ~basic_quantifier() noexcept                             = default;

        void process(event_type const& event, code_type btn_code) noexcept;

        [[nodiscard]] value_type consume_steps() noexcept;
    } quantifier;

    /**
     * If you want to quantify your mouse movements into steps, this is what you need.
     */
    constexpr struct [[nodiscard]] basic_mice_quantifier {
        using value_type = event_type::value_type;
        using code_type  = event_type::code_type;

      private:
        value_type step       = 10; // pixels/units
        value_type x_value    = 0;
        value_type y_value    = 0;
        value_type last_abs_x = 0;
        value_type last_abs_y = 0;

      public:
        constexpr basic_mice_quantifier() noexcept = default;

        explicit basic_mice_quantifier(value_type inp_step) noexcept;

        consteval basic_mice_quantifier(basic_mice_quantifier const&)                = default;
        constexpr basic_mice_quantifier(basic_mice_quantifier&&) noexcept            = default;
        consteval basic_mice_quantifier& operator=(basic_mice_quantifier const&)     = default;
        constexpr basic_mice_quantifier& operator=(basic_mice_quantifier&&) noexcept = default;
        constexpr ~basic_mice_quantifier() noexcept                                  = default;

        [[nodiscard]] value_type consume_x() noexcept;
        [[nodiscard]] value_type consume_y() noexcept;

        // This can be used like:
        //    mise_quantifier(20)
        consteval basic_mice_quantifier operator()(value_type const steps) const noexcept {
            return basic_mice_quantifier{steps};
        }

        void operator()(event_type const& event) noexcept;
    } mice_quantifier;


} // namespace fs8
