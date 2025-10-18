// Created by moisrex on 10/11/25.

module;
#include <stdexcept>
#include <string_view>
export module foresight.mods.typer;
import foresight.mods.context;
import foresight.mods.event;

namespace foresight {

    using typer_iterator = std::string_view::const_iterator;

    /**
     * @returns invalid_user_event when the string is wrong.
     */
    // user_event parse_next_event (typer_iterator& pos, typer_iterator  endp) noexcept;

    // struct [[nodiscard]] basic_typer {
    //     private:
    //     std::string_view evstr; // event string
    //
    //   public:
    //     explicit consteval basic_typer(std::string_view const inp_evstr) noexcept : evstr{inp_evstr} {}
    //
    //     constexpr basic_typer() noexcept                        = default;
    //     constexpr basic_typer(basic_typer&&)                     = default;
    //     constexpr basic_typer(basic_typer const&) noexcept       = default;
    //     constexpr basic_typer& operator=(basic_typer const&)     = default;
    //     constexpr basic_typer& operator=(basic_typer&&) noexcept = default;
    //     constexpr ~basic_typer()                                = default;
    //
    //     void operator()(Context auto& ctx) noexcept {
    //         typer_iterator const endp = evstr.end();
    //         for (typer_iterator pos = evstr.begin(); pos != endp ;) {
    //             auto const cur_event = parse_next_event(pos, endp);
    //             if (is_invalid(cur_event)) [[unlikely]] {
    //                 break;
    //             }
    //             std::ignore = ctx.fork_emit(cur_event);
    //         }
    //     }
    // };
} // namespace foresight
