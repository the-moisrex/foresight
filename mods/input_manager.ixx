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
        /// back from them carry their normal device id plus the
        /// `source_id_owned` origin bit (answered through the in/out
        /// `source_registered` payload, or pushed via `source_owned` for
        /// devices registered before the tag). `is_owned` answers whether a
        /// device id belongs to one of ours.
        void own_device(std::string_view devnode) noexcept;

        /// Whether `dev` is a uinput device created by this process.
        [[nodiscard]] bool is_owned(evdev const& dev) const noexcept;

        /// Whether the sysname (e.g. "event9") belongs to one of our devices.
        [[nodiscard]] bool is_owned_sysname(std::string_view sysname) const noexcept;

        /// The legacy sysname hash of a device (event source_id for devices that
        /// have no provider-registered source_id).
        [[nodiscard]] std::uint32_t source_id_of(evdev const& dev) const noexcept;

        /// Register a source_id → device mapping and answer the origin flags:
        /// ORs the `source_id_owned` / `source_id_chained` bits into
        /// `info.source_id` (one sysname/phys check each) so the registering
        /// mod reads them straight back from the id it caches. Called by
        /// provider mods (e.g. intercept) that create mod_id-prefixed
        /// source_ids so that `device_of(source_id)` can resolve them back to
        /// live devices.
        void register_source(source_info& info) noexcept;

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
        /// O(1): reads the flags cached when the source was registered.
        [[nodiscard]] bool is_owned(std::uint32_t id) const noexcept;

        /// Whether `id` belongs to another process's foresight virtual device
        /// (its phys starts with "foresight:"). O(1): reads the flags cached
        /// when the source was registered.
        [[nodiscard]] bool is_chained(std::uint32_t id) const noexcept;

        /// A range view over the owned devices (stable handles: the storage is a
        /// `std::list`, so adds/removes never invalidate existing devices).
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::const_iterator> devices() const noexcept;
        [[nodiscard]] std::ranges::subrange<std::list<evdev>::iterator>       devices() noexcept;

        /// Start monitoring; also used by `intercept` to trigger enumeration.
        /// Registers the udev monitor fd through an `io_watch` broadcast, so
        /// this pipeline needs an `io_manager` somewhere (it reports `exit`
        /// otherwise).
        /// todo: we should make this private
        context_action start() noexcept;

        context_action operator()(control_event const& event) noexcept;

        /// Pass-through: input_manager doesn't consume events, but it must be
        /// callable in the pipeline dispatch.
        context_action operator()(Context auto&) noexcept {
            return context_action::next;
        }

        /// io_manager callback for the udev monitor FD only.
        context_action operator()(io_fd const& ready_fd) noexcept;
    } input_manager;

    struct [[nodiscard]] device_list_snapshot {
        constexpr device_list_snapshot(context_action const inp_action, device_list&& inp_devices) noexcept : action_{inp_action} {
#ifdef __clang__
            devices_ = std::move(inp_devices);
#else
            for (auto const dev : inp_devices) {
                if (count_ == tracked_device_capacity) [[unlikely]] {
                    break;
                }
                devices_[count_++] = dev;
            }
#endif
        }

        /// `true` to proceed; `false` on recovery/exit (take `action()`).
        [[nodiscard]] explicit operator bool() const noexcept {
            return !is_exiting(action_);
        }

        [[nodiscard]] context_action action() const noexcept {
            return action_;
        }

        [[nodiscard]] auto begin() const noexcept {
            return data();
        }

        [[nodiscard]] auto end() const noexcept {
            return data() + count();
        }

        [[nodiscard]] auto size() const noexcept {
            return count();
        }

        [[nodiscard]] auto empty() const noexcept {
            return count() == 0;
        }

        [[nodiscard]] auto const& operator[](std::size_t const index) const noexcept {
            return data()[index];
        }

        [[nodiscard]] auto& operator[](std::size_t const index) noexcept {
            return data()[index];
        }

      private:
        [[nodiscard]] evdev* const* data() const noexcept {
#ifdef __clang__
            return devices_.data();
#else
            return devices_;
#endif
        }

        [[nodiscard]] evdev** data() noexcept {
#ifdef __clang__
            return devices_.data();
#else
            return devices_;
#endif
        }

        [[nodiscard]] std::size_t count() const noexcept {
#ifdef __clang__
            return devices_.size();
#else
            return count_;
#endif
        }

        context_action action_ = context_action::exit;
#ifdef __clang__
        device_list devices_{};
#else
        /// todo: on GCC use `device_list` (`std::inplace_vector`) here too —
        /// but NOT before GCC fixes bugzilla c++/124478 / c++/125144: a
        /// by-value `inplace_vector` member in an exported type makes every
        /// consumer of this BMI die with "failed to read compiled module
        /// cluster N: Bad file data". Reproduced on GCC 16.2.1
        /// (Arch gcc-16.2.1+r23+gd564253eb6c8-1); Clang is unaffected and
        /// already stores `device_list`.
        evdev*      devices_[tracked_device_capacity]{};
        std::size_t count_ = 0;
#endif
    };

    /// Pull the input_manager's device list via the `enumerate_devices`
    /// broadcast. Accepts a Context (uses `.broadcast`) or `dynamic_context`.
    ///
    /// Returns a snapshot: a range of `evdev*` plus the broadcast's
    /// `context_action`. `operator bool` is `true` while the action is safe
    /// to continue with (`!is_exiting`); on an exiting action the list is
    /// cleared so callers never seed from a partial/aborted enumeration.
    /// (Storage differs by compiler: Clang stores the `inplace_vector` member
    /// directly, GCC copies it into a raw array because a by-value
    /// `inplace_vector` member in an exported type corrupts its BMI. See the
    /// todo on `device_list_snapshot`.)
    template <Context CtxT>
    device_list_snapshot tracked_devices(CtxT& ctx) noexcept {
        device_list devices;
        auto const  action = ctx.broadcast(enumerate_devices + &devices);
        if (is_exiting(action)) [[unlikely]] {
            devices.clear();
        }
        return device_list_snapshot{action, std::move(devices)};
    }

} // namespace fs8
