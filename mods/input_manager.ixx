// Created by moisrex on 7/10/26.

module;
#include <list>
#include <ranges>
export module fs8.mods.input_manager;
import fs8.context;
import fs8.devices.evdev;
import fs8.devices.queries;
import fs8.mods.io_manager;
import fs8.pimpl;

export namespace fs8 {

    /**
     * Monitor and manage input devices.
     *
     * Owns and maintains discovered `evdev` devices but does not provide input
     * events. `intercept` is the event provider, integrating evdev readiness
     * with `io_manager`.
     */
    constexpr struct [[nodiscard]] basic_input_manager : pimpl_idiom<basic_input_manager> {
        /// Add device manually
        void add(evdev&& inp_dev);
        void add(device_query const& inp_query);

        /// A range view over the owned devices (stable handles: the storage is a
        /// `std::list`, so adds/removes never invalidate existing devices).
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::const_iterator> devices() const noexcept;
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::iterator>       devices() noexcept;

        /// Start monitoring; also used by `intercept` to trigger enumeration.
        context_action start(basic_io_manager& io) noexcept;

        template <ContextWith<basic_io_manager> ContextT>
        context_action operator()(ContextT& ctx, start_tag) noexcept {
            return start(ctx.mod(io_manager));
        }

        /// Pass-through: input_manager doesn't consume events, but it must be
        /// callable in the pipeline dispatch.
        context_action operator()(Context auto&) noexcept {
            return context_action::next;
        }

        /// io_manager callback for the udev monitor FD only.
        context_action operator()(io_fd const& ready_fd) noexcept;
    } input_manager;

} // namespace fs8
