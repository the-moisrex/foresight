// Created by moisrex on 7/10/26.

module;
#include <cstdint>
#include <inplace_vector>
#include <list>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
export module fs8.mods:input_manager;
import fs8.context;
import fs8.devices.evdev;
import fs8.devices.queries;
import :io_manager;
import fs8.pimpl;

export namespace fs8 {

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

        /// Request that any in-progress enumeration aborts early.
        /// Safe to call from a signal handler (uses relaxed atomic ops).
        void request_stop() noexcept;

        /// Whether stop has been requested.
        [[nodiscard]] bool stop_requested() const noexcept;

        /// Signal-handler-friendly stop (calls request_stop()).
        void stop() noexcept {
            request_stop();
        }

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

        /// The legacy sysname hash of a device (event source_id for devices that
        /// have no provider-registered source_id).
        [[nodiscard]] std::uint32_t source_id_of(evdev const& dev) const noexcept;

        /// Register a source_id → device mapping.  Called by provider mods
        /// (e.g. intercept) that create mod_id-prefixed source_ids so that
        /// `device_of(source_id)` can resolve them back to live devices.
        void register_source(std::uint32_t source_id, evdev& dev) noexcept;

        /// Unregister a previously registered source_id.
        void unregister_source(std::uint32_t source_id) noexcept;

        /// Resolve a source_id back to the live device, or nullptr if it is
        /// unknown or the device has been removed.  First checks the source_id
        /// map (populated by provider mods), then falls back to a sysname-hash
        /// lookup.
        [[nodiscard]] evdev*       device_of(std::uint32_t id) noexcept;
        [[nodiscard]] evdev const* device_of(std::uint32_t id) const noexcept;

        /// The open file descriptor of the device, or -1 if unknown.
        [[nodiscard]] int fd_of(std::uint32_t id) const noexcept;

        /// The sysname of the device (e.g. "event9"), or empty if unknown.
        [[nodiscard]] std::string sysname_of(std::uint32_t id) const noexcept;

        /// The device name, or empty if unknown.
        [[nodiscard]] std::string_view name_of(std::uint32_t id) const noexcept;

        /// Whether `id` belongs to a uinput device this process created.
        [[nodiscard]] bool is_owned(std::uint32_t id) const noexcept;

        /// Whether `id` belongs to another process's foresight virtual device
        /// (its phys starts with "foresight:").
        [[nodiscard]] bool is_chained(std::uint32_t id) const noexcept;

        /// A range view over the owned devices (stable handles: the storage is a
        /// `std::list`, so adds/removes never invalidate existing devices).
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::const_iterator> devices() const noexcept;
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::iterator>       devices() noexcept;

        /// Start monitoring; also used by `intercept` to trigger enumeration.
        /// todo: we should make this private
        context_action start(basic_io_manager& io) noexcept;

        template <Context ContextT>
        context_action operator()(ContextT& ctx, control_event const& tag) noexcept {
            using enum context_action;
            switch (tag.code) {
                case fs8::start.code: return start(ctx.mod(io_manager));
                case we_own_device.code: own_device(payload<we_own_device>(tag)); return next;
                case register_query_provider.code:
                    add_query_provider(std::move(payload<register_query_provider>(tag)));

                    // If `input_manager` started before us, it already enumerated without any
                    // queries registered; re-run the enumeration now that we're a provider
                    // (no-op when it hasn't started yet, so both pipeline orderings work).
                    requery();
                    return next;
                case add_evdev_device.code: add(std::move(payload<add_evdev_device>(tag))); return next;
                case source_registered.code: {
                    auto const reg = payload<source_registered>(tag);
                    register_source(reg.source_id, *reg.device);
                    return next;
                }
                case source_unregistered.code: unregister_source(payload<source_unregistered>(tag)); return next;
                case enumerate_devices.code: {
                    auto& list = payload<enumerate_devices>(tag);
                    for (auto& dev : devices()) {
                        if (list.size() == list.capacity()) [[unlikely]] {
                            break; // inplace_vector::push_back past capacity is UB
                        }
                        list.push_back(&dev);
                    }
                    return next;
                }
                default: return drop_event;
            }
        }

        /// Pass-through: input_manager doesn't consume events, but it must be
        /// callable in the pipeline dispatch.
        context_action operator()(Context auto&) noexcept {
            return context_action::next;
        }

        /// io_manager callback for the udev monitor FD only.
        context_action operator()(io_fd const& ready_fd) noexcept;
    } input_manager;

    struct [[nodiscard]] device_list_snapshot {
        constexpr device_list_snapshot(context_action const inp_action, device_list inp_devices) noexcept
          : action_{inp_action},
            devices_{std::move(inp_devices)} {}

        /// `true` to proceed; `false` on recovery/exit (take `action()`).
        [[nodiscard]] explicit operator bool() const noexcept {
            return !is_exiting(action_);
        }

        [[nodiscard]] context_action action() const noexcept {
            return action_;
        }

        [[nodiscard]] auto begin() const noexcept {
            return devices_.begin();
        }

        [[nodiscard]] auto end() const noexcept {
            return devices_.end();
        }

        [[nodiscard]] auto size() const noexcept {
            return devices_.size();
        }

        [[nodiscard]] auto empty() const noexcept {
            return devices_.empty();
        }

        [[nodiscard]] auto const& operator[](std::size_t const index) const noexcept {
            return devices_[index];
        }

        [[nodiscard]] auto& operator[](std::size_t const index) noexcept {
            return devices_[index];
        }

      private:
        context_action action_;
        device_list    devices_;
    };

    /// Pull the input_manager's device list via the `enumerate_devices`
    /// broadcast. Accepts a Context (uses `.broadcast`) or `dynamic_context`.
    ///
    /// Returns a snapshot: a range of `evdev*` plus the broadcast's
    /// `context_action`. `operator bool` is `true` while the action is safe
    /// to continue with (`!is_exiting`); on an exiting action the list is
    /// cleared so callers never seed from a partial/aborted enumeration.
    /// (`device_list` is held by the local snapshot and returned through a
    /// deduced type, not named in the interface: a by-value `inplace_vector`
    /// member in an exported type corrupts GCC BMIs.)
    template <Context CtxT>
    device_list_snapshot tracked_devices(CtxT&& ctx) noexcept {
        device_list devices;
        auto const  action = ctx.broadcast(enumerate_devices + &devices);
        if (is_exiting(action)) [[unlikely]] {
            devices.clear();
        }
        return device_list_snapshot{action, std::move(devices)};
    }

} // namespace fs8
