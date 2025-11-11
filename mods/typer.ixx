// Created by moisrex on 10/11/25.

module;
#include <string_view>
#include <vector>
export module foresight.mods.typer;
import foresight.mods.context;
import foresight.mods.event;
import foresight.lib.xkb.how2type;

namespace foresight {

    /**
     * This struct will help you emit events corresponding to a string
     */
    export constexpr struct [[nodiscard]] basic_typist {
      private:
        // we ust optional to make `constexpr` possible
        std::vector<user_event>      events;

      public:
        constexpr basic_typist()                                   = default;
        constexpr basic_typist(basic_typist const&)                = default;
        constexpr basic_typist(basic_typist&&) noexcept            = default;
        constexpr basic_typist& operator=(basic_typist const&)     = default;
        constexpr basic_typist& operator=(basic_typist&&) noexcept = default;
        constexpr ~basic_typist()                                  = default;

        void emit(std::u32string_view str, user_event_callback);
        void emit(std::u32string_view str);

        void operator()(Context auto& ctx) noexcept {
            for (user_event const& event : events) {
                std::ignore = ctx.fork_emit(event_type{event});
            }
        }
    } typist;
} // namespace foresight
