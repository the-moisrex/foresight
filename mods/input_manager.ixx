// Created by moisrex on 7/10/26.

module;
#include <functional>
#include <list>
#include <memory>
#include <ranges>
#include <span>
export module fs8.mods.input_manager;
import fs8.context;
import fs8.devices.evdev;
import fs8.devices.queries;
import fs8.mods.io_manager;
import fs8.pimpl;

export namespace fs8 {

    /// A type-erased reference to a registered query provider. The handle owns
    /// a small closure that references the provider object, which must outlive
    /// the input_manager (same requirement as `io_manager` handlers, since the
    /// identity is its address).
    struct [[nodiscard]] query_provider_handle {
        void const*                                                       identity = nullptr;
        std::move_only_function<std::span<device_query const>() noexcept> invoke;

        [[nodiscard]] std::span<device_query const> operator()() noexcept {
            return invoke ? invoke() : std::span<device_query const>{};
        }
    };

    template <typename T>
    concept query_provider = requires(T& p) {
        { p.queries() } noexcept -> std::same_as<std::span<device_query const>>;
    };

    /// Type-erase a provider object into a `query_provider_handle`. May throw
    /// (constructing the closure), so callers must handle it.
    template <query_provider ProviderT>
    [[nodiscard]] query_provider_handle provider_handle(ProviderT& provider) {
        return query_provider_handle{
          .identity = std::addressof(provider),
          .invoke   = [&provider]() noexcept -> std::span<device_query const> {
              return provider.queries();
          },
        };
    }

    /**
     * Monitor and manage input devices.
     *
     * Owns and maintains discovered `evdev` devices but does not provide input
     * events. `intercept` is the event provider, integrating evdev readiness
     * with `io_manager`. Queries are pulled on demand from registered
     * providers, so the manager can re-ask everyone via `requery()`.
     */
    constexpr struct [[nodiscard]] basic_input_manager : pimpl_idiom<basic_input_manager> {
        /// Add device manually
        void add(evdev&& inp_dev);

        /// Register a query provider by reference (idempotent per provider).
        void add_query_provider(query_provider_handle provider);

        /// Re-ask every registered provider for its queries, then re-run
        /// enumeration and rebuild the udev monitor filter so hotplug
        /// continues to match the fresh set.
        void requery();

        /// Record a device node (e.g. "/dev/input/event9") of a uinput device
        /// that this process created. Events read back from such a device are
        /// reported with `event_origin::self` by the interceptor. Devices are
        /// only ever *tagged*; they are still enumerated and watched like any
        /// other device.
        void own_device(std::string_view devnode) noexcept;

        /// Whether `dev` is a uinput device created by this process.
        [[nodiscard]] bool is_owned(evdev const& dev) const noexcept;

        /// Whether the sysname (e.g. "event9") belongs to one of our devices.
        [[nodiscard]] bool is_owned_sysname(std::string_view sysname) const noexcept;

        /// A range view over the owned devices (stable handles: the storage is a
        /// `std::list`, so adds/removes never invalidate existing devices).
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::const_iterator> devices() const noexcept;
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::iterator>       devices() noexcept;

        /// Start monitoring; also used by `intercept` to trigger enumeration.
        context_action start(basic_io_manager& io) noexcept;

        template <Context ContextT>
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
