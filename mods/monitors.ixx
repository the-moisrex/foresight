module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <span>
#include <string_view>
export module fs8.mods:monitors;
import fs8.compositor.monitor_detection;
import fs8.context;
import fs8.event;
import fs8.log;
import fs8.pimpl;
import :io_manager;
import :input_manager;

export namespace fs8 {

    /// Literal, consteval-friendly monitor name (usable in pipeline expressions).
    struct [[nodiscard]] monitor_name {
        std::array<char, 32> data{};
        std::uint8_t         len = 0;

        constexpr monitor_name() noexcept = default;

        explicit constexpr monitor_name(std::string_view sv) noexcept {
            auto const n = static_cast<std::size_t>(std::min(sv.size(), std::size_t{31}));
            std::copy_n(sv.begin(), n, data.begin());
            data[n] = '\0';
            len     = static_cast<std::uint8_t>(n);
        }

        [[nodiscard]] constexpr std::string_view view() const noexcept {
            return {data.data(), len};
        }

        [[nodiscard]] constexpr bool empty() const noexcept {
            return len == 0;
        }

        constexpr auto operator<=>(monitor_name const&) const = default;
        constexpr bool operator==(monitor_name const&) const  = default;
    };

    constexpr struct [[nodiscard]] basic_monitors : pimpl_idiom<basic_monitors> {
        using pimpl_idiom::pimpl_idiom;

        template <Context CtxT>
            requires has_mod<basic_io_manager, CtxT>
        context_action operator()(CtxT& ctx, control_event const& tag) noexcept {
            switch (tag.code) {
                case start.code: {
                    if (auto const action = do_start(ctx.mod(io_manager)); action != context_action::next) {
                        return action;
                    }
                    if constexpr (has_mod<basic_input_manager, CtxT>) {
                        for (auto const& dev : ctx.mod(input_manager).devices()) {
                            if (dev.has_abs_info()) {
                                float range_x = 0.0f;
                                float range_y = 0.0f;
                                if (auto const* x = dev.abs_info(ABS_X); x != nullptr) {
                                    range_x = static_cast<float>(x->maximum - x->minimum);
                                }
                                if (auto const* y = dev.abs_info(ABS_Y); y != nullptr) {
                                    range_y = static_cast<float>(y->maximum - y->minimum);
                                }
                                set_effective_tablet_range(range_x, range_y);
                                break;
                            }
                        }
                    }
                    return context_action::next;
                }
                case load_event.code:
                case next_event.code:
                    if (consume_hotplug_pending() && refresh()) {
                        if (
                          auto const res = ctx.broadcast(monitors_updated); res == context_action::exit || res == context_action::recovery)
                        {
                            return res;
                        }
                    }
                    [[fallthrough]];
                default: return context_action::drop_event;
            }
        }

        context_action operator()() noexcept {
            return context_action::next;
        }

        context_action operator()(io_fd& fd) noexcept;

        [[nodiscard]] std::span<compositor::monitor_info const> monitors() const noexcept;
        [[nodiscard]] compositor::desktop_bounds                desktop() const noexcept;

        void                set_effective_tablet_range(float x, float y) noexcept;
        [[nodiscard]] float effective_tablet_range_x() const noexcept;
        [[nodiscard]] float effective_tablet_range_y() const noexcept;

        [[nodiscard]] bool refresh() noexcept;

      private:
        context_action     do_start(basic_io_manager& io) noexcept;
        [[nodiscard]] bool consume_hotplug_pending() noexcept;
    } monitors;

    static_assert(Modifier<basic_monitors>);

} // namespace fs8
