
module;
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <linux/input-event-codes.h>
#include <type_traits>
export module foresight.mods.mouse_status;
import foresight.mods.event;
import foresight.mods.context;

export namespace foresight {

    /**
     * If you need to check if a key is pressed or not, this is what you need to use.
     */
    template <std::size_t N = 2>
    struct [[nodiscard]] basic_mouse_history {
        using value_type = event_type::value_type;

        static_assert(N > 1, "History is too small.");
        static_assert(N < std::numeric_limits<std::uint16_t>::max(), "History is too big.");

        struct [[nodiscard]] position {
            value_type x = 0;
            value_type y = 0;
        };

      private:
        std::array<position, N> hist{};
        std::uint16_t           fill_index = 0;
        std::uint16_t           cur_index  = 0;

      public:
        // the copy ctor/assignment-op are marked consteval to stop from copying at run time.
        constexpr basic_mouse_history() noexcept                                 = default;
        consteval basic_mouse_history(basic_mouse_history const&)                = default;
        constexpr basic_mouse_history(basic_mouse_history&&) noexcept            = default;
        consteval basic_mouse_history& operator=(basic_mouse_history const&)     = default;
        constexpr basic_mouse_history& operator=(basic_mouse_history&&) noexcept = default;
        constexpr ~basic_mouse_history() noexcept                                = default;

        [[nodiscard]] constexpr position const& cur() const noexcept {
            assert(cur_index < hist.size());
            return hist.at(cur_index);
        }

        [[nodiscard]] constexpr position& cur() noexcept {
            assert(cur_index < hist.size());
            return hist.at(cur_index);
        }

        [[nodiscard]] constexpr position prev(std::int16_t const last = 1) const noexcept {
            assert(last > 0);
            assert(last < static_cast<std::int16_t>(hist.size()));
            std::int16_t prev_index = static_cast<std::int16_t>(cur_index) - last;
            if (prev_index < 0) {
                prev_index += static_cast<std::int16_t>(hist.size());
            }
            return hist.at(prev_index);
        }

        constexpr void push(position pos = {}) noexcept {
            if (++fill_index >= hist.size()) {
                fill_index = 0;
            }
            hist.at(fill_index) = std::move(pos);
        }

        void operator()(Context auto& ctx) noexcept {
            auto const& event = ctx.event();
            switch (event.type()) {
                case EV_REL: break;
                case EV_SYN: push(); return;
                default: return;
            }
            cur_index = fill_index;
            switch (event.code()) {
                case REL_X: cur().x += event.value(); break;
                case REL_Y: cur().y += event.value(); break;
                default: break;
            }
        }
    };

    constexpr basic_mouse_history<> mouse_history;

} // namespace foresight
