// Created by moisrex on 7/10/26.

module;
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <ranges>
#include <span>
#include <string>
export module fs8.mods:input_manager;
import fs8.context;
import fs8.devices.evdev;
import fs8.devices.queries;
import :io_manager;
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

    /// Whether a device was just connected or disconnected.
    enum struct [[nodiscard]] device_change : std::uint8_t {
        connected,
        disconnected
    };

    /// A type-erased listener that is notified when a device is connected or
    /// disconnected.  The listener receives the device_id and the change type.
    /// For connect events the device can be resolved via
    /// `input_manager::device_of(id)`; for disconnect events the device has
    /// already been removed.
    struct [[nodiscard]] device_change_handle {
        void const*                                                      identity = nullptr;
        std::move_only_function<void(device_id, device_change) noexcept> invoke;
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

        /// Register a listener that is notified on device connect/disconnect.
        /// Idempotent by identity pointer.
        void add_device_change_listener(device_change_handle listener);

        /// Re-ask every registered provider for its queries, then re-run
        /// enumeration and rebuild the udev monitor filter so hotplug
        /// continues to match the fresh set.
        void requery();

        /// Record a device node (e.g. "/dev/input/event9") of a uinput device
        /// that this process created. Devices are only ever *tagged*; they are
        /// still enumerated and watched like any other device, and events read
        /// back from them carry their normal device id. `is_owned` answers
        /// whether a device id belongs to one of ours.
        void own_device(std::string_view devnode) noexcept;

        /// Whether `dev` is a uinput device created by this process.
        [[nodiscard]] bool is_owned(evdev const& dev) const noexcept;

        /// Whether the sysname (e.g. "event9") belongs to one of our devices.
        [[nodiscard]] bool is_owned_sysname(std::string_view sysname) const noexcept;

        /// The opaque id of a device (a hash of its sysname), or `none` if the
        /// device has no usable sysname.
        [[nodiscard]] device_id device_id_of(evdev const& dev) const noexcept;

        /// Resolve a device id back to the live device, or nullptr if it is a
        /// special id (`stdin`/`self`/`none`) or the device has been removed.
        [[nodiscard]] evdev*       device_of(device_id id) noexcept;
        [[nodiscard]] evdev const* device_of(device_id id) const noexcept;

        /// The open file descriptor of the device, or -1 if unknown.
        [[nodiscard]] int fd_of(device_id id) const noexcept;

        /// The sysname of the device (e.g. "event9"), or empty if unknown.
        [[nodiscard]] std::string sysname_of(device_id id) const noexcept;

        /// The device name, or empty if unknown.
        [[nodiscard]] std::string_view name_of(device_id id) const noexcept;

        /// Whether `id` belongs to a uinput device this process created.
        [[nodiscard]] bool is_owned(device_id id) const noexcept;

        /// Whether `id` belongs to another process's foresight virtual device
        /// (its phys starts with "foresight:").
        [[nodiscard]] bool is_chained(device_id id) const noexcept;

        /// A range view over the owned devices (stable handles: the storage is a
        /// `std::list`, so adds/removes never invalidate existing devices).
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::const_iterator> devices() const noexcept;
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::iterator>       devices() noexcept;

        /// Opaque generation token that changes on every device add/remove.
        /// Used only for equality checks; not ordered.  The value is
        /// randomized so overflow is practically impossible.
        [[nodiscard]] std::uint32_t devices_generation() const noexcept;

        /// Start monitoring; also used by `intercept` to trigger enumeration.
        context_action start(basic_io_manager& io) noexcept;

        template <Context ContextT>
        context_action operator()(ContextT& ctx, special_event const& tag) noexcept {
            if (tag.code != special_start.code) {
                return context_action::drop_event;
            }
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
