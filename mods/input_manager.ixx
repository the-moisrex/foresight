// Created by moisrex on 7/10/26.

module;
#include <span>
export module fs8.mods.input_manager;
import fs8.context;
import fs8.devices.evdev;
import fs8.devices.queries;
import fs8.mods.io_manager;
import fs8.nullable_indirect;
import fs8.pimpl;

export namespace fs8 {

    /**
     * Monitor and manage input devices.
     *
     * Owns and maintains discovered `evdev` devices but does not provide input
     * events. `intercept` remains the event provider and will integrate evdev
     * readiness with `io_manager` in a later issue.
     */
    constexpr struct [[nodiscard]] basic_input_manager : pimpl_idiom<basic_input_manager> {
        /// Add device manually
        void add(evdev&& inp_dev);
        void add(device_query const& inp_query);

        [[nodiscard]] std::span<evdev const> devices() const noexcept;
        [[nodiscard]] std::span<evdev>       devices() noexcept;

        template <ContextWith<basic_io_manager> ContextT>
        context_action operator()(ContextT& ctx, start_tag) noexcept {
            return start(ctx.mod(io_manager));
        }

        /// io_manager callback for the udev monitor FD only.
        context_action operator()(io_fd const& ready_fd) noexcept;

      private:
        context_action start(basic_io_manager& io) noexcept;
    } input_manager;

} // namespace fs8
